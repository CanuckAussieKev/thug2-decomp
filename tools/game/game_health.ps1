# Dot-sourced by run_game.ps1, game_cmd.ps1, game_lock.ps1 and load_check.ps1: is a game we started still healthy, and
# closing one that isn't. A game is in trouble when
#   - it exited by itself with an exit code other than 0 (the game's crash handling ends it with 3 or the exception's
#     code after logging - src/game/sys/crash.h; a normal quit is 0),
#   - Windows Error Reporting runs for it (a WerFault.exe whose command line names its process id),
#   - it shows a dialog (a visible top-level #32770 window of its own: a message box - "Buffer overrun detected!",
#     an assertion...; the game shows none since crash.h, but the game's own D3D error box and older builds do),
#   - its window has stopped responding (IsHungAppWindow) for -HangSeconds in a row (default 45) while it was idle (less
#     than a tenth of a core: a deadlock, a wait) - a long load also stops the message loop (a cold start of a workspace
#     with big overlays: over a minute), but it works the CPU. A game that spins in a loop isn't caught here: the
#     scripts' own time limits end those runs.
# Close-Game asks first (CloseMainWindow), then kills it and its WerFault. Write-GameOutcome prints what happened with
# the end of the game's log and appends it to <folder>\thug2-decomp-crashes.txt (ours - may be deleted).

