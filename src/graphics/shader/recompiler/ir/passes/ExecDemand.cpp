#include "graphics/shader/recompiler/ir/passes/ExecDemand.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <numeric>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Libs::Graphics::ShaderRecompiler::IR {
namespace {

// A demand is a disjunction of terms, each a conjunction of lane masks (U1 values, possibly
// negated): no term, no lane can observe the value; one empty term, every lane may. Every limit
// below only ever widens a demand or fails a proof, which keeps a select.
constexpr size_t   MaxTerms    = 8;  // more terms widen to the masks they all share
constexpr size_t   MaxLiterals = 8;  // a term drops further masks (it then holds for more lanes)
constexpr size_t   MaxNodes    = 32; // boolean expression nodes one mask or proof looks through
constexpr size_t   MaxAtoms    = 8;  // free variables of the truth-table proof
constexpr uint32_t MaxUpdates  = 16; // a demand that keeps changing widens to every lane

struct Literal {
	const Inst* mask    = nullptr; // never LogicalNot, nor LogicalAnd unless negated
	uint32_t    order   = 0;       // the mask's program order, for a deterministic sort
	bool        negated = false;

	bool operator==(const Literal& other) const = default;
};

bool LiteralLess(const Literal& lhs, const Literal& rhs) {
	return lhs.order != rhs.order ? lhs.order < rhs.order : lhs.negated < rhs.negated;
}

using Term = std::vector<Literal>; // sorted by LiteralLess

bool TermLess(const Term& lhs, const Term& rhs) {
	return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), LiteralLess);
}

bool Contains(const Term& term, const Inst* mask, bool negated) {
	return std::ranges::any_of(term, [&](const Literal& literal) {
		return literal.mask == mask && literal.negated == negated;
	});
}

// Whether `term` holds only where `other` does: it has every literal of `other`.
bool Includes(const Term& term, const Term& other) {
	return std::includes(term.begin(), term.end(), other.begin(), other.end(), LiteralLess);
}

struct Demand {
	std::vector<Term> terms; // sorted by TermLess

	bool operator==(const Demand& other) const = default;
};

Demand Everyone() {
	Demand demand;
	demand.terms.emplace_back();
	return demand;
}

bool IsEveryone(const Demand& demand) {
	return demand.terms.size() == 1 && demand.terms.front().empty();
}

void AddTerm(Demand& demand, Term term) {
	for (const auto& other: demand.terms) {
		if (Includes(term, other)) {
			return;
		}
	}
	std::erase_if(demand.terms, [&](const Term& other) { return Includes(other, term); });
	demand.terms.insert(std::upper_bound(demand.terms.begin(), demand.terms.end(), term, TermLess),
	                    std::move(term));
	if (demand.terms.size() > MaxTerms) {
		// Every term implies the masks they all share.
		Term shared = demand.terms.front();
		for (const auto& other: demand.terms) {
			Term kept;
			std::set_intersection(shared.begin(), shared.end(), other.begin(), other.end(),
			                      std::back_inserter(kept), LiteralLess);
			shared = std::move(kept);
		}
		demand.terms.assign(1, std::move(shared));
	}
}

void Merge(Demand& demand, const Demand& other) {
	for (const auto& term: other.terms) {
		AddTerm(demand, term);
	}
}

bool IsSelect(ValueOpcode opcode) {
	return opcode == ValueOpcode::SelectU1 || opcode == ValueOpcode::SelectU32 ||
	       opcode == ValueOpcode::SelectF32;
}

bool IsBooleanOperator(ValueOpcode opcode) {
	return opcode == ValueOpcode::LogicalAnd || opcode == ValueOpcode::LogicalOr ||
	       opcode == ValueOpcode::LogicalXor || opcode == ValueOpcode::LogicalNot ||
	       opcode == ValueOpcode::SelectU1;
}

// A lane's result depends only on the same lane's operands.
bool IsLaneLocal(ValueOpcode opcode) {
	// These opcode-table groups hold only construction, conversion, modifiers and arithmetic
	// (ValueOpcodes.inc); the cross-lane operations follow FPFract32.
	return (opcode >= ValueOpcode::BitCastU16F16 && opcode <= ValueOpcode::BitFieldSExtract) ||
	       (opcode >= ValueOpcode::IAdd32 && opcode <= ValueOpcode::FPFract32) ||
	       opcode == ValueOpcode::WqmU64 || opcode == ValueOpcode::Identity ||
	       opcode == ValueOpcode::WriteLane || opcode == ValueOpcode::MakeImageAddress;
}

