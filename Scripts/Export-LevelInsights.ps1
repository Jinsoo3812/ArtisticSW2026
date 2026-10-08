param(
    [Parameter(Mandatory)][string]$Metadata,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.7'
)
$ErrorActionPreference = 'Stop'
$metadataPath = (Resolve-Path -LiteralPath $Metadata).Path
$capture = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
if (-not $capture.stopped_own_trace -or -not (Test-Path -LiteralPath $capture.trace)) {
    throw 'A completed capture JSON and its trace are required.'
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$exportPath = (Resolve-Path -LiteralPath $OutputDirectory).Path
$csvPath = $exportPath.Replace('\', '/')
# Insights command parser accepts quoted output paths (including spaces).
$commands = @(
    "TimingInsights.ExportTimingEvents `"$csvPath/gpu_events.csv`" -threads=GPU* -columns=ThreadId,TimerId,TimerName,StartTime,EndTime,Depth",
    "TimingInsights.ExportTimerStatistics `"$csvPath/game_stats.csv`" -threads=GameThread"
)
$commandPath = Join-Path $exportPath 'commands.txt'
[IO.File]::WriteAllLines($commandPath, $commands)
$insights = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealInsights.exe'
$arguments = @(
    ('-OpenTraceFile="' + $capture.trace + '"'), '-AutoQuit', '-NoUI',
    ('-ExecOnAnalysisCompleteCmd="@=' + $commandPath + '"'),
    ('-abslog="' + (Join-Path $exportPath 'export.log') + '"')
)
$process = Start-Process -FilePath $insights -ArgumentList ($arguments -join ' ') -WindowStyle Hidden -PassThru -Wait
if ($process.ExitCode -ne 0) { throw "Insights export failed: $($process.ExitCode)" }
foreach ($file in @('gpu_events.csv', 'game_stats.csv')) {
    if (-not (Test-Path -LiteralPath (Join-Path $exportPath $file))) { throw "Missing export: $file" }
}
Copy-Item -LiteralPath $metadataPath -Destination (Join-Path $exportPath 'metadata.json') -Force
Write-Host "Exported: $exportPath"
