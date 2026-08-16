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

## 13. M5.2 네트워크 이벤트 연속성 수정

### 런타임에서 확인한 현상

두 번의 PIE 격리 시험에서 시각 관측은 `정상 → 진동 → 정상 → 정상 → 진동` 순서였다. 로그를 명령 입력 시점별로 분리해 확인한 결과는 다음과 같다.

- `sw.ShipWake.Enable=0`에서는 Kelvin material 기여가 제거되어 진동이 사라졌다.
- `DebugLockFieldCenter=1`일 때 `CenterStep=(0,0)`이었지만 클라이언트 `MaxDelta`는 평균 약 17.74 cm, 최대 약 34.99 cm였다. 필드 중심 GridSnap은 주원인이 아니다.
- 서버에서 활성 이벤트가 있던 대응 샘플 75개 중 서버의 `Events=0`은 0개였지만, 클라이언트 `NetMode=3`은 35개, 약 46.7%가 `Events=0`이었다.
- 같은 구간의 클라이언트 필드는 평균 약 18.39 cm, 최대 약 36.25 cm/fixed-step만큼 변했다.
- `FreezeHistory + Alpha 0/1` 시험은 명령 입력 전에 이미 `Events=0`, `TargetRMS=0`, `OutputRMS≈0`이었으므로 활성 히스토리 보간을 검증한 시험으로 보지 않는다.

### 원인

동일 `EventId`의 multicast 갱신을 받으면 기존 이벤트를 즉시 덮어썼고, 이벤트 활성 조건은 `LocalEstimatedServerTime >= Event.UpdateServerTime`을 요구했다. 클라이언트의 보정된 서버 시계가 수 ms 뒤에 있을 때 새 이벤트는 잠시 미래 상태가 된다. 이때 직전의 유효한 이벤트가 이미 사라졌으므로 active snapshot이 다음처럼 끊겼다.

```text
Current active event
  -> future-dated update immediately replaces Current
  -> ActiveEvents=0
  -> client clock catches up
  -> ActiveEvents=1
```

세 장의 높이 텍스처 자체는 순환하고 있었지만, 기록식의 `GoldenTarget` 입력이 Kelvin과 0 사이를 반복했다. 따라서 세 텍스처 모두 불연속 입력을 추적하며 파면이 펄스처럼 움찔했다.

### 구현

`USWShipWakeSubsystem`에 emitter별 대기 상태를 추가했다.

- 도착한 이벤트가 로컬 추정 서버 시간보다 미래이면 현재 `Events`를 덮어쓰지 않는다.
- `PendingEvents`에는 해당 emitter의 가장 이른 미래 갱신을 유지한다. 더 새로운 패킷으로 대기 시각을 계속 뒤로 미루지 않는다.
- 매 tick 시작 시 `PromotePendingEvents(ServerTime)`를 호출한다.
- 대기 이벤트의 시작 시간이 되면 같은 `EventId`의 현재 상태를 교체한다.
- 오래되거나 순서가 뒤바뀐 갱신은 현재 상태보다 새로울 때만 적용한다.
- current와 pending 모두 기존 retention 정책으로 정리한다.

이 방식은 자주 보내는 unreliable 상태 RPC를 reliable로 바꾸어 큐를 누적시키지 않으면서, 패킷 사이에 직전의 정상 상태를 유지한다. 첫 이벤트가 실제 시작 시각에 도달하기 전의 초기 대기는 허용하지만, 이후 갱신마다 active event가 0이 되는 현상은 제거한다.

### 추가 런타임 로그

`[M5Runtime]`에 다음 필드를 추가했다.

| 필드 | 의미 |
|---|---|
| `Events` | 로그 시각에 실제 활성인 이벤트 수 |
| `StepEvents` | 마지막 fixed field step이 사용한 이벤트 수 |
| `RawEvents` | 활성 여부와 무관한 현재 이벤트 수 |
| `Pending` | 미래 시각이라 현재 상태 뒤에서 대기 중인 이벤트 수 |
| `RawSignedAge` | 현재 이벤트의 signed age. 음수이면 미래 이벤트가 current에 잘못 들어간 것 |
| `PendingLead` | 가장 가까운 pending 이벤트까지 남은 초. 양수이면 정상적으로 대기 중 |

`Events`와 `StepEvents`는 서로 다른 시각을 측정한다. 전자는 로그 시각 snapshot이고 후자는 마지막 20 Hz step 통계이므로 한 줄에서 잠시 다를 수 있다.

### M5.2 재검증 기준

`MaximumAmplitudeCm=200`, `DebugLogInterval=0.05~0.25`로 클라이언트 `NetMode=3`을 확인한다.

