# Ship Foam / Kelvin Wake M5 구현 기록

작성일: 2026-08-16  
적용 대상: `/Game/New/Water/Realistic_Water/M_Realistic_Water`  
범위: Kelvin displacement와 부력 통합. Foam 통합은 제외.

## 1. M4 문제

M4는 12개 Froude Golden Image를 정확히 샘플링했지만, 배마다 하나뿐인 이벤트를 주기적으로 덮어썼다. 이벤트가 바뀔 때 현재 속도로 파장과 atlas UV를 다시 계산했으므로 과거 경로 전체의 마루와 골도 함께 이동했다. `MinimumEmissionInterval`과 `EmissionDistanceCm`을 바꾸면 점멸 주기가 같이 변한 이유다.

논문과 Houdini의 Golden Image는 일정 속도의 정상상태 해다. 가속 중에 하나의 정상상태 해로 과거 파면 전체를 다시 해석하면 안 된다.

## 2. M5 결정

M5는 사용자가 제안한 두 요소를 결합한다.

1. Godot처럼 `Previous / Current / Next` 기록 상태 3개를 순환한다.
2. 기존 Golden Atlas의 12개 Froude slice 중 현재 속도에 맞는 인접 두 장을 선형 보간한다.

단, Godot의 유한차분 비분산 파동식은 사용하지 않는다. Golden Image가 이미 심해 Kelvin 분산 적분 결과이기 때문이다. M5의 기록 버퍼는 새 파형을 만드는 solver가 아니라 서로 다른 속도의 Golden 결과를 세계공간에서 시간적으로 누적·혼합하는 역할만 한다.

## 3. 데이터 흐름

```text
배 위치/속도/trajectory
        ↓
현재 Froude 계산
        ↓
12-slice Golden Atlas에서 인접 2 slice 보간
        ↓
현재 시각의 signed Golden target field
        ↓
Previous + Current + target → Next
        ↓
3개 상태 index 순환
        ↓
Current CPU 배열 ── 부력 높이/기울기
        └────────── R32F texture upload ── Water WPO
```

기록 갱신식은 다음과 같다.

```text
Next = Current
     + (Current - Previous) * HistoryMomentum
     + (GoldenTarget - Current) * ResponseAlpha

ResponseAlpha = 1 - exp(-HistoryResponseRate * FixedDeltaTime)
```

필드 중심이 배를 따라 이동할 때 Previous와 Current는 각 상태의 과거 중심을 사용해 세계공간으로 재투영한다. 따라서 텍스처가 이동해도 기록된 파면이 배와 함께 통째로 미끄러지지 않는다.

## 4. CPU/GPU 일치

M3와 달리 CPU 배열이 authoritative state다.

- CPU는 세 개의 `float` signed-height 배열을 갱신한다.
- 각 배열에 대응하는 세 개의 transient `PF_R32_FLOAT` 텍스처가 있다.
- 현재 CPU 배열을 현재 GPU 텍스처에 그대로 업로드한다.
- Water WPO는 현재 텍스처를 bilinear sample한다.
- 부력은 같은 현재 CPU 배열을 같은 world-to-UV 변환으로 bilinear sample한다.
- Kelvin gradient도 같은 배열의 중앙 차분으로 계산한다.

GPU readback은 없으며, 렌더 전용 solver와 CPU용 근사식을 별도로 두지 않는다. GPU 업로드 큐로 인한 최대 한 렌더 프레임 지연은 있을 수 있지만 파면 데이터의 출처는 동일하다.

## 5. 머티리얼 변경

`M_Realistic_Water`의 기존 최종 WPO Add 연결은 유지했다. M4 Custom 노드만 다음 M5 Custom으로 변경했다.

- Description: `SW Kelvin Wake M5 Golden History WPO`
- Include File Path: `/Project/SWShipWakeHistory.ush`
- Inputs:
  - `WorldPosition`
  - `ShipWakeHeightField`
  - `ShipWakeFieldCenter`
  - `ShipWakeFieldSizeCm`