if (-not ('GameHealth' -as [type])) {
    Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class GameHealth {
    public delegate bool EnumProc(IntPtr hwnd, IntPtr lParam);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc proc, IntPtr lParam);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumProc proc, IntPtr lParam);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr hwnd, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr hwnd, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool IsHungAppWindow(IntPtr hwnd);
    static string Text(IntPtr h) { var s = new StringBuilder(1024); GetWindowText(h, s, 1024); return s.ToString(); }
    static string Class(IntPtr h) { var s = new StringBuilder(256); GetClassName(h, s, 256); return s.ToString(); }
    // The process' visible dialogs: "<title>: <its static texts>" each.
    public static List<string> Dialogs(uint pid) {
        var found = new List<string>();
        EnumWindows((h, l) => {
            uint owner; GetWindowThreadProcessId(h, out owner);
            if (owner == pid && IsWindowVisible(h) && Class(h) == "#32770") {
                var texts = new List<string>();
                EnumChildWindows(h, (c, l2) => { if (Class(c) == "Static") { var t = Text(c).Trim(); if (t.Length > 0) texts.Add(t); } return true; }, IntPtr.Zero);
                found.Add((Text(h) + ": " + String.Join(" / ", texts)).Replace("\r", " ").Replace("\n", " "));
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
"@
}

# WerFault.exe processes reporting on process $id.
function Get-WerFaultFor([int]$id) {
    @(Get-CimInstance Win32_Process -Filter "Name='WerFault.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -match "-p\s+$id(\s|$)" })
}

# A watch on one game: Update-GameWatch returns $null while it's fine, else why it isn't.
function New-GameWatch($process, [int]$HangSeconds = 45) {
    # (its handle opened now: a process found with Get-Process - not started here - has no exit code afterwards
    # otherwise: ExitCode is $null once it has exited, and a normal quit looked like a crash)
    try { $null = $process.Handle } catch { }
    @{ process = $process; hangSeconds = $HangSeconds; hungSince = $null; hungCpu = 0.0; lastWer = [DateTime]::MinValue }
}

function Update-GameWatch($watch) {
    $p = $watch.process
    $p.Refresh()
    if ($p.HasExited) {
        if ($null -ne $p.ExitCode -and $p.ExitCode -ne 0) { return "it ended with exit code $(Format-ExitCode $p.ExitCode)" }
        return $null
    }
    $dialogs = [GameHealth]::Dialogs([uint32]$p.Id)
    if ($dialogs.Count) { return "it shows a dialog - $($dialogs -join ' | ')" }
    # (WMI is slow: at most every 2 s)
    if (((Get-Date) - $watch.lastWer).TotalSeconds -ge 2) {
        $watch.lastWer = Get-Date
        $wer = Get-WerFaultFor $p.Id
        if ($wer.Count) { return "Windows Error Reporting runs for it (WerFault $($wer[0].ProcessId))" }
    }
    $h = $p.MainWindowHandle
    if ($h -ne [IntPtr]::Zero -and [GameHealth]::IsHungAppWindow($h)) {
        $cpu = $p.TotalProcessorTime.TotalSeconds
        if (-not $watch.hungSince) { $watch.hungSince = Get-Date; $watch.hungCpu = $cpu }
        $s = ((Get-Date) - $watch.hungSince).TotalSeconds
        if ($s -ge $watch.hangSeconds) {
            # (a long load also stops the message loop - but it works: a game that used a tenth of a core or more
            # meanwhile isn't hung; the count starts again)
            if ($cpu - $watch.hungCpu -lt 0.1 * $s) {
                return ("its window hasn't responded for {0:N0} s, idle ({1:N1} s of CPU)" -f $s, ($cpu - $watch.hungCpu))
            }
            $watch.hungSince = Get-Date; $watch.hungCpu = $cpu
        }
    } else { $watch.hungSince = $null }
    $null
}

function Format-ExitCode([int]$code) {
    $hex = '0x{0:X8}' -f $code
    switch ($hex) {
        '0x00000003' { return "3 (the game logged a fatal report and ended it - src/game/sys/crash.h)" }
        '0xC0000005' { return "$hex (access violation)" }
        '0xC0000409' { return "$hex (buffer overrun: the game's /GS check, or a fast-fail exit - nothing logged then)" }
        '0xC00000FD' { return "$hex (stack overflow)" }
        '0xC0000374' { return "$hex (heap corruption)" }
        default { return "$code ($hex)" }
    }
}

# Closes the game: WM_CLOSE to its window first, then a kill (and its WerFault). Returns how it went.
function Close-Game($process, [int]$GraceSeconds = 5) {
    $p = $process
    $p.Refresh()
    if ($p.HasExited) { return "it had exited (code $(Format-ExitCode $p.ExitCode))" }
    $how = 'killed'
    try {
        if ($p.MainWindowHandle -ne [IntPtr]::Zero -and $p.CloseMainWindow()) {
            if ($p.WaitForExit($GraceSeconds * 1000)) { $how = 'closed (its window closed)' }
        }
    } catch { }
    foreach ($w in (Get-WerFaultFor $p.Id)) { Stop-Process -Id $w.ProcessId -Force -ErrorAction SilentlyContinue }
    if (-not $p.HasExited) {
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        $p.WaitForExit(10000) | Out-Null
    }
    if ($p.HasExited) { $how } else { 'STILL RUNNING after a kill' }
}

# The last lines of the game's log (the newest thug2-decomp*.log in the folder: a second copy writes -2.log). The
# log can only be read once the game has closed it.
function Get-GameLogTail([string]$folder, [int]$Lines = 30) {
    $log = Get-ChildItem -Path $folder -Filter 'thug2-decomp*.log' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $log) { return @('(no log)') }
    try {
        $stream = [IO.File]::Open($log.FullName, 'Open', 'Read', [IO.FileShare]'ReadWrite, Delete')
        try { $text = (New-Object IO.StreamReader($stream)).ReadToEnd() } finally { $stream.Dispose() }
        @("($($log.Name))") + @($text -split "`r?`n" | Where-Object { $_ -ne '' } | Select-Object -Last $Lines)
    } catch { @("($($log.Name) can't be read yet: $($_.Exception.Message))") }
}

# Prints the outcome with the log's last lines (and a crash dump written since $since) and records it in
# <folder>\thug2-decomp-crashes.txt.
function Write-GameOutcome([string]$folder, [string]$what, [string]$closed, [datetime]$since) {
    $dumps = @(Get-ChildItem (Join-Path $env:LOCALAPPDATA 'CrashDumps') -Filter 'THUG2*.dmp' -ErrorAction SilentlyContinue |
        Where-Object { $_.LastWriteTime -ge $since } | ForEach-Object FullName)
    $tail = Get-GameLogTail $folder
    $block = @("=== $(Get-Date -Format s) $folder", "GAME IN TROUBLE: $what", "then: $closed") +
        $(if ($dumps.Count) { @("crash dump: $($dumps -join ', ') (node tools/pipeline/game/read_dump.ts <dump>)") } else { @() }) +
        @('the log ends:') + ($tail | ForEach-Object { "  $_" }) +
        @('(an EXCEPTION / RUN-TIME CHECK / ... report in it: node tools/symbolize.ts --pdb-dir <your build dir> <the log>)')
    $block
    try { Add-Content -Path (Join-Path $folder 'thug2-decomp-crashes.txt') -Value $block -Encoding utf8 } catch { }
}
