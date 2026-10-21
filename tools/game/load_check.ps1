param([Parameter(Mandatory = $true)][string]$Game, [Parameter(Mandatory = $true)][string[]]$Levels,
      [int]$Seconds = 50, [string]$ShotDir = '')
# Loads each named level into free skate in the -Game folder and reports whether it loaded and ran without errors:
#   load_check.ps1 -Game C:\Games\THUG2-2 -Levels thaw_dt, thaw_ma
# Take the folder's lock first (game_lock.ps1). Each level: run_game.ps1 starts the game with -level <name> (straight
# into free skate there - src/game/front/start_level.h; no keys, the overlay untouched), then the log is read (its
# level's collision loaded, no exception, run-time check failure or debug CRT report).
# A load check only: whether anything works when skated into is for the user to try.
. (Join-Path $PSScriptRoot 'game_exe.ps1')
$Game = Resolve-GameFolder $Game  # (the install root or its Game folder: the log is beside the exe)
if (-not $ShotDir) { $ShotDir = Join-Path $env:TEMP 'thug2-shots' }
$log = Join-Path $Game 'thug2-decomp.log'
foreach ($l in $Levels) {
    Remove-Item $log -ErrorAction SilentlyContinue
    $r = & "$PSScriptRoot\run_game.ps1" -Game $Game -Seconds $Seconds -Shot $l -ShotDir $ShotDir -Level $l
    $text = Get-Content $log -ErrorAction SilentlyContinue
    # (case-sensitive: the scripts' SetOnExceptionScript lines aren't errors)
    $bad = $text | Select-String -CaseSensitive -Pattern '^EXCEPTION|RUN-TIME CHECK FAILURE|DEBUG CRT REPORT|SELFTEST' | Select-Object -First 3
    $loaded = ($text | Select-String -SimpleMatch "levels\$l\$l.col.xbx" | Measure-Object).Count -gt 0
    "$l : $(($r | Select-String 'running after|exited early') -join ' ') loaded=$loaded $(($bad | % Line) -join ' | ')"
}