- Output: `(0, 0, recorded Kelvin height)`

따라서 최종 구조는 계속 다음과 같다.

```text
기존 Gerstner/Ripple WPO + M5 Kelvin recorded WPO
```

M4에서 사용하던 `ShipWakeTex`, `ShipWakeTrajectoryTex`, `ShipWakeAtlas`, `ShipWakeServerTime`, `ShipWakeCount`는 더 이상 Water Custom 노드가 직접 샘플링하지 않는다. Subsystem의 CPU 기록 단계가 이 데이터를 소비한다.

## 6. 기본 CVar

| CVar | 기본값 | 의미 |
|---|---:|---|
| `sw.ShipWake.FieldResolution` | `256` | 3개 기록 필드의 한 변 해상도 |
| `sw.ShipWake.FieldWorldSizeCm` | `80000` | 기록 필드 세계 크기, 800 m |
| `sw.ShipWake.FieldSimulationHz` | `20` | 고정 기록 갱신률 |
| `sw.ShipWake.HistoryResponseRate` | `4.0` | 새 속도 Golden 결과를 받아들이는 속도 |
| `sw.ShipWake.HistoryMomentum` | `0.08` | Previous/Current 이력 관성 |
| `sw.ShipWake.FieldMaximumHeightCm` | `200` | 최종 signed height 안전 clamp |

### 조절 지침

- 전환이 아직 빠르게 떨리면 `HistoryResponseRate`를 `2~3`으로 낮춘다.
- 배를 따라오는 반응이 너무 늦으면 `HistoryResponseRate`를 `5~8`로 높인다.
- 잔상이 너무 짧으면 `HistoryMomentum`을 `0.10~0.20`으로 올린다.
- 재진동이 생기면 `HistoryMomentum`을 먼저 `0`으로 내린다. `0.5` 이상은 코드에서 금지한다.
- 장거리 wake가 잘리면 `FieldWorldSizeCm`을 늘린다. 같은 해상도에서 늘리면 공간 정밀도는 낮아진다.
- 파면이 계단져 보이면 `FieldResolution=512`를 시험할 수 있지만 CPU 평가량과 GPU 업로드량이 약 4배가 된다.

## 7. 변경 파일

- `Source/WaterAndShip/Public/SWShipWakeSubsystem.h`
- `Source/WaterAndShip/Private/SWShipWakeSubsystem.cpp`
- `Shaders/SWShipWakeHistory.ush`
- `Tools/UpgradeRealisticWaterKelvinM5.py`
- `Tools/ValidateRealisticWaterKelvinM5.py`
- `Content/New/Water/Realistic_Water/M_Realistic_Water.uasset`

M3의 `M_SWShipWakeFieldUpdate`와 `SWShipWakeField.ush`는 비교 및 롤백용 artifact로 남겼지만 M5 기본 경로에서는 사용하지 않는다.

## 8. 검증 결과

- `ArtisticSW2026Editor Win64 Development`: 빌드 성공.
- Unreal Python upgrade: `M_Realistic_Water` 저장 성공.
- Unreal Python validation: M5 Custom, include path, 4개 입력 연결 PASS.
- 자동화 테스트 `ArtisticSW.Water.ShipWake.M4AtlasShape`: Success. 이 테스트 이름은 기존 Golden Atlas 회귀 테스트 이름이며 M5도 동일 atlas를 사용한다.

## 9. 플레이 확인 항목

1. 일정 속도 직진에서 V 내부 파면이 안정적으로 보이는지 확인한다.
2. `MinimumEmissionInterval=0.05`, `EmissionDistanceCm=50`에서도 기존 규칙적 점멸이 사라졌는지 확인한다.
3. 가속·감속할 때 이전 파장과 새 파장이 짧은 시간 함께 보인 뒤 자연스럽게 전환되는지 확인한다.
4. 급회전에서 세계공간에 남은 wake가 배 방향으로 즉시 재회전하지 않는지 확인한다.
5. 부력 디버그 포인트의 Kelvin 높이와 눈에 보이는 WPO의 마루/골 위치가 일치하는지 확인한다.