// A memory access performed only for the lanes of its last operand, the EXEC mask.
bool IsMaskedAccess(ValueOpcode opcode) {
	const auto count = NumArgsOf(opcode);
	if (count == 0 || ArgTypeOf(opcode, count - 1) != Type::U1) {
		return false;
	}
	const auto shared = SharedAccessOf(opcode);
	return BufferAccessOf(opcode) != BufferAccess::None || shared == SharedAccess::Read ||
	       shared == SharedAccess::Write || shared == SharedAccess::Atomic ||
	       AddressOpcodeInfoOf(opcode).access != AddressAccess::None ||
	       ImageOpcodeInfoOf(opcode).access != ImageAccess::None;
}

class Analysis {
public:
	explicit Analysis(Program& program): m_program(program) {}

	bool     Prepare();
	uint32_t ShortenChains();
	void     Solve();
	uint32_t Apply();

private:
	struct Proof {
		const Inst* mask    = nullptr;
		bool        negated = false;
		bool        holds   = false;
	};

	[[nodiscard]] bool Dominates(const Block* dominator, const Block* block) const {
		const auto target  = m_block_index.at(dominator);
		auto       current = m_block_index.at(block);
		while (current > target) {
			current = m_idom[current];
		}
		return current == target;
	}

	[[nodiscard]] bool AddMask(Term& term, Value mask, bool negated) const {
		std::vector<std::pair<const Inst*, bool>> visited;
		return AddMask(term, mask, negated, visited);
	}
	bool AddMask(Term& term, Value mask, bool negated,
	             std::vector<std::pair<const Inst*, bool>>& visited) const;
	[[nodiscard]] Demand Conjoin(const Demand& demand, Value mask, bool negated) const;
	[[nodiscard]] Demand Hoist(const Demand& demand, const Block* header) const;
	[[nodiscard]] bool   SampleIsLaneLocal(const Inst& inst) const;
	void                 Contribute(Demand& demand, const Inst& user, size_t operand) const;
	[[nodiscard]] Demand Compute(const Inst& inst) const;
	[[nodiscard]] bool   Implies(const Term& term, Value mask, bool negated) const;
	bool Proves(const Term& term, Value mask, bool negated, std::vector<Proof>& proofs,
	            size_t depth) const;
	[[nodiscard]] bool TruthTable(const Term& term, Value mask, bool negated) const;
	[[nodiscard]] bool MaskImplies(Value premise, Value conclusion) const {
		Term term;
		return !AddMask(term, premise, false) || Implies(term, conclusion, false);
	}

	Program&                                   m_program;
	std::unordered_map<const Block*, uint32_t> m_block_index; // reverse post-order
	std::vector<uint32_t>                      m_idom;
	std::unordered_map<const Inst*, uint32_t>  m_order;
	std::vector<Inst*>                         m_insts;
	std::vector<Demand>                        m_demand;
	std::vector<uint32_t>                      m_updates;
};

