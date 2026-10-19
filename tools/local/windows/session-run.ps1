param([string]$Label = 'session', [string]$Exe = '', [int]$Cycles = 2, [string[]]$Set = @(), [string[]]$Extra = @(),
      [switch]$NoPrecompile, [switch]$KeepRunning, [int]$Vblank = 0, [int]$StartupSeconds = 240)
# A play-like session without a player: the launch from the bench save (bench-run.ps1), then the worlds in turn by
# archstone travel (gamecmd +cp11_loadLevel), each with the seconds right after the load measured standing (the
# stalls after an area load hide there), a camera turn and a short walk, and the whole card's video memory sampled
# every second (nvidia-smi: the desktop and other programs count too, as they do for Windows' paging). Warp tours of
# a few minutes missed what an hour of play on a 12 GB card ran into; this covers loads, turns and session length.
#   session-run.ps1 -Label base               the bench save (bench-windows.py prepare), 2 cycles of 5 worlds
#   session-run.ps1 -Label x -Set KEY=VALUE   a switch of the launch config changed for this run
# Results: _Build\session\<Label>\legs.csv (one line per measure) and vram.csv; the summary is printed.
$S = $PSScriptRoot
$root = (Resolve-Path "$PSScriptRoot\..\..\..").Path
Set-Location $root
$out = "$root\_Build\session\$Label"
New-Item -ItemType Directory -Force $out | Out-Null

$params = @{ Label = $Label; KeepRunning = $true; NoWalk = $true; ShotOnly = $true; StartupSeconds = $StartupSeconds }
if ($Exe) { $params['Exe'] = $Exe }
if ($Set.Count) { $params['Set'] = $Set }
if ($Extra.Count) { $params['Extra'] = $Extra }
if ($Vblank -gt 0) { $params['Vblank'] = $Vblank }
if ($NoPrecompile) { $params['NoPrecompile'] = $true }
$started = & "$S\bench-run.ps1" @params
$started | Select-Object -Last 3
# (A start-up still at the menu leaves the emulator running: only the HUD counts.)
if (!($started -match '^HUD after')) { 'no HUD: the session stops'; Get-Process kyty_emulator -ErrorAction SilentlyContinue | Stop-Process -Force; exit 1 }
if (!(Get-Process kyty_emulator -ErrorAction SilentlyContinue | Where-Object { $_.Threads.Count -gt 1 })) { 'emulator not running'; exit 1 }

# The whole card's memory, once a second, until this script ends.
$vram = Start-Job -ScriptBlock {
	param($file)
	"unix_ms,used_mib,total_mib" | Set-Content $file -Encoding ascii
	while ($true) {
		$line = nvidia-smi --query-gpu=memory.used,memory.total --format=csv,noheader,nounits 2>$null | Select-Object -First 1
		if ($line) { "{0},{1}" -f [DateTimeOffset]::Now.ToUnixTimeMilliseconds(), ($line -replace '\s', '') | Add-Content $file -Encoding ascii }
		Start-Sleep -Milliseconds 1000
	}
} -ArgumentList "$out\vram.csv"

