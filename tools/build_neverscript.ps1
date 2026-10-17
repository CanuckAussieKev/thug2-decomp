# Builds NeverScript's `ns` (QB compiler / decompiler, tools/neverscript - see tools/neverscript/THUG2.md) into
# out/neverscript/ns.exe. Uses `go` from PATH if there is one, otherwise downloads a portable Go 1.22.12 into
# out/neverscript/go (checked against its SHA-256). Its one dependency (github.com/pmezard/go-difflib, MIT) is
# fetched by Go's module proxy and checked against go.sum.
#   powershell -ExecutionPolicy Bypass -File tools\build_neverscript.ps1
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$out = Join-Path $repo 'out\neverscript'
New-Item -ItemType Directory -Force $out | Out-Null

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
Push-Location (Join-Path $repo 'tools\neverscript')
try {
    & $go build -o (Join-Path $out 'ns.exe') ./cmd/ns
    if ($LASTEXITCODE -ne 0) { throw "go build failed ($LASTEXITCODE)" }
} finally {
    Pop-Location
}
Write-Host "Built $(Join-Path $out 'ns.exe')"
