[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RepositoryRoot,
    [Parameter(Mandatory)][string]$ValidatedBinary,
    [Parameter(Mandatory)][string]$TestReport,
    [Parameter(Mandatory)][string]$ToolDirectory,
    [string]$NativeLfs = 'C:\Program Files\Git\mingw64\bin\git-lfs.exe',
    [string]$NativeGit = 'C:\Program Files\Git\cmd\git.exe',
    [switch]$Apply
)

$ErrorActionPreference = 'Stop'
$repoPath = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$binaryPath = (Resolve-Path -LiteralPath $ValidatedBinary).Path
$reportPath = (Resolve-Path -LiteralPath $TestReport).Path
$toolRoot = [IO.Path]::GetFullPath($ToolDirectory).TrimEnd('\')
$sourcePath = Join-Path $PSScriptRoot 'LfsDiffFilter.cs'
$binaryHash = (Get-FileHash -LiteralPath $binaryPath -Algorithm SHA256).Hash.ToLowerInvariant()
$sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash.ToLowerInvariant()
$testResults = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
if ($testResults.adapterSha256 -ne $binaryHash -or $testResults.sourceSha256 -ne $sourceHash -or $testResults.passed -lt 15 -or
    $testResults.checks -notcontains 'native-add-commit-smudge-restore-and-object-integrity' -or
    $testResults.checks -notcontains 'terminated-mid-content-without-orphan-temporary-file') {
    throw 'This exact binary must pass the integration tests before installation.'
}
$nativeLfsPath = (Resolve-Path -LiteralPath $NativeLfs).Path
$nativeGitPath = (Resolve-Path -LiteralPath $NativeGit).Path
$actualRepo = (& $nativeGitPath -C $repoPath rev-parse --show-toplevel).Replace('/', '\')
if ($LASTEXITCODE -ne 0 -or $actualRepo -ne $repoPath) { throw 'Expected repository root.' }
if ($toolRoot.StartsWith($repoPath + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Install the immutable tool outside the watched working tree and disposable Saved folder.'
}
$unsupported = @(& $nativeGitPath -C $repoPath config --get-regexp '^(lfs\.extension\.|diff\.lfs\.(command|textconv)$)')
if ($LASTEXITCODE -eq 0 -or $unsupported.Count) { throw 'Custom LFS extensions or diff drivers require a separate review.' }
if ($LASTEXITCODE -ne 1) { throw 'Could not inspect LFS configuration.' }

$existingLocal = @(& $nativeGitPath -C $repoPath config --local --get filter.lfs.process)
$existingExit = $LASTEXITCODE
$installRoot = Join-Path $toolRoot $sourceHash.Substring(0, 16)
$destination = Join-Path $installRoot 'LfsDiffFilter.exe'
function Quote-GitShellArgument([string]$Value) {
    $apostropheEscape = [string][char]39 + [char]34 + [char]39 + [char]34 + [char]39
    return "'" + $Value.Replace('\', '/').Replace("'", $apostropheEscape) + "'"
}
$commandParts = @($destination, '--native', $nativeLfsPath, '--git', $nativeGitPath) | ForEach-Object { Quote-GitShellArgument $_ }
$filterCommand = $commandParts -join ' '
if ($existingExit -eq 0 -and ($existingLocal -join '') -ne $filterCommand) {
    $knownInstallation = @()
    if (Test-Path -LiteralPath $toolRoot) {
        $knownInstallation = @(Get-ChildItem -LiteralPath $toolRoot -Directory | ForEach-Object {
            $knownJournal = Join-Path $_.FullName 'installation.json'
            if (Test-Path -LiteralPath $knownJournal) { Get-Content -LiteralPath $knownJournal -Raw | ConvertFrom-Json }
        } | Where-Object { $_.Repository -eq $repoPath -and $_.ProcessFilter -ceq ($existingLocal -join '') })
    }
    if (-not $knownInstallation.Count) {
        throw 'An unrecognized local process filter must not be overwritten automatically.'
    }
}
$plan = [ordered]@{
    Repository = $repoPath
    Destination = $destination
    BinarySha256 = $binaryHash
    SourceSha256 = $sourceHash
    OriginalLocalFilter = $existingLocal -join ''
    ProcessFilter = $filterCommand
    NativeLfs = $nativeLfsPath
    NativeGit = $nativeGitPath
}
if (-not $Apply) { $plan | ConvertTo-Json -Depth 4; return }
New-Item -ItemType Directory -Path $installRoot -Force | Out-Null
if (Test-Path -LiteralPath $destination) {
    if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $binaryHash) {
        throw 'The versioned installation path already contains a different binary.'
    }
} else {
    Copy-Item -LiteralPath $binaryPath -Destination $destination
}
if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $binaryHash) {
    throw 'Installed binary hash does not match the tested binary.'
}
$configPath = & $nativeGitPath -C $repoPath rev-parse --path-format=absolute --git-path config
if ($LASTEXITCODE -ne 0) { throw 'Cannot locate repository config.' }
$backup = Join-Path $installRoot ('git-config-before-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.txt')
Copy-Item -LiteralPath $configPath -Destination $backup
Copy-Item -LiteralPath $sourcePath -Destination (Join-Path $installRoot 'LfsDiffFilter.cs')
Copy-Item -LiteralPath $reportPath -Destination (Join-Path $installRoot 'test-report.json')
$plan.ConfigBackup = $backup
$plan | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $installRoot 'installation.json') -Encoding utf8
& $nativeGitPath -C $repoPath config --local filter.lfs.process $filterCommand
if ($LASTEXITCODE -ne 0) { throw 'Could not apply repository-local process filter.' }
[pscustomobject]@{ Installed = $destination; Repository = $repoPath; BinarySha256 = $binaryHash; TestsPassed = $testResults.passed; ConfigBackup = $backup } | ConvertTo-Json
