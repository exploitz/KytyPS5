param([string]$Label = 'walker', [string]$Plan = 'w6,r1.5,w6,l1.2,w8,r2,w6', [int]$StartupSeconds = 240,
      [string[]]$Set = @())
# The route walker's check without a player: a scripted walk from the bench save's spot is recorded (KYTY_INPUT_RECORD:
# the tape and its .track of positions), then a second launch walks that track with KYTY_ROUTE. The walker's own
# "Route:" lines (waypoint, metres off the route, heading error, glides) and a screenshot every 5 s show how close it
# stays; the summary is its "Route: done" line.
#   -Plan "w6,r1.5,w6": walk 6 s, walk turning right 1.5 s, walk 6 s (l: left; s: stand) - pad commands, one a step
$S = $PSScriptRoot
$root = (Resolve-Path "$PSScriptRoot\..\..\..").Path
Set-Location $root
$out = "$root\_Build\walker\$Label"
New-Item -ItemType Directory -Force $out | Out-Null
$tape = "$out\record.tape"
Remove-Item "$tape*" -ErrorAction SilentlyContinue
function Live([string]$commands) { python tools\local\bench-windows.py live $commands | Where-Object { $_ -match '^LIVE_' } }
function Launch([string[]]$extra) {
	$started = & "$S\bench-run.ps1" -Label $Label -KeepRunning -NoWalk -ShotOnly -StartupSeconds $StartupSeconds `
		-Set (@('KYTY_KEYBOARD_LAYOUT=classic') + $extra + $Set)
	$started | Select-Object -Last 2
	if (!($started -match '^HUD after')) { 'no HUD'; Get-Process kyty_emulator -ErrorAction SilentlyContinue | Stop-Process -Force; exit 1 }
}
function Stop-Game {
	Get-Process kyty_emulator -ErrorAction SilentlyContinue | Where-Object { $_.Threads.Count -gt 1 } | Stop-Process -Force
	Start-Sleep 3
}

# 1. The recording: the plan as pad commands (LX LY RX RY BUTTONS SECONDS), the camera turning while walking.
Launch @("KYTY_INPUT_RECORD=$tape")
$seconds = 0
foreach ($step in $Plan -split ',') {
	$kind = $step.Substring(0, 1); $time = [double]$step.Substring(1); $seconds += $time
	$pad = switch ($kind) { 'w' { "pad 128 0 128 128 0 $time" } 'r' { "pad 128 0 200 128 0 $time" }
		'l' { "pad 128 0 56 128 0 $time" } default { "pad 128 128 128 128 0 $time" } }
	Live "$pad ; sleep $time" | Out-Null # (the live sleep takes seconds)
}
Start-Sleep 2
powershell -NoProfile -File "$S\screen.ps1" -Out "$out\recorded-end.png" -Scale 0.25 | Out-Null
Stop-Game
if (!(Test-Path "$tape.track")) { "no track was recorded ($tape.track)"; exit 1 }
"recorded: $((Get-Content "$tape.track").Count) track points over $seconds s"

# 2. The walk: the same spot, KYTY_ROUTE on the track; screenshots while it walks.
# (bench-run.ps1 puts a fresh bench save in itself; a second reset in the same second fails on its folder name.)
Launch @("KYTY_ROUTE=$tape.track")
for ($t = 5; $t -le [int]($seconds * 2 + 20); $t += 5) {
	Start-Sleep 5
	powershell -NoProfile -File "$S\screen.ps1" -Out "$out\walk-$t.png" -Scale 0.25 | Out-Null
	$log = Get-ChildItem "$root\_Build\run-logs\*.out.log" | Sort-Object LastWriteTime | Select-Object -Last 1
	if (Select-String -Path $log.FullName -Pattern 'Route: done|Route: .*ended' -Quiet) { break }
}
$log = Get-ChildItem "$root\_Build\run-logs\*.out.log" | Sort-Object LastWriteTime | Select-Object -Last 1
Select-String -Path $log.FullName -Pattern '^Route' | ForEach-Object { $_.Line } | Set-Content "$out\route.txt" -Encoding ascii
Get-Content "$out\route.txt" | Select-Object -Last 12
Stop-Game
