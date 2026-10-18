param([int]$Seconds = 40, [string]$Shot = 'shot', [string]$Keys = '', [string]$Game = 'C:\Games\THUG2',
      [string]$ShotDir = '')
# Starts the game in the -Game folder (C:\Games\THUG2, or a workspace copy C:\Games\THUG2-1, -2...), waits, takes a
# picture of its window (half size), optionally sends keys, then closes that game (only the one it started).
# Workspace folders (anything but C:\Games\THUG2) never take the foreground: keys are posted to the window
# (KEYS_POST) and pictures taken with PrintWindow (window_shot.ps1), so several can run while the user uses the PC.
$dir = $PSScriptRoot
# (pictures: -ShotDir, else %TEMP%\thug2-shots - never into the repo)
if (-not $ShotDir) { $ShotDir = Join-Path $env:TEMP 'thug2-shots' }
New-Item -ItemType Directory -Force $ShotDir | Out-Null
$Game = $Game.TrimEnd('\')
$exe = Join-Path $Game 'THUG2PM.exe'
if (-not (Test-Path $exe)) { "NO GAME IN $Game"; exit 2 }
$workspace = $Game -ne 'C:\Games\THUG2'
function Get-GamesHere { Get-Process THUG2PM -ErrorAction SilentlyContinue | Where-Object { $_.Path -and (Split-Path $_.Path -Parent) -eq $Game } }
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
$p = Start-Process -FilePath $exe -WorkingDirectory $Game -PassThru
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
for ($i = 0; $i -lt $Seconds; $i++) {
    Start-Sleep -Seconds 1
    if ($p.HasExited) { break }
}
if ($p.HasExited) { "exited early (code $($p.ExitCode)) after $i s" } else {
    Snap "$Shot-1"
    if ($Keys -ne '') {
        if ($workspace) { $env:KEYS_POST = '1'; $env:KEYS_NO_FOCUS = '1' }
        & "$dir\keys.ps1" -ProcessId $p.Id -Keys $Keys
        Start-Sleep -Milliseconds 400
        Snap "$Shot-mid"
        Start-Sleep -Seconds 3
        Snap "$Shot-2"
    }
    "running after $Seconds s; responding=$($p.Responding)"
    Stop-Process -Id $p.Id -Force
}
Start-Sleep -Seconds 2
if (Get-Process -Id $p.Id -ErrorAction SilentlyContinue) { "STILL RUNNING" } else { "closed" }
