# Ship Foam / Kelvin Wake M4 구현 기록

작성일: 2026-08-16  
대상: `C:\Unreal Projects\ArtisticSW2026`  
범위: M4-A, M4-B 구현. M4-C Foam 통합은 제외.

## 1. 결과 요약

Python Bake의 Golden Image payload를 M4 production wake의 원본으로 채택했다. M3의 GPU 유한차분장과 CPU 16방향 근사를 기본 경로에서 중단하고, GPU WPO와 CPU/Async Physics 부력이 동일한 FP16 signed-height atlas를 샘플하도록 교체했다.

```text
Python 논문식 bake
  -> kelvin_wake_atlas_fp16.bin
       -> CPU bilinear sampler -> Water query / Async Physics buoyancy
       -> transient R16F GPU atlas -> M_Realistic_Water WPO
```

M4-B에는 하나의 Kelvin apex, 방향 yaw, pressure size와 최대 16점 trajectory를 추가했다. 직진에서는 Golden Image의 하나의 V를 유지하고, 회전에서는 query를 최근 항적 polyline의 arc-length/lateral 좌표로 변환한다. M2의 Bow V + Stern V 두 소스는 더 이상 사용하지 않는다.

## 2. Python Bake 포맷 평가

### 채택한 데이터

- Baker: `M4A_1.0_Darmon2013`
- payload SHA256: `7b85a7f719cc6697e15acd5cabe52b45688888784241f6b2c8604debe9863066`
- signed R16F raw payload: 3,145,728 bytes
- slice: 12개
- Froude: `0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 1.0, 1.25, 1.5, 1.75, 2.0`
- downstream domain: `0..10 lambda`, 512 samples
- lateral domain: `-3..3 lambda`, 256 samples
- raw layout: `[slice][downstream U][lateral V]`, V가 연속

이 포맷은 signed height를 보존하고, GPU와 CPU가 같은 FP16 양자화 값을 읽을 수 있으므로 M4에 적합하다. PNG preview는 검안용이고 런타임 데이터로 사용하지 않는다.

### Unreal에서의 packing

Texture2DArray uasset을 새로 import하는 대신 raw 순서를 유지한 `256 x 6144` `PF_R16F` transient Texture2D로 해석한다.

```text
texture X = lateral V (256)
texture Y = slice * 512 + downstream U
```

필터는 nearest로 두고 HLSL에서 네 texel을 직접 bilinear 보간한다. 이로써 slice 경계에서 이웃 slice가 섞이지 않으며 C++ sampler와 같은 interpolation 규칙을 사용한다.

### 필요한 보정

raw 적분값의 peak는 Fr=0.5에서 약 1.387, Fr=2.0에서 약 35.433으로 크게 달라진다. raw를 그대로 `KelvinAmplitudeCm`에 곱하면 속도 변화에 따라 파고가 폭주한다. 현재 구현은 각 Froude slice의 abs peak로 먼저 정규화한 뒤 slice interpolation한다. 따라서 `MaximumAmplitudeCm`가 런타임 파고의 직접적인 미술 단위가 된다.

Fr=0.2 slice는 FP16 최소 표현 한계보다 작아 사실상 0이다. Fr 0.2 이하에서는 wake를 끄고 0.2~0.3에서 smooth startup fade를 적용했다.

## 3. M4-A 구현

### Atlas load 및 CPU sampler

- `SWKelvinWakeAtlas.h/.cpp`
- 프로젝트 payload: `Content/New/Water/Realistic_Water/Kelvin/kelvin_wake_atlas_fp16.bin`
- metadata도 같은 폴더에 보존
- packaged build에서는 해당 폴더를 NonUFS로 staging

CPU 좌표식:

```text
Fr = SmoothedSpeed / sqrt(980 * PressureSizeCm)
lambda_cm = 2 pi Speed^2 / 980
u = downstream_cm / (lambda_cm * LongitudinalScale)
v = lateral_cm / (lambda_cm * LateralScale)
height_cm = MaximumAmplitudeCm * SampleNormalized(u, v, Fr)
```