bool Analysis::Prepare() {
	const auto& blocks = m_program.blocks;
	if (blocks.empty() || m_program.dispatcher_fallback) {
		return false;
	}
	// Depth-first post-order from the entry, noting the edges back to a block still on the path.
	std::vector<const Block*>                          post_order;
	std::vector<std::pair<const Block*, const Block*>> retreating;
	std::unordered_map<const Block*, bool>             on_path;
	std::vector<std::pair<const Block*, size_t>>       path {{blocks.front(), 0}};
	on_path.emplace(blocks.front(), true);
	while (!path.empty()) {
		const auto* block      = path.back().first;
		const auto  successors = block->ImmSuccessors();
		if (path.back().second < successors.size()) {
			const auto* successor   = successors[path.back().second++];
			const auto [entry, added] = on_path.emplace(successor, true);
			if (added) {
				path.emplace_back(successor, 0);
			} else if (entry->second) {
				retreating.emplace_back(block, successor);
			}
			continue;
		}
		on_path[block] = false;
		post_order.push_back(block);
		path.pop_back();
	}
	if (post_order.size() != blocks.size()) {
		return false;
	}
	const std::vector<const Block*> order(post_order.rbegin(), post_order.rend());
	for (uint32_t index = 0; index < order.size(); index++) {
		m_block_index.emplace(order[index], index);
	}
	// Cooper, Harvey and Kennedy's iterative dominators over the reverse post-order.
	constexpr uint32_t None = UINT32_MAX;
	m_idom.assign(order.size(), None);
	m_idom.front()       = 0;
	const auto intersect = [&](uint32_t lhs, uint32_t rhs) {
		while (lhs != rhs) {
			while (lhs > rhs) {
				lhs = m_idom[lhs];
			}
			while (rhs > lhs) {
				rhs = m_idom[rhs];
			}
		}
		return lhs;
	};
	for (bool changed = true; changed;) {
		changed = false;
		for (uint32_t index = 1; index < order.size(); index++) {
			uint32_t idom = None;
			for (const auto* predecessor: order[index]->ImmPredecessors()) {
				const auto other = m_block_index.at(predecessor);
				if (m_idom[other] != None) {
					idom = idom == None ? other : intersect(other, idom);
				}
			}
			if (idom != m_idom[index]) {
				m_idom[index] = idom;
				changed       = true;
			}
		}
	}
	// An edge back to a block that does not dominate its source closes an irreducible cycle: no
	// header separates its iterations.
	if (!std::ranges::all_of(retreating, [&](const auto& edge) {
		    return Dominates(edge.second, edge.first);
	    })) {
		return false;
	}
	for (auto* block: blocks) {
		for (auto& inst: *block) {
			m_order.emplace(&inst, static_cast<uint32_t>(m_insts.size()));
			m_insts.push_back(&inst);
		}
	}
	m_demand.assign(m_insts.size(), {});
	m_updates.assign(m_insts.size(), 0);
	return true;
}

// Conjoins the lanes where `mask` (its negation when `negated`) holds; false when the term then
// holds for no lane. The newest mask of a translated conjunction is its second operand
// (LogicalAnd(exec, compare)), so that one goes first when the limits leave masks out.
bool Analysis::AddMask(Term& term, Value mask, bool negated,
                       std::vector<std::pair<const Inst*, bool>>& visited) const {
	mask = mask.Resolve();
	if (mask.IsImmediate()) {
		return mask.U1() != negated;
	}
	const auto* inst = mask.Instruction();
	if (std::ranges::find(visited, std::pair {inst, negated}) != visited.end() ||
	    visited.size() == MaxNodes) {
		return true;
	}
	visited.emplace_back(inst, negated);
	const auto opcode = inst->GetOpcode();
	if (opcode == ValueOpcode::LogicalNot) {
		return AddMask(term, inst->Arg(0), !negated, visited);
	}
	// A conjunction, or the negation of a disjunction.
	if (opcode == (negated ? ValueOpcode::LogicalOr : ValueOpcode::LogicalAnd)) {
		return AddMask(term, inst->Arg(1), negated, visited) &&
		       AddMask(term, inst->Arg(0), negated, visited);
	}
	const auto order = m_order.find(inst);
	if (order == m_order.end()) {
		return true;
	}
	for (const auto& literal: term) {
		if (literal.mask == inst) {
			return literal.negated == negated;
		}
	}
	if (term.size() < MaxLiterals) {
		const Literal literal {inst, order->second, negated};
		term.insert(std::upper_bound(term.begin(), term.end(), literal, LiteralLess), literal);
	}
	return true;
}

Demand Analysis::Conjoin(const Demand& demand, Value mask, bool negated) const {
	Demand result;
	for (const auto& term: demand.terms) {
		auto conjunction = term;
		if (AddMask(conjunction, mask, negated)) {
			AddTerm(result, std::move(conjunction));
		}
	}
	return result;
}

// The demand a header Phi passes back along a back edge reaches the previous iteration, where the
// masks defined in the loop held other values: a term keeps those defined before the loop.
Demand Analysis::Hoist(const Demand& demand, const Block* header) const {
	Demand result;
	for (const auto& term: demand.terms) {
		Term kept;
		for (const auto& literal: term) {
			const auto* block = literal.mask->Parent();
			if (block != header && Dominates(block, header)) {
				kept.push_back(literal);
			}
		}
		AddTerm(result, std::move(kept));
	}
	return result;
}

