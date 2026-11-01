param([int]$ProcessId, [string]$Keys)
$script:shots = 0
# Sends key presses by scan code (SDL reads scan codes) to the game's window. Keys: down, up, enter, esc, left, right.
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class KeyInput {
    [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Explicit, Size = 28)] public struct INPUT { [FieldOffset(0)] public uint type; [FieldOffset(4)] public KEYBDINPUT ki; }
    [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] inputs, int size);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int cmd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
    public static void Post(IntPtr hwnd, ushort vk, ushort scan, bool extended) {
        int l = 1 | (scan << 16) | (extended ? (1 << 24) : 0);
        PostMessage(hwnd, 0x0100, (IntPtr)vk, (IntPtr)l);
        System.Threading.Thread.Sleep(300);
        PostMessage(hwnd, 0x0101, (IntPtr)vk, (IntPtr)(l | unchecked((int)0xC0000000)));
    }
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    public static void Key(ushort scan, bool extended) {
        uint flags = 0x0008u | (extended ? 0x0001u : 0u); // SCANCODE (| EXTENDEDKEY)
        INPUT[] down = new INPUT[1]; down[0].type = 1; down[0].ki.wScan = scan; down[0].ki.dwFlags = flags;
        SendInput(1, down, Marshal.SizeOf(typeof(INPUT)));
        System.Threading.Thread.Sleep(300);
        INPUT[] up = new INPUT[1]; up[0].type = 1; up[0].ki.wScan = scan; up[0].ki.dwFlags = flags | 0x0002u;
        SendInput(1, up, Marshal.SizeOf(typeof(INPUT)));
    }
}
"@
$codes = @{ down = @(0x50, $true, 0x28); up = @(0x48, $true, 0x26); left = @(0x4B, $true, 0x25); right = @(0x4D, $true, 0x27); enter = @(0x1C, $false, 0x0D); esc = @(0x01, $false, 0x1B); pgup = @(0x49, $true, 0x21); pgdn = @(0x51, $true, 0x22); home = @(0x47, $true, 0x24); w = @(0x11, $false, 0x57); x = @(0x2D, $false, 0x58); z = @(0x2C, $false, 0x5A); bksp = @(0x0E, $false, 0x08); a = @(0x1E, $false, 0x41); s = @(0x1F, $false, 0x53); d = @(0x20, $false, 0x44); q = @(0x10, $false, 0x51); e = @(0x12, $false, 0x45); g = @(0x22, $false, 0x47); del = @(0x53, $true, 0x2E); b = @(0x30, $false, 0x42); f = @(0x21, $false, 0x46); f1 = @(0x3B, $false, 0x70); f8 = @(0x42, $false, 0x77); f9 = @(0x43, $false, 0x78); f10 = @(0x44, $false, 0x79); f11 = @(0x57, $false, 0x7A); f12 = @(0x58, $false, 0x7B); tab = @(0x0F, $false, 0x09); t = @(0x14, $false, 0x54); y = @(0x15, $false, 0x59); r = @(0x13, $false, 0x52); space = @(0x39, $false, 0x20); kp8 = @(0x48, $false, 0x68); kp2 = @(0x50, $false, 0x62); c = @(0x2E, $false, 0x43) }
$p = Get-Process -Id $ProcessId
# KEYS_NO_FOCUS=1 (workspace games, C:\Games\THUG2-n): the window is never brought to the front, no Alt tap, no mouse -
# only messages posted to this game's window (set KEYS_POST=1 too), so the user's own windows keep the focus.
if (-not $env:KEYS_NO_FOCUS) {
[KeyInput]::ShowWindow($p.MainWindowHandle, 9) | Out-Null
# (Windows lets a process take the foreground right after a key press of its own: tap Alt first)
[KeyInput]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)
[KeyInput]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
[KeyInput]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
}
"game window $($p.MainWindowHandle) '$($p.MainWindowTitle)', foreground $([KeyInput]::GetForegroundWindow())"
Start-Sleep -Milliseconds 500
foreach ($k in $Keys.Split(',')) {
    if ($k.Trim() -eq 'pause') { Start-Sleep -Seconds 4; continue }
    if ($env:KEYS_NO_FOCUS -and $k.Trim() -match '^(mouse:|click$|look:|wheel|rdown$|rup$|ldown$|lup$)') { "skipped $($k.Trim()): the mouse is the user's (KEYS_NO_FOCUS)"; continue }
    # mouse:X:Y (screen pixels) moves the cursor; click / rdown / rup press the mouse buttons; ldown / lup hold.
    if ($k.Trim().StartsWith('mouse:')) { $m = $k.Trim().Split(':'); [KeyInput]::SetCursorPos([int]$m[1], [int]$m[2]) | Out-Null; Start-Sleep -Milliseconds 600; continue }
    if ($k.Trim() -eq 'click') { [KeyInput]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 150; [KeyInput]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 600; continue }
    # look:DX:DY - a relative mouse move in steps (raw input: the editor's mouse look); rdown / rup - the right button
    if ($k.Trim().StartsWith('look:')) { $m = $k.Trim().Split(':'); for ($i = 0; $i -lt 10; $i++) { [KeyInput]::mouse_event(1, [int]([int]$m[1] / 10), [int]([int]$m[2] / 10), 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 40 }; Start-Sleep -Milliseconds 400; continue }
    if ($k.Trim() -eq 'wheelup') { [KeyInput]::mouse_event(0x800, 0, 0, 120, [UIntPtr]::Zero); Start-Sleep -Milliseconds 400; continue }
    if ($k.Trim() -eq 'wheeldown') { [KeyInput]::mouse_event(0x800, 0, 0, [uint32]4294967176, [UIntPtr]::Zero); Start-Sleep -Milliseconds 400; continue }
    if ($k.Trim() -eq 'rdown') { [KeyInput]::mouse_event(8, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 300; continue }
    if ($k.Trim() -eq 'rup') { [KeyInput]::mouse_event(0x10, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 400; continue }
    if ($k.Trim() -eq 'ldown') { [KeyInput]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 300; continue }
    if ($k.Trim() -eq 'lup') { [KeyInput]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 600; continue }
    # ctrl:<key>: Ctrl held while that key is pressed (posted messages)
    if ($k.Trim().StartsWith('ctrl:')) { $ck = $codes[$k.Trim().Substring(5)]; $h = $p.MainWindowHandle; [KeyInput]::PostMessage($h, 0x0100, [IntPtr]0x11, [IntPtr](1 -bor (0x1D -shl 16))) | Out-Null; Start-Sleep -Milliseconds 100; [KeyInput]::Post($h, [uint16]$ck[2], [uint16]$ck[0], [bool]$ck[1]); Start-Sleep -Milliseconds 100; [KeyInput]::PostMessage($h, 0x0101, [IntPtr]0x11, [IntPtr]([int](1 -bor (0x1D -shl 16)) -bor -1073741824)) | Out-Null; Start-Sleep -Milliseconds 1200; continue }
    # ctrlz: Ctrl held while Z is pressed (posted messages)
    if ($k.Trim() -eq 'ctrlz') { $h = $p.MainWindowHandle; [KeyInput]::PostMessage($h, 0x0100, [IntPtr]0x11, [IntPtr](1 -bor (0x1D -shl 16))) | Out-Null; Start-Sleep -Milliseconds 100; [KeyInput]::Post($h, 0x5A, 0x2C, $false); Start-Sleep -Milliseconds 100; [KeyInput]::PostMessage($h, 0x0101, [IntPtr]0x11, [IntPtr]([int](1 -bor (0x1D -shl 16)) -bor -1073741824)) | Out-Null; Start-Sleep -Milliseconds 1200; continue }
    if ($k.Trim() -eq 'ctrly') { $h = $p.MainWindowHandle; [KeyInput]::PostMessage($h, 0x0100, [IntPtr]0x11, [IntPtr](1 -bor (0x1D -shl 16))) | Out-Null; Start-Sleep -Milliseconds 100; [KeyInput]::Post($h, 0x59, 0x15, $false); Start-Sleep -Milliseconds 100; [KeyInput]::PostMessage($h, 0x0101, [IntPtr]0x11, [IntPtr]([int](1 -bor (0x1D -shl 16)) -bor -1073741824)) | Out-Null; Start-Sleep -Milliseconds 1200; continue }
    # hold:<key>:<ms> - the key held down for that long (posted messages, auto-repeat downs every 50 ms)
    if ($k.Trim().StartsWith('hold:')) {
        $m = $k.Trim().Split(':'); $hc = $codes[$m[1]]; $h = $p.MainWindowHandle
        $l = 1 -bor ([int]$hc[0] -shl 16) -bor ($(if ($hc[1]) { 1 -shl 24 } else { 0 }))
        $end = (Get-Date).AddMilliseconds([int]$m[2]); $first = $true
        while ((Get-Date) -lt $end) { [KeyInput]::PostMessage($h, 0x0100, [IntPtr][int]$hc[2], [IntPtr]($(if ($first) { $l } else { $l -bor 0x40000000 }))) | Out-Null; $first = $false; Start-Sleep -Milliseconds 50 }
        [KeyInput]::PostMessage($h, 0x0101, [IntPtr][int]$hc[2], [IntPtr]($l -bor -1073741824)) | Out-Null
        Start-Sleep -Milliseconds 300; continue
    }
    if ($k.Trim() -eq 'shot') {
        # (only the game's own window, and only when it's in front - game_shot.ps1; never the rest of the screen)
        $script:shots = $script:shots + 1
        $shotDir = if ($env:KEYS_SHOT_DIR) { $env:KEYS_SHOT_DIR } else { Join-Path $env:TEMP 'thug2-shots' }
        New-Item -ItemType Directory -Force $shotDir | Out-Null
        if ($env:KEYS_NO_FOCUS) { & "$PSScriptRoot\window_shot.ps1" -ProcessId $ProcessId -Path "$shotDir\step$($script:shots)-$ProcessId.png" }
        else { & "$PSScriptRoot\game_shot.ps1" -Path "$shotDir\step$($script:shots).png" }
        continue
    }
    # now:<key> - the key without the wait after it (for screenshots right after it); posted down, then up 60 ms later
    if ($k.Trim().StartsWith('now:')) {
        $nc = $codes[$k.Trim().Substring(4)]; $h = $p.MainWindowHandle
        $l = 1 -bor ([int]$nc[0] -shl 16) -bor ($(if ($nc[1]) { 1 -shl 24 } else { 0 }))
        [KeyInput]::PostMessage($h, 0x0100, [IntPtr][int]$nc[2], [IntPtr]$l) | Out-Null
        Start-Sleep -Milliseconds 60
        [KeyInput]::PostMessage($h, 0x0101, [IntPtr][int]$nc[2], [IntPtr]($l -bor -1073741824)) | Out-Null
        continue
    }
    $c = $codes[$k.Trim()]
    if ($env:KEYS_POST) { [KeyInput]::Post($p.MainWindowHandle, [uint16]$c[2], [uint16]$c[0], [bool]$c[1]) }
    else { [KeyInput]::Key([uint16]$c[0], [bool]$c[1]) }
    Start-Sleep -Milliseconds 1200
}
if (-not $env:KEYS_NO_FOCUS) {
# (the game back in front for the screenshot that follows: a level change can lose the foreground)
[KeyInput]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)
[KeyInput]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
[KeyInput]::SetForegroundWindow((Get-Process -Id $ProcessId).MainWindowHandle) | Out-Null
}