CPU는 인접 U/V 네 texel과 인접 Froude 두 slice를 보간한다. gradient는 동일 height evaluator의 중앙차분이다.

### GPU HLSL

`Shaders/SWShipWake.ush`를 M4 atlas evaluator로 교체했다. Custom node는 다음 여섯 입력만 가진다.

```text
WorldPosition
ShipWakeTex
ShipWakeTrajectoryTex
ShipWakeAtlas
ShipWakeServerTime
ShipWakeCount
```

이전 M3의 `HeightScale`과 `Enable`은 GPU에만 적용되어 부력과 파고를 어긋나게 하므로 M4 Custom 입력에서 제거했다. 파고는 Emitter의 `MaximumAmplitudeCm`에서만 조정한다.

`M_Realistic_Water`의 기존 최종 WPO Add 구조는 유지했다. 즉 기존 Gerstner/Ripple WPO에 M4 Kelvin Z가 더해진다. M3 persistent height field update는 Tick 기본 경로에서 중단했으며 관련 material/코드는 비교용 artifact로 남아 있다.

### Gameplay와 Async Physics

기존 합산 지점은 유지하고 `FSWShipWakeEvaluator` 내부만 atlas sampler로 교체했다.

```text
USWRippleWaterWaves:
  Gerstner + Ripple + M4 Kelvin height/gradient

FShipPhysicsAsync:
  Gerstner + Ripple + 동일 M4 Kelvin height
```

atlas payload는 subsystem 초기화 시 한 번 읽은 immutable 메모리다. Async Physics에는 UObject texture를 전달하지 않고 event snapshot만 전달하며, 물리 스레드는 같은 FP16 payload를 읽는다.

## 4. M4-B 구현

`SWShipWakeEmitterComponent`에 다음 값을 추가했다.

| 값 | 의미 | 기본값 |
|---|---|---:|
| `KelvinApexLocalOffset` | 배 local space의 단일 V 꼭짓점 | `(1500, 0, 0) cm` |
| `KelvinDirectionYawDegrees` | 배 forward 기준 wake 방향 보정 | `0 deg` |
| `PressureSizeCm` | Froude 계산의 Gaussian pressure size b | `2400 cm` |
| `LongitudinalScale` | downstream atlas scale | `1` |
| `LateralScale` | lateral atlas scale | `1` |
| `NearHullSuppressDistanceCm` | apex 주변 선택적 제거 반경 | `0 cm` |
| `TrajectorySampleDistanceCm` | 과거 경로 anchor 간격 | `500 cm` |

`KelvinApexLocalOffset`에는 `MakeEditWidget`을 지정해 Details/Viewport에서 메시 선수 위치에 맞출 수 있게 했다. 기본 1500 cm는 M3에서 계산하던 `Owner - SternOffset(900) + HullLength(2400)`의 bow 위치와 같다.

trajectory는 newest-to-oldest 최대 16점이다. 각 query는 가장 가까운 segment를 찾고 다음 좌표를 만든다.

```text
downstream = current apex부터 closest point까지 누적 arc length
lateral = closest segment의 right 방향 signed distance
```

고정 16점으로 10 lambda 전체를 저장할 수 없으므로 가장 오래된 segment tangent를 뒤로 연장한다. 최근 회전은 실제 path를 따르고, 먼 후류는 마지막 과거 방향으로 자연스럽게 계속된다.

## 5. 변경한 에셋과 파일

### 실제 에셋

- `/Game/New/Water/Realistic_Water/M_Realistic_Water`
  - Custom node: `SW Kelvin Wake M4 Golden Atlas WPO`
  - include: `/Project/SWShipWake.ush`
  - 6개 입력 연결
- `/Game/Tests/Landscape/Kelvin/BP_PlayerShip_Kelvin`
  - M4 Emitter 기본값 저장

`Realistic_Water.umap`은 이번 자동화에서 열거나 저장하지 않았다. 작업 전부터 존재한 변경을 보존했다.

### 코드 및 도구

