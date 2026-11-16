<#
  Keeps a disk busy: a big file written over and over (flushed to the disk each time) in a folder, until the stop file
  appears or -Seconds pass. The stall test of the disk work (tools/game/scenarios/boston_disk_soak.txt): the game's
  main loop must not wait on the disk while this runs next to it.
  powershell -File tools\game\disk_hammer.ps1 -Folder <folder on the game's drive> [-Megabytes 256] [-Seconds 900]
  Stop it early by creating <Folder>\hammer.stop. Its own files are removed when it ends.
#>
param(
    [Parameter(Mandatory = $true)][string]$Folder,
    [int]$Megabytes = 256,
    [int]$Seconds = 900
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $Folder | Out-Null
$stop = Join-Path $Folder 'hammer.stop'
Remove-Item -Force -ErrorAction SilentlyContinue $stop
$chunk = New-Object byte[] (4MB)
(New-Object System.Random 1).NextBytes($chunk)
$end = (Get-Date).AddSeconds($Seconds)
$rounds = 0
try {
    while ((Get-Date) -lt $end -and -not (Test-Path $stop)) {
        $path = Join-Path $Folder ("hammer_{0}.bin" -f ($rounds % 2))
        $stream = New-Object System.IO.FileStream($path, 'Create', 'Write', 'None', 4096, 'WriteThrough')
        try {
            for ($written = 0; $written -lt $Megabytes; $written += 4) {
                $stream.Write($chunk, 0, $chunk.Length)
            }
            $stream.Flush($true)
        } finally {
            $stream.Dispose()
        }
        $rounds++
    }
} finally {
    Get-ChildItem $Folder -Filter 'hammer_*.bin' | Remove-Item -Force -ErrorAction SilentlyContinue
    Remove-Item -Force -ErrorAction SilentlyContinue $stop
    "hammer: $rounds rounds of $Megabytes MB" | Write-Output
}
