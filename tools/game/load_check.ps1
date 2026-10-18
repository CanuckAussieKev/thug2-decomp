param([Parameter(Mandatory = $true)][string]$Game, [Parameter(Mandatory = $true)][string[]]$Levels,
      [int]$Seconds = 50, [string]$ShotDir = '')
# Loads each named level into free skate in the -Game folder and reports whether it loaded and ran without errors:
#   load_check.ps1 -Game C:\Games\THUG2-2 -Levels thaw_dt, thaw_ma
# Take the folder's lock first (game_lock.ps1). Each level: boot_level.py points SKATE NOW at it, run_game.ps1 starts
# the game and presses Enter on the main menu, then the log is read (its level's collision loaded, no exception, run-time
# check failure or debug CRT report). Ends with make_menu_level.py --game putting the real SKATE NOW back.
# A load check only: whether anything works when skated into is for the user to try.
$Game = $Game.TrimEnd('\')
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $ShotDir) { $ShotDir = Join-Path $env:TEMP 'thug2-shots' }
$log = Join-Path $Game 'thug2-decomp.log'
foreach ($l in $Levels) {
    python -I "$PSScriptRoot\boot_level.py" $l $Game | Out-Null
    Remove-Item $log -ErrorAction SilentlyContinue
    $r = & "$PSScriptRoot\run_game.ps1" -Game $Game -Seconds $Seconds -Shot $l -ShotDir $ShotDir `
        -Keys 'enter,pause,pause,pause,pause,pause,pause,pause,pause,pause,pause'
    $text = Get-Content $log -ErrorAction SilentlyContinue
    # (case-sensitive: the scripts' SetOnExceptionScript lines aren't errors)
    $bad = $text | Select-String -CaseSensitive -Pattern '^EXCEPTION|RUN-TIME CHECK FAILURE|DEBUG CRT REPORT' | Select-Object -First 3
    $loaded = ($text | Select-String -SimpleMatch "levels\$l\$l.col.xbx" | Measure-Object).Count -gt 0
    "$l : $(($r | Select-String 'running after|exited early') -join ' ') loaded=$loaded $(($bad | % Line) -join ' | ')"
}
python -I "$repo\tools\menu_level\make_menu_level.py" --game $Game | Out-Null
