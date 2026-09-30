<#
.SYNOPSIS
  Downloads the ArpSID VST3 for this PC, verifies it and installs it.

.DESCRIPTION
  irm https://raw.githubusercontent.com/djayuffe/ArpSID-public/main/scripts/install/get_arpsid.ps1 | iex
  or, with options, save the script and run:
    powershell -ExecutionPolicy Bypass -File .\get_arpsid.ps1 -Version 0.9.8 -Scope User

  Picks the x64, arm64 or (32-bit Windows) x86 zip, checks it against the
  release's SHA256SUMS.txt, then runs the installer inside the zip
  (install.ps1). -Arch x86 installs the 32-bit plug-in for 32-bit hosts on
  64-bit Windows (into C:\Program Files (x86)\Common Files\VST3).

.PARAMETER Version
  Release to install (default: latest).
.PARAMETER Scope
  System (default; C:\Program Files\Common Files\VST3, needs an elevated
  PowerShell) or User (%LOCALAPPDATA%\Programs\Common\VST3).
.PARAMETER Arch
  x64, arm64 or x86 (default: this machine's; also $env:ARPSID_ARCH).
#>
param(
  [string] $Version = '',
  [ValidateSet('System', 'User')] [string] $Scope = 'System',
  [ValidateSet('', 'x64', 'arm64', 'x86')] [string] $Arch = '',
  [string] $Repo = 'djayuffe/ArpSID-public'
)
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

if (-not $Version) {
  $Version = (Invoke-RestMethod "https://api.github.com/repos/$Repo/releases/latest").tag_name.TrimStart('v')
}
$Version = $Version.TrimStart('v')
# The OS architecture (a 32-bit PowerShell on 64-bit Windows reports x86 in
# PROCESSOR_ARCHITECTURE and the real one in PROCESSOR_ARCHITEW6432).
$arch = $Arch
if (-not $arch -and $env:ARPSID_ARCH) { $arch = $env:ARPSID_ARCH }
if (-not $arch) {
  $os = if ($env:PROCESSOR_ARCHITEW6432) { $env:PROCESSOR_ARCHITEW6432 } else { $env:PROCESSOR_ARCHITECTURE }
  $arch = switch ($os) { 'ARM64' { 'arm64' } 'x86' { 'x86' } default { 'x64' } }
}
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
