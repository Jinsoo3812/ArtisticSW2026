# run_benchmark_session.ps1
$ErrorActionPreference = "Continue"

$destDir = "C:\Users\vlvkr\OneDrive\Desktop\Optimization\Data"
if (-not (Test-Path $destDir)) {
    New-Item -ItemType Directory -Path $destDir -Force | Out-Null
}

$ueExe = "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe"
$uproject = "c:\Unreal Projects\ArtisticSW2026\ArtisticSW2026.uproject"
$mapPath = "/Game/New/Water/Realistic_Water/Realistic_Water"

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$csvBaseName = "RealisticWater_$timestamp"

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host " [1/5] Launching Dedicated Server: $mapPath" -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Cyan

$serverArgs = @("$uproject", "$mapPath", "-server", "-log", "-nosteam", "-port=7777")
$serverProcess = Start-Process -FilePath $ueExe -ArgumentList $serverArgs -PassThru

Write-Host "Waiting 6 seconds for Server to initialize..." -ForegroundColor Yellow
Start-Sleep -Seconds 6

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host " [2/5] Launching Client (Windowed 1920x1080)..." -ForegroundColor Green
Write-Host " Auto-capturing 700 frames to: $csvBaseName.csv" -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Cyan

$clientArgs = @(
    "$uproject",
    "127.0.0.1:7777",
    "-game",
    "-windowed",
    "-ResX=1920",
    "-ResY=1080",
    "-ExecCmds=csvprofile frames=700 filename=$csvBaseName,stat Unit,stat SceneRendering"
)
$clientProcess = Start-Process -FilePath $ueExe -ArgumentList $clientArgs -PassThru

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Magenta
Write-Host ">>> CLIENT IS RUNNING! PLEASE PRESS 'W' TO MOVE FORWARD! <<<" -ForegroundColor Magenta
Write-Host ">>> Capturing for 15 seconds... <<<" -ForegroundColor Magenta
Write-Host "==========================================================" -ForegroundColor Magenta
Write-Host ""

# Countdown 15 seconds
for ($i = 15; $i -gt 0; $i--) {
    Write-Host "Remaining test time: $i seconds..." -ForegroundColor Cyan
    Start-Sleep -Seconds 1
}

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host " [3/5] 15 Seconds Elapsed! Gracefully closing client & server..." -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Cyan

# Gracefully close client
if ($clientProcess -and -not $clientProcess.HasExited) {
    $clientProcess.CloseMainWindow() | Out-Null
    Start-Sleep -Seconds 3
    if (-not $clientProcess.HasExited) {
        Stop-Process -Id $clientProcess.Id -Force -ErrorAction SilentlyContinue
    }
}

# Stop server
if ($serverProcess -and -not $serverProcess.HasExited) {
    $serverProcess.CloseMainWindow() | Out-Null
    Start-Sleep -Seconds 2
    if (-not $serverProcess.HasExited) {
        Stop-Process -Id $serverProcess.Id -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host " [4/5] Copying collected CSV and Logs to Desktop/Optimization/Data" -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Cyan

# Find latest CSV matching our name or newest
$csvDir = "c:\Unreal Projects\ArtisticSW2026\Saved\Profiling\CSV"
$latestCsv = Get-ChildItem -Path $csvDir -Filter "*$csvBaseName*.csv" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $latestCsv) {
    $latestCsv = Get-ChildItem -Path $csvDir -Filter "*.csv" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
}

if ($latestCsv) {
    $destCsv = Join-Path $destDir $latestCsv.Name
    Copy-Item -Path $latestCsv.FullName -Destination $destCsv -Force
    Write-Host "[SUCCESS] Copied CSV: $($latestCsv.Name) -> $destCsv" -ForegroundColor Green
} else {
    Write-Host "[WARNING] No CSV found in $csvDir" -ForegroundColor Red
}

# Copy Log
$logFile = "c:\Unreal Projects\ArtisticSW2026\Saved\Logs\ArtisticSW2026_2.log"
if (-not (Test-Path $logFile)) {
    $logFile = "c:\Unreal Projects\ArtisticSW2026\Saved\Logs\ArtisticSW2026.log"
}
if (Test-Path $logFile) {
    $destLog = Join-Path $destDir "ClientLog_$timestamp.log"
    Copy-Item -Path $logFile -Destination $destLog -Force
    Write-Host "[SUCCESS] Copied Log: $logFile -> $destLog" -ForegroundColor Green
}

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host " [5/5] Test Complete! All data archived safely." -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Cyan
