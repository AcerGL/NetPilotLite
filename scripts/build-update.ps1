param([Parameter(Mandatory=$true)][string]$SigningKey,[string]$Tag='v1.5.0',[string]$Python='python')
$ErrorActionPreference='Stop'
& "$PSScriptRoot/build-public.ps1" -SigningKey $SigningKey -Tag $Tag -Python $Python
