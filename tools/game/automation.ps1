# Dot-sourced by run_game.ps1 and game_cmd.ps1: the test-automation channel's files in a game folder
# (src/devtools/automation.h - [features] automation=1, set in the workspaces by make_workspace.ps1).
#   <folder>\thug2-decomp-commands.txt   commands the game runs, one a line (appended)
#   <folder>\thug2-decomp-status.json    where the game is and the last command's result (rewritten by the game)

function Get-CommandFile([string]$folder) { Join-Path $folder 'thug2-decomp-commands.txt' }
function Get-StatusFile([string]$folder) { Join-Path $folder 'thug2-decomp-status.json' }

# Whether the folder's ini has the channel on.
function Test-AutomationOn([string]$folder) {
    $ini = Join-Path $folder 'thug2-decomp.ini'
    (Test-Path $ini) -and ((Get-Content $ini) -match '^\s*automation\s*=\s*1')
}

# Before a run: the last run's commands and status gone.
function Clear-AutomationFiles([string]$folder) {
    foreach ($f in @((Get-CommandFile $folder), (Get-StatusFile $folder))) {
        if (Test-Path $f) { Remove-Item $f -Force -ErrorAction SilentlyContinue }
    }
}

# The status (null if there's none yet, or it was being rewritten).
function Read-GameStatus([string]$folder) {
    $path = Get-StatusFile $folder
    if (-not (Test-Path $path)) { return $null }
    try {
        $stream = [IO.File]::Open($path, 'Open', 'Read', [IO.FileShare]'ReadWrite, Delete')
        try { $text = (New-Object IO.StreamReader($stream)).ReadToEnd() } finally { $stream.Dispose() }
        if (-not $text.Trim().EndsWith('}')) { return $null }
        return $text | ConvertFrom-Json
    } catch { return $null }
}

# Append command lines (UTF-8 without a BOM, each ended); returns the file's line count after them. A ';' also
# separates commands (`powershell -File` passes an array as one string: use "waitfor ready;choose menu_options").
function Add-GameCommands([string]$folder, [string[]]$commands) {
    $commands = @($commands | ForEach-Object { $_ -split ';' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    $path = Get-CommandFile $folder
    $text = ($commands | ForEach-Object { $_ + "`n" }) -join ''
    $enc = New-Object Text.UTF8Encoding($false)
    for ($try = 0; $try -lt 20; $try++) {
        try { [IO.File]::AppendAllText($path, $text, $enc); break } catch { Start-Sleep -Milliseconds 50 }
    }
    $all = [IO.File]::ReadAllText($path, $enc)
    [regex]::Matches($all, "`n").Count
}

# Wait until the game has done `lines` lines of the command file (or one failed, or the game exited, or the time ran
# out, or - with a game_health.ps1 watch - it's in trouble). Returns @{ ok; why; status; seconds; trouble; exited }
# (trouble: Update-GameWatch's reason, else $null; exited: the game ended after doing them - a `quit`).
#
# A `quit` ends the game: it writes its last status ("quit -> ok", every line done) and only then shuts down, so
# whether the process is still there when that status is read is a race. Both orders give the same answer here: with
# the commands done and the last one a quit, the wait goes on until the process has exited (its exit code in `why`;
# ok only with code 0), and a process found gone has its status read once more before it counts as an early exit.
function Wait-GameCommands([string]$folder, [int]$lines, [int]$timeoutSeconds = 120, $process = $null, $gameWatch = $null) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $status = $null
    # The commands done: the result - after a quit, once the process has gone (up to 30 s, more if -Seconds has more).
    $finish = {
        param($s)
        $ok = -not ($s.result -match '^(error|refused):')
        if ($process -and $s.last -eq 'quit') {
            $left = [Math]::Max(30, $timeoutSeconds - $watch.Elapsed.TotalSeconds)
            if (-not $process.WaitForExit([int]($left * 1000))) {
                return @{ ok = $false; why = "$($s.result); quit, but the game still runs $([int]$left) s later"; status = $s; seconds = $watch.Elapsed.TotalSeconds }
            }
            $process.WaitForExit()  # (the exit code is read only once the process's streams are closed too)
            $code = $process.ExitCode
            return @{ ok = ($ok -and $code -eq 0); why = "$($s.result); the game exited (code $code) after its quit"; status = $s; seconds = $watch.Elapsed.TotalSeconds; exited = $true }
        }
        @{ ok = $ok; why = $s.result; status = $s; seconds = $watch.Elapsed.TotalSeconds }
    }
    while ($watch.Elapsed.TotalSeconds -lt $timeoutSeconds) {
        if ($gameWatch) {
            $trouble = Update-GameWatch $gameWatch
            if ($trouble) {
                return @{ ok = $false; why = "the game is in trouble: $trouble"; status = (Read-GameStatus $folder); seconds = $watch.Elapsed.TotalSeconds; trouble = $trouble }
            }
        }
        if ($process -and $process.HasExited) {
            # (its last status may have been written just before it went)
            $s = Read-GameStatus $folder
            if ($s -and $s.lines_done -ge $lines) { return (& $finish $s) }
            if ($s) { $status = $s }
            return @{ ok = $false; why = "the game exited (code $($process.ExitCode))"; status = $status; seconds = $watch.Elapsed.TotalSeconds }
        }
        $s = Read-GameStatus $folder
        if ($s) {
            $status = $s
            if ($s.lines_done -ge $lines) { return (& $finish $s) }
        }
        Start-Sleep -Milliseconds 100
    }
    @{ ok = $false; why = "timed out after $timeoutSeconds s"; status = $status; seconds = $watch.Elapsed.TotalSeconds }
}

# One line about a status.
function Format-GameStatus($s) {
    if (-not $s) { return 'no status' }
    "frames=$($s.frames) ready=$($s.ready) where=$($s.where) level=$($s.level) mode=$($s.game_mode) paused=$($s.paused) loading=$($s.loading) " +
    "focus=[$($s.focus -join ',')] selected=[$($s.selected -join ',')] done=$($s.lines_done)/$($s.lines_read) " +
    "last='$($s.last)' result='$($s.result)' errors=$($s.errors) refusals=$($s.refusals)"
}
