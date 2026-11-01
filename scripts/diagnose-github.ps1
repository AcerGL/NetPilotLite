param([string]$Repository = 'AcerGL/NetPilotLite', [string]$OutputPath = '', [switch]$Anonymous)
# Read-only. No credential value or authenticated response body is persisted.
$ErrorActionPreference = 'Stop'
if ($Repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') { throw 'Invalid owner/repository' }
$project = Split-Path -Parent $PSScriptRoot
$git = Get-Command git -ErrorAction SilentlyContinue
$gh = Get-Command gh -ErrorAction SilentlyContinue
$remote = $null
if ($git) {
    $localRoot = & $git.Source -C $project rev-parse --show-toplevel 2>$null
    if ($LASTEXITCODE -eq 0) {
        $remote = (& $git.Source -C $project remote get-url origin 2>$null | Out-String).Trim()
        if ($remote -match '^(https://github.com/|git@github.com:)([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+?)(\.git)?$') {
            $Repository = $Matches[2]
        } elseif ($remote) { $remote = 'Nonstandard remote (redacted); explicit repository used' }
    }
}
$authenticated = $false
if ($gh -and !$Anonymous) {
    # Discard auth output: CLI versions differ in whether token fragments are printed.
    & $gh.Source auth status --hostname github.com *> $null
    $authenticated = $LASTEXITCODE -eq 0
}
$results = @()
foreach ($suffix in @('', '/tags', '/releases', '/releases/latest', '/actions/workflows', '/actions/permissions', '/actions/permissions/workflow', '/actions/runners')) {
    $endpoint = "repos/$Repository$suffix"
    $status = 0; $body = $null; $errorKind = ''; $headers = ''
    if ($authenticated) {
        $raw = (& $gh.Source api --hostname github.com --method GET --include $endpoint 2>&1 | Out-String)
        if ($raw -match '(?m)^HTTP/\S+ (\d+)') { $status = [int]$Matches[1] }
        $split = $raw -split '\r?\n\r?\n', 2
        if ($split.Count -eq 2) { $headers = $split[0]; try { $body = $split[1] | ConvertFrom-Json } catch {} }
        if (!$status) { $errorKind = 'CLI transport failure (not an HTTP permission verdict)' }
    } else {
        try {
            $response = Invoke-WebRequest -UseBasicParsing -Uri "https://api.github.com/$endpoint" -Headers @{Accept='application/vnd.github+json';'X-GitHub-Api-Version'='2022-11-28'} -UserAgent 'NetPilotLite-ReadOnly-Diagnostics' -TimeoutSec 20
            $status = [int]$response.StatusCode
            $body = $response.Content | ConvertFrom-Json
        } catch {
            if ($_.Exception.Response) { $status = [int]$_.Exception.Response.StatusCode }
            if (!$status) { $errorKind = $_.Exception.GetType().Name + ': network/DNS/proxy/timeout; no HTTP response' }
            if ($_.ErrorDetails.Message) { try { $body = $_.ErrorDetails.Message | ConvertFrom-Json } catch {} }
        }
    }
    $meaning = switch ($status) {
        200 {'Readable'} 401 {'Authentication failed'} 403 {'Permission/policy/rate limit; inspect API message'}
        404 {'Missing, invisible/private, or incorrect endpoint; not proof of no permission'}
        429 {'Rate limited'} 0 {$errorKind} default {'HTTP failure'}
    }
    $safe = [ordered]@{ endpoint=$endpoint; httpStatus=$status; meaning=$meaning; message=$body.message }
    if ($status -eq 200) {
        if ($suffix -eq '') { $safe.private=$body.private; $safe.permissions=$body.permissions; $safe.defaultBranch=$body.default_branch }
        elseif ($suffix -eq '/tags') { $safe.tags=@($body | ForEach-Object { $_.name }) }
        elseif ($suffix -match '^/releases') {
            $safe.releases=@($body | ForEach-Object { @{tag=$_.tag_name;draft=$_.draft;prerelease=$_.prerelease;assets=@($_.assets | ForEach-Object { @{name=$_.name;size=$_.size;digest=$_.digest} })} })
        } elseif ($suffix -eq '/actions/workflows') { $safe.workflows=@($body.workflows | ForEach-Object { @{name=$_.name;state=$_.state;path=$_.path} }) }
        elseif ($suffix -eq '/actions/runners') { $safe.runnerCount=$body.total_count }
        else { $safe.policy=$body }
    }
    $results += $safe
}
$report = [ordered]@{checkedAt=(Get-Date -Format o); repository=$Repository; remote=$remote; repositorySource=$(if($remote){'git origin'}else{'explicit configured destination; no local origin found'}); ghInstalled=[bool]$gh; authenticated=$authenticated; requests=$results; requiredForRelease='contents:write only for publication; admin permission may be required to read Actions policies'; publicationPerformed=$false}
$json = $report | ConvertTo-Json -Depth 12
if ($OutputPath) { $json | Set-Content -LiteralPath $OutputPath -Encoding utf8 }
$json
