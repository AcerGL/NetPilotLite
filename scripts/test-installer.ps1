# Run INSIDE a disposable Windows VM only. This script creates a real installation/uninstall record.
param([switch]$IsolatedWindowsConfirmed, [string]$OfflineSetup)
$ErrorActionPreference='Stop'
if (!$IsolatedWindowsConfirmed) { throw 'Refusing host installation. Supply -IsolatedWindowsConfirmed only inside a disposable Windows VM.' }
if (!$OfflineSetup) { $OfflineSetup=Join-Path (Split-Path -Parent $PSScriptRoot) 'dist\NetPilotLite-Offline-Setup.exe' }
$OfflineSetup=(Resolve-Path -LiteralPath $OfflineSetup).Path
if (!( [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run in an elevated disposable VM session.' }
$run=Join-Path $env:TEMP ('NetPilot-install-acceptance-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
$target=Join-Path $env:ProgramFiles 'NetPilot Lite 测试 Space'
$before=Get-NetAdapter -IncludeHidden | Select-Object InterfaceGuid,AdminStatus,Status
$results=@()
foreach($language in @('zhcn','en')) {
    $log=Join-Path $run ('install-'+$language+'.log')
    # Explicit interactive repair/upgrade/UAC confirmation is part of acceptance, not auto-clicked.
    $args=@('/LANG='+$language,('/DIR="'+$target+'"'),('/LOG="'+$log+'"'),'/NORESTART')
    $p=Start-Process -FilePath $OfflineSetup -ArgumentList $args -PassThru -Wait -WindowStyle Normal
    $results+=@{phase='install-or-repair';language=$language;exit=$p.ExitCode;log=$log}
    if ($p.ExitCode) { throw "Installation failed; preserve $run" }
    if (!(Test-Path -LiteralPath (Join-Path $target 'NetPilotLite.exe'))) { throw 'Missing target EXE' }
}
$after=Get-NetAdapter -IncludeHidden | Select-Object InterfaceGuid,AdminStatus,Status
$results | ConvertTo-Json | Set-Content (Join-Path $run 'results.json')
$before | ConvertTo-Json | Set-Content (Join-Path $run 'network-before.json')
$after | ConvertTo-Json | Set-Content (Join-Path $run 'network-after.json')
Write-Output "Evidence: $run. Complete the manual upgrade/rollback/uninstall matrix in docs/INSTALLER_ACCEPTANCE.md. This script alone is not full acceptance."
