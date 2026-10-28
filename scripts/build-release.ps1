param(
    [Parameter(Mandatory=$true)][string]$SigningKey,
    [Parameter(Mandatory=$true)][string]$InnoCompiler
)
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$SigningKey = (Resolve-Path -LiteralPath $SigningKey).Path
$InnoCompiler = (Resolve-Path -LiteralPath $InnoCompiler).Path
Push-Location $project
try {
    $auth = Get-AuthenticodeSignature -LiteralPath $InnoCompiler
    if ($auth.Status -ne 'Valid' -or $auth.SignerCertificate.Subject -notlike 'CN=Pyrsys B.V.,*') { throw 'Untrusted Inno compiler' }
    $banner = (& $InnoCompiler '--version' 2>&1 | Out-String)
    if ($banner -notmatch '(Inno Setup 7|7\.1\.)') { throw 'Inno Setup 7 compiler is required' }
    & .\publish.cmd $SigningKey
    if ($LASTEXITCODE) { throw 'Product build/sign failed' }
    & .\installer\build.cmd
    if ($LASTEXITCODE) { throw 'Installer native build failed' }
    $version=Get-Content -Raw dist/version.json | ConvertFrom-Json
    & $InnoCompiler ('/DProductVersion=' + $version.version) installer/offline/NetPilotLite.iss
    if ($LASTEXITCODE) { throw 'Offline Setup compile failed' }
    & .\bin\Release\ReleaseTool.exe sign-installer $SigningKey dist/NetPilotLite-Offline-Setup.exe
    if ($LASTEXITCODE) { throw 'Installer signing failed' }
    Copy-Item -LiteralPath bin/Release/NetPilotLite-Web-Setup.exe -Destination dist/NetPilotLite-Web-Setup.exe
    & .\bin\Release\NetPilotLite.Tests.exe --verify-release dist/NetPilotLite.exe
    if ($LASTEXITCODE) { throw 'Product signature verification failed' }
    & .\bin\Release\NetPilotLite.Tests.exe --verify-installer dist/NetPilotLite-Offline-Setup.exe
    if ($LASTEXITCODE) { throw 'Installer signature verification failed' }
    & .\bin\Release\NetPilotLite.Tests.exe --installer-tests dist/NetPilotLite-Offline-Setup.exe
    if ($LASTEXITCODE) { throw 'Installer protocol tests failed' }
    # Only fixed public assets. Never include work/, obj/, private keys or local user configuration.
    Compress-Archive -Path dist/NetPilotLite.exe,dist/NetPilotLite.exe.update,dist/version.json,LICENSE,assets/NOTICE.md -DestinationPath dist/NetPilotLite-Portable-x64.zip -Force
    $names=@('NetPilotLite-Web-Setup.exe','NetPilotLite-Offline-Setup.exe','NetPilotLite-Offline-Setup.exe.update','NetPilotLite-Portable-x64.zip','NetPilotLite.exe','NetPilotLite.exe.update','version.json')
    $entries=@();$checksums=@()
    foreach ($name in $names) {
        $file=Get-Item -LiteralPath (Join-Path dist $name)
        $hash=(Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash.ToLowerInvariant()
        $entries+=@{name=$name;size=$file.Length;sha256=$hash}
        $checksums+="$hash  $name"
    }
    @{schema=1;product='NetPilot Lite';version=$version.version;platform='windows-x64';appId='{C47B553D-22CE-4CDD-BA10-2930FC8266D4}';assets=$entries;installerAcceptance='NOT_PERFORMED_IN_THIS_BUILD'} | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 dist/release-catalog.json
    & .\bin\Release\ReleaseTool.exe sign-catalog $SigningKey dist/release-catalog.json
    if ($LASTEXITCODE) { throw 'Release catalog signing failed' }
    & .\bin\Release\NetPilotLite.Tests.exe --verify-catalog dist/release-catalog.json
    if ($LASTEXITCODE) { throw 'Catalog verification failed' }
    foreach ($name in @('release-catalog.json','release-catalog.json.update')) {
        $hash=(Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path dist $name)).Hash.ToLowerInvariant()
        $checksums+="$hash  $name"
    }
    $checksums | Set-Content -Encoding ascii dist/SHA256SUMS.txt
    'Build/sign checks complete. Real isolated Windows installation acceptance remains a separate gate.'
} finally { Pop-Location }
