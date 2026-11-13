param([Parameter(Mandatory = $true)][string[]]$Games, [string]$BuildDir = 'out\build\qbts-x64-release',
      [ValidateSet('x86', 'x64')][string]$Arch = 'x64', [int]$Seconds = 21600, [string[]]$PlanArgs = @(),
      [ValidateSet('both', 'qb', 'ts')][string]$Pass = 'both', [switch]$NoPrepare)
# NOT ORIGINAL - the script check (docs/script-check.md) split across several workspaces at once: each game folder gets
# the build and its share of the plan (script_check.ts shares, then plan --share), the shares run side by
# side (script_check.ps1, both passes), then one report from all of them (script_check.ts compare --game a,b,...) in
# the first folder's thug2-decomp-check. The caller holds every folder's lock (game_lock.ps1) and releases them after.
#   tools/game/script_check_parallel.ps1 -Games C:\Games\THUG2-1,C:\Games\THUG2-2 [-PlanArgs --runs,64] [-Pass qb|ts]
#   [-NoPrepare: the folders already have the build and their shares - e.g. the ts pass after a qb one]
# (the TypeScript pass runs in the game unpacked - no archive, no .qb: unpack_game.ts --check, script_check.ps1)
$ErrorActionPreference = 'Continue' # (node's warnings go to stderr: exit codes are checked)
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Set-Location $repo
$n = $Games.Count
if (-not $NoPrepare) {
    # (the shares and the plan are made from the games as installed: an unpacked one put back first - unpack_game.ts)
    foreach ($game in $Games) {
        if (Test-Path (Join-Path $game 'thug2-decomp-backup\unpacked.json')) {
            node tools/qbjs/unpack_game.ts --game $game --restore 2>$null | Select-Object -Last 1
        }
    }
    # (each module to one folder that has it - their overlays differ - about as many scripts each)
    node tools/qbjs/script_check.ts shares --game ($Games -join ',') 2>$null
    if ($LASTEXITCODE -ne 0) { 'shares failed'; exit 1 }
}
# Each share's time limit: -Seconds, or more for a big share - the TypeScript pass takes about a second a script
# (2026-11-13: 26,800 scripts a share - two workspaces - ran past 6 hours and were cut, a fifth of the plan untested):
# 1.5 s a script, -Seconds at the least. (A limit given with -Seconds is kept as it is.)
$limits = @{}
foreach ($game in $Games) { $limits[$game] = $Seconds }
for ($i = 0; $i -lt $n -and -not $NoPrepare; ++$i) {
    $game = $Games[$i]
    & (Join-Path $PSScriptRoot 'deploy_build.ps1') -Game $game -Arch $Arch -BuildDir $BuildDir -NoEditorTools | Select-Object -Last 1
    $planned = @(node tools/qbjs/script_check.ts plan --game $game --share @PlanArgs 2>$null)
    if ($LASTEXITCODE -ne 0) { "plan failed in $game"; exit 1 }
    $planned
    $size = $planned | Select-String -Pattern '^plan: \d+ modules, (\d+) scripts' | Select-Object -Last 1
    if ($size -and -not $PSBoundParameters.ContainsKey('Seconds')) {
        $limits[$game] = [Math]::Max($Seconds, [int](1.5 * [int]$size.Matches[0].Groups[1].Value))
        "time limit in ${game}: $($limits[$game]) s a pass"
    }
}
$started = Get-Date
$runs = foreach ($game in $Games) {
    $log = Join-Path $game 'thug2-decomp-check-run.txt'
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'script_check.ps1'),
        '-Game', $game, '-Arch', $Arch, '-Seconds', $limits[$game], '-Pass', $Pass)
    Start-Process powershell -PassThru -WindowStyle Hidden -RedirectStandardOutput $log -ArgumentList $arguments
}
$runs | Wait-Process
foreach ($game in $Games) { "== $game"; Get-Content (Join-Path $game 'thug2-decomp-check-run.txt') }
"all shares: $([int]((Get-Date) - $started).TotalSeconds) s"
node tools/qbjs/script_check.ts compare --game ($Games -join ',') 2>$null
