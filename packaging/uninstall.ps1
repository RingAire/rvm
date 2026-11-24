# rvm uninstaller for Windows PowerShell 5.1+
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File uninstall.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File uninstall.ps1 -Yes
#
# Removes the user PATH entry, the "Apps & features" registration and the
# installation directory. Per-user only; no administrator rights.

[CmdletBinding()]
param([switch]$Yes)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$InstallDir = Split-Path -Parent $PSCommandPath
$UninstallKey = if ($env:RVM_UNINSTALL_KEY) { $env:RVM_UNINSTALL_KEY } else { "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\rvm" }

if (-not $Yes) {
    $answer = Read-Host "Remove rvm from '$InstallDir'? [y/N]"
    if ($answer -notmatch "^(y|yes)$") {
        Write-Host "Cancelled."
        exit 0
    }
}

$current = [Environment]::GetEnvironmentVariable("Path", "User")
if ($current) {
    $kept = @($current -split ";" | Where-Object {
        $_ -ne "" -and $_.TrimEnd("\") -ine $InstallDir.TrimEnd("\")
    })
    [Environment]::SetEnvironmentVariable("Path", ($kept -join ";"), "User")
}

if (Test-Path -LiteralPath $UninstallKey) {
    Remove-Item -LiteralPath $UninstallKey -Recurse -Force
}

$manifest = Join-Path $InstallDir "install.json"
if (Test-Path -LiteralPath $manifest) { Remove-Item -LiteralPath $manifest -Force }

# Do not hold the installation directory as the working directory: Windows
# refuses to delete it while a process is inside it.
Set-Location $env:TEMP
Remove-Item -LiteralPath $InstallDir -Recurse -Force -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath $InstallDir) {
    # A running rvm.exe (or an open file) can block deletion; retry after exit.
    Start-Process -FilePath $env:ComSpec `
        -ArgumentList "/c", "timeout /t 2 /nobreak >nul & rmdir /s /q `"$InstallDir`"" `
        -WindowStyle Hidden
    Write-Host "Scheduled removal of $InstallDir (a file was still in use)."
} else {
    Write-Host "rvm removed from $InstallDir"
}
