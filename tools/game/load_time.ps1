param([Parameter(Mandatory = $true)][string]$Game, [string]$Level = 'bo', [string[]]$Modes = @('0', '1', '2'),
      [int]$Runs = 3, [int]$Seconds = 120)
# NOT ORIGINAL - how long the game takes from its start to free skate in a level, with each [features] ts_scripts mode:
# 0 the .qb interpreter only, 1 TypeScript modules beside the .qb files (install_ts.ts), 2 the game unpacked
# (tools/qbjs/unpack_game.ts: no archive, no .qb - each file from its .ts). Workspaces only, under the folder's lock
# (game_lock.ps1); the folder needs the modules the modes use. Each run: the game started with -level <name>, timed until its automation status is in the level and
# not loading, then closed; the ini's ts_scripts is put back after.
#   tools/game/load_time.ps1 -Game C:\Games\THUG2-1 [-Level bo] [-Modes 0,2] [-Runs 3]
. (Join-Path $PSScriptRoot 'game_exe.ps1')
. (Join-Path $PSScriptRoot 'automation.ps1')
$Game = Resolve-GameFolder $Game
if (-not (Test-Workspace $Game)) { "load_time.ps1 is for the workspaces only - $Game left alone"; exit 2 }
$exe = Join-Path $Game 'thug2-decomp.exe'
$iniPath = Join-Path $Game 'thug2-decomp.ini'
$original = [IO.File]::ReadAllText($iniPath)
# (the automation channel's state - [features] automation=1, as the workspaces have it: the log is locked while the
# game runs)
$status = Join-Path $Game 'thug2-decomp-status.json'
if (-not (Test-AutomationOn $Game)) { "the automation channel is off in $Game ([features] automation=1)"; exit 4 }
try {
    foreach ($mode in $Modes) {
        [IO.File]::WriteAllText($iniPath, ($original -replace '(?m)^ts_scripts=.*$', "ts_scripts=$mode"))
        $times = foreach ($run in 1..$Runs) {
            Remove-Item $status -ErrorAction SilentlyContinue
            $watch = [Diagnostics.Stopwatch]::StartNew()
            $p = Start-Process -FilePath $exe -WorkingDirectory $Game -ArgumentList @('-level', $Level) -PassThru
            $seen = $false
            while (-not $seen -and $watch.Elapsed.TotalSeconds -lt $Seconds -and -not $p.HasExited) {
                Start-Sleep -Milliseconds 100
                try {
                    $state = Get-Content $status -Raw -ErrorAction Stop | ConvertFrom-Json
                    $seen = $state.where -eq 'level' -and $state.level -eq $Level -and -not $state.loading
                } catch {}
            }
            $elapsed = $watch.Elapsed.TotalSeconds
            if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -Confirm:$false }
            $p.WaitForExit()
            if ($seen) { [Math]::Round($elapsed, 1) } else { "none (not in $Level after $([int]$elapsed) s)" }
        }
        "ts_scripts=$mode`: $($times -join ' s, ') s"
    }
} finally {
    [IO.File]::WriteAllText($iniPath, $original)
}