$legs = New-Object System.Collections.Generic.List[string]
$legs.Add('label,start_ms,end_ms,fps,low1,p99_ms,max_ms,over50,over100,render_busy')
function Live([string]$commands) {
	python tools\local\bench-windows.py live $commands | Where-Object { $_ -match '^LIVE_' }
}
function Leg([string]$name, [string]$before = '') {
	$begin = [DateTimeOffset]::Now.ToUnixTimeMilliseconds()
	$lines = Live (($before, $name) -ne '' -join ' ; ')
	$end = [DateTimeOffset]::Now.ToUnixTimeMilliseconds()
	foreach ($line in $lines) {
		if ($line -match '^LIVE_MEASURE .*label=(\S+) .*fps=([\d.]+) .*render_busy=([\d.]+)% low1=([\d.]+) p99_ms=([\d.]+) max_ms=([\d.]+) over50=(\d+) over100=(\d+)') {
			$legs.Add(('{0},{1},{2},{3},{4},{5},{6},{7},{8},{9}' -f $Matches[1], $begin, $end, $Matches[2], $Matches[4], $Matches[5], $Matches[6], $Matches[7], $Matches[8], $Matches[3]))
			"{0,-14} fps {1,6} low1 {2,6} p99 {3,6} max {4,7} >100ms {5}" -f $Matches[1], $Matches[2], $Matches[4], $Matches[5], $Matches[6], $Matches[8]
		}
	}
	$legs | Set-Content "$out\legs.csv" -Encoding ascii
}
# pad LX LY RX RY BUTTONS SECONDS (128 = centred): the turn spins the camera right, the walk goes ahead turning slowly.
Live 'gamecmd +cp11_unlockAllArchstones' | Out-Null
Leg 'measure 20 start-stand'
Leg 'measure 20 start-turn' 'pad 128 128 200 128 0 20'
Leg 'measure 15 start-walk' 'pad 128 0 150 128 0 15'
$worlds = @(@(2, 1), @(3, 1), @(4, 1), @(5, 1), @(1, 1))
for ($cycle = 1; $cycle -le $Cycles; $cycle++) {
	foreach ($w in $worlds) {
		$tag = "c$cycle-w$($w[0])-$($w[1])"
		Live "gamecmd +cp11_loadLevel $($w[0]) $($w[1])" | Out-Null
		# The load (fog, then the area): nothing measured while it runs; 35 s covers this PC's loads.
		Start-Sleep -Seconds 35
		Leg "measure 20 $tag-arrive"
		Leg "measure 20 $tag-turn" 'pad 128 128 200 128 0 20'
		Leg "measure 15 $tag-walk" 'pad 128 0 150 128 0 15'
		powershell -NoProfile -File "$S\screen.ps1" -Out "$out\$tag.png" -Scale 0.25 | Out-Null
	}
}
Stop-Job $vram; Remove-Job $vram

# Summary: all legs, the arrivals alone, and the card.
$rows = Import-Csv "$out\legs.csv"
$card = Import-Csv "$out\vram.csv"
function Mean($values) { if ($values.Count) { ($values | Measure-Object -Average).Average } else { 0 } }
"== ${Label}: $($rows.Count) legs"
"fps mean {0:N1}  low1 mean {1:N1}  p99 mean {2:N1} ms  max {3:N0} ms  frames >100 ms {4}  >50 ms {5}" -f
	(Mean ($rows | ForEach-Object { [double]$_.fps })), (Mean ($rows | ForEach-Object { [double]$_.low1 })),
	(Mean ($rows | ForEach-Object { [double]$_.p99_ms })), (($rows | ForEach-Object { [double]$_.max_ms } | Measure-Object -Maximum).Maximum),
	(($rows | ForEach-Object { [int]$_.over100 } | Measure-Object -Sum).Sum), (($rows | ForEach-Object { [int]$_.over50 } | Measure-Object -Sum).Sum)
$arrive = $rows | Where-Object { $_.label -like '*-arrive' }
"arrivals: low1 mean {0:N1}  frames >100 ms {1}" -f (Mean ($arrive | ForEach-Object { [double]$_.low1 })),
	(($arrive | ForEach-Object { [int]$_.over100 } | Measure-Object -Sum).Sum)
if ($card.Count) {
	"card memory: max {0} of {1} MiB, mean {2:N0}" -f (($card | ForEach-Object { [int]$_.used_mib } | Measure-Object -Maximum).Maximum),
		$card[0].total_mib, (Mean ($card | ForEach-Object { [double]$_.used_mib }))
}
if (!$KeepRunning) { Get-Process kyty_emulator -ErrorAction SilentlyContinue | Where-Object { $_.Threads.Count -gt 1 } | Stop-Process -Force }
