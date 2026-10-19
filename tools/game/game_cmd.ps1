param([string]$Game = 'C:\Games\THUG2', [string[]]$Commands = @(), [int]$Timeout = 120, [string]$Shot = '',
      [switch]$Status)
# Commands to a running game through the test-automation channel (src/game/front/automation.h - [features]
# automation=1, the workspaces' setting): appends them to <folder>\thug2-decomp-commands.txt, waits until the game has
# done them (its status file, <folder>\thug2-decomp-status.json), prints the result and the status.
#   game_cmd.ps1 -Game C:\Games\THUG2-4 -Commands "waitfor ready","choose menu_singlesess","waitfor menu ss_play_level"
#   powershell -File game_cmd.ps1 -Game C:\Games\THUG2-4 -Commands "waitfor ready;choose menu_options"   (';' too)
#   game_cmd.ps1 -Game C:\Games\THUG2-4 -Status            (only the status)
#   -Shot <png>: a picture of the game's window afterwards (PrintWindow, window_shot.ps1).
# Exit code: 0 done, 1 a command failed or was refused, 2 timed out / the game exited, 3 the channel is off / no game.
# Take the folder's game lock first (game_lock.ps1) - this drives a game you started.
. (Join-Path $PSScriptRoot 'game_exe.ps1')
. (Join-Path $PSScriptRoot 'automation.ps1')
$Game = Resolve-GameFolder $Game
if (-not (Test-AutomationOn $Game)) { "THE CHANNEL IS OFF in $Game ([features] automation=1 - make_workspace.ps1 sets it)"; exit 3 }
$process = Get-GameProcesses | Where-Object { $_.Path -and (Split-Path $_.Path -Parent) -eq $Game } | Select-Object -First 1
if ($Status) { Format-GameStatus (Read-GameStatus $Game); exit 0 }
if (-not $process) { "NO GAME RUNNING FROM $Game"; exit 3 }
$lines = Add-GameCommands $Game $Commands
$r = Wait-GameCommands $Game $lines $Timeout $process
"{0} after {1:N1} s: {2}" -f ($(if ($r.ok) { 'done' } else { 'FAILED' })), $r.seconds, $r.why
Format-GameStatus $r.status
# (in a child PowerShell: window_shot's Add-Type fails in some hosts)
if ($Shot -and -not $process.HasExited) {
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'window_shot.ps1') -ProcessId $process.Id -Path $Shot
}
if ($r.ok) { exit 0 }
if ($r.why -match '^(error|refused):') { exit 1 }
exit 2
