param(
    [Parameter(Mandatory = $true)][string]$Game,
    [ValidateSet('x86', 'x64')][string]$Arch = 'x86',
    [string]$BuildDir = '',
    [string]$EditorTools = '',
    [switch]$NoEditorTools
)
# Puts a build of the game - thug2-decomp.exe, its PDB and the DLLs the build copied beside it - into a game folder, as the
# game lock allows: the folder's lock must be held (game_lock.ps1 -Action acquire), and no game running from it.
#   deploy_build.ps1 -Game C:\Games\THUG2-2 -Arch x64 [-BuildDir out\build\x64-debug]
# A 32-bit and a 64-bit build have the same file names (thug2-decomp.exe, d3dx9_43.dll, SDL3.dll, FFmpeg's avutil-59.dll...
# - game_exe.ps1's $DeployedFiles, the files the lock backs up and puts back): the build you deploy goes IN PLACE of the
# one in the folder, and release gives the folder back as it was acquired. The folder is never both at once.
# -Arch: which build (default x86); the files are checked to be that machine (a 64-bit exe next to a 32-bit SDL3.dll
#        never starts). -BuildDir: the CMake build folder (default <this checkout>\out\build\<Arch>-debug, the preset's).
# The FFmpeg DLLs (the movies' and streams' codec, both machines) come from the build folder (the build copies them from
# out\ffmpeg\<arch> once tools/ffmpeg/build_ffmpeg.ts --arch <arch> has made them). The level editor plugin goes into
# the folder's plugins\ (made when missing), its launcher (thug2-editor.exe) beside the exe. Files of the other machine left in the folder (an x64 build's
# vcruntime140_1.dll after an x86 one) are listed.
# The level editor's tools folder (editor-tools\: Node, NeverScript's ns, the content pipeline and its level converters
# - what File > Import Level runs) goes in beside the exe, whole: -EditorTools <folder> one made already; else
# out\editor-tools, made (or found up to date) by `node tools/editor_tools.ts` first. Copied only when its bundle.json
# differs from the one in the game folder. -NoEditorTools: left as it is.
. (Join-Path $PSScriptRoot 'game_exe.ps1')
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$folder = Resolve-GameFolder $Game
if (-not (Find-GameExe $folder)) { "NO GAME IN $folder (no $($GameExeNames -join ' or ') in it or in its Game folder)"; exit 2 }
if (-not (Test-Path (Join-Path $folder '.game-lock'))) { "THE LOCK IS NOT HELD for $folder - game_lock.ps1 -Action acquire -Owner <you> -Game $folder first"; exit 2 }
$running = Get-GameProcesses | Where-Object { $_.Path -and (Split-Path $_.Path -Parent) -eq $folder }
if ($running) { "A GAME IS RUNNING FROM $folder - not deployed"; exit 3 }
if (-not $BuildDir) { $BuildDir = Join-Path $root "out\build\$Arch-debug" }
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
$exe = Join-Path $BuildDir $LaunchExeName
if (-not (Test-Path $exe)) { "NO $LaunchExeName IN $BuildDir (build it: cmake --build, the $Arch preset)"; exit 2 }
$have = Get-PeArch $exe
if ($have -ne $Arch) { "$exe is $(if ($have) { $have } else { 'not a PE file' }), not $Arch"; exit 2 }

$copied = @()
foreach ($f in $DeployedFiles) {
    $src = Join-Path $BuildDir $f
    if (-not (Test-Path $src)) { continue }
    if ($f -like '*.exe' -or $f -like '*.dll') {
        $m = Get-PeArch $src
        if ($m -ne $Arch) { "$src is $(if ($m) { $m } else { 'not a PE file' }), not $Arch - not deployed"; exit 2 }
    }
    # (plugins\...: its folder made first)
    $into = Split-Path (Join-Path $folder $f) -Parent
    if (-not (Test-Path $into)) { New-Item -ItemType Directory -Path $into | Out-Null }
    Copy-Item -LiteralPath $src -Destination (Join-Path $folder $f) -Force
    $copied += $f
}
if ($copied -notcontains 'd3dx9_43.dll' -or $copied -notcontains 'SDL3.dll') {
    "WARNING: $BuildDir has no d3dx9_43.dll / SDL3.dll (the build copies them beside the exe): the game won't start without them"
}
$stale = @($DeployedFiles | Where-Object { ($copied -notcontains $_) -and (($_ -like '*.exe') -or ($_ -like '*.dll')) -and
        (Test-Path (Join-Path $folder $_)) -and ((Get-PeArch (Join-Path $folder $_)) -notin @($null, $Arch)) })
if ($stale.Count) { "left in $folder, built for the other machine: $($stale -join ', ')" }
$ffmpeg = @('avutil-59.dll', 'avcodec-61.dll', 'avformat-61.dll', 'swscale-8.dll') | Where-Object { $copied -notcontains $_ }
if ($ffmpeg) { "WARNING: FFmpeg's DLLs missing from the build ($($ffmpeg -join ', ')): the movies and music need them (node tools/ffmpeg/build_ffmpeg.ts --arch $Arch, then build again)" }
if (-not $NoEditorTools) {
    if (-not $EditorTools) {
        $EditorTools = Join-Path $root 'out\editor-tools'
        & node (Join-Path $root 'tools\editor_tools.ts') --out $EditorTools
        if ($LASTEXITCODE -ne 0) { "the editor's tools couldn't be made (node tools/editor_tools.ts) - not deployed"; exit 2 }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $EditorTools 'bundle.json'))) { "NO bundle.json IN $EditorTools (node tools/editor_tools.ts makes the folder)"; exit 2 }
    $target = Join-Path $folder 'editor-tools'
    $same = (Test-Path -LiteralPath (Join-Path $target 'bundle.json')) -and
        ((Get-Content -LiteralPath (Join-Path $target 'bundle.json') -Raw) -eq (Get-Content -LiteralPath (Join-Path $EditorTools 'bundle.json') -Raw))
    if ($same) { "editor-tools\ up to date" }
    else {
        if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
        Copy-Item -LiteralPath $EditorTools -Destination $target -Recurse
        $copied += 'editor-tools\'
    }
}
"DEPLOYED the $Arch build from $BuildDir into ${folder}: $($copied -join ', ')"
exit 0
