param([Parameter(Mandatory = $true)][ValidateRange(1, 9)][int]$Number, [string]$From = 'C:\Games\THUG2')
# Makes (or refreshes) a workspace copy of the game, C:\Games\THUG2-<Number>, from the main install, for an agent's
# game runs alongside the user's: a 1280x720 window (not borderless), allow_multiple_instances=1, debug_collision=0,
# and the self-tests on ([selftests] enabled=1 - the main install, the user's own, has them off: all testing happens in
# the workspaces).
#   make_workspace.ps1 -Number 3
# A refresh mirrors the main install over it (main's DLL, levels and overlay; files only the workspace had - its logs,
# an agent's levels - go). Refuses while the workspace's lock is held or a game runs from either folder.
$From = $From.TrimEnd('\')
$to = "C:\Games\THUG2-$Number"
if (-not (Test-Path (Join-Path $From 'THUG2PM.exe'))) { "NO GAME IN $From"; exit 2 }
if (Test-Path (Join-Path $to '.game-lock')) { "LOCKED: $to (game_lock.ps1 -Action status -Game $to)"; exit 1 }
$running = Get-Process THUG2PM -ErrorAction SilentlyContinue | Where-Object { $_.Path -and (Split-Path $_.Path -Parent) -in @($From, $to) }
if ($running) { 'A GAME IS RUNNING FROM ONE OF THE FOLDERS - not copied'; exit 3 }
# (robocopy exit codes below 8 are success: 1 = files copied)
robocopy $From $to /MIR /XD (Join-Path $From '.game-lock') /XF 'thug2-decomp*.log' /NFL /NDL /NJH /NP | Out-Null
if ($LASTEXITCODE -ge 8) { "robocopy failed ($LASTEXITCODE)"; exit 4 }
$ini = Join-Path $to 'thug2-decomp.ini'
$text = [IO.File]::ReadAllText($ini)
foreach ($kv in @(@('ResolutionX', '1280'), @('ResolutionY', '720'), @('Borderless', '0'),
                  @('allow_multiple_instances', '1'), @('debug_collision', '0'))) {
    $text = [regex]::Replace($text, "(?m)^$($kv[0])=.*$", "$($kv[0])=$($kv[1])")
}
# (the self-tests on - the main install's are off)
$text = [regex]::Replace($text, '(?m)^enabled=\d', 'enabled=1')  # ([selftests] has the ini's only enabled=)
[IO.File]::WriteAllText($ini, $text)
"made $to from $From"
Select-String -Path $ini -Pattern '^(ResolutionX|ResolutionY|Borderless|allow_multiple_instances|debug_collision|enabled)=' | % Line
exit 0
