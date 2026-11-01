# Builds NeverScript's `ns` (QB compiler / decompiler - see docs/neverscript.md) into out/neverscript/ns.exe.
# NeverScript is not kept in this repo: the script clones our fork CanuckAussieKev/NeverScript (violetvandal's THUG2
# fork + our -lenient) at the pinned commit of its `thug2` branch into out/neverscript/src (skipped when it is there
# already) and builds.
# Uses `go` from PATH if there is one, otherwise downloads a portable Go 1.22.12 into out/neverscript/go (checked
# against its SHA-256). NeverScript's one dependency (github.com/pmezard/go-difflib, MIT) is fetched by Go's module
# proxy and checked against go.sum. Needs git.
#   powershell -ExecutionPolicy Bypass -File tools\build_neverscript.ps1 [-Out <folder>]
param([string]$Out = '')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if ($Out -eq '') { $out = Join-Path $repo 'out\neverscript' } else { $out = $Out }
New-Item -ItemType Directory -Force $out | Out-Null

$upstream = 'https://github.com/CanuckAussieKev/NeverScript.git'
$pin = 'a7f32ac0a9f0fa8263e37990cb6c1bd7dc420ef6'
$src = Join-Path $out 'src'

# Run git, stop on a failure (Windows PowerShell 5.1 turns native stderr into errors, so go through cmd-style checks).
function Invoke-Git {
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & git @args 2>&1 | ForEach-Object { "$_" } | Write-Host } finally { $ErrorActionPreference = $saved }
    if ($LASTEXITCODE -ne 0) { throw "git $($args -join ' ') failed ($LASTEXITCODE)" }
}

$ready = $false
if (Test-Path (Join-Path $src '.git')) {
    $head = (& git -C $src rev-parse HEAD 2>$null)
    $dirty = (& git -C $src status --porcelain 2>$null)
    if ($LASTEXITCODE -eq 0 -and $head -eq $pin -and -not $dirty) { $ready = $true }
}
if (-not $ready) {
    if (Test-Path (Join-Path $src '.git')) {
        # present but at another commit, changed, or a clone of the old upstream: put it back to the pin
        Invoke-Git -C $src remote set-url origin $upstream
        Invoke-Git -C $src fetch -q origin
        Invoke-Git -C $src reset --hard -q
        Invoke-Git -C $src clean -fdq
    } else {
        if (Test-Path $src) { Remove-Item -Recurse -Force $src }
        Invoke-Git -c core.autocrlf=false clone -q $upstream $src
    }
    Invoke-Git -C $src -c advice.detachedHead=false checkout -q $pin
}

$go = (Get-Command go -ErrorAction SilentlyContinue).Source
if (-not $go) {
    $go = Join-Path $out 'go\bin\go.exe'
    if (-not (Test-Path $go)) {
        $zip = Join-Path $out 'go1.22.12.windows-amd64.zip'
        Invoke-WebRequest 'https://go.dev/dl/go1.22.12.windows-amd64.zip' -OutFile $zip
        $hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
        if ($hash -ne '2ceda04074eac51f4b0b85a9fcca38bcd49daee24bed9ea1f29958a8e22673a6') {
            throw "Go download's SHA-256 doesn't match: $hash"
        }
        Expand-Archive $zip -DestinationPath $out -Force
        Remove-Item $zip
    }
}

$env:GOPATH = Join-Path $out 'gopath'
$env:GOCACHE = Join-Path $out 'gocache'
$env:GOFLAGS = '-mod=mod'
Push-Location $src
try {
    & $go build -o (Join-Path $out 'ns.exe') ./cmd/ns
    if ($LASTEXITCODE -ne 0) { throw "go build failed ($LASTEXITCODE)" }
} finally {
    Pop-Location
}
Write-Host "Built $(Join-Path $out 'ns.exe')"
