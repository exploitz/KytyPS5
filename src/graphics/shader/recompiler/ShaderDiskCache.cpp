#include "graphics/shader/recompiler/ShaderDiskCache.h"

#include "graphics/shader/recompiler/ir/passes/SrtWalker.h"

#include <bit>
#include <cstring>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>

namespace Libs::Graphics::ShaderRecompiler::DiskCache {

namespace {

constexpr uint32_t PlanMagic   = 0x4e4c5052; // "RPLN"
constexpr uint32_t ModuleMagic = 0x444f4d45; // "EMOD"

// One code path for both directions (upstream #988): every Serialize below reads or writes its
// fields depending on how the archive was opened, so the writer and the reader cannot drift apart.
class Archive {
public:
	explicit Archive(std::vector<uint8_t>& out): m_out(&out) {}
	explicit Archive(std::span<const uint8_t> in): m_in(in) {}

	[[nodiscard]] bool Writing() const { return m_out != nullptr; }
	[[nodiscard]] bool Ok() const { return m_ok; }
	void               Fail() { m_ok = false; }

	void Bytes(void* data, size_t size) {
		if (!m_ok) return;
		if (Writing()) {
			const auto* bytes = static_cast<const uint8_t*>(data);
			m_out->insert(m_out->end(), bytes, bytes + size);
		} else if (size > m_in.size() - m_pos) {
			m_ok = false;
		} else {
			if (size != 0) std::memcpy(data, m_in.data() + m_pos, size);
			m_pos += size;
		}
	}

	template <typename T>
	requires(std::is_arithmetic_v<T> || std::is_enum_v<T>)
	void Scalar(T& value) {
		Bytes(&value, sizeof(T));
	}

	// Element counts are checked against what is left before anything is allocated.
	uint32_t Count(size_t size, size_t min_element_bytes) {
		auto count = static_cast<uint32_t>(size);
		Scalar(count);
		if (!Writing() && m_ok &&
		    (count > m_in.size() - m_pos || static_cast<uint64_t>(count) * min_element_bytes > m_in.size() - m_pos)) {
			m_ok = false;
		}
		return m_ok ? count : 0;
	}

