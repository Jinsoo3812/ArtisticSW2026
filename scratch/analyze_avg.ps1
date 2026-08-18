# Parse ProfileGPU and stat dumpframe from the user logs or CSV files
$csv1 = Import-Csv "c:\Unreal Projects\ArtisticSW2026\Saved\Profiling\CSV\Profile(20260818_182922).csv"
$csv2 = Import-Csv "c:\Unreal Projects\ArtisticSW2026\Saved\Profiling\CSV\Profile(20260818_183011).csv"
$csv3 = Import-Csv "c:\Unreal Projects\ArtisticSW2026\Saved\Profiling\CSV\Profile(20260818_183133).csv"

function Get-Stats($csv, $col) {
    $vals = $csv | ForEach-Object { [double]$_.$col } | Where-Object { $_ -gt 0 }
    return ($vals | Measure-Object -Average).Average
}

$cols = @("FrameTime", "GPUTime", "GameThreadTime", "RenderThreadTime", "RHIThreadTime")
foreach ($col in $cols) {
    $v1 = Get-Stats $csv1 $col
    $v2 = Get-Stats $csv2 $col
    $v3 = Get-Stats $csv3 $col
    $avg = ($v1 + $v2 + $v3) / 3.0
    Write-Host ("{0}: 1={1:N2}ms, 2={2:N2}ms, 3={3:N2}ms -> Avg={4:N2}ms (FPS: {5:N1})" -f $col, $v1, $v2, $v3, $avg, (1000.0 / $avg))
}
