#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SHADERDISKCACHE_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SHADERDISKCACHE_H_

#include "graphics/shader/recompiler/ir/ShaderIR.h"
#include "graphics/shader/recompiler/ir/passes/ResourceMaterialization.h"

#include <cstdint>
#include <span>
#include <vector>

// Serialization of what a translation leaves the shader cache, for keeping translations on disk
// (after upstream PR #988, extended to this tree's plan): a program's resource plan (the value graph
// MaterializeResources evaluates, its linear SRT program rebuilt on load) and a module's shader info
// with the specialization it was compiled for. Nothing depends on user data or guest memory. Readers
// are bounds-checked and return false on malformed input. Every field is written by name: a field
// added to these structures needs its line here (the recompiler's source signature keys the files, so
// the change also invalidates what was kept before; KYTY_SHADER_STORE_VERIFY=1 compares kept
// translations with fresh ones).
namespace Libs::Graphics::ShaderRecompiler::DiskCache {

// Bumped by hand when the layout written by this file changes.
inline constexpr uint32_t kFormatVersion = 1;

// Empty: the plan holds a value this format cannot express (it is then not kept).
[[nodiscard]] std::vector<uint8_t> SerializePlan(const IR::ResourcePlan& plan);
// The plan's linear SRT program is rebuilt (BuildLinearSrtPlan), as ExtractResourcePlan builds it.
[[nodiscard]] bool DeserializePlan(std::span<const uint8_t> data, IR::ResourcePlan& plan);

[[nodiscard]] std::vector<uint8_t> SerializeModule(const IR::ResourceSpecialization& specialization,
                                                   const IR::CompiledShaderInfo&     info);
[[nodiscard]] bool DeserializeModule(std::span<const uint8_t> data, IR::ResourceSpecialization& specialization,
                                     IR::CompiledShaderInfo& info);

} // namespace Libs::Graphics::ShaderRecompiler::DiskCache

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SHADERDISKCACHE_H_ */
