param(
    [Parameter(Mandatory=$true)][string]$Tag,
    [Parameter(Mandatory=$true)][string]$Directory,
    [Parameter(Mandatory=$true)][string]$VerifierPath,
    [string]$Repository=$env:GH_REPO,
    [string]$AcceptedOfflineSHA256=$env:ACCEPTED_OFFLINE_SHA256,
    [switch]$PublishApproved
)
# Default is offline validation. Publishing is an explicit final action, using the
# job-scoped GITHUB_TOKEN; never embeds a personal credential in an asset.
$ErrorActionPreference='Stop'
$Directory=(Resolve-Path -LiteralPath $Directory).Path
$VerifierPath=(Resolve-Path -LiteralPath $VerifierPath).Path
if ($Tag -notmatch '^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') { throw 'Invalid stable tag' }
$payload=@('NetPilotLite-Web-Setup.exe','NetPilotLite-Offline-Setup.exe','NetPilotLite-Offline-Setup.exe.update','NetPilotLite-Portable-x64.zip','NetPilotLite.exe','NetPilotLite.exe.update','version.json','release-catalog.json','release-catalog.json.update')
$expected=@($payload)+@('SHA256SUMS.txt')
$hashes=@{}
foreach ($line in Get-Content -LiteralPath (Join-Path $Directory 'SHA256SUMS.txt')) {
    if ($line -cnotmatch '^([0-9a-f]{64})  ([A-Za-z0-9._-]+)$') { throw 'Malformed checksum entry' }
    $hash=$Matches[1];$name=$Matches[2]
    if ($payload -cnotcontains $name -or $hashes.ContainsKey($name)) { throw 'Unexpected/duplicate checksum entry' }
    $item=Get-Item -LiteralPath (Join-Path $Directory $name)
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse-point asset refused' }
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $item.FullName).Hash -ine $hash) { throw "Checksum mismatch: $name" }
    $hashes[$name]=$hash
}
if ($hashes.Count -ne $payload.Count) { throw 'Incomplete checksum manifest' }
$version=Get-Content -Raw -LiteralPath (Join-Path $Directory 'version.json') | ConvertFrom-Json
if (('v'+$version.version) -cne $Tag) { throw 'Tag/package version mismatch' }
foreach ($pair in @(@('--verify-release','NetPilotLite.exe'),@('--verify-installer','NetPilotLite-Offline-Setup.exe'),@('--verify-catalog','release-catalog.json'))) {
    & $VerifierPath $pair[0] (Join-Path $Directory $pair[1])
    if ($LASTEXITCODE) { throw "Pinned identity verification failed: $($pair[1])" }
}
$catalog=Get-Content -Raw -LiteralPath (Join-Path $Directory 'release-catalog.json') | ConvertFrom-Json
if ($catalog.version -cne $version.version -or $catalog.platform -cne 'windows-x64') { throw 'Signed catalog version/platform mismatch' }
$catalogNames=@{}
foreach ($entry in $catalog.assets) {
    if ($payload[0..6] -cnotcontains $entry.name -or $catalogNames.ContainsKey($entry.name)) { throw 'Unexpected/duplicate signed catalog entry' }
    $catalogNames[$entry.name]=$true
    $file=Get-Item -LiteralPath (Join-Path $Directory $entry.name)
    if ($entry.sha256 -ine $hashes[$entry.name] -or $entry.size -ne $file.Length) { throw "Signed catalog mismatch: $($entry.name)" }
}
if ($catalogNames.Count -ne 7) { throw 'Incomplete signed catalog' }
if (!$PublishApproved) { 'Validated signed release bundle offline. Nothing uploaded or published.'; return }
if ($Repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') { throw 'Invalid owner/repository' }
if ($AcceptedOfflineSHA256 -notmatch '^[0-9a-fA-F]{64}$' -or $AcceptedOfflineSHA256 -ine $hashes['NetPilotLite-Offline-Setup.exe']) { throw 'This exact installer has no recorded isolated installation acceptance' }
if (!(Get-Command gh -ErrorAction SilentlyContinue)) { throw 'GitHub CLI is required for publication' }
if (!$env:GH_TOKEN) { throw 'Missing job-scoped GH_TOKEN; never put a PAT in this project' }
function Get-Release {
    $raw=(& gh api --hostname github.com --method GET --include "repos/$Repository/releases/tags/$Tag" 2>&1 | Out-String)
    $exit=$LASTEXITCODE
    if ($raw -match '(?m)^HTTP/\S+ 404\b') { return $null }
    if ($exit) {
        $status='no HTTP response'; if ($raw -match '(?m)^HTTP/\S+ (\d+)') { $status=$Matches[1] }
        throw "Read release failed: $status (401 auth; 403 policy/rate; 429 throttled; otherwise transport). No publication performed by this read."
    }
    $parts=$raw -split '\r?\n\r?\n',2
    if ($parts.Count -ne 2) { throw 'Unexpected GitHub response framing' }
    return ($parts[1] | ConvertFrom-Json)
}
& gh api --hostname github.com "repos/$Repository/git/ref/tags/$Tag" --silent
if ($LASTEXITCODE) { throw 'Release tag missing or unreadable; no tag is created implicitly' }
$release=Get-Release
if (!$release) {
    & gh release create $Tag --repo $Repository --verify-tag --draft --generate-notes --title $Tag
    if ($LASTEXITCODE) { throw 'Draft creation failed; inspect Actions error for HTTP status' }
    $release=Get-Release
}
if (!$release -or $release.prerelease -or $release.tag_name -cne $Tag) { throw 'Unexpected release identity/channel' }
foreach ($asset in $release.assets) { if ($expected -cnotcontains $asset.name) { throw 'Unexpected remote asset; refusing to modify this release' } }
foreach ($name in $expected) {
    $existing=@($release.assets | Where-Object { $_.name -ceq $name })
    if ($existing.Count -gt 1) { throw 'Duplicate remote asset' }
    if (!$existing.Count) {
        if (!$release.draft) { throw "Published release is incomplete; refusing mutable replacement: $name" }
        & gh release upload $Tag (Join-Path $Directory $name) --repo $Repository
        if ($LASTEXITCODE) { throw "Upload failed: $name; draft retained for a safe retry" }
    }
}
# Download ALL uploaded bytes again. Do not trust only GitHub's same-origin digest.
$readback=Join-Path ([IO.Path]::GetTempPath()) ('NetPilot-release-check-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $readback | Out-Null
$release=Get-Release
if (@($release.assets).Count -ne $expected.Count) { throw 'Uploaded asset count mismatch' }
foreach ($name in $expected) {
    & gh release download $Tag --repo $Repository --pattern $name --dir $readback
    if ($LASTEXITCODE) { throw "Asset cannot be read back: $name; release remains draft" }
    $a=Get-Item -LiteralPath (Join-Path $Directory $name)
    $b=Get-Item -LiteralPath (Join-Path $readback $name)
    if ($a.Length -ne $b.Length -or (Get-FileHash $a.FullName).Hash -ne (Get-FileHash $b.FullName).Hash) { throw "Remote asset mismatch: $name; refusing publication" }
}
if ($release.draft) {
    & gh release edit $Tag --repo $Repository --draft=false --prerelease=false --latest
    if ($LASTEXITCODE) { throw 'Final publication failed; inspect retained draft' }
}
$latestRaw=(& gh api --hostname github.com "repos/$Repository/releases/latest" 2>&1 | Out-String)
if ($LASTEXITCODE) { throw 'Published release latest API verification failed' }
$latest=$latestRaw | ConvertFrom-Json
if ($latest.tag_name -cne $Tag -or $latest.draft -or $latest.prerelease -or @($latest.assets).Count -ne $expected.Count) { throw 'Latest release does not match the published bundle' }
"Published and authenticated readback verified: https://github.com/$Repository/releases/tag/$Tag"
'Public anonymous client access must be checked separately; a private repository is not an anonymous update channel.'
