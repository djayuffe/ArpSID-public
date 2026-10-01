<#
.SYNOPSIS
  ArpSID standalone app installer for Windows.

.DESCRIPTION
  Copies ArpSID.exe (next to this script, or -From) into a program folder,
  removes the "downloaded from the internet" mark and adds a Start menu
  shortcut.

  Run from an extracted release zip:
    powershell -ExecutionPolicy Bypass -File .\install.ps1
  For you only (default): %LOCALAPPDATA%\Programs\ArpSID, no admin rights.
  -Scope System installs to %ProgramFiles%\ArpSID for all users and needs an
  elevated PowerShell.

.PARAMETER Scope
  User (default) or System.
.PARAMETER From
  Path to ArpSID.exe.
.PARAMETER Dest
  Install into this folder instead of the Scope folder.
.PARAMETER NoShortcut
  Do not add a Start menu shortcut.
.PARAMETER Uninstall
  Remove ArpSID and its shortcut (settings in %APPDATA%\ArpSID are kept).
.PARAMETER Check
  Only check the program (it starts and reports its version).
#>
param(
  [ValidateSet('User', 'System')] [string] $Scope = 'User',
  [string] $From = '',
  [string] $Dest = '',
  [switch] $NoShortcut,
  [switch] $Uninstall,
  [switch] $Check
)
$ErrorActionPreference = 'Stop'
$exeName = 'ArpSID.exe'

if (-not $From) {
  foreach ($c in @((Join-Path $PSScriptRoot $exeName), (Join-Path (Get-Location) $exeName))) {
    if (Test-Path $c) { $From = $c; break }
  }
}
if (-not $Dest) {
  if ($Scope -eq 'System') { $Dest = Join-Path $env:ProgramFiles 'ArpSID' }
  else { $Dest = Join-Path $env:LOCALAPPDATA 'Programs\ArpSID' }
}
$target = Join-Path $Dest $exeName
$menu = if ($Scope -eq 'System') { [Environment]::GetFolderPath('CommonPrograms') } else { [Environment]::GetFolderPath('Programs') }
$shortcut = Join-Path $menu 'ArpSID.lnk'

if ($Uninstall) {
  if (Test-Path $target) { Remove-Item -Force $target }
  if ((Test-Path $Dest) -and -not (Get-ChildItem $Dest -Force)) { Remove-Item -Force $Dest }
  if (Test-Path $shortcut) { Remove-Item -Force $shortcut }
  Write-Host "Removed ArpSID from $Dest (settings in $env:APPDATA\ArpSID are kept)."
  exit 0
}

if (-not $From -or -not (Test-Path $From)) { throw "ArpSID.exe not found (use -From PATH)" }

if ($Check) {
  $p = Start-Process -FilePath $From -ArgumentList '--version' -NoNewWindow -Wait -PassThru
  if ($p.ExitCode -ne 0) { throw "ArpSID.exe --version failed ($($p.ExitCode))" }
  Write-Host 'ArpSID.exe runs on this machine.'
  exit 0
}

New-Item -ItemType Directory -Force -Path $Dest | Out-Null
Copy-Item -Force $From $target
Unblock-File -Path $target -ErrorAction SilentlyContinue

if (-not $NoShortcut) {
  New-Item -ItemType Directory -Force -Path $menu | Out-Null
  $shell = New-Object -ComObject WScript.Shell
  $lnk = $shell.CreateShortcut($shortcut)
  $lnk.TargetPath = $target
  $lnk.WorkingDirectory = $Dest
  $lnk.Description = 'ArpSID SID synthesizer'
  $lnk.Save()
}
Write-Host "Installed ArpSID to $target"
if (-not $NoShortcut) { Write-Host "Start it from the Start menu (ArpSID)." }
