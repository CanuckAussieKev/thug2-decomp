param([Parameter(Mandatory = $true)][ValidateRange(1, 9)][int]$Number, [string]$From = 'C:\Games\THUG2', [string]$To = '')
# Makes (or refreshes) a workspace copy of the game, C:\Games\THUG2-<Number>, from the main install, for an agent's
# game runs alongside the user's: a 1280x720 window (not borderless), allow_multiple_instances=1, debug_collision=0,
# the self-tests on ([selftests] enabled=1 - the main install, the user's own, has them off: all testing happens in
# the workspaces) and the test-automation channel on ([features] automation=1: commands from
# thug2-decomp-commands.txt, src/game/front/automation.h - never on in the main install).
#   make_workspace.ps1 -Number 3
# -To <folder>: make it there instead of C:\Games\THUG2-<Number> (a scratch folder, to try the script itself).
# A refresh mirrors the main install over it (main's DLL, levels and overlay; files only the workspace had - its logs,
# an agent's levels - go). Refuses while the workspace's lock is held or a game runs from either folder.
. (Join-Path $PSScriptRoot 'game_exe.ps1')
# (the main install is a stock one: its game is in C:\Games\THUG2\Game - a workspace is a copy of that folder)
$From = Resolve-GameFolder $From
$to = if ($To) { $To } else { "C:\Games\THUG2-$Number" }
if (-not (Find-GameExe $From)) { "NO GAME IN $From (no $($GameExeNames -join ' or ') in it or in its Game folder)"; exit 2 }
if (Test-Path (Join-Path $to '.game-lock')) { "LOCKED: $to (game_lock.ps1 -Action status -Game $to)"; exit 1 }
$running = Get-GameProcesses | Where-Object { $_.Path -and (Split-Path $_.Path -Parent) -in @($From, $to) }
if ($running) { 'A GAME IS RUNNING FROM ONE OF THE FOLDERS - not copied'; exit 3 }
# (robocopy exit codes below 8 are success: 1 = files copied)
robocopy $From $to /MIR /XD (Join-Path $From '.game-lock') /XF 'thug2-decomp*.log' 'thug2-decomp-commands.txt' 'thug2-decomp-status.json' /NFL /NDL /NJH /NP | Out-Null
if ($LASTEXITCODE -ge 8) { "robocopy failed ($LASTEXITCODE)"; exit 4 }
$ini = Join-Path $to 'thug2-decomp.ini'
$text = [IO.File]::ReadAllText($ini)
foreach ($kv in @(@('ResolutionX', '1280'), @('ResolutionY', '720'), @('Borderless', '0'),
                  @('allow_multiple_instances', '1'), @('debug_collision', '0'))) {
    $text = [regex]::Replace($text, "(?m)^$($kv[0])=.*$", "$($kv[0])=$($kv[1])")
}
# (the self-tests on - the main install's are off)
$text = [regex]::Replace($text, '(?m)^enabled=\d', 'enabled=1')  # ([selftests] has the ini's only enabled=)
# (and the early tests skipped in game runs: thug2_check runs them offline - run_game.ps1 -FullTests runs everything)
if ($text -match '(?m)^early=') { $text = [regex]::Replace($text, '(?m)^early=.*$', 'early=0') }
else { $text = [regex]::Replace($text, '(?m)^enabled=1', "enabled=1`r`nearly=0") }
# (the test-automation channel on: run_game.ps1 -Commands, game_cmd.ps1)
if ($text -match '(?m)^automation=') { $text = [regex]::Replace($text, '(?m)^automation=.*$', 'automation=1') }
elseif ($text -match '(?m)^\[features\]') { $text = [regex]::Replace($text, '(?m)^\[features\]\r?\n', "[features]`r`nautomation=1`r`n") }
else { $text += "`r`n[features]`r`nautomation=1`r`n" }
[IO.File]::WriteAllText($ini, $text)
Set-WorkspaceMuted $to  # (its games always run muted: [features] mute_audio=1)
"made $to from $From"
Select-String -Path $ini -Pattern '^(ResolutionX|ResolutionY|Borderless|allow_multiple_instances|debug_collision|enabled|early|automation|mute_audio)=' | % Line
exit 0
