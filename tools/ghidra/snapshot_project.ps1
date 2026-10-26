# Copy the Ghidra project to a snapshot folder for read-only scripting while the MCP server holds the lock.
# The snapshot reflects the project as last saved to disk.
param([string]$Dest = "C:\Games\THUG2-ghidra-snapshot")
if (Test-Path $Dest) { Remove-Item $Dest -Recurse -Force -Confirm:$false }
Copy-Item "C:\Games\THUG2-ghidra" $Dest -Recurse
Get-ChildItem $Dest -Filter *.lock* | Remove-Item -Force -Confirm:$false
Write-Output "Snapshot at $Dest"
