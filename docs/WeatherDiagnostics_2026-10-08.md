# Lvl_CY 날씨 끊김 장시간 수집

현재 상태: 2026-10-08 후속 사용자 요청으로 날씨 런타임 출력문을 모두 주석 처리했다. 아래 수집 명령은 전체 엔진 로그를 저장하지만, 현재 빌드에서는 sw.Weather.Diagnostics 값과 관계없이 날씨 진단 출력은 나오지 않는다. 아래 설명은 출력 복원 시 참고용으로 남긴다.

2026-10-08: 기존 진단 출력이 주석 처리되어 있음을 확인하고 복원했다. 기존의 연속 재생과 2초 지연 보간은 유지한다. 이전 Hour 8→9 경계에서 CloudMaterial.Star_GChannel delta=1.0 기록이 있으나, 현재 화면 끊김의 원인으로 확정하지 않았다.

## 두 실행의 로그 분리

아래를 PowerShell에 붙여 넣는다. 기존 실행 옵션과 시작 맵 흐름을 유지한다. 실행 후 기존 방법대로 Lvl_CY에 들어간다. Instance1/2는 실행 순서이며 호스트/게스트 역할은 실제 접속 후 Authority/NetMode 필드로 확인한다.

```powershell
$editor = 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe'
$project = 'C:\Unreal Projects\ArtisticSW2026\ArtisticSW2026.uproject'
$projectDirectory = Split-Path -Parent $project
$session = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
$logDirectory = Join-Path $projectDirectory "Saved\Logs\Weather_$session"
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null

foreach ($instance in 1..2) {
    $logFile = Join-Path $logDirectory "Instance$instance.log"
    $arguments = '"' + $project + '" -game -log -windowed -ResX=1000 -ResY=700' +
                 ' -abslog="' + $logFile + '" -ExecCmds="sw.Weather.Diagnostics 2"'
    $process = Start-Process -FilePath $editor -ArgumentList $arguments `
        -WorkingDirectory $projectDirectory -PassThru
    Write-Host "Instance$instance / PID=$($process.Id) / $logFile"
}
Write-Host "로그 폴더: $logDirectory"
```

두 전체 로그는 해당 폴더에 독립 저장된다. 콘솔 창 텍스트를 복사할 필요가 없다. 일반 종료 시 로그가 마무리되며 엔진은 기본 비동기 주기 flush를 사용한다. 끊김을 목격하면 실제 시각과 창 번호를 적어 둔다. 전체 로그에는 날씨 외의 로딩·네트워크·렌더링 관련 증거도 있으므로 함께 보존한다.

## 로그 해석

| Phase / 필드 | 용도 |
|---|---|
| DiagnosticsStart | PID, World, Actor, Authority, NetMode 식별. 시작 직후 ExecCmds 적용 전 Level=1이 찍힐 수 있음 |
| BoundaryEnd → BoundaryRestored → BoundaryStart | 이전 hour alpha=1, 새 snapshot 복원, 새 hour alpha=0 평가 단계 구분. Restored의 조명 값은 아직 이전 평가 값일 수 있음 |
| BoundaryDiscontinuity | 경계의 0.01 초과 파라미터 변화. Changes의 From/To, 색·태양 곡선과 scalar curve 경로, 이전 target→새 source 텍스처 연결 확인 |
| FrameParameterChange | 동일 구간 안의 0.05 초과 변화 또는 텍스처/태양 billboard material 교체. 초당 최대 1회. 회전 delta는 도 단위 |
| PublishWindow / ReceiveWindow | 창 게시/수신, WindowPublished, RawBytes/WireBytes 확인 |
| BufferHold / BufferResume | 다음 구간 누락·준비 지연. NextPresent, NextReady, Loads, BufferAhead 확인 |
| ClockJump / ClockError / AlphaRegression | 서버 시각 보정·재생 시각 오차·진행도 역행 구분 |
| FrameHitch | local elapsed가 250ms를 넘은 프레임. Hitches/MaxElapsed는 누적값, Step은 재생 증분 |
| RecoveryStart / RecoveryComplete | 과거 history 유실 뒤 2초 복구 여부 |
| Reason=... | 어댑터/생성/참조/텍스트 검증 실패. Epoch/Sequence/reason당 1회 |

Status: 0=WaitingInitialState, 1=Playing, 2=HoldingForData, 3=Frozen, 4=WaitingEpochReset, 5=WaitingRecoveryTarget, 6=Recovering.

정상 보간도 프레임 delta 임계값을 넘을 수 있다. 파라미터 변화 경고 자체는 화면 끊김의 확정 증거가 아니다. 관찰 시각, alpha 증분, elapsed, 경계 단계와 실제 변화값을 대조한다. SuppressedFrameChanges는 로그 출력만 생략한 횟수로 변화가 없었다는 의미가 아니다. GPU 픽셀/프레임 시간 전체를 계측하는 로그는 아니다.

## 수집량 조절 및 날씨 로그 추출

`sw.Weather.Diagnostics 1`은 5초마다 요약과 경계/이벤트를 기록한다. `2`는 1초마다 상세 표본을 기록한다. `0`은 표본/비교 출력을 끈다. 시작/검증 실패의 중요한 로그는 남는다. 실행 명령어의 `2`를 `1`로 바꾸거나 게임 콘솔에서 값을 바꾼다.

수집 종료 후 전체 로그를 유지하면서 날씨 전용 사본을 만들 수 있다. 같은 PowerShell 창에서 실행한다.

```powershell
foreach ($instance in 1..2) {
    Select-String -Path (Join-Path $logDirectory "Instance$instance.log") `
        -Pattern 'LogSWWeatherDiagnostics' |
        ForEach-Object { $_.Line } |
        Set-Content -Path (Join-Path $logDirectory "Instance$instance.weather.log") -Encoding utf8
}
```

검증: 변경 C++ 컴파일 및 ArtisticSW2026Editor Win64 Development 최종 링크 성공. 실제 플레이와 장시간 수집은 수행하지 않았다.