// Sample coordinates without implicit derivatives: an explicit LOD or gradient, or any stage
// but pixel, which the backend samples at an explicit LOD (spirvEmitterImage.cpp).
bool Analysis::SampleIsLaneLocal(const Inst& inst) const {
	if (m_program.stage != ShaderType::Pixel) {
		return true;
	}
	const auto index = inst.Flags<MemoryFlags>().index;
	return index < m_program.memory_info.size() &&
	       (m_program.memory_info[index].image_sample_flags &
	        (Decoder::ImageSampleFlagLod | Decoder::ImageSampleFlagLevelZero |
	         Decoder::ImageSampleFlagDerivative)) != 0;
}

// Adds the lanes under which `user` observes its operand.
void Analysis::Contribute(Demand& demand, const Inst& user, size_t operand) const {
	const auto order = m_order.find(&user);
	if (order == m_order.end()) {
		demand = Everyone();
		return;
	}
	const auto& observed = m_demand[order->second];
	const auto  opcode   = user.GetOpcode();
	switch (opcode) {
		case ValueOpcode::Phi: {
			const auto* header = user.Parent();
			Merge(demand, Dominates(header, user.PhiBlock(operand)) ? Hoist(observed, header)
			                                                        : observed);
			return;
		}
		case ValueOpcode::SelectU1:
		case ValueOpcode::SelectU32:
		case ValueOpcode::SelectF32:
			Merge(demand, operand == 0 ? observed : Conjoin(observed, user.Arg(0), operand == 2));
			return;
		case ValueOpcode::LogicalAnd:
		case ValueOpcode::LogicalOr:
			// The second operand (a translated conjunction's compare) matters only where the first leaves
			// the result open; the first keeps the whole demand. Narrowing both would leave a lane where
			// both decide the result (both false for an And) to neither, and changing both there (when
			// they share a replaced select) changes the result.
			Merge(demand, operand == 1 ? Conjoin(observed, user.Arg(0), opcode == ValueOpcode::LogicalOr)
			                           : observed);
			return;
		case ValueOpcode::DppMoveU32:
			// Without fetch-inactive, a source lane outside EXEC reads as zero (EmitValueFlow).
			if (operand == 0 && !user.Flags<DppMoveFlags>().fetch_inactive) {
				Merge(demand, Conjoin(Everyone(), user.Arg(1), false));
				return;
			}
			break;
		case ValueOpcode::DppUpdateU32:
			// The new value is written only within EXEC; the old one may remain anywhere.
			if (operand == 0) {
				Merge(demand, Conjoin(observed, user.Arg(2), false));
				return;
			}
			if (operand == 1) {
				Merge(demand, observed);
				return;
			}
			break;
		case ValueOpcode::ReadFirstLane:
		case ValueOpcode::SetAttribute:
			// The first lane of its EXEC operand; an export of the lanes of its EXEC operand.
			if (operand == 0) {
				Merge(demand, Conjoin(Everyone(), user.Arg(1), false));
				return;
			}
			break;
		case ValueOpcode::ImageSampleRaw:
			if (operand == 2 && SampleIsLaneLocal(user)) {
				Merge(demand, observed);
				return;
			}
			break;
		default:
			if (IsLaneLocal(opcode)) {
				Merge(demand, observed);
				return;
			}
			if (IsMaskedAccess(opcode) && operand + 1 < user.NumArgs()) {
				// Performed for its EXEC lanes; a load also gives the other lanes a value (zero, or
				// what an unguarded storage buffer load reads), which its users may observe.
				Merge(demand, Conjoin(Everyone(), user.Arg(user.NumArgs() - 1), false));
				if (user.GetType() != Type::Void) {
					Merge(demand, observed);
				}
				return;
			}
			break;
	}
	// Cross-lane reads, branch conditions, descriptors, EXEC operands and anything unknown.
	demand = Everyone();
}

Demand Analysis::Compute(const Inst& inst) const {
	Demand demand;
	for (const auto& use: inst.Uses()) {
		Contribute(demand, *use.user, use.operand);
		if (IsEveryone(demand)) {
			break;
		}
	}
	return demand;
}

