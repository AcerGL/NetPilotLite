param([Parameter(Mandatory=$true)][string]$SigningKey,[string]$Tag='v1.5.0',[string]$Python='python')
$ErrorActionPreference='Stop'
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
 ./scripts/validate-version.ps1 -Tag $Tag
 & ./build.cmd
 if($LASTEXITCODE){throw 'Native build failed'}
 & $Python ./scripts/package-public.py --key $SigningKey --tag $Tag
 if($LASTEXITCODE){throw 'Public file signing failed'}
 & ./bin/Release/NetPilotLite.Tests.exe --public-package-test dist/public
 if($LASTEXITCODE){throw 'Manifest/package verification failed'}
} finally {Pop-Location}
