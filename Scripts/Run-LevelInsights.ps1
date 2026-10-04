param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.7',
    [ValidatePattern('^/Game/[\p{L}\p{N}_/]+$')][string]$Map = '/Game/Level/Lvl_CY',
    [string]$Label = 'LvlCY',
    [ValidateRange(1, 300)][int]$Seconds = 20,
    [ValidateRange(1, 300)][int]$Warmup = 20,
    [ValidateSet('On', 'Off')][string]$LandscapeNanite = 'On',
    [float]$YawOffset = 0,
    [float]$Pitch = -18,
    [float]$CameraZOffset = 0,
    [int]$Width = 1632,
    [int]$Height = 980,
    [switch]$Offscreen,
    [switch]$Standalone
)
$ErrorActionPreference = 'Stop'
$running = Get-Process UnrealEditor, UnrealEditor-Cmd, UnrealBuildTool, dotnet, MSBuild, ShaderCompileWorker, LiveCodingConsole -ErrorAction SilentlyContinue
if ($running) { throw ('Wait for these processes to finish or close the editor: ' + ($running.Name -join ', ')) }
$projectRoot = Split-Path $PSScriptRoot -Parent
$editorName = if ($Standalone) { 'UnrealEditor-Cmd.exe' } else { 'UnrealEditor.exe' }
$editor = Join-Path $EngineRoot ('Engine\Binaries\Win64\' + $editorName)
$project = Join-Path $projectRoot 'ArtisticSW2026.uproject'
$output = Join-Path $projectRoot 'Saved\Profiling\Insights'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$safeLabel = $Label -replace '[^\p{L}\p{N}_-]', '_'
if ($safeLabel.Length -gt 64) { $safeLabel = $safeLabel.Substring(0, 64) }
if (-not $safeLabel) { $safeLabel = 'Capture' }
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$naniteValue = if ($LandscapeNanite -eq 'On') { 1 } else { 0 }
$arguments = @(
    ('"' + $project + '"'), ('"' + $Map + '"'), '-d3d12', '-windowed', '-unattended', '-nosound', '-nop4', '-NoSplash', '-traceautostart=0',
    "-ResX=$Width", "-ResY=$Height", '-ForceRes',
    "-SWInsightsCapture=$safeLabel", "-SWInsightsWarmup=$Warmup", "-SWInsightsSeconds=$Seconds", '-SWInsightsAutoQuit',
    '-SWProfileLevel', "-SWProfileLevelMap=$([IO.Path]::GetFileName($Map))",
    "-SWProfileLevelWarmup=$($Warmup + 1)", '-SWProfileLevelFrames=120',
    '-SWProfileFixedWaterCamera', '-SWProfileSavedEditorCamera', "-SWProfileFixedCameraZOffset=$CameraZOffset", "-SWProfileFixedCameraPitch=$Pitch",
    "-SWProfileFixedCameraYawOffset=$YawOffset", '-SWProfileScreenshot',
    ('-SWProfileScreenshotName="' + "$output\${safeLabel}_${stamp}.png" + '"'),
    ('-ExecCmds="Landscape.RenderNanite ' + $naniteValue + '"'),
    ('-abslog="' + "$output\${safeLabel}_${stamp}.log" + '"')
)
if ($Standalone) { $arguments += '-game' }
else {
    $arguments += '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities'
    $arguments += ('-ExecutePythonScript="' + (Join-Path $PSScriptRoot 'Capture-LevelInsightsPIE.py') + '"')
    $arguments += "-SWInsightsMap=$Map"
}
if ($Offscreen) { $arguments += '-RenderOffscreen' }
Write-Host "Capturing $Label, Landscape.RenderNanite=$naniteValue, warmup ${Warmup}s, duration ${Seconds}s."
$captureStarted = Get-Date
$captureProcess = Start-Process -FilePath $editor -ArgumentList ($arguments -join ' ') -WindowStyle Hidden -PassThru -Wait
if ($captureProcess.ExitCode -ne 0) { throw "Unreal exited with code $($captureProcess.ExitCode). See $output" }
$metadataFile = Get-ChildItem -LiteralPath $output -Filter "${safeLabel}_*.json" |
    Where-Object { $_.LastWriteTime -ge $captureStarted } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $metadataFile) { throw "No completed capture found. See $output\${safeLabel}_${stamp}.log" }
$metadata = Get-Content -LiteralPath $metadataFile.FullName -Raw | ConvertFrom-Json
if (-not $metadata.stopped_own_trace -or -not (Test-Path -LiteralPath $metadata.trace)) { throw 'The bounded trace did not finish correctly. Inspect the capture log.' }
if ($LandscapeNanite -eq 'On') {
    $landscapes = @($metadata.landscapes)
    $invalidLandscapes = @($landscapes | Where-Object {
        -not $_.nanite_enabled -or -not $_.nanite_up_to_date -or $_.nanite_components -le 0
    })
    if ($landscapes.Count -eq 0 -or $invalidLandscapes.Count -gt 0) {
        throw 'No valid, up-to-date Nanite landscape components found. This capture is not a valid Nanite On comparison.'
    }
}
Write-Host "Trace, settings JSON, screenshot and log: $output"