void Analysis::Solve() {
	// Demand flows from uses to definitions: start at the end, and revisit the operands of a
	// value whose demand changed. A demand of every lane cannot change any more.
	std::vector<uint32_t> pending(m_insts.size());
	std::vector<uint8_t>  queued(m_insts.size(), 1);
	std::iota(pending.begin(), pending.end(), 0u);
	while (!pending.empty()) {
		const auto index = pending.back();
		pending.pop_back();
		queued[index] = 0;
		auto& demand  = m_demand[index];
		if (IsEveryone(demand)) {
			continue;
		}
		auto next = Compute(*m_insts[index]);
		if (next == demand) {
			continue;
		}
		demand = ++m_updates[index] > MaxUpdates ? Everyone() : std::move(next);
		const auto& inst = *m_insts[index];
		for (size_t arg = 0; arg < inst.NumArgs(); arg++) {
			const auto* definition = inst.Arg(arg).TryInstruction();
			const auto  order      = definition != nullptr ? m_order.find(definition) : m_order.end();
			if (order != m_order.end() && queued[order->second] == 0) {
				queued[order->second] = 1;
				pending.push_back(order->second);
			}
		}
	}
}

bool Analysis::Implies(const Term& term, Value mask, bool negated) const {
	std::vector<Proof> proofs;
	return Proves(term, mask, negated, proofs, 0) || TruthTable(term, mask, negated);
}

// Syntactic implication: the mask is one of the term's literals, or follows from them through
// its conjunctions, disjunctions and negations.
bool Analysis::Proves(const Term& term, Value mask, bool negated, std::vector<Proof>& proofs,
                      size_t depth) const {
	mask = mask.Resolve();
	if (mask.IsImmediate()) {
		return mask.U1() != negated;
	}
	const auto* inst = mask.Instruction();
	if (Contains(term, inst, negated)) {
		return true;
	}
	const auto opcode = inst->GetOpcode();
	if (opcode != ValueOpcode::LogicalNot && opcode != ValueOpcode::LogicalAnd &&
	    opcode != ValueOpcode::LogicalOr) {
		return false;
	}
	for (const auto& proof: proofs) {
		if (proof.mask == inst && proof.negated == negated) {
			return proof.holds;
		}
	}
	if (proofs.size() == MaxNodes || depth == MaxNodes) {
		return false;
	}
	bool holds = false;
	if (opcode == ValueOpcode::LogicalNot) {
		holds = Proves(term, inst->Arg(0), !negated, proofs, depth + 1);
	} else {
		// A conjunction (a disjunction when negated) needs both operands, otherwise either.
		const bool both = (opcode == ValueOpcode::LogicalAnd) != negated;
		holds           = Proves(term, inst->Arg(0), negated, proofs, depth + 1);
		if (holds == both) {
			holds = Proves(term, inst->Arg(1), negated, proofs, depth + 1);
		}
	}
	proofs.push_back({inst, negated, holds});
	return holds;
}

