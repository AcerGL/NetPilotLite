param([Parameter(Mandatory=$true)][string]$SigningKey,[Parameter(Mandatory=$true)][string]$Tag,[string]$Python='python')
$ErrorActionPreference='Stop'
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
  ./scripts/validate-version.ps1 -Tag $Tag
  & ./build.cmd
  if($LASTEXITCODE){throw 'Native build/tests failed'}
  & $Python ./scripts/package-update.py --key $SigningKey --tag $Tag
  if($LASTEXITCODE){throw 'Signed package build failed'}
  & ./bin/Release/NetPilotLite.Tests.exe --private-tests
  if($LASTEXITCODE){throw 'Private protocol tests failed'}
  & ./bin/Release/NetPilotLite.Tests.exe --package-test dist/update
  if($LASTEXITCODE){throw 'Package verification failed'}
} finally {Pop-Location}
