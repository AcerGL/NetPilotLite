param([Parameter(Mandatory=$true)][string]$SigningKey,[string]$ExpectedTag='v1.5.0',[string]$Python='python')
$ErrorActionPreference='Stop'
& "$PSScriptRoot/build-public.ps1" -SigningKey $SigningKey -Tag $ExpectedTag -Python $Python
