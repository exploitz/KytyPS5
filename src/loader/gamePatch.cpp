#include "loader/gamePatch.h"

#include "loader/gamePatchLocate.h"

#include "common/stringUtils.h"
#include "common/virtualMemory.h"
#include "kernel/memory.h"
#include "loader/elf.h"
#include "loader/runtimeLinker.h"
#include "loader/systemContent.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

namespace Loader::GamePatch {

namespace {

constexpr uint64_t kPageSize = 0x4000;

// One cheat (a GoldHEN "mod"): its writes are placed together, or not at all.
struct Mod {
	std::string           name;
	std::vector<Write>    writes;
	std::vector<uint64_t> cave_pages;
};

struct Plan {
	std::string      title_id;
	std::string      version;
	std::string      process;
	std::vector<Mod> mods;
	bool             same_version = true;
};

using Json = nlohmann::json;

std::unique_ptr<Plan> g_pending_plan;
std::vector<uint64_t> g_applied_cave_pages;

bool Fail(std::string* error, std::string message) {
	*error = std::move(message);
	return false;
}

bool ParseBytes(const std::string& text, std::vector<uint8_t>* bytes) {
	if (text.empty() || (text.size() % 2) != 0) {
		return false;
	}

	bytes->resize(text.size() / 2);
	for (size_t index = 0; index < bytes->size(); index++) {
		(*bytes)[index] = static_cast<uint8_t>(std::stoul(text.substr(index * 2, 2), nullptr, 16));
	}
	return true;
}

bool LoadPlan(const std::filesystem::path& path, Plan* plan, std::string* error) {
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		return Fail(error, "could not read cheat file");
	}

	const auto root = Json::parse(file, nullptr, false);
	if (!root.is_object() || !root.contains("id") || !root.contains("version") ||
	    !root.contains("process") || !root.contains("mods")) {
		return Fail(error, "expected a GoldHEN-style mods JSON file");
	}
	plan->title_id = root["id"].get<std::string>();
	plan->version  = root["version"].get<std::string>();
	plan->process  = root["process"].get<std::string>();

	for (const auto& entry: root["mods"]) {
		if (!entry.value("enabled", true)) {
			continue;
		}
		Mod mod;
		mod.name = entry["name"].get<std::string>();
		for (const auto& line: entry["memory"]) {
			Write write;
			write.source_address = std::stoull(line["offset"].get<std::string>(), nullptr, 16);
			if (!ParseBytes(line["off"].get<std::string>(), &write.off) ||
			    !ParseBytes(line["on"].get<std::string>(), &write.on) ||
			    write.off.size() != write.on.size()) {
				return Fail(error, "invalid mod memory bytes in " + mod.name);
			}
			mod.writes.push_back(std::move(write));
		}
		if (!mod.writes.empty()) {
			plan->mods.push_back(std::move(mod));
		}
	}
	return true;
}

// The game and its executable must be the file's; another version of it is allowed, each cheat then
// being placed only where its original bytes are found (Locate).
bool ValidateGame(Plan* plan, const Program* main_program, std::string* error) {
	if (main_program == nullptr || main_program->base_vaddr == 0) {
		return Fail(error, "main executable is not loaded");
	}
	std::string title_id;
	std::string version;
	if (!SystemContentParamSfoGetString("TITLE_ID", &title_id) ||
	    !SystemContentParamSfoGetString("APP_VER", &version) ||
	    !Common::EqualNoCase(main_program->file_name.filename().string(), plan->process) ||
	    !Common::EqualNoCase(title_id, plan->title_id)) {
		return Fail(error, "cheat file does not match the loaded game");
	}
	plan->same_version = version == plan->version;
	if (!plan->same_version) {
		::printf("Game cheat: the file is for version %s, the game is %s: each cheat is applied only where its "
		         "original code is found\n",
		         plan->version.c_str(), version.c_str());
	}
	return true;
}

// The module as the locator (gamePatchLocate.h) reads it.
Image ImageOf(const Program& program) {
	Image image;
	image.base        = program.base_vaddr;
	image.mapped_size = program.mapped_size;
	const auto* ehdr  = program.elf->GetEhdr();
	const auto* phdr  = program.elf->GetPhdr();
	for (Elf64_Half index = 0; index < ehdr->e_phnum; index++) {
		if (phdr[index].p_type == PT_LOAD || phdr[index].p_type == PT_OS_RELRO) {
			image.segments.push_back({phdr[index].p_vaddr, phdr[index].p_filesz});
		}
	}
	return image;
}

bool IsInsideProgram(const Program& program, uint64_t address, size_t size) {
	return size != 0 && address >= program.base_vaddr &&
	       address + size <= program.base_vaddr + program.mapped_size;
}

void ReleaseCaves(std::vector<uint64_t>* cave_pages) {
	for (auto page = cave_pages->rbegin(); page != cave_pages->rend(); ++page) {
		EXIT_IF(!Libs::LibKernel::Memory::FreeGuestMemory(*page, kPageSize));
	}
	cave_pages->clear();
}

bool AllocateCave(Mod* mod, const Program& program, std::string* error) {
	std::vector<uint64_t> pages;
	for (const auto& write: mod->writes) {
		if (IsInsideProgram(program, write.target_address, write.off.size())) {
			continue;
		}
		const auto write_end = write.target_address + write.off.size();
		for (auto page = write.target_address & ~(kPageSize - 1); page < write_end; page += kPageSize) {
			if (std::find(pages.begin(), pages.end(), page) == pages.end() &&
			    std::find(g_applied_cave_pages.begin(), g_applied_cave_pages.end(), page) == g_applied_cave_pages.end()) {
				pages.push_back(page);
			}
		}
	}

	for (const auto page: pages) {
		const auto allocated = Libs::LibKernel::Memory::AllocateRuntimeMemory(
		    page, kPageSize, Common::VirtualMemory::Mode::ExecuteReadWrite, "game_cheat_code_cave", true);
		if (allocated != page) {
			if (allocated != 0) {
				Libs::LibKernel::Memory::FreeGuestMemory(allocated, kPageSize);
			}
			ReleaseCaves(&mod->cave_pages);
			return Fail(error, "could not allocate code-cave page");
		}
		mod->cave_pages.push_back(page);
	}
	return true;
}

bool ApplyWrites(Mod* mod, const Program& program, std::string* error) {
	for (int external = 1; external >= 0; external--) {
		for (const auto& write: mod->writes) {
			if (static_cast<int>(!IsInsideProgram(program, write.target_address, write.on.size())) != external) {
				continue;
			}
			std::memcpy(reinterpret_cast<void*>(write.target_address), write.on.data(), write.on.size());
			if (!Common::VirtualMemory::FlushInstructionCache(write.target_address, write.on.size())) {
				return Fail(error, "could not finalize cheat write");
			}
		}
	}
	return true;
}

} // namespace