	[[nodiscard]] bool AtEnd() const { return Writing() || m_pos == m_in.size(); }

private:
	std::vector<uint8_t>*    m_out = nullptr;
	std::span<const uint8_t> m_in;
	size_t                   m_pos = 0;
	bool                     m_ok  = true;
};

template <typename T>
requires(std::is_arithmetic_v<T> || std::is_enum_v<T>)
void Serialize(Archive& ar, T& value) {
	ar.Scalar(value);
}

template <typename T, size_t N>
void Serialize(Archive& ar, std::array<T, N>& values) {
	for (auto& value: values) Serialize(ar, value);
}

template <typename T, size_t N>
void Serialize(Archive& ar, T (&values)[N]) {
	for (auto& value: values) Serialize(ar, value);
}

template <typename T>
void Serialize(Archive& ar, std::vector<T>& values) {
	const auto count = ar.Count(values.size(), 1);
	if (!ar.Writing()) values.resize(count);
	for (auto& value: values) Serialize(ar, value);
}

void Serialize(Archive& ar, std::string& value) {
	const auto count = ar.Count(value.size(), 1);
	if (!ar.Writing()) value.resize(count);
	ar.Bytes(value.data(), count);
}

template <typename... Fields>
void All(Archive& ar, Fields&... fields) {
	(Serialize(ar, fields), ...);
}

// Values refer to instructions by position in ResourcePlan::value_storage.
struct ValueTable {
	std::unordered_map<const IR::Inst*, uint32_t> index;
	std::vector<IR::Inst*>                        insts;
};

enum class ValueTag : uint8_t { Empty, Inst, ScalarReg, VectorReg, U1, U8, U16, U32, U64, F16, F32 };

void Serialize(Archive& ar, IR::Value& value, ValueTable& table) {
	ValueTag tag  = ValueTag::Empty;
	uint64_t bits = 0;
	if (ar.Writing()) {
		if (const auto* inst = value.TryInstruction(); inst != nullptr) {
			const auto found = table.index.find(inst);
			if (found == table.index.end()) {
				ar.Fail();
				return;
			}
			tag  = ValueTag::Inst;
			bits = found->second;
		} else {
			switch (value.GetType()) {
				case IR::Type::Void: break;
				case IR::Type::ScalarReg:
					tag  = ValueTag::ScalarReg;
					bits = static_cast<uint64_t>(value.ScalarRegister());
					break;
				case IR::Type::VectorReg:
					tag  = ValueTag::VectorReg;
					bits = static_cast<uint64_t>(value.VectorRegister());
					break;
				case IR::Type::U1: tag = ValueTag::U1, bits = value.U1() ? 1u : 0u; break;
				case IR::Type::U8: tag = ValueTag::U8, bits = value.U8(); break;
				case IR::Type::U16: tag = ValueTag::U16, bits = value.U16(); break;
				case IR::Type::U32: tag = ValueTag::U32, bits = value.U32(); break;
				case IR::Type::U64: tag = ValueTag::U64, bits = value.U64(); break;
				case IR::Type::F16: tag = ValueTag::F16, bits = value.F16Bits(); break;
				case IR::Type::F32: tag = ValueTag::F32, bits = std::bit_cast<uint32_t>(value.F32Value()); break;
				default: ar.Fail(); return;
			}
		}
	}
	ar.Scalar(tag);
	ar.Scalar(bits);
	if (ar.Writing() || !ar.Ok()) return;
	switch (tag) {
		case ValueTag::Empty: value = IR::Value(); break;
		case ValueTag::Inst:
			if (bits >= table.insts.size()) {
				ar.Fail();
				return;
			}
			value = IR::Value(table.insts[bits]);
			break;
		case ValueTag::ScalarReg: value = IR::Value(static_cast<IR::ScalarReg>(bits)); break;
		case ValueTag::VectorReg: value = IR::Value(static_cast<IR::VectorReg>(bits)); break;
		case ValueTag::U1: value = IR::Value(bits != 0); break;
		case ValueTag::U8: value = IR::Value(static_cast<uint8_t>(bits)); break;
		case ValueTag::U16: value = IR::Value(static_cast<uint16_t>(bits)); break;
		case ValueTag::U32: value = IR::Value(static_cast<uint32_t>(bits)); break;
		case ValueTag::U64: value = IR::Value(bits); break;
		case ValueTag::F16: value = IR::Value::F16(static_cast<uint16_t>(bits)); break;
		case ValueTag::F32: value = IR::Value::F32(std::bit_cast<float>(static_cast<uint32_t>(bits))); break;
		default: ar.Fail(); break;
	}
}

template <size_t N>
void Serialize(Archive& ar, std::array<IR::Value, N>& values, ValueTable& table) {
	for (auto& value: values) Serialize(ar, value, table);
}

void Serialize(Archive& ar, IR::MemoryInfo& v) {
	All(ar, v.kind, v.resource, v.sampler, v.offset, v.secondary_offset, v.dmask, v.data_dwords, v.data_bits,
	    v.component_index, v.component_count, v.data_format, v.number_format, v.image_sample_flags, v.image_dimension,
	    v.image_address_components, v.image_nsa_dwords, v.image_nsa_addr, v.memory_segment, v.address_is_full,
	    v.data_signed, v.typed, v.formatted, v.image_has_mip, v.image_r128, v.glc, v.slc, v.idxen, v.offen,
	    v.planning_only);
}

void Serialize(Archive& ar, IR::BufferResource& v) {
	All(ar, v.source, v.first_use_pc, v.max_byte_extent, v.packed_stride, v.descriptor_format, v.descriptor_swizzle,
	    v.image_alias, v.read, v.written, v.atomic, v.formatted, v.scalar, v.byte_base_offset);
}

void Serialize(Archive& ar, IR::ImageResource& v) {
	All(ar, v.source, v.first_use_pc, v.resource_class, v.numeric_class, v.dimension, v.mip_mode, v.mip_count,
	    v.conversion_format, v.shader_swizzle, v.read, v.written, v.atomic, v.depth_compare, v.cube, v.r128,
	    v.indirect_root, v.indirect_mapping_offset, v.indirect_search_iterations, v.indirect_resources);
}

void Serialize(Archive& ar, IR::SamplerResource& v) {
	All(ar, v.source, v.first_use_pc, v.force_point_filtering, v.depth_compare);
}

void Serialize(Archive& ar, IR::SampledResourcePair& v) {
	All(ar, v.image, v.sampler, v.first_use_pc);
}

void Serialize(Archive& ar, IR::StageInput& v) {
	All(ar, v.kind, v.location, v.component_count, v.debug_name, v.per_vertex);
}

void Serialize(Archive& ar, IR::StageOutput& v) {
	All(ar, v.kind, v.index, v.location, v.debug_name);
}

void Serialize(Archive& ar, IR::ShaderInfo& v) {
	All(ar, v.buffers, v.images, v.samplers, v.sampled_pairs, v.inputs, v.outputs, v.vertex_fetch_components,
	    v.vertex_offset_sgpr, v.instance_offset_sgpr, v.has_bitwise_xor, v.uses_dma);
}

void Serialize(Archive& ar, IR::DescriptorBinding& v) {
	All(ar, v.kind, v.resources);
}

void Serialize(Archive& ar, IR::BindingLayout& v) {
	All(ar, v.push_data_start_dword, v.memory_offset_dword, v.memory_offset_count, v.buffer_word_count,
	    v.lod_stats_count, v.table_block_dwords, v.user_data_registers, v.descriptors);
}

void Serialize(Archive& ar, IR::BdaReadWord& v) {
	All(ar, v.kind, v.value);
}

void Serialize(Archive& ar, IR::BdaReadSpan& v) {
	All(ar, v.low, v.high, v.begin, v.end, v.scalar_base);
}

void Serialize(Archive& ar, IR::BdaReadPlan& v) {
	All(ar, v.spans, v.complete);
}

void Serialize(Archive& ar, IR::TablePlan::Operand& v) {
	All(ar, v.kind, v.value);
}

void Serialize(Archive& ar, IR::TablePlan::Slot& v) {
	All(ar, v.low, v.high, v.offset, v.immediate);
}

void Serialize(Archive& ar, IR::TablePlan::Descriptor& v) {
	All(ar, v.words, v.count);
}

void Serialize(Archive& ar, IR::TablePlan& v) {
	All(ar, v.slots, v.buffers, v.images, v.samplers, v.gpu, v.cpu, v.global_memory);
}

void Serialize(Archive& ar, IR::CompiledShaderInfo& v) {
	All(ar, v.bda_read_plan, v.stage, v.shader_hash, v.wave_size, v.user_data_base, v.user_data_count,
	    v.scratch_dwords, v.param_export_mask, v.table_mode, v.table_plan, v.info, v.bindings);
}

void Serialize(Archive& ar, IR::ResourceSpecialization::Buffer& v) {
	All(ar, v.packed_stride, v.descriptor_format, v.descriptor_swizzle, v.byte_base_offset);
}

void Serialize(Archive& ar, IR::ResourceSpecialization::Image& v) {
	All(ar, v.numeric_class, v.dimension, v.mip_count, v.conversion_format, v.shader_swizzle, v.indirect_root,
	    v.indirect_mapping_offset, v.indirect_search_iterations, v.cube, v.fmask);
}

void Serialize(Archive& ar, IR::ResourceSpecialization& v) {
	All(ar, v.buffers, v.images);
}

void Serialize(Archive& ar, IR::UniformFill& v) {
	All(ar, v.kind, v.resource, v.group_stride, v.words, v.value);
}

void Serialize(Archive& ar, IR::DescriptorSource::IndirectImage& v) {
	All(ar, v.material_source, v.heap_source, v.selector_stride, v.selector_offset, v.key_arg, v.table_offset,
	    v.key_bound, v.bound_source, v.bound_signed, v.item_bound, v.selector_immediate);
}

void Serialize(Archive& ar, IR::DescriptorSource& v, ValueTable& table) {
	Serialize(ar, v.dwords, table);
	Serialize(ar, v.dword_count);
	bool has_indirect = v.indirect_image.has_value();
	Serialize(ar, has_indirect);
	if (!has_indirect) {
		v.indirect_image.reset();
		return;
	}
	// (Assigned, not emplace(): libstdc++ with clang takes IndirectImage, a nested struct with default member
	// initializers, for not default-constructible, as std::optional's member is declared before they are complete.)
	if (!ar.Writing()) v.indirect_image = IR::DescriptorSource::IndirectImage {};
	Serialize(ar, *v.indirect_image);
}

void Serialize(Archive& ar, IR::ResourceBlock& v, ValueTable& table) {
	Serialize(ar, v.condition, table);
	All(ar, v.successors, v.sources);
}

void Serialize(Archive& ar, IR::ResourcePlan& plan, ValueTable& table) {
	All(ar, plan.stage, plan.shader_hash, plan.user_data_base, plan.user_data_count);

	// Instructions first, so that operands may refer to any of them (loops through phis).
	const auto inst_count = ar.Count(plan.value_storage.size(), 4);
	if (ar.Writing()) {
		for (auto& inst: plan.value_storage) {
			table.index.emplace(&inst, static_cast<uint32_t>(table.insts.size()));
			table.insts.push_back(&inst);
		}
		for (auto* inst: table.insts) {
			auto opcode = inst->GetOpcode();
			auto flags  = inst->Flags<uint64_t>();
			auto args   = static_cast<uint32_t>(inst->NumArgs());
			All(ar, opcode, flags, args);
		}
		for (auto* inst: table.insts) {
			for (size_t i = 0; i < inst->NumArgs(); ++i) {
				auto arg = inst->Arg(i);
				Serialize(ar, arg, table);
			}
		}
	} else {
		std::vector<uint32_t> arg_counts;
		for (uint32_t i = 0; i < inst_count && ar.Ok(); ++i) {
			IR::ValueOpcode opcode {};
			uint64_t        flags = 0;
			uint32_t        args  = 0;
			All(ar, opcode, flags, args);
			if (!ar.Ok()) break;
			if (static_cast<uint32_t>(opcode) >= static_cast<uint32_t>(IR::ValueOpcode::Count) ||
			    (opcode != IR::ValueOpcode::Phi && IR::NumArgsOf(opcode) != args)) {
				ar.Fail();
				break;
			}
			table.insts.push_back(&plan.value_storage.emplace_back(opcode, flags));
			arg_counts.push_back(args);
		}
		for (uint32_t i = 0; i < table.insts.size() && ar.Ok(); ++i) {
			auto* inst = table.insts[i];
			for (uint32_t arg_index = 0; arg_index < arg_counts[i] && ar.Ok(); ++arg_index) {
				IR::Value arg;
				Serialize(ar, arg, table);
				if (!ar.Ok()) break;
				if (inst->GetOpcode() == IR::ValueOpcode::Phi) {
					inst->AddPhiOperand(nullptr, arg);
				} else {
					inst->SetArg(arg_index, arg);
				}
			}
		}
	}

	Serialize(ar, plan.memory_info);
	const auto source_count = ar.Count(plan.descriptor_sources.size(), 1);
	if (!ar.Writing()) plan.descriptor_sources.resize(source_count);
	for (auto& source: plan.descriptor_sources) Serialize(ar, source, table);

	const auto block_count = ar.Count(plan.control_flow.size(), 1);
	if (!ar.Writing()) plan.control_flow.resize(block_count);
	for (auto& block: plan.control_flow) Serialize(ar, block, table);

	All(ar, plan.materialization_sources);
	const auto read_count = ar.Count(plan.srt_reads.size(), 1);
	if (!ar.Writing()) plan.srt_reads.resize(read_count);
	for (auto& read: plan.srt_reads) {
		Serialize(ar, read.value, table);
		Serialize(ar, read.flat_offset);
	}
	All(ar, plan.clean_flat_slots, plan.requires_specialization_memory, plan.srt_plan_complete,
	    plan.resource_tracking_complete, plan.info, plan.uniform_fill.fill);
	Serialize(ar, plan.uniform_fill.values, table);
}

} // namespace

std::vector<uint8_t> SerializePlan(const IR::ResourcePlan& plan) {
	std::vector<uint8_t> out;
	Archive              ar(out);
	auto                 magic   = PlanMagic;
	auto                 version = kFormatVersion;
	All(ar, magic, version);
	ValueTable table;
	// Writing only reads the plan.
	Serialize(ar, const_cast<IR::ResourcePlan&>(plan), table);
	if (!ar.Ok()) return {};
	return out;
}

bool DeserializePlan(std::span<const uint8_t> data, IR::ResourcePlan& plan) {
	Archive  ar(data);
	uint32_t magic   = 0;
	uint32_t version = 0;
	All(ar, magic, version);
	if (!ar.Ok() || magic != PlanMagic || version != kFormatVersion) return false;
	IR::ResourcePlan result;
	ValueTable       table;
	Serialize(ar, result, table);
	if (!ar.Ok() || !ar.AtEnd()) return false;
	IR::BuildLinearSrtPlan(result);
	plan = std::move(result);
	return true;
}

std::vector<uint8_t> SerializeModule(const IR::ResourceSpecialization& specialization,
                                     const IR::CompiledShaderInfo&     info) {
	std::vector<uint8_t> out;
	Archive              ar(out);
	auto                 magic   = ModuleMagic;
	auto                 version = kFormatVersion;
	All(ar, magic, version);
	Serialize(ar, const_cast<IR::ResourceSpecialization&>(specialization));
	Serialize(ar, const_cast<IR::CompiledShaderInfo&>(info));
	if (!ar.Ok()) return {};
	return out;
}

bool DeserializeModule(std::span<const uint8_t> data, IR::ResourceSpecialization& specialization,
                       IR::CompiledShaderInfo& info) {
	Archive  ar(data);
	uint32_t magic   = 0;
	uint32_t version = 0;
	All(ar, magic, version);
	if (!ar.Ok() || magic != ModuleMagic || version != kFormatVersion) return false;
	IR::ResourceSpecialization specialization_result;
	IR::CompiledShaderInfo     info_result;
	Serialize(ar, specialization_result);
	Serialize(ar, info_result);
	if (!ar.Ok() || !ar.AtEnd()) return false;
	specialization = std::move(specialization_result);
	info           = std::move(info_result);
	return true;
}

} // namespace Libs::Graphics::ShaderRecompiler::DiskCache