1. 배가 계속 움직이는 동안 `RawEvents=1`이면 `Events=1`이 연속적으로 유지되어야 한다.
2. 시계가 뒤처진 갱신을 받으면 `Pending=1`, `PendingLead>0`이 잠시 나타날 수 있다.
3. 이때 `Events`는 0으로 내려가면 안 된다. 기존 current가 계속 사용되어야 한다.
4. `RawSignedAge`가 음수가 되면 미래 이벤트가 current로 승격된 결함이다.
5. 네트워크 단절이나 배 정지로 `StateLifetime`이 실제 만료된 경우의 `Events=0`은 정상이다.
6. 이벤트 연속성이 확보된 뒤에도 진동하면 활성 파면을 유지한 상태에서 `FreezeHistory` 및 Alpha 0/1 시험을 다시 수행하여 field step/texture interpolation을 다음 원인으로 분리한다.

### 구현 검증

- 2026-08-16 `ArtisticSW2026Editor Win64 Development` 전체 빌드 성공.
- `SWShipWakeSubsystem.cpp`는 adaptive non-unity 대상으로 직접 컴파일되었다.
- 자동화 테스트 `ArtisticSW.Water.ShipWake.M4AtlasShape`: `Success` (1/1).
- 빌드 중 출력된 경고는 UE 5.7의 기존 NetworkPhysics/Chaos deprecated API 및 비권장 MSVC 버전 경고이며 M5.2 변경으로 추가된 컴파일 경고나 오류는 없다.
- 네트워크 시계 편차와 multicast 순서는 실제 PIE world가 필요한 동적 조건이므로 최종 판정은 위의 `Pending/RawSignedAge/Events` 런타임 로그로 수행한다.

## 14. M5.3 고정 스텝 선박 상태 타임라인

> M5.2의 `Current + Pending`은 클라이언트의 `Events=0`을 제거한 중간 수정이다. M5.3은 해당 구조를 여러 불변 샘플을 가진 시간축으로 대체한다. 따라서 M5.2의 `Pending`, `RawSignedAge`, `PendingLead` 판독법은 과거 로그 분석용이며 현재 런타임 필드는 아래 표를 사용한다.

### M5.2 이후 런타임 증거

M5.2 재시험에서 현재 시각의 이벤트 연속성은 확보되었다.

- 서버 66행과 클라이언트 64행 모두 `Events=0`은 0회였다.
- 클라이언트는 64행 중 35행에서 `Pending=1`이었지만 기존 `Events=1`을 유지했다.

그러나 서버 66행 중 13행에서 `Events=1`과 동시에 `StepEvents=0`, `TargetRMS=0`이 기록됐다. emitter가 현재 서버 시각으로 같은 `EventId`를 덮어쓴 직후, 높이 필드는 accumulator 때문에 그보다 과거인 fixed-step 시각을 평가했다. 새 상태는 fixed-step 기준 미래였고 이전 상태는 이미 삭제되어 target이 0이 됐다.

클라이언트는 active event가 끊기지 않았지만 future pending을 승격할 때 원점, 속도, 파장, Froude slice, 진폭, trajectory가 한 번에 바뀌었다. 전체 256² Golden target을 이 새 상태로 다시 평가했기 때문에 평균 약 33.55 cm, 최대 약 64.04 cm/fixed-step의 변화가 발생했다.

`MaximumAmplitudeCm=300` 시험에서 실제 이벤트 진폭은 Froude 배율 1.5로 최대 450 cm였다. 클라이언트 64행 중 62행에서 Golden target이 `-200/+200 cm` 양쪽 clamp에 도달했다. 큰 진폭은 원인을 만들지는 않지만 시간축 불연속을 매우 강하게 드러냈다.

### Ripple과의 차이에서 가져온 원칙

Ripple은 충돌마다 고유하고 불변인 이벤트를 추가하고, shader/CPU evaluator가 연속 ServerTime으로 파면을 계산한다. Kelvin은 배 하나가 같은 이벤트를 계속 덮어썼다. M5.3은 Kelvin의 연속 선박 상태를 Ripple의 불변 이벤트 원칙에 맞춰 다음과 같이 저장한다.

```text
EventId = vessel identity

Samples[EventId] =
    S0(time, origin, speed, trajectory, spectrum...)
    S1(time, origin, speed, trajectory, spectrum...)
    S2(time, origin, speed, trajectory, spectrum...)
    ...
```

같은 `EventId`와 같은 timestamp만 중복 갱신으로 교체한다. 시간이 다른 갱신은 서버와 클라이언트 모두 immutable sample로 추가한다. 전체 샘플 큐는 `WakeCapacity * 8 = 512`로 제한하고, 기존 `StateLifetime + PhysicsHistoryRetentionSeconds`가 지난 샘플을 제거한다.

### 고정 스텝 평가

새 CVar 기본값은 다음과 같다.

```text
sw.ShipWake.StateInterpolationDelay 0.10
sw.ShipWake.HistoryMomentum 0
```

고정 스텝과 legacy descriptor texture/material time은 현재 시각보다 `StateInterpolationDelay`만큼 뒤의 동일한 평가 시각을 사용한다. delay는 최소 한 fixed-step 이상으로 제한한다.

