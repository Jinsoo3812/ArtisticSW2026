$csvFiles = @(
    "c:\Unreal Projects\ArtisticSW2026\Saved\Profiling\CSV\Profile(20260818_183628).csv",
    "c:\Unreal Projects\ArtisticSW2026\Saved\Profiling\CSV\Profile(20260818_183902).csv",
    "c:\Unreal Projects\ArtisticSW2026\Saved\Profiling\CSV\Profile(20260818_184012).csv"
)

function Get-Avg($filePath, $colName) {
    $lines = Get-Content $filePath
    $headers = $lines[0].Split(",")
    $idx = -1
    for ($i = 0; $i -lt $headers.Length; $i++) {
        if ($headers[$i].Trim('"') -eq $colName) {
            $idx = $i
            break
        }
    }
    if ($idx -eq -1) { return 0.0 }
    $sum = 0.0
    $count = 0
    for ($i = 1; $i -lt $lines.Length; $i++) {
        $cols = $lines[$i].Split(",")
        if ($cols.Length -gt $idx) {
            $val = 0.0
            if ([double]::TryParse($cols[$idx].Trim('"'), [ref]$val)) {
                $sum += $val
                $count++
            }
        }
    }
    if ($count -eq 0) { return 0.0 }
    return $sum / $count
}

$metrics = @(
    "FrameTime",
    "GPUTime",
    "GameThreadTime",
    "RenderThreadTime",
    "RHIThreadTime",
    "Exclusive/RenderThread/EventWait/Visibility",
    "Exclusive/GameThread/EventWait",
    "RHI/DrawCalls",
    "RHI/PrimitivesDrawn"
)

Write-Host "=== 최신 3회 CSV 프로파일 평균 결과 ==="
foreach ($m in $metrics) {
    $v1 = Get-Avg $csvFiles[0] $m
    $v2 = Get-Avg $csvFiles[1] $m
    $v3 = Get-Avg $csvFiles[2] $m
    $avg = ($v1 + $v2 + $v3) / 3.0
    Write-Host ("{0,-45} | 1회: {1,7:N2} | 2회: {2,7:N2} | 3회: {3,7:N2} | 평균: {4,7:N2}" -f $m, $v1, $v2, $v3, $avg)
}
