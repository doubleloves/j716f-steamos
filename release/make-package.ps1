# Assemble a Windows line-flash release package (run on PC). ASCII-only.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File make-package.ps1 `
#       -InputDir D:\j716f-build -Version v1.0.0 -OutDir D:\j716f-release `
#       -PlatformTools D:\1s\platform-tools
#
# Input:  <InputDir>\boot.img, <InputDir>\rootfs.simg
# Output: <OutDir>\j716f-arch-steam-<Version>\
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$InputDir,
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$OutDir,
    [string]$PlatformTools = "",
    [string]$RepoRoot = ""
)

$ErrorActionPreference = "Stop"
if (-not $RepoRoot) { $RepoRoot = Split-Path -Parent $PSScriptRoot }
if (-not (Test-Path $RepoRoot)) { throw "RepoRoot not found: $RepoRoot" }

$boot = Join-Path $InputDir "boot.img"
$rootfs = Join-Path $InputDir "rootfs.simg"
foreach ($f in @($boot, $rootfs)) {
    if (-not (Test-Path $f)) { throw "missing input file: $f" }
}

$head = [IO.File]::ReadAllBytes($boot)
if ([System.Text.Encoding]::ASCII.GetString($head, 0, 8) -ne "ANDROID!") {
    throw "boot.img is not an Android boot image"
}
$head8 = [IO.File]::ReadAllBytes($rootfs)
$magic = [BitConverter]::ToUInt32($head8, 0)
if ($magic -ne 0xED26FF3A) { throw ("rootfs.simg is not an Android sparse image (magic=0x{0:X8})" -f $magic) }

$name = "j716f-arch-steam-$Version"
$dest = Join-Path $OutDir $name
$imgDir = Join-Path $dest "images"
$ptDir = Join-Path $dest "tools\platform-tools"
New-Item -ItemType Directory -Force -Path $imgDir, $ptDir | Out-Null

Write-Host "copying images ..."
Copy-Item $boot (Join-Path $imgDir "boot.img")
Copy-Item $rootfs (Join-Path $imgDir "rootfs.simg")

Write-Host "copying scripts/docs ..."
Copy-Item (Join-Path $RepoRoot "release\lineflash\install.ps1") (Join-Path $dest "install.ps1")
Copy-Item (Join-Path $RepoRoot "release\NOTICE-SOURCE.md") (Join-Path $dest "NOTICE-SOURCE.md")
$readme = Get-Content -Raw (Join-Path $RepoRoot "release\README-TEMPLATE.md")
$readme = $readme.Replace("<ver>", $Version)
Set-Content -Path (Join-Path $dest "README.md") -Value $readme -Encoding UTF8

if ($PlatformTools -and (Test-Path $PlatformTools)) {
    Write-Host "copying platform-tools ..."
    foreach ($f in @("fastboot.exe", "AdbWinApi.dll", "AdbWinUsbApi.dll")) {
        $src = Join-Path $PlatformTools $f
        if (Test-Path $src) { Copy-Item $src (Join-Path $ptDir $f) }
    }
} else {
    Write-Warning "no -PlatformTools given: package has no fastboot.exe"
}

Write-Host "writing SHA256SUMS / manifest.txt ..."
$files = Get-ChildItem $imgDir -File
$sums = foreach ($f in $files) {
    $h = (Get-FileHash -Algorithm SHA256 $f.FullName).Hash.ToLower()
    "$h  images/$($f.Name)"
}
Set-Content -Path (Join-Path $dest "SHA256SUMS") -Value ($sums -join "`n") -Encoding ASCII

$manifest = @(
    "release=$Version",
    "date=$([DateTime]::Now.ToString('s'))",
    "device=TB-J716F",
    "boot_img=$((Get-Item $boot).Length)",
    "rootfs_simg=$((Get-Item $rootfs).Length)"
)
Set-Content -Path (Join-Path $dest "manifest.txt") -Value ($manifest -join "`n") -Encoding ASCII

if ((Get-Item $rootfs).Length -gt 2GB) {
    Write-Warning "rootfs.simg > 2 GB: GitHub Release asset limit is 2 GB per file"
}
Write-Host ""
Write-Host "package: $dest"
Get-ChildItem $dest -Recurse -File | Select-Object FullName, Length | Format-Table -AutoSize
