# Dot-sourced by the game scripts: which exe a game folder has. The DLL runs on either - the same build, the same
# addresses: THUG2PM.exe (PartyMod's patched exe) or a stock, already unwrapped THUG2.exe (a no-CD build; the exe is
# never changed). The retail SafeDisc-encrypted THUG2.exe can't run anything (tools/game/install_manifest.ts tells them
# apart). A stock install keeps the game in a Game subfolder: Resolve-GameFolder goes in there.
$GameExeNames = @('THUG2PM.exe', 'THUG2.exe')
$GameProcessNames = @('THUG2PM', 'THUG2')

function Resolve-GameFolder([string]$folder) {
    $folder = $folder.TrimEnd('\')
    if (-not (Find-GameExe $folder) -and (Find-GameExe (Join-Path $folder 'Game'))) { return (Join-Path $folder 'Game') }
    $folder
}

function Find-GameExe([string]$folder) {
    foreach ($name in $GameExeNames) {
        $path = Join-Path $folder $name
        if (Test-Path $path) { return $path }
    }
    $null
}

# The running games (any folder).
function Get-GameProcesses { Get-Process -Name $GameProcessNames -ErrorAction SilentlyContinue }
