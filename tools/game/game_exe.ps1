# Dot-sourced by the game scripts: which exe a game folder has - THUG2.exe, or another name listed in the decomp's
# local.json (exeNames; thug2-level-converter-core/lib/decomp.ts) - which marks it as a game folder (never run or changed), and the
# exe the scripts start there: the game built from the decomp's source, thug2-decomp.exe ($LaunchExeName), copied into
# the game folder with its DLLs (the decomp's README, "Running"). A stock install keeps the game in a Game
# subfolder; some players delete what's around it (launcher, docs) and keep Game's contents at the top:
# Resolve-GameFolder takes either.
$GameExeNames = @('THUG2.exe')
$localJson = Join-Path $PSScriptRoot '..\..\local.json'
# (a linked git worktree has no local.json of its own - it's gitignored: its .git file's gitdir has a commondir naming
# the main checkout's .git, where it is)
$dotGit = Join-Path $PSScriptRoot '..\..\.git'
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
$LaunchExeName = 'thug2-decomp.exe'
# (ours, and the game's own exe started by hand)
$GameProcessNames = @(@($LaunchExeName) + $GameExeNames | ForEach-Object { [IO.Path]::GetFileNameWithoutExtension($_) })
# The files of ours a game lock backs up and puts back (those that are there; the ones that weren't are removed at
# release): the exe, its PDB, the editor's program, the DLLs next to them (D3DX9, SDL3, FFmpeg's), the controller
# database; an earlier development build's dinput8.dll. The same names for a 32-bit and a 64-bit build: a build is
# deployed INSTEAD of the one in the folder (deploy_build.ps1), under the lock, and release puts the other back.
$DeployedFiles = @($LaunchExeName, 'thug2-decomp.pdb', 'thug2-editor.exe', 'thug2-editor.pdb', 'd3dx9_43.dll',
                   'SDL3.dll', 'gamecontrollerdb.txt', 'avutil-59.dll', 'avcodec-61.dll', 'avformat-61.dll',
                   'swscale-8.dll', 'dinput8.dll')

# A PE file's machine: 'x86', 'x64', or $null when it isn't a PE file (what a 32-bit or 64-bit build of the game is).
function Get-PeArch([string]$path) {
    try {
        $fs = [IO.File]::OpenRead($path)
        try {
            $head = New-Object byte[] 64
            if ($fs.Read($head, 0, 64) -lt 64 -or $head[0] -ne 0x4D -or $head[1] -ne 0x5A) { return $null }
            $fs.Position = [BitConverter]::ToInt32($head, 60)
            $pe = New-Object byte[] 6
            if ($fs.Read($pe, 0, 6) -lt 6 -or [BitConverter]::ToInt32($pe, 0) -ne 0x4550) { return $null }
            switch ([BitConverter]::ToUInt16($pe, 4)) { 0x14C { return 'x86' } 0x8664 { return 'x64' } default { return $null } }
        } finally { $fs.Dispose() }
    } catch { return $null }
}

# Each game's marker files / folders (the PowerShell twin of thug2-level-converter-core/lib/install.ts's INSTALLS - keep them the same): any
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
    thpg  = @(, @('DATA\COMPRESSED\compress.toc.xen', 'DATA\COMPRESSED\PAK\qb.pak.xen'))
}
$InstallTitles = @{ thug2 = 'THUG2'; thps3 = 'THPS3'; thps4 = 'THPS4'; thug1 = 'THUG1'; thaw = 'THAW'; thps2 = 'THPS2'; thp8 = 'THP8'; thpg = 'THPG' }

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

# The exe to start in a game folder (thug2-decomp.exe), $null when it isn't there.
function Find-LaunchExe([string]$folder) {
    $path = Join-Path $folder $LaunchExeName
    if (Test-Path $path) { return $path }
    $null
}

# The running games (any folder).
function Get-GameProcesses { Get-Process -Name $GameProcessNames -ErrorAction SilentlyContinue }

# A workspace copy (C:\Games\THUG2-1, -2, ... or its Game folder) - never the main install C:\Games\THUG2 or a backup.
function Test-Workspace([string]$folder) { $folder.TrimEnd('\', '/') -match '^C:\\Games\\THUG2-\d+(\\Game)?$' }

# An ini's text with <key>=<value> in [<section>]: the key's line there replaced, else added under the section's
# header, else the section added at the end (the TypeScript twin: tools/game/workspace_ini.ts setIniValue).
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
# thug2-decomp.ini (src/engine/sound/mute_audio.h), and on the user's second monitor: [Graphics] WindowDisplay=2
# (src/platform/sdl_window.cpp; the primary one when there's no second). Does nothing anywhere else - the main
# install's ini is never touched.
function Set-WorkspaceMuted([string]$folder) {
    if (-not (Test-Workspace $folder)) { return }
    $ini = Join-Path $folder 'thug2-decomp.ini'
    $text = if (Test-Path $ini) { [IO.File]::ReadAllText($ini) } else { '' }
    $new = Set-IniValue (Set-IniValue $text 'features' 'mute_audio' '1') 'Graphics' 'WindowDisplay' '2'
    if ($new -ne $text) { [IO.File]::WriteAllText($ini, $new) }
}
