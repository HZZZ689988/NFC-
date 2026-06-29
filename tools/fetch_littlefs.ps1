$ErrorActionPreference = "Stop"

$base = "https://raw.githubusercontent.com/littlefs-project/littlefs/master"
$dest = Join-Path $PSScriptRoot "..\firmware\third_party\littlefs"
New-Item -ItemType Directory -Force $dest | Out-Null

foreach ($file in @("lfs.c", "lfs.h", "lfs_util.c", "lfs_util.h", "LICENSE.md")) {
    Invoke-WebRequest -Uri "$base/$file" -OutFile (Join-Path $dest $file) -UseBasicParsing
}

Write-Host "LittleFS source updated in $dest"
