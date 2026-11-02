param([switch]$WriteDefaults)
$ErrorActionPreference='Stop'
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
 $remote=(& git config --get remote.origin.url 2>$null)
 if($LASTEXITCODE){throw 'This directory has no Git origin. Existing configured repository is retained.'}
 if($remote -match '^https://github\.com/([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+?)(?:\.git)?/?$' -or
    $remote -match '^git@github\.com:([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+?)(?:\.git)?$') {
   $owner=$Matches[1];$repo=$Matches[2]
 } else {throw 'Unsupported origin; credential-bearing/third-party URLs are not accepted'}
 # Read identity using the existing GitHub CLI account, never emit its credentials.
 $raw=& gh api "repos/$owner/$repo" --jq '{full_name,private}'
 if($LASTEXITCODE){throw 'Cannot verify repository visibility with current GitHub CLI account'}
 $identity=$raw|ConvertFrom-Json
 if($identity.full_name -ine "$owner/$repo"){throw 'Repository identity mismatch'}
 if($WriteDefaults) {
   $text="#define NP_GITHUB_OWNER L`"$owner`"`n#define NP_GITHUB_REPO L`"$repo`"`n"
   $text|Set-Content -LiteralPath src/repository.h -Encoding utf8
 }
 $identity|ConvertTo-Json -Compress
} finally {Pop-Location}
