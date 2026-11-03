param([Parameter(Mandatory=$true)][string]$Tag)
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
if ($Tag -notmatch '^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') { throw 'Expected stable vMAJOR.MINOR.PATCH tag' }
$version=$Tag.Substring(1)
foreach ($part in $version.Split('.')) { if ([long]$part -gt 65535) { throw 'Windows version component overflow' } }
$header=Get-Content -Raw -LiteralPath (Join-Path $project 'src/update.h')
if ($header -notmatch '#define NP_VERSION L"([0-9.]+)"' -or $Matches[1] -cne $version) { throw 'Source/tag version mismatch' }
$numbers=$version.Replace('.', ',')+',0'
if ($header.Replace(' ','') -notmatch ('#defineNP_VERSION_NUM'+[regex]::Escape($numbers)+'\b')) { throw 'Numeric source version mismatch' }
foreach ($file in @('src/app.rc','installer/web/web.rc')) {
    $rc=Get-Content -Raw -LiteralPath (Join-Path $project $file)
    foreach ($field in @('FILEVERSION','PRODUCTVERSION')) {
        if ($rc -notmatch ($field+'\s+'+[regex]::Escape($numbers)+'\b')) { throw "$file $field mismatch" }
    }
    foreach ($field in @('FileVersion','ProductVersion')) {
        if ($rc -notmatch ('"'+$field+'",\s*"'+[regex]::Escape($version)+'(\.0)?\\0')) { throw "$file string version mismatch" }
    }
}
"Validated source and Windows resource version: $Tag"
