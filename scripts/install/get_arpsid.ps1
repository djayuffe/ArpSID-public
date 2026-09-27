<#
.SYNOPSIS
  Downloads the ArpSID VST3 for this PC, verifies it and installs it.

.DESCRIPTION
  irm https://raw.githubusercontent.com/djayuffe/ArpSID-public/main/scripts/install/get_arpsid.ps1 | iex
  or, with options, save the script and run:
    powershell -ExecutionPolicy Bypass -File .\get_arpsid.ps1 -Version 0.9.8 -Scope User

  Picks the x64 or arm64 zip, checks it against the release's
  SHA256SUMS.txt, then runs the installer inside the zip (install.ps1).

.PARAMETER Version
  Release to install (default: latest).
.PARAMETER Scope
  System (default; C:\Program Files\Common Files\VST3, needs an elevated
  PowerShell) or User (%LOCALAPPDATA%\Programs\Common\VST3).
#>
param(
  [string] $Version = '',
  [ValidateSet('System', 'User')] [string] $Scope = 'System',
  [string] $Repo = 'djayuffe/ArpSID-public'
)
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

if (-not $Version) {
  $Version = (Invoke-RestMethod "https://api.github.com/repos/$Repo/releases/latest").tag_name.TrimStart('v')
}
$Version = $Version.TrimStart('v')
$arch = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'x64' }
$asset = "ArpSID-$Version-vst3-windows-$arch.zip"
$base = "https://github.com/$Repo/releases/download/v$Version"
$work = Join-Path ([IO.Path]::GetTempPath()) ("arpsid-install-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
  Write-Host "ArpSID $Version ($arch) from $Repo"
  Invoke-WebRequest -UseBasicParsing "$base/SHA256SUMS.txt" -OutFile (Join-Path $work 'SHA256SUMS.txt')
  Invoke-WebRequest -UseBasicParsing "$base/$asset" -OutFile (Join-Path $work $asset)
  $line = Get-Content (Join-Path $work 'SHA256SUMS.txt') | Where-Object { $_ -match [regex]::Escape($asset) + '$' }
  if (-not $line) { throw "$asset is not listed in SHA256SUMS.txt" }
  $expected = ($line -split '\s+')[0].ToLower()
  $actual = (Get-FileHash -Algorithm SHA256 (Join-Path $work $asset)).Hash.ToLower()
  if ($expected -ne $actual) { throw "checksum mismatch for $asset" }
  Write-Host "  verified $asset"
  $extract = Join-Path $work 'extract'
  Expand-Archive -Path (Join-Path $work $asset) -DestinationPath $extract -Force
  $installer = Join-Path $extract 'install.ps1'
  if (-not (Test-Path $installer)) {
    # Releases before 0.9.8 have no installer inside the zip.
    $installer = Join-Path $work 'install.ps1'
    Invoke-WebRequest -UseBasicParsing "https://raw.githubusercontent.com/$Repo/main/scripts/install/install_vst3.ps1" -OutFile $installer
  }
  & powershell -NoProfile -ExecutionPolicy Bypass -File $installer -Scope $Scope -From (Join-Path $extract 'arpsid_vst3.vst3')
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
  Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
