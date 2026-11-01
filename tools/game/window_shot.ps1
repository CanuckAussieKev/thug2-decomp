param([Parameter(Mandatory = $true)][int]$ProcessId, [Parameter(Mandatory = $true)][string]$Path)
# A picture of ONE game window (the THUG2 process given), taken with PrintWindow: the window's own contents, whether
# or not it's in front - nothing else on the screen is ever read. Saved at half size (THUG2_SHOT_FULL=1: full size). Prints what it did.
Add-Type -AssemblyName System.Drawing
if (-not ('WindowShot' -as [type])) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class WindowShot {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
}
"@
}
$p = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
. (Join-Path $PSScriptRoot 'game_exe.ps1')
if (-not $p -or $p.ProcessName -notin $GameProcessNames) { "NOT CAPTURED: $ProcessId isn't a running THUG2"; exit 1 }
$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { "NOT CAPTURED: the game has no window yet"; exit 1 }
$r = New-Object WindowShot+RECT
[WindowShot]::GetClientRect($h, [ref]$r) | Out-Null
$w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
if ($w -le 0 -or $ht -le 0) { "NOT CAPTURED: no window area (minimised?)"; exit 1 }
$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
# 1 = client area only, 2 = PW_RENDERFULLCONTENT (DirectX contents through the compositor)
$ok = [WindowShot]::PrintWindow($h, $hdc, 3)
$g.ReleaseHdc($hdc); $g.Dispose()
if (-not $ok) { $bmp.Dispose(); "NOT CAPTURED: PrintWindow failed"; exit 1 }
# (THUG2_SHOT_FULL=1: the full size, for zooming on details)
$div = if ($env:THUG2_SHOT_FULL -eq '1') { 1 } else { 2 }
(New-Object System.Drawing.Bitmap $bmp, ([int]($w / $div)), ([int]($ht / $div))).Save($Path)
$bmp.Dispose()
"captured game $ProcessId's window: $Path"