## 10. 현재 한계

- 12장의 Golden slice 범위는 Froude `0.2~2.0`이다. 범위 밖은 가장자리 slice 동작에 제한된다.
- 기록 필드는 배를 중심으로 한 유한 영역이므로 영역 밖의 오래된 wake는 보존되지 않는다.
- CPU authoritative 256² 필드를 20 Hz로 갱신하므로 다수의 동시 wake emitter에서는 프로파일링이 필요하다.
- 이번 단계에서는 Kelvin 기반 FoamMask를 추가하지 않았다.

## 11. M5.1 연속 표시 수정

### 발견된 M5 결함

초기 M5는 세 개 상태를 정상적으로 순환했지만 Water material에는 `Current` 한 장만 전달했다. 기본 20 Hz에서 텍스처가 0.05초마다 순간 교체되어, 기록 이력은 있어도 렌더 프레임 사이의 연속성은 없었다. 또한 80000/256 설정에서는 필드 중심이 312.5 cm 단위로 이동하므로 재투영 오차가 낮은 주기의 움찔거림으로 보일 수 있었다.

`MaximumAmplitudeCm=10000` 테스트에서는 Golden target과 기록 필드가 모두 ±200 cm safety clamp에 포화되어 이 교체 경계가 더 강하게 드러났다. 이는 파고 확대 테스트가 아니라 포화 경계 이동 테스트가 된다.

### 수정 구조

렌더와 CPU 부력이 모두 다음 고정 스텝 보간을 사용한다.

```text
Alpha = FixedStepAccumulator / FixedDeltaTime

VisibleHeight = lerp(
    Sample(Previous, PreviousCenter, WorldPosition),
    Sample(Current,  CurrentCenter,  WorldPosition),
    Alpha)
```

상태 순환 경계는 연속이다.

```text
교체 직전: lerp(Previous, Current, 1) = Current
교체 직후: lerp(OldCurrent, NewCurrent, 0) = OldCurrent
```

각 텍스처를 자신의 세계 중심으로 따로 샘플링하므로 필드 중심이 GridSnap될 때에도 두 상태 사이를 프레임 단위로 혼합한다. CPU 부력의 높이와 gradient도 같은 Previous/Current/Alpha를 사용한다. 이 방식은 한 fixed simulation step의 의도적인 표시 지연을 가지지만 렌더와 부력은 동일하다.

### M5.1 Custom 입력

- Description: `SW Kelvin Wake M5.1 Interpolated Golden History WPO`
- Include: `/Project/SWShipWakeHistory.ush`
- Inputs:
  - `WorldPosition`
  - `ShipWakePreviousHeightField`
  - `ShipWakePreviousFieldCenter`
  - `ShipWakeHeightField`
  - `ShipWakeFieldCenter`
  - `ShipWakeFieldSizeCm`
  - `ShipWakeHistoryAlpha`

### 권장 재테스트

1. `MaximumAmplitudeCm=200`으로 먼저 연속성을 확인한다.
2. `MinimumEmissionInterval=0.05`, `EmissionDistanceCm=50`은 그대로 둔다.
3. `HistoryResponseRate=4`, `HistoryMomentum=0.08` 기본값으로 시작한다.
4. 파면이 아니라 화면 전체 프레임이 0.05초 주기로 멈춘다면 `StepHeightHistory`의 동기 CPU 비용을 별도로 프로파일링한다. 이 경우 다음 최적화 단계는 background field build다.

## 12. M5 런타임 진단

정적 빌드와 머티리얼 연결 검증만으로 진동 원인을 판정할 수 없으므로 다음 CVar를 추가했다.

