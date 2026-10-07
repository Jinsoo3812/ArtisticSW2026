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
    [switch]$Standalone,
    [ValidateSet('Current', 'MaskedDepth', 'MaskedBaseVelocity')][string]$RenderProfile = 'Current',
    [string]$CameraMetadata,
    [switch]$ControlledComparison,
    [ValidateRange(0, 23)][int]$ComparisonHour = 18
)
$ErrorActionPreference = 'Stop'
$running = Get-Process UnrealEditor, UnrealEditor-Cmd, UnrealBuildTool, dotnet, MSBuild, ShaderCompileWorker, LiveCodingConsole -ErrorAction SilentlyContinue
if ($running) { throw ('Wait for these processes to finish or close the editor: ' + ($running.Name -join ', ')) }
$projectRoot = Split-Path $PSScriptRoot -Parent
if ($ControlledComparison -and $Standalone) { throw 'Controlled comparisons use PIE to preserve uncooked Landscape Nanite.' }
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
$arguments += "-SWRenderProfile=$RenderProfile"
if ($RenderProfile -ne 'Current') {
    $renderOverrides = [ordered]@{
        'r.EarlyZPass' = 2
        'r.EarlyZPassOnlyMaterialMasking' = 1
        'r.VelocityOutputPass' = $(if ($RenderProfile -eq 'MaskedBaseVelocity') { 1 } else { 0 })
    }
    foreach ($entry in $renderOverrides.GetEnumerator()) {
        # Startup-only shader settings: never use ExecCmds for these.
        $arguments += "-ini:Engine:[/Script/Engine.RendererSettings]:$($entry.Key)=$($entry.Value)"
    }
}
if ($CameraMetadata) {
    $cameraSource = Get-Content -LiteralPath $CameraMetadata -Raw | ConvertFrom-Json
    $location = $cameraSource.camera_start.location_cm
    $rotation = $cameraSource.camera_start.rotation_degrees
    if ($location -notmatch '^X=([-\d.]+) Y=([-\d.]+) Z=([-\d.]+)$') { throw 'Invalid camera location metadata.' }
    $arguments += "-SWProfileCameraX=$($Matches[1])", "-SWProfileCameraY=$($Matches[2])", "-SWProfileCameraZ=$($Matches[3])"
    if ($rotation -notmatch '^P=([-\d.]+) Y=([-\d.]+) R=([-\d.]+)$') { throw 'Invalid camera rotation metadata.' }
    $absolutePitch = $Matches[1]
    $absoluteYaw = ([double]::Parse($Matches[2], [Globalization.CultureInfo]::InvariantCulture) + $YawOffset).ToString([Globalization.CultureInfo]::InvariantCulture)
    $arguments += "-SWProfileCameraYaw=$absoluteYaw"
    # Remove the default pitch so command-line parsing cannot select it first.
    $arguments = @($arguments | Where-Object { $_ -notmatch '^-[Ss][Ww]ProfileFixedCameraPitch=' }) + "-SWProfileFixedCameraPitch=$absolutePitch"
}
if ($ControlledComparison) {
    $arguments += '-SWControlledComparison', "-SWComparisonHour=$ComparisonHour"
    $playOverrides = [ordered]@{ PlayNetMode = 'PIE_Standalone'; RunUnderOneProcess = 'True'; PlayNumberOfClients = 1;
        bLaunchSeparateServer = 'False'; NewWindowWidth = $Width; NewWindowHeight = $Height }
    foreach ($entry in $playOverrides.GetEnumerator()) {
        $arguments += "-ini:EditorPerProjectUserSettings:[/Script/UnrealEd.LevelEditorPlaySettings]:$($entry.Key)=$($entry.Value)"
    }
}
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
if ($RenderProfile -ne 'Current') {
    foreach ($entry in $renderOverrides.GetEnumerator()) {
        if ([string]$metadata.cvars.($entry.Key) -ne [string]$entry.Value) { throw "Startup setting did not apply: $($entry.Key)" }
    }
}
if ($ControlledComparison) {
    if ($metadata.net_mode -ne 0) { throw 'Controlled capture did not use standalone PIE.' }
    # EditorRequestBeginPlay embeds PIE in the active viewport. ResX/ResY set the
    # editor window, not its content area: report actual dimensions for A/B checks.
    Write-Host "Actual embedded PIE viewport: $($metadata.viewport_width)x$($metadata.viewport_height)"
    if (($metadata.camera_start | ConvertTo-Json -Compress) -ne ($metadata.camera_end | ConvertTo-Json -Compress)) { throw 'Camera moved during comparison.' }
    if (($metadata.render_state_start | ConvertTo-Json -Depth 5 -Compress) -ne ($metadata.render_state_end | ConvertTo-Json -Depth 5 -Compress)) { throw 'Directional light changed during comparison.' }
    if ($metadata.shader_jobs_start -gt 0 -or $metadata.shader_jobs_end -gt 0) { throw 'Shaders were still compiling during comparison.' }
}
