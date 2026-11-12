param([Parameter(Mandatory = $true)][string]$Game, [string]$Level = 'bo', [string[]]$Modes = @('0', '1'),
      [int]$Runs = 3, [int]$Seconds = 120)
# NOT ORIGINAL - how long the game takes from its start to free skate in a level, with each [features] ts_scripts mode:
# 0 the .qb files, 1 the game unpacked (tools/qbjs/unpack_game.ts: no archive, no .qb - each file from its .ts); the
# folder is unpacked or put back for each mode and left as the last one had it. Workspaces only, under the folder's
# lock (game_lock.ps1). Each run: the game started with -level <name>, timed until its automation status is in the
# level and not loading, then closed; the ini's ts_scripts is put back after.
#   tools/game/load_time.ps1 -Game C:\Games\THUG2-1 [-Level bo] [-Modes 0,1] [-Runs 3]
. (Join-Path $PSScriptRoot 'game_exe.ps1')
. (Join-Path $PSScriptRoot 'automation.ps1')
$Game = Resolve-GameFolder $Game
$unpack = Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) 'tools\qbjs\unpack_game.ts'
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
        $unpacked = Test-Path (Join-Path $Game 'thug2-decomp-backup\unpacked.json')
        if ($mode -eq '0' -and $unpacked) { node $unpack --game $Game --restore | Select-Object -Last 1 }
        if ($mode -ne '0' -and -not $unpacked) { node $unpack --game $Game | Select-Object -Last 1 }
        $text = if ($original -match '(?m)^ts_scripts=') { $original -replace '(?m)^ts_scripts=.*$', "ts_scripts=$mode" }
                else { $original -replace '(?m)^\[features\]\r?$', "[features]`r`nts_scripts=$mode" }
        [IO.File]::WriteAllText($iniPath, $text)
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
