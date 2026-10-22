#pragma once

#include "graphics/shader/recompiler/ir/ShaderIR.h"

namespace Libs::Graphics::ShaderRecompiler::IR {

struct ExecDemandStats {
	uint32_t selects_removed  = 0; // selects replaced by the operand every observing lane takes
	uint32_t chains_shortened = 0; // Select(m, x, Select(m', y, z)) -> Select(m, x, z), m' implies m
};

// The default of CompileOptions::exec_demand: a translation feature (Frontend::TranslatorFeatureExecDemand). It
// changes the SPIR-V the recompiler produces, so it is on where the static pipeline store was precompiled with it
// (its stamp), and a store made without it keeps it off until the precompile runs again (no compile stalls after an
// update); KYTY_EXEC_DEMAND=0/1 forces it. The translation store keys on the features (pipelineCache.cpp
// StoreSwitches). Measured on chen's 5f9ef5e3, 120 Hz vblank, the session (5 worlds x 2, 30 legs, launches
// alternating off on on off): fps +9.4% (every leg), 1% low +6.6% (27 of 30).
bool ExecDemandEnabled();

// Demand-driven removal of the lane predication the translator puts on every VGPR write
// (Select(exec, value, old)): for each value, the lane masks under which some use can observe it.
// A select whose every such mask implies its condition becomes its true operand (or its false
// operand when they all imply the negation). Lane-local operations pass the demand through, a
// memory access or export demands its operands under its own EXEC argument, and cross-lane
// operations, branch conditions and anything unknown demand every lane. Demand crossing a loop's
// back edge keeps only masks defined before the loop. Programs on the dispatcher fallback, and
// irreducible ones, are left alone. Run RemoveIdentities and EliminateDeadCode afterwards.
ExecDemandStats EliminateMaskedSelects(Program& program);

} // namespace Libs::Graphics::ShaderRecompiler::IR
