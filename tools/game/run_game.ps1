param([int]$Seconds = 40, [string]$Shot = 'shot', [string]$Keys = '', [string]$Game = 'C:\Games\THUG2',
      [string]$ShotDir = '', [string]$Level = '', [string]$Tests = '', [switch]$FullTests, [string[]]$Commands = @(),
      [switch]$WaitReady, [string]$Run = '', [int]$HangSeconds = 45)
# A game in trouble (game_health.ps1: it crashed - exit code not 0 -, Windows Error Reporting runs for it, it shows a
# dialog, or its window hasn't responded for -HangSeconds) is noticed while waiting, closed (WM_CLOSE, then killed),
# and reported with the end of its log - also appended to <folder>\thug2-decomp-crashes.txt. Exit code 5 then.
# -Level <name>: the game started with -level <name> - straight into free skate there (src/game/front/start_level.h).
# Self-tests (start-up time): workspaces skip the early tests ([selftests] early=0 - thug2_check runs them offline).
#   -Tests "anim,coll": only the self-tests whose names contain one of those words run (THUG2_SELFTESTS) - the ones of
#                       the work being tested; -FullTests: every stage and test (THUG2_SELFTESTS_EARLY=1) - a range's
#                       final run.
# Commands (the test-automation channel, src/game/front/automation.h - [features] automation=1, set in the workspaces):
#   -Commands "waitfor ready","choose menu_singlesess",...: written to <folder>\thug2-decomp-commands.txt before the
#                       game starts; the run waits until the game has done them (its status file) - -Seconds is then
#                       the most it waits - and the picture is taken right after. No fixed waits, no posted keys.
#   -WaitReady: wait until the main menu is up (the status file's "ready") instead of -Seconds (then the most).
#   -Run <script>: the game started with -run <script> (run once the main menu is up).
#   game_cmd.ps1 sends more commands to a game that runs. -Keys still works (after the commands).
# Starts the game in the -Game folder (C:\Games\THUG2, or a workspace copy C:\Games\THUG2-1, -2...), waits, takes a
# picture of its window (half size), optionally sends keys, then closes that game (only the one it started).
# Workspace folders (anything but the main install, C:\Games\THUG2\Game) never take the foreground: keys are posted to the window
# (KEYS_POST) and pictures taken with PrintWindow (window_shot.ps1), so several can run while the user uses the PC.
$dir = $PSScriptRoot
# (pictures: -ShotDir, else %TEMP%\thug2-shots - never into the repo)
if (-not $ShotDir) { $ShotDir = Join-Path $env:TEMP 'thug2-shots' }
New-Item -ItemType Directory -Force $ShotDir | Out-Null
. (Join-Path $dir 'game_exe.ps1')
. (Join-Path $dir 'automation.ps1')
. (Join-Path $dir 'game_health.ps1')
$Game = Resolve-GameFolder $Game
$exe = Find-GameExe $Game
if (-not $exe) { "NO GAME IN $Game (no $($GameExeNames -join ' or ') in it or in its Game folder)"; exit 2 }
$workspace = $Game -notin @('C:\Games\THUG2', 'C:\Games\THUG2\Game')
function Get-GamesHere { Get-GameProcesses | Where-Object { $_.Path -and (Split-Path $_.Path -Parent) -eq $Game } }
# Never close a game that's already running: the user may be playing. Refuse instead.
if (Get-GamesHere) { "THUG2 IS ALREADY RUNNING FROM $Game (the user may be playing) - not started; try later"; exit 3 }
if ($workspace -and -not ('FocusBack' -as [type])) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class FocusBack {
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
"@
}
$before = if ($workspace) { [FocusBack]::GetForegroundWindow() } else { [IntPtr]::Zero }
$env:THUG2_SELFTESTS = if ($FullTests) { '' } else { $Tests }  # (the game inherits these)
$env:THUG2_SELFTESTS_EARLY = if ($FullTests) { '1' } else { '' }
$useStatus = $Commands.Count -gt 0 -or $WaitReady
if ($useStatus -and -not (Test-AutomationOn $Game)) {
    "THE AUTOMATION CHANNEL IS OFF in $Game ([features] automation=1 - make_workspace.ps1 sets it) - not started"; exit 4
}
if (Test-AutomationOn $Game) { Clear-AutomationFiles $Game }  # (the last run's commands never run again)
$lines = if ($Commands.Count) { Add-GameCommands $Game $Commands } else { 0 }
$gameArgs = @()
if ($Level) { $gameArgs += @('-level', $Level) }
if ($Run) { $gameArgs += @('-run', $Run) }
Set-WorkspaceMuted $Game  # (a workspace's games always run muted: [features] mute_audio=1)
$started = [Diagnostics.Stopwatch]::StartNew()
$startTime = Get-Date
if ($gameArgs.Count) { $p = Start-Process -FilePath $exe -WorkingDirectory $Game -ArgumentList $gameArgs -PassThru }
else { $p = Start-Process -FilePath $exe -WorkingDirectory $Game -PassThru }
$watch = New-GameWatch $p $HangSeconds
$trouble = $null
if ($workspace) {
    # (a new window takes the front: once the game's window is up, the window the user had in front gets it back)
    for ($i = 0; $i -lt 60 -and -not $p.HasExited; $i++) {
        Start-Sleep -Milliseconds 500; $p.Refresh()
        if ($p.MainWindowHandle -ne [IntPtr]::Zero) { break }
    }
    Start-Sleep -Seconds 2
    if ($before -ne [IntPtr]::Zero) {
        [FocusBack]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [FocusBack]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
        [FocusBack]::SetForegroundWindow($before) | Out-Null
    }
}
function Snap($name) {
    # (only the game's own window - never the rest of the screen)
    if ($workspace) { & "$dir\window_shot.ps1" -ProcessId $p.Id -Path "$ShotDir\$name.png" }
    else { & "$dir\game_shot.ps1" -Path "$ShotDir\$name.png" }
}
if ($Commands.Count) {
    $r = Wait-GameCommands $Game $lines $Seconds $p $watch
    "commands {0} after {1:N1} s from the start: {2}" -f ($(if ($r.ok) { 'done' } else { 'FAILED' })), $started.Elapsed.TotalSeconds, $r.why
    Format-GameStatus $r.status
    $trouble = $r.trouble
} elseif ($WaitReady) {
    while ($started.Elapsed.TotalSeconds -lt $Seconds -and -not $p.HasExited) {
        $s = Read-GameStatus $Game
        if ($s -and $s.ready) { break }
        $trouble = Update-GameWatch $watch
        if ($trouble) { break }
        Start-Sleep -Milliseconds 100
    }
    "ready after {0:N1} s: {1}" -f $started.Elapsed.TotalSeconds, (Format-GameStatus (Read-GameStatus $Game))
} else {
    for ($i = 0; $i -lt $Seconds; $i++) {
        Start-Sleep -Seconds 1
        if ($p.HasExited) { break }
        $trouble = Update-GameWatch $watch
        if ($trouble) { break }
    }
}
if (-not $trouble) { $trouble = Update-GameWatch $watch }
if (-not $trouble -and -not $p.HasExited) {
    # (a window that doesn't respond while the game keeps a core busy - a loop that never ends - isn't counted as idle
    # by the watch; but PrintWindow below would wait for it for ever: a hung window at the end is trouble too)
    $p.Refresh()
    $h = $p.MainWindowHandle
    if ($h -ne [IntPtr]::Zero -and [GameHealth]::IsHungAppWindow($h)) {
        $trouble = "its window doesn't respond at the end of the run (busy: a loop that never ends?)"
    }
}
if ($trouble) {
    # (a picture of a dialog's game; never of a hung window - PrintWindow would wait for it for ever)
    if (-not $p.HasExited -and $trouble -match 'dialog') { Snap "$Shot-trouble" }
    $closed = Close-Game $p
    Write-GameOutcome $Game $trouble $closed $startTime
    $cmdFile = Get-CommandFile $Game
    if (Test-Path $cmdFile) { Remove-Item $cmdFile -Force -ErrorAction SilentlyContinue }
    exit 5
}
if ($p.HasExited) { "exited early (code $($p.ExitCode)) after {0:N1} s" -f $started.Elapsed.TotalSeconds } else {
    Snap "$Shot-1"
    if ($Keys -ne '') {
        if ($workspace) { $env:KEYS_POST = '1'; $env:KEYS_NO_FOCUS = '1' }
        & "$dir\keys.ps1" -ProcessId $p.Id -Keys $Keys
        Start-Sleep -Milliseconds 400
        Snap "$Shot-mid"
        Start-Sleep -Seconds 3
        Snap "$Shot-2"
        $trouble = Update-GameWatch $watch
        if ($trouble) {
            Write-GameOutcome $Game $trouble (Close-Game $p) $startTime
            exit 5
        }
    }
    "running after {0:N1} s; responding=$($p.Responding)" -f $started.Elapsed.TotalSeconds
    Stop-Process -Id $p.Id -Force
}
Start-Sleep -Seconds 2
if (Get-Process -Id $p.Id -ErrorAction SilentlyContinue) { "STILL RUNNING" } else {
    "closed"
    # (the commands are done with: a later start mustn't run them again - the status stays for reading)
    $cmdFile = Get-CommandFile $Game
    if (Test-Path $cmdFile) { Remove-Item $cmdFile -Force -ErrorAction SilentlyContinue }
}
