param(
    [Parameter(Mandatory = $true)][ValidateSet('acquire', 'release', 'status')][string]$Action,
    [string]$Owner = '',
    [int]$WaitMinutes = 8,
    [string]$Game = 'C:\Games\THUG2',
    [int]$ExitWaitSeconds = 60
)
# One game per game folder: whoever holds <Game>\.game-lock may deploy a build, edit the ini, launch and close the game
# in that folder. -Game: C:\Games\THUG2 (the user's, the default) or a workspace copy C:\Games\THUG2-1, -2, -3...
#   acquire: waits up to -WaitMinutes for the lock (prints BUSY and exits 1 if still held - do other work, try again);
#            on success backs up the deployed thug2-decomp.exe (with its PDB, the level editor plugin in plugins, d3dx9_43.dll, SDL3.dll,
#            FFmpeg's DLLs, the C runtime's, gamecontrollerdb.txt; dinput8.dll - those that are there: game_exe.ps1's $DeployedFiles, a
#            32-bit and a 64-bit build alike - deploy_build.ps1 puts one in INSTEAD of the other) and
#            thug2-decomp.ini into the lock folder; in a workspace
#            then sets [features] mute_audio=1 (its games always run muted: Set-WorkspaceMuted, game_exe.ps1).
#   release: in a workspace, first closes a game still running from that folder (the holder's: crashed, stuck on a
#            dialog, or left open - its outcome and log tail recorded, game_health.ps1) and waits for it to go (up to
#            -ExitWaitSeconds, then kills it); then puts the backed-up files and ini back (removing the deployed ones
#            that weren't there) and removes the lock. Never closes a game in the main install C:\Games\THUG2. While a
#            game from the folder is still running it refuses (exit 1): the lock kept, nothing put back.
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
                $had = @()
                foreach ($f in $DeployedFiles) {
                    if (Test-Path (Join-Path $game $f)) { Copy-Item (Join-Path $game $f) (Join-Path $lock (Get-BackupName $f)); $had += $f }
                }
                Set-Content -Path (Join-Path $lock 'deployed.txt') -Value ($had -join "`r`n") -Encoding ascii
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
        # Nothing is put back while a game from this folder is still running (its exe is in use: the copy would fail
        # and the deployed files would be lost with the lock). A closed game can take a while to go (its process
        # listed until it has gone): waited for, up to -ExitWaitSeconds; a workspace's then killed again.
        $deadline = (Get-Date).AddSeconds($ExitWaitSeconds)
        while ((Get-GamesHere) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
        if (-not $mainInstall) {
            foreach ($running in @(Get-GamesHere)) { "a game from $game still running at release: $(Close-Game $running)" }
        }
        if (Get-GamesHere) {
            "NOT RELEASED - a game is still running from $game$(if ($mainInstall) { ' (the user''s?)' }); the lock is kept and nothing put back: release again once it has exited"
            exit 1
        }
        $had = @(Get-Content (Join-Path $lock 'deployed.txt') -ErrorAction SilentlyContinue | Where-Object { $_ })
        # (a game that has just exited can hold its exe open for a moment: each copy tried again for up to 10 s, and
        # the lock - with its backups - kept if one still fails)
        $failed = @()
        foreach ($f in $DeployedFiles) {
            $done = $false
            for ($try = 0; $try -lt 20 -and -not $done; $try++) {
                try {
                    if ($f -in $had) {
                        $into = Split-Path (Join-Path $game $f) -Parent
                        if (-not (Test-Path $into)) { New-Item -ItemType Directory -Path $into -ErrorAction Stop | Out-Null }
                        Copy-Item (Join-Path $lock (Get-BackupName $f)) (Join-Path $game $f) -Force -ErrorAction Stop
                    }
                    elseif (Test-Path (Join-Path $game $f)) { Remove-Item (Join-Path $game $f) -Force -ErrorAction Stop }
                    $done = $true
                } catch { Start-Sleep -Milliseconds 500 }
            }
            if (-not $done) { $failed += $f }
        }
        if ($failed.Count) {
            "NOT RELEASED - $($failed -join ', ') couldn't be put back (in use?); the lock and its backups are kept: release again"
            exit 1
        }
        # (a folder of deployed files - plugins\ - left empty by the release is removed: it held only ours)
        foreach ($folder in @($DeployedFiles | Where-Object { $_ -match '[\\/]' } | ForEach-Object { Split-Path $_ -Parent } | Sort-Object -Unique)) {
            $path = Join-Path $game $folder
            if ((Test-Path $path) -and -not (Get-ChildItem -LiteralPath $path -Force)) { Remove-Item -LiteralPath $path -Force }
        }
        Copy-Item (Join-Path $lock 'thug2-decomp.ini.backup') (Join-Path $game 'thug2-decomp.ini') -Force
        Remove-Item $lock -Recurse -Force
        "RELEASED ($($DeployedFiles -join ', ') and ini as they were)"
        exit 0
    }
}