- `Source/WaterAndShip/Public/SWKelvinWakeAtlas.h`
- `Source/WaterAndShip/Private/SWKelvinWakeAtlas.cpp`
- `SWShipWakeTypes`, `SWShipWakeEmitterComponent`, `SWShipWakeSubsystem`
- `Shaders/SWShipWake.ush`
- `Tools/UpgradeRealisticWaterKelvinM4.py`
- `Tools/ValidateRealisticWaterKelvinM4.py`
- `Source/WaterAndShip/Private/Tests/SWShipWakeTests.cpp`

## 6. 검증 결과

### Python bake 원본 검증

제공된 baker 결과에 기록된 검증:

- quadrature max error: 약 `1.27e-7`
- Fr=0.5 dominant wavelength: 약 `1.0027 lambda`
- 외부/내부 decay ratio: 약 `0.0129`
- FP16 max relative error: 약 `0.0389%`

### Unreal 검증

- `ArtisticSW2026Editor Win64 Development`: 빌드 성공
- atlas runtime load: `256 x 6144`, 3,145,728 bytes 성공
- `M_Realistic_Water` Custom 입력/include validator: PASS, 6 inputs
- material shader recompile: error 0
- automation: `ArtisticSW.Water.ShipWake.M4AtlasShape` PASS
  - signed displacement 존재
  - apex 앞 0
  - atlas lateral domain 밖 0
  - expired state 0
  - gradient finite

## 7. 인게임 조정 순서

1. `KelvinApexLocalOffset`을 실제 배 메시의 선수 waterline 위치에 맞춘다.
2. `KelvinDirectionYawDegrees`로 메시 forward 축 오차를 보정한다.
3. 목표 속도에서 `PressureSizeCm`를 조정해 원하는 Froude 모양을 선택한다.
4. 파고는 `MaximumAmplitudeCm`로 조정한다.
5. 물리 비율을 유지하려면 `LongitudinalScale/LateralScale=1`을 유지한다. Golden Image와 다르게 보일 때만 미술 보정으로 변경한다.
6. 급회전 후 경로가 각져 보이면 `TrajectorySampleDistanceCm`를 줄인다. 네트워크 payload와 GPU segment 비용은 늘지 않지만 anchor 갱신 빈도가 올라간다.

참고로 목표 Fr=0.5를 특정 속도에서 맞추려면 다음으로 pressure size를 역산할 수 있다.

```text
PressureSizeCm = SpeedCmPerSecond^2 / (980 * 0.5^2)
```

예를 들어 1200 cm/s에서는 약 5878 cm다.

## 8. M4-C로 남긴 내용

Foam 통합은 요청에 따라 구현하지 않았다. M4-C에서는 현재 최종 WPO Add 이후가 아니라, Kelvin height/gradient를 기존 FoamMask 판정 이전의 total wave 결과에 넣어야 한다.

또한 현재 GPU Custom은 height WPO를 공통 atlas에서 읽지만 기존 water normal graph에는 Kelvin gradient를 아직 주입하지 않는다. CPU water normal에는 이미 Kelvin gradient가 들어간다. M4-C에서 Foam slope와 같은 gradient가 필요하므로 GPU normal 및 FoamMask 입력을 한 번에 재배선하는 것이 안전하다.

## 9. 알려진 제한

- atlas domain은 downstream 10 lambda, lateral ±3 lambda로 유한하다.
- 12개 Froude slice 사이를 선형 보간한다.
- Fr별 peak 정규화는 Golden shape를 유지하지만 논문 raw 진폭의 Fr 의존성은 보존하지 않는다.
- trajectory GPU position texture는 float32이므로 매우 큰 LWC 좌표에서는 CPU double path와 미세한 오차가 날 수 있다.
- 다중 선박은 최대 64 event를 지원하지만 GPU 비용은 `event 수 x 최대 15 trajectory segment x atlas samples`로 증가한다.
- M3 field 관련 `sw.ShipWake.Field*` CVar는 M4 기본 Tick 경로에서는 더 이상 효과가 없다.
