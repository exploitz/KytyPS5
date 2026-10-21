#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SHADERRECOMPILER_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SHADERRECOMPILER_H_

#include "common/common.h"
#include "common/stringUtils.h"
#include "graphics/shader/recompiler/ir/passes/ExecDemand.h"
#include "graphics/shader/recompiler/ir/passes/ResourceMaterialization.h"
#include "graphics/shader/shader.h"

#include <span>
#include <vector>

namespace Libs::Graphics::ShaderRecompiler {

// KYTY_LANE_LOCAL_TITLE: the title list (four Demon's Souls lighting and fog programs lane-local by hash, Translate.cpp)
// for every translation (1), or per permutation as kyty_local_lane_local_title is when it is made ("live": a
// same-process A/B, each side's permutations kept apart, those of the side switched to translated when first met), or
// off (anything else).
bool LaneLocalTitleDefault();
bool LaneLocalTitleLive();
bool LaneLocalTitleLiveOn();
bool LaneLocalTitleProgram(uint64_t hash);

struct CompileOptions {
	ShaderType                  stage           = ShaderType::Compute;
	uint32_t                    wave_size       = 64;
	uint32_t                    user_data_base  = 0;
	uint32_t                    scratch_dwords  = 0;
	uint64_t                    shader_hash     = 0;
	bool                        dump_ir                    = true;
	bool                        early_dump                 = false;
	bool                        enable_lod_stats           = false;
	bool                        table_mode                 = false; // IR::EnterTableMode
	bool                        exec_demand                = IR::ExecDemandEnabled(); // IR::EliminateMaskedSelects
	bool                        lane_local_title           = LaneLocalTitleDefault(); // the title list (see above)
	const char*                 dump_label                 = nullptr;
	std::span<const uint32_t>   user_data;
	std::span<const uint32_t>   back_code;
	ShaderStageInputInfo        input_info;
};

struct TranslateResult {
	IR::Program program;
	std::string decoded_dump;
	std::string cfg_dump;
};

struct CompileResult {
	std::vector<uint32_t>  spirv;
	std::string            decoded_dump;
	std::string            ir_dump;
	IR::Program            program;
};

// Storage buffer accesses without an explicit bounds check where the device checks them the same
// way: robustBufferAccess2 with byte-exact ranges (robustStorageBufferAccessSizeAlignment 1) reads
// zero out of bounds and drops the stores. Set with the device, before any program compiles.
void SetDeviceStorageBufferBounds(bool enabled);

[[nodiscard]] TranslateResult TranslateProgram(std::span<const uint32_t> code,
                                               const CompileOptions& options);
[[nodiscard]] CompileResult CompileProgram(TranslateResult translated,
                                           const CompileOptions& options,
                                           const IR::ResourceSpecialization& specialization,
	                                       uint32_t push_data_start_dword = 0);
// A CompileResult's SPIR-V the caller is done with: the thread's next CompileProgram writes its module there.
void RecycleSpirv(std::vector<uint32_t>&& spirv);

} // namespace Libs::Graphics::ShaderRecompiler

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SHADERRECOMPILER_H_ */
