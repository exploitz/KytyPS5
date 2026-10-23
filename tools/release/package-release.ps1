# The release package of this fork: tools\release\build-release.cmd's emulator and precompile program, the launcher
# (tools\release\build-launcher.cmd's deploy folder: the exe and its Qt libraries), the launch config, the 1080p
# patches, the FidelityFX and FSR 4 files, the precompile's render-pass states and hints, the Visual C++ runtime, the
# licenses and the guide, in one folder and its zip.
#   .\tools\release\package-release.ps1 -Version 1.0.0
#   -BuildDir     the release build (default _Build\windows-release)
#   -LauncherDir  the launcher's deploy folder (default _Build\fork-launcher\deploy)
#   -DepsDir      the run-time files that are not in the repository: deps\fsr (AMD FidelityFX, MIT) and deps\fsr4 (the
#                 FSR 4 v07 model files and their notice) (default _Build\windows\deps)
#   -Out          where the folder and the zip go (default _Build\package)
# The folder is checked before it is zipped: every file the launcher needs is there, and no text file names a path or
# user of the building PC.
param(
	[Parameter(Mandatory = $true)][string]$Version,
	[string]$BuildDir = '',
	[string]$LauncherDir = '',
	[string]$DepsDir = '',
	[string]$Out = ''
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot\..\..").Path
if (!$BuildDir) { $BuildDir = "$repo\_Build\windows-release" }
if (!$LauncherDir) { $LauncherDir = "$repo\_Build\fork-launcher\deploy" }
if (!$DepsDir) { $DepsDir = "$repo\_Build\windows\deps" }
if (!$Out) { $Out = "$repo\_Build\package" }
$name = "KytyPS5-DemonsSouls-$Version"
$folder = Join-Path $Out $name

foreach ($file in "$BuildDir\kyty_emulator.exe", "$BuildDir\kyty_shader_precompile.exe", "$LauncherDir\KytyPS5 Launcher.exe") {
	if (!(Test-Path -LiteralPath $file)) { throw "missing ${file}: build it first (tools\release\build-release.cmd, build-launcher.cmd)" }
}
if (Test-Path -LiteralPath $folder) { Remove-Item -LiteralPath $folder -Recurse -Force }
New-Item -ItemType Directory -Force $folder | Out-Null

# The launcher and its Qt libraries at the top, the emulator beside them (it finds deps\, patches\ and its caches in
# its own folder).
Copy-Item "$LauncherDir\*" $folder -Recurse
Copy-Item "$BuildDir\kyty_emulator.exe", "$BuildDir\kyty_shader_precompile.exe" $folder
if (Test-Path "$BuildDir\libwinpthread-1.dll") { Copy-Item "$BuildDir\libwinpthread-1.dll" $folder }
Copy-Item "$repo\run-windows.json" "$folder\launch.json"
# The command-line launch, for advanced use and as a fallback (the launcher does the same natively).
Copy-Item "$repo\run-windows.ps1", "$repo\precompile-windows.ps1" $folder
Set-Content "$folder\run.cmd" -Encoding ASCII -Value @(
	'@echo off',
	'rem Starts the game from the command line; the options are those of run-windows.ps1 (e.g. run.cmd -Fullscreen).',
	'powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-windows.ps1" -Prompt -Follow %*',
	'if %errorlevel% neq 0 pause')
Set-Content "$folder\precompile.cmd" -Encoding ASCII -Value @(
	'@echo off',
	'rem Every shader and pipeline of the game into the static pipeline cache (the launcher''s Precompile does the same).',
	'powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0precompile-windows.ps1" %*',
	'pause')
New-Item -ItemType Directory -Force "$folder\patches" | Out-Null
Copy-Item "$repo\patches\*.json" "$folder\patches"
$states = "$folder\tools\local\static-precompile"
New-Item -ItemType Directory -Force $states | Out-Null
Copy-Item "$repo\tools\local\static-precompile\pass-states.json", "$repo\tools\local\static-precompile\hints-*.hints" $states

# Run-time files from outside the repository.
foreach ($dep in 'fsr', 'fsr4') {
	if (Test-Path "$DepsDir\$dep") {
		New-Item -ItemType Directory -Force "$folder\deps\$dep" | Out-Null
		Copy-Item "$DepsDir\$dep\*" "$folder\deps\$dep"
	} else {
		Write-Warning "no ${DepsDir}\${dep}: the package goes without it (its launcher option says the files are missing)"
	}
}

# The licenses: the emulator's (GPL-2.0), its components', the run-time files'.
Copy-Item "$repo\LICENSE" $folder
Copy-Item "$repo\LICENSES" $folder -Recurse
if (Test-Path "$folder\deps\fsr\LICENSE-FidelityFX.txt") { Copy-Item "$folder\deps\fsr\LICENSE-FidelityFX.txt" "$folder\LICENSES\" }
if (Test-Path "$folder\deps\fsr4\LICENSE-FSR4-v07.txt") { Copy-Item "$folder\deps\fsr4\LICENSE-FSR4-v07.txt" "$folder\LICENSES\" }
if (Test-Path "$repo\3rdparty\fsr4-v07\LICENSE.txt") { Copy-Item "$repo\3rdparty\fsr4-v07\LICENSE.txt" "$folder\LICENSES\FSR4-v07-port-MIT.txt" }
if (Test-Path "$repo\3rdparty\fsr4-v07\LICENSE-AMD-FidelityFX-SDK.txt") {
	Copy-Item "$repo\3rdparty\fsr4-v07\LICENSE-AMD-FidelityFX-SDK.txt" "$folder\LICENSES\FSR4-v07-port-AMD-FidelityFX-SDK-MIT.txt"
}
# The components built into the exes (3rdparty submodules, CMake-fetched sources) and the launcher's Qt, each license
# as LICENSES\third-party\<component>.txt.
$third = "$folder\LICENSES\third-party"
New-Item -ItemType Directory -Force $third | Out-Null
$fetched = @{}
foreach ($dir in "$BuildDir\_deps", "$repo\_Build\deps") {
	foreach ($src in Get-ChildItem $dir -Directory -Filter '*-src' -ErrorAction SilentlyContinue) {
		$fetchedName = $src.Name -replace '(_source)?-src$', ''
		if (!$fetched.Contains($fetchedName)) { $fetched[$fetchedName] = $src.FullName }
	}
}
# Sources the build took from elsewhere (FETCHCONTENT_SOURCE_DIR_<NAME> in its cache: mimalloc, xbyak, zydis).
foreach ($line in Get-Content "$BuildDir\CMakeCache.txt" -ErrorAction SilentlyContinue) {
	if ($line -match '^FETCHCONTENT_SOURCE_DIR_([A-Z0-9_]+):[A-Z]+=(.+)$' -and (Test-Path $Matches[2])) {
		$fetched[$Matches[1].ToLowerInvariant()] = $Matches[2]
	}
}
$components = @{}
foreach ($sub in Get-ChildItem "$repo\3rdparty" -Directory) { $components[$sub.Name] = $sub.FullName }
foreach ($entry in $fetched.GetEnumerator()) { $components[$entry.Key] = $entry.Value }
foreach ($component in $components.GetEnumerator() | Sort-Object Key) {
	if ($component.Key -in 'overrides', 'patches', 'renderdoc') { continue } # (no code in the exes)
	$files = @(Get-ChildItem $component.Value -File -ErrorAction SilentlyContinue |
		Where-Object { $_.Name -match '^(LICEN[CS]E|COPYING|COPYRIGHT|NOTICE)(\..*)?$|^copyright$|^LICENSE\.MIT$' })
	if (!$files) { Write-Warning "no license file found for $($component.Key)"; continue }
	$text = foreach ($file in $files) { "==== $($component.Key): $($file.Name) ===="; Get-Content $file.FullName -Raw }
	Set-Content "$third\$($component.Key).txt" -Encoding UTF8 -Value ($text -join "`r`n")
}
Set-Content "$third\Qt.txt" -Encoding UTF8 -Value @(
	'The launcher (KytyPS5 Launcher.exe) uses the Qt 6.8 libraries (Qt6Core, Qt6Gui, Qt6Widgets, Qt6Network and their',
	'plugins), (c) The Qt Company Ltd. and other contributors, under the GNU Lesser General Public License version 3',
	'(LGPL-3.0-only), whose text and the GNU GPL 3.0 it refers to are in this folder (LGPL-3.0.txt, GPL-3.0.txt).',
	'The Qt libraries are separate DLL files next to the launcher and can be replaced with compatible builds.',
	'Qt''s source code: https://download.qt.io/official_releases/qt/6.8/')
Copy-Item "$repo\tools\release\licenses\LGPL-3.0.txt", "$repo\tools\release\licenses\GPL-3.0.txt" $third

# The guide and the first thing to read.
Copy-Item "$repo\docs\PORTABLE-README.md" "$folder\README.md"
Set-Content "$folder\START HERE.txt" -Encoding UTF8 -Value @(
	'Demon''s Souls (PS5) on KytyPS5',
	'',
	'1. Keep this whole folder together: unzip all of it into a folder of its own, for example C:\Games\KytyPS5',
	'   (not Program Files, Downloads or OneDrive; the shader caches need about 20 GB).',
	'2. Double-click "KytyPS5 Launcher.exe". If Windows says it protected your PC, click More info, then Run anyway.',
	'3. Follow the setup: it checks this PC, asks for your game folder and sets everything up.',
	'',
	'The full guide is README.md (also under Help in the launcher).')

# The Visual C++ runtime next to the exes (Microsoft's redistributable files): nothing to install.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -property installationPath
$crt = Get-Item "$vs\VC\Redist\MSVC\*\x64\Microsoft.VC14*.CRT" | Where-Object { $_.Parent.Parent.Name -match '^\d+\.' } |
	Sort-Object { [version]$_.Parent.Parent.Name } | Select-Object -Last 1
if (!$crt) { throw 'the Visual C++ redistributable files were not found (Visual Studio''s VC\Redist)' }
Copy-Item "$($crt.FullName)\msvcp140*.dll", "$($crt.FullName)\vcruntime140*.dll" $folder -Force

# Checks: what the launcher needs, and nothing of the building PC.
$needed = 'KytyPS5 Launcher.exe', 'kyty_emulator.exe', 'kyty_shader_precompile.exe', 'launch.json', 'patches\1080p-quality.json',
	'patches\1080p-fast.json', 'tools\local\static-precompile\pass-states.json', 'LICENSE', 'README.md', 'START HERE.txt'
$missing = $needed | Where-Object { !(Test-Path -LiteralPath (Join-Path $folder $_)) }
if ($missing) { throw "the package lacks: $($missing -join ', ')" }
$user = [Environment]::UserName
$leaks = Get-ChildItem $folder -Recurse -File -Include *.txt, *.md, *.json, *.ps1, *.cmd, *.conf |
	Select-String -SimpleMatch -Pattern @("C:\Users\$user", "C:/Users/$user", "\Users\$user\", "/home/$user") -List
if ($leaks) { throw "files name this PC's paths: $(($leaks | ForEach-Object { $_.Path.Substring($folder.Length + 1) }) -join ', ')" }
# The programs built here (assert messages' source paths, the PDB path: build-release.cmd's KYTY_RELEASE_PATHS).
$latin1 = [Text.Encoding]::GetEncoding(28591)
foreach ($exe in 'kyty_emulator.exe', 'kyty_shader_precompile.exe', 'KytyPS5 Launcher.exe') {
	$bytes = $latin1.GetString([IO.File]::ReadAllBytes((Join-Path $folder $exe)))
	foreach ($pattern in "Users\$user\", "Users/$user/") {
		if ($bytes.IndexOf($pattern, [StringComparison]::OrdinalIgnoreCase) -ge 0) { throw "$exe names this PC's paths ($pattern)" }
	}
}

# The zip and its checksum.
$zip = Join-Path $Out "$name.zip"
Remove-Item -LiteralPath $zip -ErrorAction SilentlyContinue
& tar.exe -a -c -f $zip -C $Out $name
if ($LASTEXITCODE -ne 0) { throw 'tar failed to make the zip' }
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content "$zip.sha256" -Encoding ASCII -Value "$hash  $name.zip"
$root = (Resolve-Path $folder).Path
Get-ChildItem $folder -Recurse -File | ForEach-Object { '{0,12:N0}  {1}' -f $_.Length, $_.FullName.Substring($root.Length + 1) }
Write-Host ("{0}: {1:N0} MB, SHA-256 {2}" -f $zip, ((Get-Item $zip).Length / 1MB), $hash)
