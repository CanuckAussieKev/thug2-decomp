param([Parameter(Mandatory = $true)][string]$Game, [Parameter(Mandatory = $true)][string[]]$Levels,
      [int]$Seconds = 50, [string]$ShotDir = '')
# Loads each named level into free skate in the -Game folder and reports whether it loaded and ran without errors:
#   load_check.ps1 -Game C:\Games\THUG2-2 -Levels thaw_dt, thaw_ma
# Take the folder's lock first (game_lock.ps1). Each level: run_game.ps1 starts the game with -level <name> (straight
# into free skate there - src/game/front/start_level.h; no keys, the overlay untouched), then the log is read (its
# level's collision loaded, no exception, run-time check failure or debug CRT report). A level whose game crashed or got
# stuck (run_game.ps1 closes it: game_health.ps1) is reported with the end of its log, and the check goes on.
# A load check only: whether anything works when skated into is for the user to try.
. (Join-Path $PSScriptRoot 'game_exe.ps1')
$Game = Resolve-GameFolder $Game  # (the install root or its Game folder: the log is beside the exe)
if (-not $ShotDir) { $ShotDir = Join-Path $env:TEMP 'thug2-shots' }
$log = Join-Path $Game 'thug2-decomp.log'
foreach ($l in $Levels) {
    Remove-Item $log -ErrorAction SilentlyContinue
    $r = & "$PSScriptRoot\run_game.ps1" -Game $Game -Seconds $Seconds -Shot $l -ShotDir $ShotDir -Level $l
    $text = Get-Content $log -ErrorAction SilentlyContinue
    # (case-sensitive: the scripts' SetOnExceptionScript lines aren't errors; the reports: src/game/sys/crash.h)
    $bad = $text | Select-String -CaseSensitive -Pattern '^EXCEPTION|RUN-TIME CHECK FAILURE|DEBUG CRT REPORT|^INVALID PARAMETER|^PURE VIRTUAL CALL|^ABORT|^BUFFER OVERRUN|^GAME CRT MESSAGE|SELFTEST' |
        Where-Object { $_.Line -notmatch 'self-tests: skipped' } | Select-Object -First 3
    $loaded = ($text | Select-String -SimpleMatch "levels\$l\$l.col.xbx" | Measure-Object).Count -gt 0
    # (the script pools once the level is up - start_level.cpp's line; CComponent / CStruct, and any pool full)
    $pools = ''
    $p = $text | Select-String -Pattern 'script pools in use .*' | Select-Object -Last 1
    if ($p -and $p.Line -match 'CComponent (\d+) \(peak (\d+)\) of (\d+), CStruct (\d+) \(peak (\d+)\) of (\d+)') {
        $pools = "CComponent=$($Matches[1])/peak $($Matches[2])/$($Matches[3]) CStruct=$($Matches[4])/peak $($Matches[5])/$($Matches[6])"
    }
    $full = ($text | Select-String -Pattern 'pool \S+ (full|80% full)' | % { $_.Line -replace '^.*?(pool )', '$1' }) -join ' | '
    # (a crashed or stuck game: run_game closed it and printed "GAME IN TROUBLE: ..." with the log's end - shown whole)
    $trouble = $r | Select-String -SimpleMatch 'GAME IN TROUBLE'
    "$l : $(($r | Select-String 'running after|exited early|GAME IN TROUBLE') -join ' ') loaded=$loaded $pools $full $(($bad | % Line) -join ' | ')"
    if ($trouble) { $r | Select-Object -Skip 1 }
}