// Implication over every assignment of the atoms (values that are not boolean operators) of the
// term and the mask. Atoms are free variables: a relation between two of them is not known, which
// only fails proofs. A mask with an atom the term does not constrain is not tried.
bool Analysis::TruthTable(const Term& term, Value mask, bool negated) const {
	struct Node {
		const Inst* inst = nullptr;
		bool        atom = false;
	};
	std::vector<Node> nodes; // operands before their users
	size_t            atoms    = 0;
	bool              fits     = true;
	const auto        index_of = [&](const Inst* inst) {
		return static_cast<size_t>(std::ranges::find(nodes, inst, &Node::inst) - nodes.begin());
	};
	const auto collect = [&](auto& self, Value value, bool from_mask, size_t depth) -> void {
		value            = value.Resolve();
		const auto* inst = value.TryInstruction();
		if (!fits || inst == nullptr || index_of(inst) != nodes.size()) {
			return;
		}
		const bool atom = !IsBooleanOperator(inst->GetOpcode());
		if (!atom && depth < MaxNodes) {
			for (size_t arg = 0; arg < inst->NumArgs(); arg++) {
				self(self, inst->Arg(arg), from_mask, depth + 1);
			}
		}
		if (depth == MaxNodes || nodes.size() == MaxNodes ||
		    (atom && (from_mask || ++atoms > MaxAtoms))) {
			fits = false;
			return;
		}
		nodes.push_back({inst, atom});
	};
	for (const auto& literal: term) {
		collect(collect, Value(const_cast<Inst*>(literal.mask)), false, 0);
	}
	collect(collect, mask, true, 0);
	if (!fits) {
		return false;
	}
	std::vector<uint8_t> values(nodes.size());
	const auto           value_of = [&](Value value) {
		value = value.Resolve();
		return value.IsImmediate() ? value.U1() : values[index_of(value.Instruction())] != 0;
	};
	for (uint32_t assignment = 0; assignment < (1u << atoms); assignment++) {
		size_t atom = 0;
		for (size_t index = 0; index < nodes.size(); index++) {
			const auto* inst  = nodes[index].inst;
			bool        value = false;
			if (nodes[index].atom) {
				value = ((assignment >> atom++) & 1u) != 0u;
			} else {
				switch (inst->GetOpcode()) {
					case ValueOpcode::LogicalAnd:
						value = value_of(inst->Arg(0)) && value_of(inst->Arg(1));
						break;
					case ValueOpcode::LogicalOr:
						value = value_of(inst->Arg(0)) || value_of(inst->Arg(1));
						break;
					case ValueOpcode::LogicalXor:
						value = value_of(inst->Arg(0)) != value_of(inst->Arg(1));
						break;
					case ValueOpcode::LogicalNot: value = !value_of(inst->Arg(0)); break;
					default:
						value = value_of(inst->Arg(0)) ? value_of(inst->Arg(1))
						                               : value_of(inst->Arg(2));
						break;
				}
			}
			values[index] = static_cast<uint8_t>(value);
		}
		const bool holds = std::ranges::all_of(term, [&](const Literal& literal) {
			return value_of(Value(const_cast<Inst*>(literal.mask))) != literal.negated;
		});
		if (holds && value_of(mask) == negated) {
			return false;
		}
	}
	return true;
}

// Select(m, x, Select(m', y, z)) is Select(m, x, z) when m' implies m, and
// Select(m, Select(m', y, z), w) is Select(m, y, w) when m implies m': repeated writes of one
// register under one mask stop chaining through each other's old values.
uint32_t Analysis::ShortenChains() {
	uint32_t shortened = 0;
	for (auto* inst: m_insts) {
		if (!IsSelect(inst->GetOpcode())) {
			continue;
		}
		const auto condition = inst->Arg(0);
		for (const size_t operand: {size_t {2}, size_t {1}}) {
			for (;;) {
				auto* inner = inst->Arg(operand).Resolve().TryInstruction();
				if (inner == nullptr || inner == inst || inner->GetOpcode() != inst->GetOpcode() ||
				    !(operand == 2 ? MaskImplies(inner->Arg(0), condition)
				                   : MaskImplies(condition, inner->Arg(0)))) {
					break;
				}
				inst->SetArg(operand, inner->Arg(operand).Resolve());
				shortened++;
			}
		}
	}
	return shortened;
}

uint32_t Analysis::Apply() {
	std::vector<std::pair<Inst*, Value>> replacements;
	for (size_t index = 0; index < m_insts.size(); index++) {
		auto& inst = *m_insts[index];
		if (!IsSelect(inst.GetOpcode()) || !inst.HasUses()) {
			continue;
		}
		const auto condition = inst.Arg(0);
		const auto every     = [&](bool negated) {
			return std::ranges::all_of(m_demand[index].terms, [&](const Term& term) {
				return Implies(term, condition, negated);
			});
		};
		if (every(false)) {
			replacements.emplace_back(&inst, inst.Arg(1));
		} else if (every(true)) {
			replacements.emplace_back(&inst, inst.Arg(2));
		}
	}
	// A replaced operand that is itself replaced resolves through its Identity.
	for (auto& [inst, value]: replacements) {
		inst->ReplaceUsesWith(value.Resolve());
	}
	return static_cast<uint32_t>(replacements.size());
}

} // namespace

bool ExecDemandEnabled() {
	static const bool enabled = [] {
		const char* value = std::getenv("KYTY_EXEC_DEMAND");
		return value != nullptr && value[0] == '1';
	}();
	return enabled;
}

ExecDemandStats EliminateMaskedSelects(Program& program) {
	ExecDemandStats stats;
	Analysis        analysis(program);
	if (!analysis.Prepare()) {
		return stats;
	}
	stats.chains_shortened = analysis.ShortenChains();
	analysis.Solve();
	stats.selects_removed = analysis.Apply();
	return stats;
}

} // namespace Libs::Graphics::ShaderRecompiler::IR
