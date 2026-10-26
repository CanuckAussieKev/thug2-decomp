param(
    [Parameter(Mandatory = $true)][ValidateSet('acquire', 'release', 'status')][string]$Action,
    [string]$Owner = '',
    [int]$WaitMinutes = 8,
    [string]$Game = 'C:\Games\THUG2'
)
# One game per game folder: whoever holds <Game>\.game-lock may deploy a DLL, edit the ini, launch and close the game
# in that folder. -Game: C:\Games\THUG2 (the user's, the default) or a workspace copy C:\Games\THUG2-1, -2, -3...
#   acquire: waits up to -WaitMinutes for the lock (prints BUSY and exits 1 if still held - do other work, try again);
#            on success backs up the deployed dinput8.dll and thug2-decomp.ini into the lock folder; in a workspace
#            then sets [features] mute_audio=1 (its games always run muted: Set-WorkspaceMuted, game_exe.ps1).
#   release: in a workspace, first closes a game still running from that folder (the holder's: crashed, stuck on a
#            dialog, or left open - its outcome and log tail recorded, game_health.ps1); then puts the backed-up
#            dinput8.dll and ini back and removes the lock. Never closes a game in the main install C:\Games\THUG2.
#   status:  who holds it, since when.
. (Join-Path $PSScriptRoot 'game_exe.ps1')
. (Join-Path $PSScriptRoot 'game_health.ps1')
$game = Resolve-GameFolder $Game
$mainInstall = $game -in @('C:\Games\THUG2', 'C:\Games\THUG2\Game')
if (-not (Find-GameExe $game)) { "NO GAME IN $game (no $($GameExeNames -join ' or ') in it or in its Game folder)"; exit 2 }
$lock = Join-Path $game '.game-lock'
# (the games running from this folder only - the other folders' games are someone else's)
function Get-GamesHere { Get-GameProcesses | Where-Object { $_.Path -and (Split-Path $_.Path -Parent) -eq $game } }
$ownerFile = Join-Path $lock 'owner.txt'

function Show-Status {
    if (Test-Path $lock) { "HELD: $(Get-Content $ownerFile -ErrorAction SilentlyContinue)" } else { 'FREE' }
}

switch ($Action) {
    'status' { Show-Status; exit 0 }
    'acquire' {
        if (-not $Owner) { 'give -Owner'; exit 2 }
        $deadline = (Get-Date).AddMinutes($WaitMinutes)
        while ($true) {
            try {
                New-Item -ItemType Directory -Path $lock -ErrorAction Stop | Out-Null
                Set-Content -Path $ownerFile -Value "$Owner since $(Get-Date -Format s)" -Encoding ascii
                Copy-Item (Join-Path $game 'dinput8.dll') (Join-Path $lock 'dinput8.dll.backup')
                Copy-Item (Join-Path $game 'thug2-decomp.ini') (Join-Path $lock 'thug2-decomp.ini.backup')
                # (a workspace's games always run muted - the user is at the machine; release puts the ini back)
                Set-WorkspaceMuted $game
                "ACQUIRED by $Owner"
                exit 0
            } catch {
                if ((Get-Date) -gt $deadline) { "BUSY - $(Show-Status)"; exit 1 }
                Start-Sleep -Seconds 15
            }
        }
    }
    'release' {
        if (-not (Test-Path $lock)) { 'not held'; exit 0 }
        $holder = Get-Content $ownerFile -ErrorAction SilentlyContinue
        if ($Owner -and -not ("$holder".StartsWith("$Owner "))) { "NOT YOURS: $holder"; exit 1 }
        # A workspace's game still running is the lock holder's (crashed, hung on a dialog, or left open): closed
        # first, its outcome recorded (game_health.ps1). The main install's never: the user may be playing.
        if (-not $mainInstall) {
            foreach ($running in @(Get-GamesHere)) {
                $why = Update-GameWatch (New-GameWatch $running 0)
                $closed = Close-Game $running
                if ($why) { Write-GameOutcome $game "at release: $why" $closed $running.StartTime }
                else { "a game was still running from $game at release: $closed" }
            }
        }
        if (Get-GamesHere) {
            Copy-Item (Join-Path $lock 'thug2-decomp.ini.backup') (Join-Path $game 'thug2-decomp.ini') -Force
            Remove-Item $lock -Recurse -Force
            "RELEASED - a game is running (the user's?), so it was left alone and dinput8.dll NOT put back (in use); ini put back"
            exit 0
        }
        Copy-Item (Join-Path $lock 'dinput8.dll.backup') (Join-Path $game 'dinput8.dll') -Force
        Copy-Item (Join-Path $lock 'thug2-decomp.ini.backup') (Join-Path $game 'thug2-decomp.ini') -Force
        Remove-Item $lock -Recurse -Force
        "RELEASED (dinput8.dll and ini put back)"
        exit 0
    }
}