```text
EvaluationTime = ServerTime
               - max(StateInterpolationDelay, FixedDeltaTime)
               - AccumulatorRemainder

Previous = newest sample where SampleTime <= EvaluationTime
Next     = oldest sample where SampleTime >  EvaluationTime
Alpha    = (EvaluationTime - Previous.Time) / (Next.Time - Previous.Time)

ResolvedState = lerp(Previous, Next, Alpha)
```

보간 대상은 다음과 같다.

- apex origin과 forward
- amplitude와 spectrum speed
- advection speed와 pressure size
- longitudinal/lateral scale
- hull/beam/draft/wake length
- transverse/divergent/stern strength와 stern phase
- 두 샘플의 trajectory point 수가 같으면 각 world-space point

보간된 상태의 timestamp는 `EvaluationTime`으로 설정하므로 evaluator에서 age가 음수가 되지 않는다. 다음 샘플이 아직 없으면 마지막 과거 샘플을 기존 advection prediction과 lifetime/freshness로 평가한다. unreliable packet이 하나 누락되어도 이전 샘플은 삭제되지 않는다.

`HistoryMomentum` 기본값은 `0.08`에서 `0`으로 변경했다. 시간축이 연속해진 뒤에도 두 이전 필드의 차이를 추가하는 momentum은 파고가 큰 상태에서 불필요한 링잉을 만들 수 있기 때문이다. 필요하면 연속성 검증 이후 소량만 다시 시험한다.

### 렌더와 부력 일치

M5.3도 최종 Golden target을 CPU authoritative 3중 signed-height field에 기록한다. 물 material은 Previous/Current field와 alpha를 샘플링하고, game-thread 및 async buoyancy query는 같은 CPU field와 같은 alpha를 샘플링한다. 상태 보간을 GPU shader에만 추가하지 않았으므로 시각과 부력의 파면 정의는 갈라지지 않는다.

### M5.3 런타임 로그

기존 `[M5Runtime]`의 네트워크 pending 필드를 시간축 필드로 교체했다.

| 필드 | 의미 |
|---|---|
| `EvalLagMs` | 현재 서버 시각과 마지막 field evaluation 시각의 차이. 기본값은 약 100~150 ms |
| `Events` | 마지막 평가 시각에서 해석된 emitter 수 |
| `StepEvents` | 마지막 fixed-step이 실제 사용한 emitter 수 |
| `TimelineSamples` | retention을 포함해 저장된 불변 선박 상태 샘플 수 |
| `FutureSamples` | 마지막 평가 시각보다 뒤에 있어 보간의 Next 후보가 되는 샘플 수 |
| `NewestSampleAge` | 평가 시각 - 가장 새 raw sample 시각. 보간 버퍼가 있으면 음수여도 정상 |

### 합격 조건

`MaximumAmplitudeCm=200`부터 시험한다. `300`이면 실제 event amplitude가 최대 450 cm까지 갈 수 있다.

```text
sw.ShipWake.DebugLog 1
sw.ShipWake.DebugLogInterval 0.1
sw.ShipWake.StateInterpolationDelay 0.10
sw.ShipWake.HistoryMomentum 0
```

1. 지속 직진 중 서버와 클라이언트 모두 `Events=1`, `StepEvents=1`이 유지되어야 한다.
2. `TimelineSamples`는 증가한 뒤 retention window에 맞춰 제한되어야 한다.
3. `FutureSamples>=1`이 자주 나타나는 것은 정상이며 이것이 Previous/Next 보간 버퍼다.
4. 서버가 `Events=1`인데 `StepEvents=0`, `TargetRMS=0`으로 내려가는 과거 패턴은 없어야 한다.
5. 서버와 클라이언트의 `TargetRMS/OutputRMS` 추세가 크게 갈라지지 않아야 한다.
6. 이벤트 연속성은 정상인데도 파면이 흔들리면 다음 분리는 전체 target 재평가와 field 기록 필터다. 그 단계에서는 Golden target의 이동 차분 주입 또는 세계공간 source-segment deposition을 검토한다.

### M5.3 정적 검증

- `ArtisticSW2026Editor Win64 Development` 전체 빌드 성공.
- adaptive non-unity 대상으로 `SWShipWakeSubsystem.cpp` 직접 컴파일 성공.
- 자동화 테스트 `ArtisticSW.Water.ShipWake.M4AtlasShape`: Success.
- 신규 자동화 테스트 `ArtisticSW.Water.ShipWake.M5TimelineInterpolation`: Success.
- 신규 테스트는 두 선박 상태 중간 시각에서 origin, forward, amplitude, spectrum/advection speed, timestamp 및 첫 trajectory point가 연속 보간되는지 검증한다. 또한 세 개의 같은-vessel 샘플 사이 두 fixed-step 시각에서 각각 정확히 하나의 활성 상태가 해석되어 과거의 `StepEvents=0` 공백이 재발하지 않는지 검증한다.
- 실제 네트워크 패킷 순서와 플레이 화면의 진동 제거 여부는 PIE의 M5.3 로그 합격 조건으로 최종 검증한다.
