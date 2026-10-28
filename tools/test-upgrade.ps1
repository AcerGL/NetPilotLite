param([string]$FixtureDirectory = '')
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
if (!$FixtureDirectory) { $FixtureDirectory = Join-Path $project 'artifacts\update-fixtures' }
$fixtures = (Resolve-Path -LiteralPath $FixtureDirectory).Path
$run = Join-Path $project ('artifacts\update-run-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
foreach ($variant in @('old','prior','new','fail','stagea')) {
    $source = Join-Path $fixtures $variant
    if ($variant -eq 'stagea' -and !(Test-Path -LiteralPath $source)) { continue }
    $destination = Join-Path $run $variant
    New-Item -ItemType Directory -Path $destination | Out-Null
    foreach ($name in @('NetPilotLite.exe','NetPilotLite.exe.update')) {
        Copy-Item -LiteralPath (Join-Path $source $name) -Destination (Join-Path $destination $name)
    }
}
# Dedicated, never-before-used fixture root. No cleanup or access to user installations.
& (Join-Path $project 'bin\Release\UpdateTests.exe') $run
exit $LASTEXITCODE
