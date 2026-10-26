# Dot-sourced by the game scripts: which exe a game folder has - THUG2.exe, or another name listed in the decomp's
# local.json (exeNames; tools/pipeline/lib/decomp.ts). The exe is never changed. The development DLL does not run on the
# retail THUG2.exe (tools/pipeline/game/install_manifest.ts tells them apart). A stock install keeps the game in a Game
# subfolder; some players delete what's around it (launcher, docs) and keep Game's contents at the top:
# Resolve-GameFolder takes either.
$GameExeNames = @('THUG2.exe')
$localJson = Join-Path $PSScriptRoot '..\..\..\local.json'
# (a linked git worktree has no local.json of its own - it's gitignored: its .git file's gitdir has a commondir naming
# the main checkout's .git, where it is)
$dotGit = Join-Path $PSScriptRoot '..\..\..\.git'
if (-not (Test-Path -LiteralPath $localJson) -and (Test-Path -LiteralPath $dotGit -PathType Leaf)) {
    $gitdir = ((Get-Content -LiteralPath $dotGit -Raw) -replace '^gitdir:\s*', '').Trim()
    if (-not [IO.Path]::IsPathRooted($gitdir)) { $gitdir = Join-Path (Split-Path $dotGit -Parent) $gitdir }
    $commondir = Join-Path $gitdir 'commondir'
    if (Test-Path -LiteralPath $commondir) {
        $common = (Get-Content -LiteralPath $commondir -Raw).Trim()
        if (-not [IO.Path]::IsPathRooted($common)) { $common = Join-Path $gitdir $common }
        $localJson = Join-Path (Split-Path ([IO.Path]::GetFullPath($common)) -Parent) 'local.json'
    }
}
if (Test-Path -LiteralPath $localJson) {
    $local = Get-Content -LiteralPath $localJson -Raw | ConvertFrom-Json
    if ($local.exeNames) { $GameExeNames += @($local.exeNames) }
}
$GameProcessNames = @($GameExeNames | ForEach-Object { [IO.Path]::GetFileNameWithoutExtension($_) })

# Each game's marker files / folders (the PowerShell twin of tools/pipeline/lib/install.ts's INSTALLS - keep them the same): any
# one entry identifies the game's folder; an entry is one or more paths that must all be in it (case doesn't matter on
# Windows).
$InstallMarkers = @{
    thug2 = @($GameExeNames | ForEach-Object { , @($_) }) + @(, @('Data\pre\qb_scripts.prx'))
    thps3 = @(, @('Data\Levels'))
    thps4 = @(, @('data\levels', 'data\skeletons'))
    thug1 = @(, @('Data\pre\qb.pre'))
    thaw  = @(, @('data\worlds\worldzones')) + @(, @('data\pak\qb.pak.wpc'))
    thps2 = @(, @('ALL.PKR'))
    thp8  = @(, @('DATA\COMPRESSED\compress.toc.xen'))
}
$InstallTitles = @{ thug2 = 'THUG2'; thps3 = 'THPS3'; thps4 = 'THPS4'; thug1 = 'THUG1'; thaw = 'THAW'; thps2 = 'THPS2'; thp8 = 'THP8' }

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

# "<folder> doesn't look like a THUG2 install: expected the reference build's exe or THUG2.exe or Data\pre\qb_scripts.prx in it or in
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

# A workspace copy (C:\Games\THUG2-1, -2, ... or its Game folder) - never the main install C:\Games\THUG2 or a backup.
function Test-Workspace([string]$folder) { $folder.TrimEnd('\', '/') -match '^C:\\Games\\THUG2-\d+(\\Game)?$' }

# An ini's text with <key>=<value> in [<section>]: the key's line there replaced, else added under the section's
# header, else the section added at the end (the TypeScript twin: tools/pipeline/game/workspace_ini.ts setIniValue).
function Set-IniValue([string]$text, [string]$section, [string]$key, [string]$value) {
    $nl = if ($text -match "`r`n") { "`r`n" } else { "`n" }
    $lines = [Collections.Generic.List[string]]::new()
    if ($text.Length) { $lines.AddRange([string[]]($text -split '\r?\n')) }
    $current = ''; $header = -1; $done = $false
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i].Trim()
        if ($line -match '^\[(.+)\]$') { $current = $Matches[1].Trim().ToLowerInvariant(); if ($current -eq $section.ToLowerInvariant()) { $header = $i }; continue }
        if ($current -eq $section.ToLowerInvariant() -and $line -match "^$([regex]::Escape($key))\s*=") {
            $lines[$i] = "$key=$value"; $done = $true; break
        }
    }
    if (-not $done) {
        if ($header -ge 0) { $lines.Insert($header + 1, "$key=$value") }
        else {
            if ($lines.Count -and $lines[$lines.Count - 1] -eq '') { $lines.RemoveAt($lines.Count - 1) }
            $lines.Add("[$section]"); $lines.Add("$key=$value"); $lines.Add('')
        }
    }
    $lines -join $nl
}

# Workspace games always run muted (the user is at the machine): [features] mute_audio=1 in a workspace's
# thug2-decomp.ini (src/game/sfx/mute_audio.h). Does nothing anywhere else - the main install's ini is never touched.
function Set-WorkspaceMuted([string]$folder) {
    if (-not (Test-Workspace $folder)) { return }
    $ini = Join-Path $folder 'thug2-decomp.ini'
    $text = if (Test-Path $ini) { [IO.File]::ReadAllText($ini) } else { '' }
    $new = Set-IniValue $text 'features' 'mute_audio' '1'
    if ($new -ne $text) { [IO.File]::WriteAllText($ini, $new) }
}
