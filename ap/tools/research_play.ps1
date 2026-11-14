# Starts Bayonetta 2 (B2recomp) normally, with your own saves, and logs every change of the game's save data to
# ap\research\play_log.txt (RECOMPS_AP_SAVEWATCH, runtime/ap_dev.c). Play as usual; each thing you do (open a chest,
# buy something, finish a Muspelheim, pick up a crow or a file) leaves its trace in the log with a timestamp.
# Write down roughly what you did and when; close the game when done.
#   powershell -ExecutionPolicy Bypass -File ap\tools\research_play.ps1
$ap = Split-Path $PSScriptRoot
$root = Join-Path (Split-Path $ap) 'B2recomp'
$log = Join-Path $ap 'research\play_log.txt'
$env:RECOMPS_AP_SAVEWATCH = '1'
$env:SSBU_ROMFS = Join-Path $root 'targets\romfs'
$env:SSBU_USERDATA = Join-Path $root 'build\play\userdata'
$env:SSBU_SHOW = '1'; $env:SSBU_VOLUME = '100'; $env:SSBU_FOCUS_NOTIFY = '0'
$env:SSBU_GRAPHICS_CONFIG = Join-Path $root 'config\graphics.ini'
$env:SSBU_CONFIG = Join-Path $root 'config\controllers.ini'
$fonts = Join-Path $root 'targets\sysdata\fonts'
if (Test-Path $fonts) { $env:SSBU_FONTS = $fonts }
$exe = Join-Path $root 'build\game_native.exe'
$elf = Join-Path $root 'targets\exefs\main.elf'
$psi = New-Object Diagnostics.ProcessStartInfo 'cmd.exe', "/c `"`"$exe`" `"$elf`" > `"$log`" 2>&1`""
$psi.WorkingDirectory = Join-Path $root 'build'
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
[Diagnostics.Process]::Start($psi) | Out-Null
"Bayonetta 2 is starting with save logging; log: $log"
