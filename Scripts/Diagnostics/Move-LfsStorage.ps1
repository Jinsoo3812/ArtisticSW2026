[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RepositoryRoot,
    [Parameter(Mandatory)][string]$StorageDirectory,
    [switch]$Apply,
    [switch]$DetachTemporaryJunction
)

$ErrorActionPreference = 'Stop'
$repoPath = (Resolve-Path -LiteralPath $RepositoryRoot).Path.TrimEnd('\')
$storePath = [IO.Path]::GetFullPath($StorageDirectory).TrimEnd('\')
$reportRoot = Join-Path ([IO.Path]::GetDirectoryName($storePath)) ('MigrationReports\' + [IO.Path]::GetFileName($storePath))
$optionalLocksBefore = $env:GIT_OPTIONAL_LOCKS
$env:GIT_OPTIONAL_LOCKS = '0'

function Invoke-RepositoryGit([string[]]$GitArguments) {
    $gitOutput = @(& git -C $repoPath -c core.fsmonitor=false @GitArguments)
    if ($LASTEXITCODE -ne 0) { throw "Git failed: $($GitArguments -join ' ')" }
    return ($gitOutput -join "`n")
}

function Get-RepositoryState {
    $indexPath = Invoke-RepositoryGit -GitArguments @('rev-parse', '--path-format=absolute', '--git-path', 'index')
    return [ordered]@{
        Head = Invoke-RepositoryGit -GitArguments @('rev-parse', 'HEAD')
        IndexSha256 = (Get-FileHash -LiteralPath $indexPath -Algorithm SHA256).Hash
        Refs = Invoke-RepositoryGit -GitArguments @('show-ref')
        Worktrees = Invoke-RepositoryGit -GitArguments @('worktree', 'list', '--porcelain')
        Status = Invoke-RepositoryGit -GitArguments @('-c', 'filter.lfs.process=', '-c', 'filter.lfs.clean=', '-c', 'filter.lfs.required=false', 'status', '--porcelain=v1', '--untracked-files=all')
    }
}

try {
    $actualRoot = (Invoke-RepositoryGit -GitArguments @('rev-parse', '--show-toplevel')).Replace('/', '\').TrimEnd('\')
    if ($actualRoot -ne $repoPath) { throw 'RepositoryRoot must be the repository root.' }
    $commonPath = (Invoke-RepositoryGit -GitArguments @('rev-parse', '--path-format=absolute', '--git-common-dir')).Replace('/', '\')
    $gitPath = (Invoke-RepositoryGit -GitArguments @('rev-parse', '--absolute-git-dir')).Replace('/', '\')
    if ($gitPath -ne $commonPath) { throw 'Migrate from the main checkout, not a linked worktree.' }
    $oldStore = Join-Path $commonPath 'lfs'
    $existingSetting = @(& git -C $repoPath config --local --get lfs.storage)
    if ($LASTEXITCODE -eq 0 -and ($existingSetting -join '') -ne '') {
        $existingFull = [IO.Path]::GetFullPath(($existingSetting -join '')).TrimEnd('\')
        if ($existingFull -eq $storePath) {
            [pscustomobject]@{ AlreadyConfigured = $true; Storage = $storePath } | ConvertTo-Json
            return
        }
        throw 'An existing custom lfs.storage setting requires a separate migration plan.'
    }
    if ([IO.Path]::GetPathRoot($oldStore) -ne [IO.Path]::GetPathRoot($storePath)) {
        throw 'This migration requires a same-volume directory rename.'
    }
    if ($storePath.StartsWith($repoPath + '\', [StringComparison]::OrdinalIgnoreCase) -or $storePath -eq $repoPath) {
        throw 'LFS storage must be outside the watched working tree.'
    }
    if (Test-Path -LiteralPath $storePath) { throw 'Destination must not already exist.' }
    if (-not (Test-Path -LiteralPath (Join-Path $oldStore 'objects') -PathType Container)) {
        throw 'Expected existing LFS object storage is missing.'
    }
    if ((Get-Item -LiteralPath $oldStore -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw 'The source storage root must not be a link.'
    }
    for ($ancestor = [IO.Path]::GetDirectoryName($storePath); $ancestor; $ancestor = [IO.Path]::GetDirectoryName($ancestor)) {
        if ((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Destination ancestors must not redirect to another location.'
        }
    }
    $before = Get-RepositoryState
    $plan = [ordered]@{ Repository = $repoPath; Source = $oldStore; Destination = $storePath; Before = $before }
    if (-not $Apply) { $plan | ConvertTo-Json -Depth 5; return }

    foreach ($lockName in @('index.lock', 'config.lock', 'HEAD.lock', 'packed-refs.lock', 'MERGE_HEAD', 'rebase-merge', 'rebase-apply', 'sequencer')) {
        if (Test-Path -LiteralPath (Join-Path $commonPath $lockName)) { throw "Active Git lock/operation: $lockName" }
    }
    $idleDeadline = (Get-Date).AddSeconds(30)
    do {
        $gitProcesses = @(Get-CimInstance Win32_Process -Filter "Name='git.exe'")
        $mutators = @($gitProcesses | Where-Object { $_.CommandLine -match '\b(add|commit|checkout|switch|merge|rebase|reset|gc|prune|fetch|pull|push|update-index|read-tree|cherry-pick|revert)\b' -and $_.CommandLine -notmatch '\bdiff\b' })
        if ($mutators.Count) { throw 'A Git write operation is running; retry after it completes.' }
        $comparisons = @($gitProcesses | Where-Object { $_.CommandLine -match '\bdiff\b.*--numstat' })
        if (-not $comparisons.Count) { break }
        if ((Get-Date) -ge $idleDeadline) { throw 'Git comparisons did not become idle; nothing was moved.' }
        Start-Sleep -Milliseconds 250
    } while ($true)

    New-Item -ItemType Directory -Path $reportRoot -Force | Out-Null
    $journal = Join-Path $reportRoot ('lfs-storage-migration-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.json')
    $configBackup = Join-Path $reportRoot ('git-config-before-storage-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.txt')
    Copy-Item -LiteralPath (Join-Path $commonPath 'config') -Destination $configBackup
    $plan | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $journal -Encoding utf8
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($storePath)) -Force | Out-Null

    # Both absolute paths were checked above; rename preserves object bytes and ACLs.
    [IO.Directory]::Move($oldStore, $storePath)
    try {
        Invoke-RepositoryGit -GitArguments @('config', '--local', 'lfs.storage', $storePath) | Out-Null
    } catch {
        if (-not (Test-Path -LiteralPath $oldStore)) { [IO.Directory]::Move($storePath, $oldStore) }
        throw
    }

    $temporaryPath = Join-Path $storePath 'tmp'
    $junctionRemoved = $false
    if ($DetachTemporaryJunction -and (Test-Path -LiteralPath $temporaryPath)) {
        $temporaryItem = Get-Item -LiteralPath $temporaryPath -Force
        if ($temporaryItem.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            if ($temporaryItem.LinkType -ne 'Junction') { throw 'Unexpected temporary-directory link type.' }
            # Delete only the junction entry, without recursing into its target.
            [IO.Directory]::Delete($temporaryPath)
            [IO.Directory]::CreateDirectory($temporaryPath) | Out-Null
            $junctionRemoved = $true
        }
    }
    $after = Get-RepositoryState
    foreach ($field in $before.Keys) {
        if ($before[$field] -cne $after[$field]) { throw "Post-migration verification failed for $field. Objects are preserved at $storePath; see $journal." }
    }
    $result = [ordered]@{
        Repository = $repoPath
        Storage = $storePath
        OldStorageRecreated = Test-Path -LiteralPath $oldStore
        TemporaryJunctionRemoved = $junctionRemoved
        Before = $before
        After = $after
        ConfigBackup = $configBackup
        LfsEnvironment = Invoke-RepositoryGit -GitArguments @('lfs', 'env')
    }
    $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $journal -Encoding utf8
    [pscustomobject]@{ Storage = $storePath; PreservedHeadIndexRefsStatusAndWorktrees = $true; TemporaryJunctionRemoved = $junctionRemoved; Journal = $journal } | ConvertTo-Json
} finally {
    $env:GIT_OPTIONAL_LOCKS = $optionalLocksBefore
}
