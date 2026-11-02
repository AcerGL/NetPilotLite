param([Parameter(Mandatory=$true)][string]$Tag,[Parameter(Mandatory=$true)][string]$Directory,
 [Parameter(Mandatory=$true)][string]$VerifierPath,[string]$Repository=$env:GH_REPO,[switch]$PublishApproved)
$ErrorActionPreference='Stop'
if($Tag -cnotmatch '^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$'){throw 'Stable tag required'}
$Directory=(Resolve-Path -LiteralPath $Directory).Path
$VerifierPath=(Resolve-Path -LiteralPath $VerifierPath).Path
$names=@('NetPilotLite-Update-x64.zip','NetPilotLite-Update-x64.json','NetPilotLite-Update-x64.json.update','NetPilotLite-Update-x64.zip.sha256')
$seen=@{}
foreach($line in Get-Content -LiteralPath (Join-Path $Directory 'SHA256SUMS.txt')) {
 if($line -cnotmatch '^([a-f0-9]{64})  ([A-Za-z0-9._-]+)$'){throw 'Invalid checksum row'}
 $hash=$Matches[1];$name=$Matches[2]
 if($names -cnotcontains $name -or $seen.ContainsKey($name)){throw 'Unexpected/duplicate asset'}
 $file=Get-Item -LiteralPath (Join-Path $Directory $name)
 if($file.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Reparse file rejected'}
 if((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ine $hash){throw "Hash mismatch: $name"}
 $seen[$name]=$hash
}
if($seen.Count -ne 4){throw 'Incomplete manifest'}
$manifest=Get-Content -Raw -LiteralPath (Join-Path $Directory 'NetPilotLite-Update-x64.json')|ConvertFrom-Json
if($manifest.release_tag -cne $Tag -or ('v'+$manifest.version) -cne $Tag){throw 'Tag mismatch'}
& $VerifierPath --package-test $Directory
if($LASTEXITCODE){throw 'Pinned signature/package verification failed'}
if(!$PublishApproved){'Signed update validated offline; nothing published.';return}
if($Repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$' -or !$env:GH_TOKEN){throw 'Missing job-scoped repository authorization'}
$private=(& gh api "repos/$Repository" --jq .private)
if($LASTEXITCODE -or $private -cne 'true'){throw 'Private repository confirmation failed'}
$existing=(& gh release list --repo $Repository --limit 100 --json tagName|ConvertFrom-Json)
if($LASTEXITCODE){throw 'Release query failed'}
if($existing.tagName -contains $Tag){throw 'Release already exists; do not overwrite published assets'}
$notes=Join-Path $Directory 'release-notes.tmp.md'
$manifest.release_notes | Set-Content -LiteralPath $notes -Encoding utf8
& gh release create $Tag --repo $Repository --verify-tag --draft --title "NetPilot Lite $($manifest.version)" --notes-file $notes
if($LASTEXITCODE){throw 'Draft creation failed'}
foreach($name in @($names)+@('SHA256SUMS.txt')) {
 & gh release upload $Tag (Join-Path $Directory $name) --repo $Repository
 if($LASTEXITCODE){throw 'Upload failed; draft left unpublished for diagnosis'}
}
$readback=Join-Path $Directory ('readback-'+[guid]::NewGuid())
New-Item -ItemType Directory -Path $readback|Out-Null
& gh release download $Tag --repo $Repository --dir $readback
if($LASTEXITCODE){throw 'Readback failed; draft not published'}
foreach($name in @($names)+@('SHA256SUMS.txt')) {
 if((Get-FileHash -LiteralPath (Join-Path $Directory $name)).Hash -ne (Get-FileHash -LiteralPath (Join-Path $readback $name)).Hash){throw 'Readback mismatch; draft not published'}
}
& $VerifierPath --package-test $readback
if($LASTEXITCODE){throw 'Readback signature verification failed'}
& gh release edit $Tag --repo $Repository --draft=false --prerelease=false --latest
if($LASTEXITCODE){throw 'Final publication failed; inspect draft status'}
'Private stable update published after signature and readback checks.'
