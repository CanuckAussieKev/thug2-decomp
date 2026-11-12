param([Parameter(Mandatory = $true)][string]$Game, [string[]]$Levels = @('BO', 'bo', 'LA'),
      [string[]]$Anteara = @('0', '1'), [int]$Runs = 1, [int]$Seconds = 90)
# NOT ORIGINAL - the game started straight into a level (-level <name>, src/game/front_end/start_level.h) reaches it:
# for each level name (as typed: BO and bo both) and each [features] anteara_mod value, the game is started with
# -level <name>, and the run passes when its automation status is in that level (any case), not loading, before
# -Seconds; else the log's last lines are printed and the game is killed. Workspaces only, under the folder's lock
# (game_lock.ps1); the scripts as the folder has them (packed: ts_scripts unset / 0; unpacked by
# tools/qbjs/unpack_game.ts: ts_scripts=1 - set it yourself). The ini's anteara_mod is put back after.
#   tools/game/start_level_check.ps1 -Game C:\Games\THUG2-1 [-Levels BO,bo,LA] [-Anteara 0,1] [-Runs 1] [-Seconds 90]
# Exit code: the number of runs that didn't reach their level.
. (Join-Path $PSScriptRoot 'game_exe.ps1')
. (Join-Path $PSScriptRoot 'automation.ps1')
. (Join-Path $PSScriptRoot 'game_health.ps1')
$Game = Resolve-GameFolder $Game
if (-not (Test-Workspace $Game)) { "start_level_check.ps1 is for the workspaces only - $Game left alone"; exit 2 }
$exe = Find-LaunchExe $Game
if (-not $exe) { "NO $LaunchExeName IN $Game"; exit 2 }
if (-not (Test-AutomationOn $Game)) { "the automation channel is off in $Game ([features] automation=1)"; exit 4 }
if (Get-GameProcesses | Where-Object { $_.Path -and (Split-Path $_.Path -Parent) -eq $Game }) {
    "THUG2 IS ALREADY RUNNING FROM $Game - not started"; exit 3
}
$iniPath = Join-Path $Game 'thug2-decomp.ini'
$original = [IO.File]::ReadAllText($iniPath)
$status = Get-StatusFile $Game
$failed = 0
try {
    foreach ($anteara in $Anteara) {
        [IO.File]::WriteAllText($iniPath, (Set-IniValue $original 'features' 'anteara_mod' $anteara))
        Set-WorkspaceMuted $Game
        foreach ($level in $Levels) {
            foreach ($run in 1..$Runs) {
                Clear-AutomationFiles $Game
                Remove-Item $status -ErrorAction SilentlyContinue
                $watch = [Diagnostics.Stopwatch]::StartNew()
                $p = Start-Process -FilePath $exe -WorkingDirectory $Game -ArgumentList @('-level', $level) -PassThru
                $state = $null
                $seen = $false
                while (-not $seen -and $watch.Elapsed.TotalSeconds -lt $Seconds -and -not $p.HasExited) {
                    Start-Sleep -Milliseconds 200
                    try {
                        $state = Get-Content $status -Raw -ErrorAction Stop | ConvertFrom-Json
                        $seen = $state.where -eq 'level' -and $state.level -eq $level -and -not $state.loading
                    } catch {}
                }
                $elapsed = [Math]::Round($watch.Elapsed.TotalSeconds, 1)
                $exited = $p.HasExited
                if (-not $exited) { Close-Game $p | Out-Null }
                $p.WaitForExit()
                $what = "anteara_mod=$anteara -level $level"
                if ($seen) { "$what`: in the level after $elapsed s" }
                else {
                    ++$failed
                    $where = if ($state) { "where=$($state.where) level=$($state.level) loading=$($state.loading) frames=$($state.frames)" } else { 'no status' }
                    "$what`: FAILED - $(if ($exited) { "the game ended ($(Format-ExitCode $p.ExitCode))" } else { "not in the level after $elapsed s" }); $where"
                    Get-GameLogTail $Game 25
                }
            }
        }
    }
} finally {
    [IO.File]::WriteAllText($iniPath, $original)
}
if ($failed) { "$failed run(s) didn't reach their level" } else { 'every run reached its level' }
exit $failed
