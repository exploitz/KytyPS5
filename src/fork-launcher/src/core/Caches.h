#pragma once

#include <QString>
#include <QStringList>

struct GameInfo;
struct Package;

// The cache files run-windows.ps1 and precompile-windows.ps1 share (both scripts have the same functions).
namespace Caches {

// Rename-TitleCaches: caches named by the title alone are from before versions had their own; they become the given
// game version's (seeds.seeds -> seeds-<title>_<version>.seeds, _PipelineCache\static\<title>.* , the warmup
// recordings). Returns the moves made (for the log).
QStringList renameTitleCaches(const Package& package, const GameInfo& game);

// "<size> <last write ticks>" of the precompile program: the stamp a seed file's <file>.program holds.
QString programStamp(const QString& tool);
// The stamp in <seeds>.program (trimmed), empty when there is none.
QString readProgramStamp(const QString& seeds);
bool    writeProgramStamp(const QString& seeds, const QString& stamp);
// SHA-256 equality of two files (Get-FileHash).
bool sameContent(const QString& a, const QString& b);
// precompile-windows.ps1 Get-SeedPipelines: the count of pipeline recipes in a seed file (-1 when unreadable).
qint64 seedPipelines(const QString& file);

// The static pipeline cache of a game version as files tell (the PowerShell launcher's Get-PrecompileState).
enum class StaticState { None, Partial, Done };
StaticState staticState(const Package& package, const QString& cacheId);
// Any shard checkpoint bigger than 1 MB in _PipelineCache\static (run-windows.ps1: a precompile that stopped).
bool hasShardCheckpoints(const Package& package);

} // namespace Caches
