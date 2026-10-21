# Dot-sourced by the game scripts: which exe a game folder has. The DLL runs on either - the same build, the same
# addresses: THUG2PM.exe (PartyMod's patched exe) or a stock, already unwrapped THUG2.exe (a no-CD build; the exe is
# never changed). The retail SafeDisc-encrypted THUG2.exe can't run anything (tools/game/install_manifest.ts tells them
# apart). A stock install keeps the game in a Game subfolder; some players delete what's around it (launcher, docs) and
# keep Game's contents at the top: Resolve-GameFolder takes either.
$GameExeNames = @('THUG2PM.exe', 'THUG2.exe')
$GameProcessNames = @('THUG2PM', 'THUG2')

# Each game's marker files / folders (the PowerShell twin of tools/lib/install.ts's INSTALLS - keep them the same): any
# one entry identifies the game's folder; an entry is one or more paths that must all be in it (case doesn't matter on
# Windows).
$InstallMarkers = @{
    thug2 = @(, @('THUG2PM.exe')) + @(, @('THUG2.exe')) + @(, @('Data\pre\qb_scripts.prx'))
    thps3 = @(, @('Data\Levels'))
    thps4 = @(, @('data\levels', 'data\skeletons'))
    thug1 = @(, @('Data\pre\qb.pre'))
    thaw  = @(, @('data\worlds\worldzones')) + @(, @('data\pak\qb.pak.wpc'))
}
$InstallTitles = @{ thug2 = 'THUG2'; thps3 = 'THPS3'; thps4 = 'THPS4'; thug1 = 'THUG1'; thaw = 'THAW' }

function Test-InstallMarkers([string]$folder, [string]$game) {
    foreach ($entry in $InstallMarkers[$game]) {
        $all = $true
        foreach ($p in $entry) { if (-not (Test-Path -LiteralPath (Join-Path $folder $p))) { $all = $false; break } }
        if ($all) { return $true }
    }
    $false
}

# The folder holding the game's files: $folder itself or its Game folder; $null when neither has the game's markers.
function Find-Install([string]$folder, [string]$game) {
    $folder = $folder.TrimEnd('\', '/')
    if (Test-InstallMarkers $folder $game) { return $folder }
    $sub = Join-Path $folder 'Game'
    if (Test-InstallMarkers $sub $game) { return $sub }
    $null
}

# "<folder> doesn't look like a THUG2 install: expected THUG2PM.exe or THUG2.exe or Data\pre\qb_scripts.prx in it or in
# its Game folder"
function Get-InstallError([string]$folder, [string]$game) {
    $what = ($InstallMarkers[$game] | ForEach-Object { $_ -join ' and ' }) -join ' or '
    "$folder doesn't look like a $($InstallTitles[$game]) install: expected $what in it or in its Game folder"
}

# THUG2's folder: Find-Install's, or $folder as given (trailing separators off) when neither layout matches - the
# callers' Find-GameExe check then says so.
function Resolve-GameFolder([string]$folder) {
    $found = Find-Install $folder 'thug2'
    if ($found) { return $found }
    $folder.TrimEnd('\', '/')
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
