# rvm installer for Windows PowerShell 5.1+
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\install.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\install.ps1 -Version 0.1.1
#
# Installs bin\rvm.exe, adds the directory to the user PATH, copies the
# uninstaller next to it, and registers an entry in Windows "Apps & features".
# Per-user only: no administrator rights, no machine-wide changes.

[CmdletBinding()]
param(
    [string]$Version = "",
    [string]$InstallDir = (Join-Path $env:LOCALAPPDATA "Programs\rvm"),
    [switch]$NoPath,
    [switch]$NoUninstallEntry,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$Repo = "RingAire/rvm"
$DownloadBase = if ($env:RVM_DOWNLOAD_BASE) { $env:RVM_DOWNLOAD_BASE.TrimEnd("/") } else { "https://github.com/$Repo/releases/download" }
$UninstallKey = if ($env:RVM_UNINSTALL_KEY) { $env:RVM_UNINSTALL_KEY } else { "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\rvm" }

function Get-Architecture {
    switch ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture) {
        "X64" { return "x64" }
        "Arm64" { return "arm64" }
        "X86" { return "x86" }
        default { throw "unsupported Windows architecture: $([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture)" }
    }
}

function Get-LatestVersion {
    $release = Invoke-RestMethod -UseBasicParsing -Uri "https://api.github.com/repos/$Repo/releases/latest" `
        -Headers @{ "User-Agent" = "rvm-installer" }
    if (-not $release.tag_name) { throw "cannot read the latest rvm release" }
    return $release.tag_name.TrimStart("v")
}

function Get-ExpectedHash {
    param([string]$SumsPath, [string]$Asset)
    foreach ($line in Get-Content -LiteralPath $SumsPath) {
        $parts = $line -split "\s+"
        if ($parts.Count -lt 2) { continue }
        $name = $parts[1]
        if ($name.StartsWith("./")) { $name = $name.Substring(2) }
        if ($name -eq $Asset) { return $parts[0].ToLowerInvariant() }
    }
    throw "SHA256SUMS has no entry for $Asset"
}

function Add-UserPath {
    param([string]$Directory)
    $current = [Environment]::GetEnvironmentVariable("Path", "User")
    $entries = @()
    if ($current) { $entries = $current -split ";" | Where-Object { $_ -ne "" } }
    foreach ($entry in $entries) {
        if ($entry.TrimEnd("\") -ieq $Directory.TrimEnd("\")) { return $false }
    }
    $updated = (@($entries) + $Directory) -join ";"
    [Environment]::SetEnvironmentVariable("Path", $updated, "User")
    return $true
}

function Remove-UserPath {
    param([string]$Directory)
    $current = [Environment]::GetEnvironmentVariable("Path", "User")
    if (-not $current) { return }
    $kept = @($current -split ";" | Where-Object { $_ -ne "" -and $_.TrimEnd("\") -ine $Directory.TrimEnd("\") })
    [Environment]::SetEnvironmentVariable("Path", ($kept -join ";"), "User")
}

$arch = Get-Architecture
if (-not $Version) { $Version = Get-LatestVersion }
$asset = "rvm-$Version-windows-$arch.zip"
$baseUrl = "$DownloadBase/v$Version"
$work = Join-Path ([System.IO.Path]::GetTempPath()) ("rvm-install-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $work | Out-Null
try {
    $archive = Join-Path $work $asset
    $sums = Join-Path $work "SHA256SUMS"
    Write-Host "Downloading rvm $Version ($arch)..."
    Invoke-WebRequest -UseBasicParsing -Uri "$baseUrl/$asset" -OutFile $archive
    Invoke-WebRequest -UseBasicParsing -Uri "$baseUrl/SHA256SUMS" -OutFile $sums

    $expected = Get-ExpectedHash -SumsPath $sums -Asset $asset
    $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($expected -ne $actual) { throw "checksum mismatch for $asset" }

    Expand-Archive -LiteralPath $archive -DestinationPath $work -Force
    $payload = Join-Path $work "rvm-$Version-windows-$arch"
    $binary = Join-Path $payload "bin\rvm.exe"
    if (-not (Test-Path -LiteralPath $binary)) { throw "unexpected archive layout: bin\rvm.exe missing" }

    if ((Test-Path -LiteralPath $InstallDir) -and -not $Force) {
        $existing = Join-Path $InstallDir "rvm.exe"
        if (Test-Path -LiteralPath $existing) {
            Write-Host "Updating existing installation in $InstallDir"
        }
    }
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
    Copy-Item -LiteralPath $binary -Destination (Join-Path $InstallDir "rvm.exe") -Force
    foreach ($name in @("LICENSE", "NOTICE", "README.md", "README.en.md")) {
        $candidate = Join-Path $payload $name
        if (Test-Path -LiteralPath $candidate) { Copy-Item -LiteralPath $candidate -Destination $InstallDir -Force }
    }
    $uninstaller = Join-Path $payload "uninstall.ps1"
    if (-not (Test-Path -LiteralPath $uninstaller)) { throw "archive is missing uninstall.ps1" }
    Copy-Item -LiteralPath $uninstaller -Destination (Join-Path $InstallDir "uninstall.ps1") -Force

    $manifest = @{ name = "rvm"; version = $Version; arch = $arch; installed_at = (Get-Date).ToUniversalTime().ToString("o") }
    $manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $InstallDir "install.json") -Encoding UTF8

    if (-not $NoUninstallEntry) {
        New-Item -Path $UninstallKey -Force | Out-Null
        Set-ItemProperty -Path $UninstallKey -Name "DisplayName" -Value "rvm (RingEcho version manager)"
        Set-ItemProperty -Path $UninstallKey -Name "DisplayVersion" -Value $Version
        Set-ItemProperty -Path $UninstallKey -Name "Publisher" -Value "RingAire"
        Set-ItemProperty -Path $UninstallKey -Name "InstallLocation" -Value $InstallDir
        Set-ItemProperty -Path $UninstallKey -Name "DisplayIcon" -Value (Join-Path $InstallDir "rvm.exe")
        Set-ItemProperty -Path $UninstallKey -Name "UninstallString" -Value "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $InstallDir 'uninstall.ps1')`""
        Set-ItemProperty -Path $UninstallKey -Name "NoModify" -Value 1 -Type DWord
        Set-ItemProperty -Path $UninstallKey -Name "NoRepair" -Value 1 -Type DWord
    }

    $pathChanged = $false
    if (-not $NoPath) { $pathChanged = Add-UserPath -Directory $InstallDir }

    Write-Host ""
    Write-Host "rvm $Version installed to $InstallDir"
    Write-Host "  rvm --version"
    if ($pathChanged) {
        Write-Host "  PATH updated for new terminals (current shell: `$env:Path = `"$InstallDir;`$env:Path`")"
    }
    if (-not $NoUninstallEntry) {
        Write-Host "  Uninstall from Settings > Apps, or run: $(Join-Path $InstallDir 'uninstall.ps1')"
    }
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
