<#
.SYNOPSIS
  ArpSID VST3 installer for Windows.

.DESCRIPTION
  Copies arpsid_vst3.vst3 (next to this script, or -From) into the VST3
  folder, removes the "downloaded from the internet" mark, and checks that
  the bundle has a module for this machine (x64 or arm64).

  Run from an extracted release zip:
    powershell -ExecutionPolicy Bypass -File .\install.ps1
  All users (C:\Program Files\Common Files\VST3, the folder every host scans)
  is the default and needs an elevated PowerShell; -Scope User installs to
  %LOCALAPPDATA%\Programs\Common\VST3 (VST3 3.7 per-user folder) instead.

.PARAMETER Scope
  System (default) or User.
.PARAMETER From
  Path to arpsid_vst3.vst3.
.PARAMETER Dest
  Install into this folder instead of the Scope folder.
.PARAMETER PresetsDest
  Install the factory presets into this folder (default: Documents\VST3 Presets
  for -Scope User, %ProgramData%\VST3 Presets for System), under
  Uber Sound Solutions\ArpSID.
.PARAMETER NoPresets
  Do not install the factory presets.
.PARAMETER Uninstall
  Remove ArpSID (and its factory presets) from the Scope (or Dest) folder.
.PARAMETER Check
  Only check the bundle.
#>
param(
  [ValidateSet('System', 'User')] [string] $Scope = 'System',
  [string] $From = '',
  [string] $Dest = '',
  [string] $PresetsDest = '',
  [switch] $NoPresets,
  [switch] $Uninstall,
  [switch] $Check
)
$ErrorActionPreference = 'Stop'
$bundleName = 'arpsid_vst3.vst3'

if (-not $Dest) {
  if ($Scope -eq 'System') { $Dest = Join-Path $env:CommonProgramFiles 'VST3' }
  else { $Dest = Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3' }
}
$target = Join-Path $Dest $bundleName
# VST3 preset folders are <root>\<vendor>\<plug-in name>.
$presetSub = 'Uber Sound Solutions\ArpSID'
if (-not $PresetsDest) {
  if ($Scope -eq 'System') { $PresetsDest = Join-Path $env:ProgramData 'VST3 Presets' }
  else { $PresetsDest = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'VST3 Presets' }
}
$presetTarget = Join-Path $PresetsDest $presetSub

function Test-Admin {
  $id = [Security.Principal.WindowsIdentity]::GetCurrent()
  (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
if ($Scope -eq 'System' -and -not $Check -and -not (Test-Admin) -and $Dest -like "$env:ProgramFiles*") {
  Write-Host "Installing for all users needs an elevated PowerShell (Run as administrator)."
  Write-Host "Or install for yourself only:  .\install.ps1 -Scope User"
  exit 1
}

if ($Uninstall) {
  if (Test-Path $target) { Remove-Item -Recurse -Force $target; Write-Host "Removed $target" }
  else { Write-Host "ArpSID is not installed in $Dest" }
  if (-not $NoPresets -and (Test-Path $presetTarget)) {
    Remove-Item -Recurse -Force $presetTarget
    $vendorDir = Split-Path $presetTarget -Parent
    if ((Test-Path $vendorDir) -and -not (Get-ChildItem $vendorDir)) { Remove-Item $vendorDir }
    Write-Host "Removed the factory presets from $presetTarget"
  }
  exit 0
}

if (-not $From) {
  foreach ($c in @((Join-Path $PSScriptRoot $bundleName), (Join-Path (Get-Location) $bundleName))) {
    if (Test-Path $c) { $From = $c; break }
  }
}
if (-not $From -or -not (Test-Path $From)) { Write-Error "No $bundleName found (use -From PATH)." }
$From = (Resolve-Path $From).Path

# The module for this machine: Contents\x86_64-win or Contents\arm64-win (arm64x also loads on x64).
$arch = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'x86_64' }
$modules = Get-ChildItem -Path (Join-Path $From 'Contents') -Directory -ErrorAction SilentlyContinue | ForEach-Object Name
$mine = $modules | Where-Object { $_ -like "$arch*-win" -or $_ -eq 'arm64x-win' }
if (-not $mine) {
  Write-Warning "This bundle has no module for $arch (it contains: $($modules -join ', ')). Download the matching zip."
  if ($Check) { exit 1 }
} else {
  Write-Host "Bundle module for this machine: Contents\$($mine | Select-Object -First 1)"
}
if ($Check) { exit 0 }

New-Item -ItemType Directory -Force -Path $Dest | Out-Null
if (Test-Path $target) { Remove-Item -Recurse -Force $target }
Copy-Item -Recurse -Force $From $target
# Files from a downloaded zip carry Zone.Identifier; some hosts refuse them.
Get-ChildItem -Recurse -File $target | Unblock-File
Write-Host "Installed ArpSID VST3 to $target"

# Factory presets (.vstpreset), shipped next to the bundle in the release zip.
$presetSrc = $null
foreach ($c in @((Join-Path $PSScriptRoot "VST3 Presets\$presetSub"), (Join-Path (Split-Path $From -Parent) "VST3 Presets\$presetSub"))) {
  if (Test-Path $c) { $presetSrc = $c; break }
}
if (-not $NoPresets -and $presetSrc) {
  if (Test-Path $presetTarget) { Remove-Item -Recurse -Force $presetTarget }
  New-Item -ItemType Directory -Force -Path $presetTarget | Out-Null
  Copy-Item -Recurse -Force (Join-Path $presetSrc '*') $presetTarget
  Get-ChildItem -Recurse -File $presetTarget | Unblock-File
  $n = (Get-ChildItem -Recurse -File -Filter '*.vstpreset' $presetTarget).Count
  Write-Host "Installed $n factory presets to $presetTarget"
} elseif (-not $NoPresets) {
  Write-Host "(no 'VST3 Presets' folder next to the bundle: factory presets not installed)"
}
Write-Host "Rescan plug-ins in your host (the plug-in is listed as 'ArpSID')."
