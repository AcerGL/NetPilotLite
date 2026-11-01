param([string]$Tag='v1.3.1')
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
$bundle=Join-Path $project 'dist'
$verifier=Join-Path $project 'bin/Release/NetPilotLite.Tests.exe'
$publisher=Join-Path $project 'scripts/publish-release.ps1'
$sha=(Get-FileHash (Join-Path $bundle 'NetPilotLite-Offline-Setup.exe')).Hash
$script:checks=0
function Assert($condition,[string]$label) { if (!$condition) { throw "FAIL: $label" }; ++$script:checks; "PASS: $label" }
function Expect-Failure([scriptblock]$test,[string]$pattern) {
    $reason='';try { & $test } catch { $reason=$_.Exception.Message }
    Assert ($reason -match $pattern) "$pattern (observed: $reason)"
}
& (Join-Path $project 'scripts/validate-version.ps1') -Tag $Tag
Expect-Failure { & (Join-Path $project 'scripts/validate-version.ps1') -Tag 'v99.99.99' } 'Source/tag'
Expect-Failure { & (Join-Path $project 'scripts/validate-version.ps1') -Tag 'v1.3.1-beta' } 'stable'
& $publisher -Tag $Tag -Directory $bundle -VerifierPath $verifier
Assert ($LASTEXITCODE -eq 0) 'offline bundle integrity and independent signature'
Expect-Failure { & $publisher -Tag 'v99.99.99' -Directory $bundle -VerifierPath $verifier } 'Tag/package'
Expect-Failure { & $publisher -Tag $Tag -Directory $bundle -VerifierPath $verifier -Repository 'AcerGL/NetPilotLite' -PublishApproved -AcceptedOfflineSHA256 ('0'*64) } 'acceptance'
# Mock the GH COMMAND, never the signature verifier. No network calls and no real Release writes.
$previousFunction=Get-Item Function:gh -ErrorAction SilentlyContinue
if ($previousFunction) { throw 'Refusing to shadow an existing gh function' }
$savedToken=$env:GH_TOKEN
if (Get-Variable NetPilotReleaseTestState -Scope Global -ErrorAction SilentlyContinue) { throw 'Mock state already exists' }
$global:NetPilotReleaseTestState=[ordered]@{release=$null; uploads=0; edits=0; failUpload=$false; corrupt=$false; status=200}
function gh {
    $global:LASTEXITCODE=0
    if ($args[0] -eq 'api') {
        $endpoint=@($args | Where-Object { $_ -like 'repos/*' })[0]
        if ($endpoint -like '*/git/ref/tags/*') { return }
        if ($global:NetPilotReleaseTestState.status -ne 200) { "HTTP/2 $($global:NetPilotReleaseTestState.status)`n`n{}"; $global:LASTEXITCODE=1; return }
        if (!$global:NetPilotReleaseTestState.release) { "HTTP/2 404`n`n{}"; $global:LASTEXITCODE=1; return }
        if ($args -contains '--include') { "HTTP/2 200`n" }
        $global:NetPilotReleaseTestState.release | ConvertTo-Json -Depth 6 -Compress
        return
    }
    if ($args[0] -ne 'release') { throw 'Unexpected mock command' }
    switch ($args[1]) {
        create { $global:NetPilotReleaseTestState.release=@{tag_name=$Tag;draft=$true;prerelease=$false;assets=@()} }
        upload {
            if ($global:NetPilotReleaseTestState.failUpload) { $global:LASTEXITCODE=1; return }
            $f=Get-Item -LiteralPath $args[3]
            $global:NetPilotReleaseTestState.release.assets+=@{name=$f.Name;size=$f.Length}
            ++$global:NetPilotReleaseTestState.uploads
        }
        download {
            $name=$args[[Array]::IndexOf($args,'--pattern')+1]
            $dir=$args[[Array]::IndexOf($args,'--dir')+1]
            $destination=Join-Path $dir $name
            Copy-Item -LiteralPath (Join-Path $bundle $name) -Destination $destination
            if ($global:NetPilotReleaseTestState.corrupt) { [IO.File]::AppendAllText($destination,'tampered') }
        }
        edit { $global:NetPilotReleaseTestState.release.draft=$false; ++$global:NetPilotReleaseTestState.edits }
        default { throw 'Unexpected release mock action' }
    }
}
try {
    'MOCK GITHUB TRANSPORT: all publication messages below describe simulated API calls only.'
    $env:GH_TOKEN='MOCK_NOT_A_CREDENTIAL'
    $invoke={ & $publisher -Tag $Tag -Directory $bundle -VerifierPath $verifier -Repository 'AcerGL/NetPilotLite' -AcceptedOfflineSHA256 $sha -PublishApproved }
    foreach ($status in @(401,403,429)) {
        $global:NetPilotReleaseTestState.status=$status
        Expect-Failure $invoke "Read release failed: $status"
        Assert (!$global:NetPilotReleaseTestState.release) 'API errors must not create a draft'
    }
    $global:NetPilotReleaseTestState.status=200; $global:NetPilotReleaseTestState.failUpload=$true
    Expect-Failure $invoke 'Upload failed'
    Assert ($global:NetPilotReleaseTestState.release.draft -and !$global:NetPilotReleaseTestState.edits) 'failed upload remains unpublished'
    $global:NetPilotReleaseTestState.failUpload=$false; $global:NetPilotReleaseTestState.corrupt=$true
    Expect-Failure $invoke 'Remote asset mismatch'
    Assert ($global:NetPilotReleaseTestState.release.draft -and !$global:NetPilotReleaseTestState.edits) 'readback corruption blocks publication'
    $global:NetPilotReleaseTestState.corrupt=$false
    & $invoke
    Assert (!$global:NetPilotReleaseTestState.release.draft -and $global:NetPilotReleaseTestState.uploads -eq 10 -and $global:NetPilotReleaseTestState.edits -eq 1) 'retry reuses exact draft assets and publishes after readback'
    & $invoke
    Assert ($global:NetPilotReleaseTestState.uploads -eq 10 -and $global:NetPilotReleaseTestState.edits -eq 1) 'published release rerun is idempotent'
} finally {
    Remove-Item Function:gh
    Remove-Variable NetPilotReleaseTestState -Scope Global
    $env:GH_TOKEN=$savedToken
}
"Release pipeline: $script:checks assertions passed; GitHub transport mocked, no actual release or installation performed."
