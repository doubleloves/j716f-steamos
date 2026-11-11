# J716F line-flash installer (Windows PowerShell). ASCII-only on purpose:
# Windows PowerShell 5.1 misparses UTF-8-no-BOM scripts with non-ASCII strings.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File install.ps1
#   (or double-click install-windows.bat)
#
# Requires:
#   * Lenovo TB-J716F, bootloader unlocked
#   * Windows 10/11 x64 + USB cable (fastboot driver installed)
#   * package layout:
#       install.ps1
#       tools\platform-tools\fastboot.exe (or fastboot in PATH)
#       images\boot.img
#       images\rootfs.simg   (Android sparse image, flashed to userdata)
#       SHA256SUMS (optional but recommended)
#
# WARNING: this erases all userdata on the tablet (Android/data). Type YES to confirm.
[CmdletBinding()]
param(
    [string]$Fastboot = "",
    [switch]$Yes
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$log = Join-Path $root "install-log.txt"

function Log($msg) {
    $line = "[{0}] {1}" -f (Get-Date -Format o), $msg
    Write-Host $line
    Add-Content -Path $log -Value $line
}

function Find-Fastboot {
    if ($Fastboot -and (Test-Path $Fastboot)) { return $Fastboot }
    $local = Join-Path $root "tools\platform-tools\fastboot.exe"
    if (Test-Path $local) { return $local }
    $cmd = Get-Command fastboot.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    throw "fastboot.exe not found: put it under tools\platform-tools\ or add it to PATH"
}

function Run([string[]]$argsArr) {
    Log ("fastboot " + ($argsArr -join " "))
    & $fb @argsArr 2>&1 | ForEach-Object { Log ("  " + $_) }
    if ($LASTEXITCODE -ne 0) { throw ("fastboot failed: " + ($argsArr -join " ")) }
}

$fb = Find-Fastboot
Log "fastboot: $fb"
Log "workdir: $root"

$boot = Join-Path $root "images\boot.img"
$rootfs = Join-Path $root "images\rootfs.simg"
if (-not (Test-Path $boot)) { throw "missing images\boot.img" }
if (-not (Test-Path $rootfs)) { throw "missing images\rootfs.simg" }

$sums = Join-Path $root "SHA256SUMS"
if (Test-Path $sums) {
    Log "verifying SHA256SUMS ..."
    Get-Content $sums | ForEach-Object {
        if ($_ -match '^([0-9a-fA-F]{64})\s+(.+)$') {
            $want = $Matches[1]
            $rel = $Matches[2].Trim()
            $file = Join-Path $root $rel
            if (Test-Path $file) {
                $got = (Get-FileHash -Algorithm SHA256 $file).Hash.ToLower()
                if ($got -ne $want.ToLower()) { throw "checksum mismatch: $rel" }
                Log "  ok: $rel"
            } else {
                Log "  skip (not present): $rel"
            }
        }
    }
}

Log "waiting for fastboot device (hold Vol+ and Power, or use adb reboot bootloader) ..."
Run @("devices")

$product = (& $fb getvar product 2>&1 | Out-String)
Log $product
if ($product -notmatch 'j716f|p11_pro_plus') {
    throw "device product does not match TB-J716F, aborting"
}

$unlocked = (& $fb getvar unlocked 2>&1 | Out-String)
Log $unlocked
if ($unlocked -notmatch 'yes') {
    throw "bootloader is not unlocked (getvar unlocked != yes), aborting"
}

$psize = (& $fb getvar partition-size:userdata 2>&1 | Out-String)
Log $psize
if ($psize -match '0x([0-9a-fA-F]+)') {
    $bytes = [Convert]::ToInt64($Matches[1], 16)
    $need = (Get-Item $rootfs).Length
    Log "userdata partition: $bytes bytes; image: $need bytes"
    if ($bytes -lt $need) { throw "userdata partition is smaller than image, aborting" }
}

if (-not $Yes) {
    Write-Host ""
    Write-Host "This will ERASE ALL USERDATA on the tablet and install J716F Arch." -ForegroundColor Yellow
    $ans = Read-Host "Type YES to continue"
    if ($ans -ne "YES") { throw "cancelled by user" }
}

Log "flash boot_a ..."; Run @("flash", "boot_a", $boot)
Log "flash boot_b ..."; Run @("flash", "boot_b", $boot)
Log "flash userdata (10-30 min, do not unplug) ..."; Run @("flash", "userdata", $rootfs)
Log "reset slot ..."; Run @("set_active", "a")
Log "reboot ..."; Run @("reboot")

Log "done. first boot takes 1-2 minutes; then configure WiFi and log in to your own Steam account."
Log "log file: $log"