bool Apply(const std::filesystem::path& plan_path, Program* main_program, const std::vector<Program*>& programs) {
	Plan        plan;
	std::string error;
	if (!LoadPlan(plan_path, &plan, &error) || !ValidateGame(&plan, main_program, &error)) {
		::printf("Game cheat error: %s\n", error.c_str());
		return false;
	}
	if (plan.mods.empty()) {
		return true;
	}

	g_pending_plan = std::make_unique<Plan>(std::move(plan));
	for (auto* program: programs) {
		if (!ApplyPending(program)) {
			return false;
		}
		if (g_pending_plan == nullptr) {
			return true;
		}
	}
	::printf("Game cheat: waiting for the matching module\n");
	return true;
}

// The pending cheats into `program` when it is the file's executable: each cheat whose original code is
// found is applied; the others are reported and left out. False only when a write itself fails.
bool ApplyPending(Program* program) {
	if (g_pending_plan == nullptr || program == nullptr || program->elf == nullptr ||
	    !Common::EqualNoCase(program->file_name.filename().string(), g_pending_plan->process)) {
		return true;
	}
	auto       plan  = std::move(g_pending_plan);
	const auto image = ImageOf(*program);
	for (auto& mod: plan->mods) {
		int64_t shift = 0;
		switch (Locate(mod.writes, image, plan->same_version, &shift)) {
			case Found::AtOffset: break;
			case Found::Moved:
				::printf("Game cheat: %s: its original code is 0x%llx bytes %s its offset\n", mod.name.c_str(),
				         static_cast<unsigned long long>(shift < 0 ? -shift : shift), shift < 0 ? "before" : "after");
				break;
			case Found::NotFound:
				::printf("Game cheat skipped: %s: its original code is not in %s\n", mod.name.c_str(),
				         program->file_name.filename().string().c_str());
				continue;
			case Found::TooShort:
				::printf("Game cheat skipped: %s: not at its offset, and too little original code (under %zu bytes) "
				         "to find it elsewhere safely\n",
				         mod.name.c_str(), kMinSearchedBytes);
				continue;
			case Found::Ambiguous:
				::printf("Game cheat skipped: %s: its original code is found in more than one place\n", mod.name.c_str());
				continue;
		}
		std::string error;
		if (!AllocateCave(&mod, *program, &error)) {
			::printf("Game cheat skipped: %s: %s\n", mod.name.c_str(), error.c_str());
			continue;
		}
		if (!ApplyWrites(&mod, *program, &error)) {
			::printf("Game cheat error: %s: %s\n", mod.name.c_str(), error.c_str());
			return false;
		}
		::printf("Successfully applied cheat: %s\n", mod.name.c_str());
		g_applied_cave_pages.insert(g_applied_cave_pages.end(), mod.cave_pages.begin(), mod.cave_pages.end());
		mod.cave_pages.clear();
	}
	return true;
}

void Clear() {
	if (g_pending_plan != nullptr) {
		for (auto& mod: g_pending_plan->mods) {
			ReleaseCaves(&mod.cave_pages);
		}
	}
	g_pending_plan.reset();
	ReleaseCaves(&g_applied_cave_pages);
}

} // namespace Loader::GamePatch
