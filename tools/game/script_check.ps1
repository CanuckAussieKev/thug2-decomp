param([string]$Game = 'C:\Games\THUG2-1', [ValidateSet('', 'x86', 'x64')][string]$Arch = 'x64',
      [ValidateSet('both', 'qb', 'ts')][string]$Pass = 'both', [int]$Seconds = 21600)
# NOT ORIGINAL - the script check's two passes in a workspace (src/script_qb/script_check.h, docs/script-check.md):
# the game started with -script-check <Game>\thug2-decomp-check, once with [features] ts_scripts=0 (QB: qb.txt) and
# once with ts_scripts=1 (TypeScript, replaying QB's runs: ts.txt) in the game unpacked - tools/qbjs/unpack_game.ts
# --check: no archive, no .qb, every file read from its .ts -, put back after. The QB pass
# runs in the game as installed (an unpacked one put back first). Each run quits by itself when the plan is done.
# The caller holds the workspace's lock (game_lock.ps1) and has written the plan (tools/qbjs/script_check.ts plan); the
# ini is put back by the lock's release.
. (Join-Path $PSScriptRoot 'game_exe.ps1')
$root = Resolve-GameFolder $Game
$check = Join-Path $root 'thug2-decomp-check'
if (-not (Test-Path (Join-Path $check 'plan.qb'))) { "no plan in $check (node tools/qbjs/script_check.ts plan --game $Game)"; exit 2 }
$ini = Join-Path $root 'thug2-decomp.ini'

# [features] ts_scripts=<on> in the ini (no byte-order mark: the game's ini reader wants none).
function Set-TsScripts([int]$on) {
    $text = [IO.File]::ReadAllText($ini)
    if ($text -match '(?m)^ts_scripts=\d') { $text = $text -replace '(?m)^ts_scripts=\d', "ts_scripts=$on" }
    else { $text = $text -replace '(?m)^\[features\]\r?$', "[features]`r`nts_scripts=$on" }
    [IO.File]::WriteAllText($ini, $text, (New-Object Text.UTF8Encoding($false)))
}

# [memory] pools four times the default: a check loads each level's file on top of the boot scripts, and a converted
# level's can be large (THPG's whole world, 5.4 MB, ran the arrays dry there).
$pools = 'component_pool=1440000', 'struct_pool=256000', 'vector_pool=192000', 'array_pool=104000'
$lines = @([IO.File]::ReadAllLines($ini) | Where-Object { $_ -notmatch '^(component|struct|vector|array)_pool=' })
$at = [array]::IndexOf($lines, '[memory]')
$lines = if ($at -ge 0) { @($lines[0..$at]) + $pools + @($lines | Select-Object -Skip ($at + 1)) } else { $lines + '[memory]' + $pools }
[IO.File]::WriteAllText($ini, ($lines -join "`r`n") + "`r`n", (New-Object Text.UTF8Encoding($false)))

$passes = if ($Pass -eq 'both') { @('qb', 'ts') } else { @($Pass) }
$unpacked = { Test-Path (Join-Path $root 'thug2-decomp-backup\unpacked.json') }
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
function Invoke-Unpack([string[]]$arguments) {
    Push-Location $repo
    node tools/qbjs/unpack_game.ts --game $root @arguments 2>$null | Select-Object -Last 1
    Pop-Location
}
foreach ($p in $passes) {
    if ($p -eq 'qb' -and (& $unpacked)) { Invoke-Unpack @('--restore') }
    if ($p -eq 'ts') { Invoke-Unpack $(if (& $unpacked) { @('--refresh', '--check') } else { @('--check') }) }
    Set-TsScripts $(if ($p -ne 'ts') { 0 } else { 1 })
    $started = Get-Date
    & (Join-Path $PSScriptRoot 'run_game.ps1') -Game $Game -Arch $Arch -Seconds $Seconds -HangSeconds $Seconds `
        -Shot "script_check_$p" -Extra @('-script-check', $check) | Select-Object -Last 1
    $out = Join-Path $check "$p.txt"
    "$p pass: $([int]((Get-Date) - $started).TotalSeconds) s, $(if (Test-Path $out) { (Get-Item $out).Length } else { 0 }) bytes"
}
if (& $unpacked) { Invoke-Unpack @('--restore') }
