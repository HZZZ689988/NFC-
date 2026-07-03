$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

python -m pip install -r requirements.txt
python -m pip install pyinstaller

python -m PyInstaller `
  --clean `
  --noconsole `
  --onefile `
  --name NFCAttendanceTool `
  run.py

Write-Host "Built: $root\dist\NFCAttendanceTool.exe"
Write-Host "Runtime data directory: $root\dist\data"