| CVar | 기본값 | 용도 |
|---|---:|---|
| `sw.ShipWake.Enable` | `1` | 렌더 Kelvin과 CPU 부력을 동시에 0/1 A/B |
| `sw.ShipWake.FreezeHistory` | `0` | 기록 갱신과 alpha 진행을 현재 상태에서 정지 |
| `sw.ShipWake.DebugLog` | `0` | `[M5Runtime]` 주기 로그 활성화 |
| `sw.ShipWake.DebugLogInterval` | `0.5` | 로그 간격(초) |
| `sw.ShipWake.DebugForceHistoryAlpha` | `-1` | `-1` 자동, `0` Previous, `1` Current 강제 |
| `sw.ShipWake.DebugLockFieldCenter` | `0` | source는 갱신하되 필드 중심만 고정 |

### PIE 절차

테스트는 `MaximumAmplitudeCm=200`으로 수행한다. PIE 콘솔에서 먼저 실행한다.

```text
sw.ShipWake.DebugLog 1
sw.ShipWake.DebugLogInterval 0.25
```

배를 직진시키며 다음을 순서대로 각각 5~10초 유지한다.

#### A. Kelvin 완전 제거

```text
sw.ShipWake.Enable 0
```

- 바다가 안정되면 원인은 Kelvin 경로다.
- 계속 진동하면 Gerstner/Ripple 또는 Kelvin 외 material 경로다. `ShipWakeEnable=0`은 최종 Kelvin Custom 출력을 0으로 만들므로 확실한 분리다.

복원:

```text
sw.ShipWake.Enable 1
```

#### B. 완전 정적 필드

```text
sw.ShipWake.FreezeHistory 1
sw.ShipWake.DebugForceHistoryAlpha 0
```

5초 후:

```text
sw.ShipWake.DebugForceHistoryAlpha 1
```

- 두 상태가 각각 정적이면 업데이트/교체/재투영 문제다.
- 정적 상태에서도 바다가 진동하면 다른 WPO와의 결합 또는 좌표 sampling 문제다.

복원:

```text
sw.ShipWake.DebugForceHistoryAlpha -1
sw.ShipWake.FreezeHistory 0
```

#### C. 필드 중심 고정

```text
sw.ShipWake.DebugLockFieldCenter 1
```

- 안정되면 312.5 cm GridSnap/reprojection이 주원인이다.
- 변화가 없으면 기록값 자체 또는 동기 step 비용을 본다.

복원:

```text
sw.ShipWake.DebugLockFieldCenter 0
```

### `[M5Runtime]` 판독

- `FrameMs`: 해당 world의 프레임 시간. 0.05초 주기 spike가 있는지 본다.
- `StepMs`: 256² Golden field 동기 계산 시간. 수 ms 이상이며 FrameMs와 같이 튀면 background build가 필요하다.
- `AlphaAuto/AlphaUsed`: 자동 alpha가 0→1을 반복하는지, 강제 alpha가 적용됐는지 본다.
- `Idx`: Previous/Current/Next texture index 순환.
- `CenterStep`: fixed step에서 중심이 움직인 거리. 기본 GridSnap에서는 312.5 cm 배수다.
- `Amp/Speed/EventAge`: 실제 replicated emitter 값. Amp가 수천이면 BP/placed actor에 극단값이 남아 있다.
- `Target/Output`: Golden target 및 누적 필드 min/max.
- `TargetRMS/OutputRMS`: 전체 필드 에너지 규모.
- `MaxDelta`: 한 fixed step에서 텍셀 하나가 변한 최대 높이.
- `Saturated`: ±`FieldMaximumHeightCm`에 붙은 텍셀 비율. 0보다 지속적으로 크면 amplitude 테스트가 포화됐다.

멀티플레이 PIE에서는 서버와 클라이언트 world가 모두 기록된다. 화면에 보이는 클라이언트는 일반적으로 `NetMode=3`, listen server는 `NetMode=2`다. 로그 파일은 `Saved/Logs/ArtisticSW2026.log`이며 `[M5Runtime]`로 필터링한다.
