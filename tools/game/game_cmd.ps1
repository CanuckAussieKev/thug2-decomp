param([string]$Game = 'C:\Games\THUG2', [string[]]$Commands = @(), [int]$Timeout = 120, [string]$Shot = '',
      [switch]$Status, [int]$HangSeconds = 45)
# Commands to a running game through the test-automation channel (src/game/front/automation.h - [features]
# automation=1, the workspaces' setting): appends them to <folder>\thug2-decomp-commands.txt, waits until the game has
# done them (its status file, <folder>\thug2-decomp-status.json), prints the result and the status.
#   game_cmd.ps1 -Game C:\Games\THUG2-4 -Commands "waitfor ready","choose menu_singlesess","waitfor menu ss_play_level"
#   powershell -File game_cmd.ps1 -Game C:\Games\THUG2-4 -Commands "waitfor ready;choose menu_options"   (';' too)
#   game_cmd.ps1 -Game C:\Games\THUG2-4 -Status            (only the status)
#   -Shot <png>: a picture of the game's window afterwards (PrintWindow, window_shot.ps1).
# Exit code: 0 done, 1 a command failed or was refused, 2 timed out / the game exited, 3 the channel is off / no game,
# 5 the game got into trouble (crashed, a dialog, Windows Error Reporting, not responding for -HangSeconds): closed and
# reported with its log's last lines (game_health.ps1).
# Take the folder's game lock first (game_lock.ps1) - this drives a game you started.
. (Join-Path $PSScriptRoot 'game_exe.ps1')
. (Join-Path $PSScriptRoot 'automation.ps1')
. (Join-Path $PSScriptRoot 'game_health.ps1')
$Game = Resolve-GameFolder $Game
if (-not (Test-AutomationOn $Game)) { "THE CHANNEL IS OFF in $Game ([features] automation=1 - make_workspace.ps1 sets it)"; exit 3 }
$process = Get-GameProcesses | Where-Object { $_.Path -and (Split-Path $_.Path -Parent) -eq $Game } | Select-Object -First 1
if ($Status) { Format-GameStatus (Read-GameStatus $Game); exit 0 }
if (-not $process) { "NO GAME RUNNING FROM $Game"; exit 3 }
$lines = Add-GameCommands $Game $Commands
$since = Get-Date
$r = Wait-GameCommands $Game $lines $Timeout $process (New-GameWatch $process $HangSeconds)
"{0} after {1:N1} s: {2}" -f ($(if ($r.ok) { 'done' } else { 'FAILED' })), $r.seconds, $r.why
Format-GameStatus $r.status
if ($r.trouble) {
    # (crashed, a dialog, WerFault or not responding: closed, and what the log says - game_health.ps1)
    Write-GameOutcome $Game $r.trouble (Close-Game $process) $since
    exit 5
}
# (in a child PowerShell: window_shot's Add-Type fails in some hosts)
if ($Shot -and -not $process.HasExited) {
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'window_shot.ps1') -ProcessId $process.Id -Path $Shot
}
if ($r.ok) { exit 0 }
if ($r.why -match '^(error|refused):') { exit 1 }
exit 2
