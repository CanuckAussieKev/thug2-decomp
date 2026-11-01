param([Parameter(Mandatory = $true)][string]$Path)
# A screenshot of THUG2's window ONLY - never the rest of the screen (the user may be using the PC: their own windows
# must never be captured). Captures nothing unless the game's window is the one in front; then only its rectangle,
# saved at half size. Prints what it did.
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
if (-not ('GameShot' -as [type])) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class GameShot {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
}
"@
}
. (Join-Path $PSScriptRoot 'game_exe.ps1')
$game = Get-GameProcesses | Select-Object -First 1
if (-not $game) { "NOT CAPTURED: the game isn't running"; exit 1 }
$front = [GameShot]::GetForegroundWindow()
[uint32]$frontPid = 0
[GameShot]::GetWindowThreadProcessId($front, [ref]$frontPid) | Out-Null
if ($frontPid -ne $game.Id) { "NOT CAPTURED: the game's window isn't the one in front (nothing else is ever captured)"; exit 1 }
$r = New-Object GameShot+RECT
[GameShot]::GetWindowRect($front, [ref]$r) | Out-Null
$w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
if ($w -le 0 -or $h -le 0) { "NOT CAPTURED: no window rectangle"; exit 1 }
$bmp = New-Object System.Drawing.Bitmap $w, $h
[System.Drawing.Graphics]::FromImage($bmp).CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size $w, $h))
# (checked again after the copy: if anything else came to the front meanwhile, the picture is thrown away)
[GameShot]::GetWindowThreadProcessId([GameShot]::GetForegroundWindow(), [ref]$frontPid) | Out-Null
if ($frontPid -ne $game.Id) { $bmp.Dispose(); "NOT CAPTURED: the game lost the front while capturing"; exit 1 }
(New-Object System.Drawing.Bitmap $bmp, ([int]($w / 2)), ([int]($h / 2))).Save($Path)
"captured the game's window: $Path"
