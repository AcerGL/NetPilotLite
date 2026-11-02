param([Parameter(Mandatory=$true)][string]$SigningKey,[string]$InnoCompiler='',
 [string]$ExpectedTag='v1.4.0',[string]$Python='python',[switch]$FirstInstall)
$ErrorActionPreference='Stop'
& "$PSScriptRoot/build-update.ps1" -SigningKey $SigningKey -Tag $ExpectedTag -Python $Python
if(!$FirstInstall){return}
if(!$InnoCompiler){throw 'First installation requires trusted Inno Setup compiler'}
$auth=Get-AuthenticodeSignature -LiteralPath $InnoCompiler
if($auth.Status -ne 'Valid' -or $auth.SignerCertificate.Subject -notlike 'CN=Pyrsys B.V.,*'){throw 'Untrusted compiler'}
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
 New-Item -ItemType Directory -Path dist -Force|Out-Null
 Copy-Item -LiteralPath bin/Release/NetPilotLite.exe,bin/Release/NetPilotLite.exe.update -Destination dist
 & ./installer/build.cmd
 if($LASTEXITCODE){throw 'Installer guard build failed'}
 & $InnoCompiler ('/DProductVersion='+$ExpectedTag.Substring(1)) installer/offline/NetPilotLite.iss
 if($LASTEXITCODE){throw 'First installer compilation failed'}
} finally {Pop-Location}
