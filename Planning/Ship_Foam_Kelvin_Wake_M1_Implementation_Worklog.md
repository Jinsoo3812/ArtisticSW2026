# Ship_Foam / Kelvin Wake M1 구현 작업 기록

작성일: 2026-08-16  
대상: Unreal Engine 5.7 / `ArtisticSW2026`  
테스트 레벨: `/Game/Tests/Landscape/Test_Level_Design`  
안전 복제 폴더: `/Game/Tests/Landscape/Kelvin`

## 1. 이번 M1의 결론

이번 구현에서는 이전 설계의 “GPU compute height field”보다 **RippleSubsystem과 같은 authoritative event evaluator 구조**를 먼저 선택했다.

이유는 요구사항이 “화면에 보이는 Kelvin Wake가 실제 물 높이를 바꾸고 모든 부력 사용자에게 영향을 주어야 한다”로 확정됐기 때문이다. GPU compute texture만 물 높이로 사용하면 CPU/Async Physics가 그 결과를 즉시 읽을 수 없고, GPU readback은 지연과 stall 때문에 부력의 기준으로 사용할 수 없다.

따라서 M1은 다음처럼 구현됐다.

```text
Kelvin 선박 이동
  -> 서버 USWShipWakeEmitterComponent가 wake event 생성
  -> USWShipWakeSubsystem의 thread-safe event cache
     ├─ C++ FSWShipWakeEvaluator
     │    ├─ USWRippleWaterWaves GT water query
     │    └─ ShipPhysicsAsync PT snapshot/evaluator
     └─ 64 x 3 float parameter texture
          └─ SWShipWake.ush Custom node
               ├─ Water WPO
               └─ Foam emissive/roughness mask
```

C++과 HLSL은 같은 event 필드, wedge mask, divergent/transverse 성분, phase, envelope, 감쇠식을 사용한다. 따라서 M1에서 보이는 WPO와 CPU 부력 높이의 정의가 동일하다.

Godot식 compute propagation은 폐기한 것이 아니라 M2 이후의 시각 최적화/지속 foam field 후보로 남겼다. 향후 compute field를 추가하더라도 authoritative height는 이 CPU/HLSL 공통 event 모델이 담당한다.

## 2. 생성한 안전 복제 에셋

원본 Landscape 에셋과 레벨은 수정하지 않았다. 다음 네 에셋만 새 폴더에 만들었다.

| 에셋 | 원본 | 용도 |
|---|---|---|
| `M_Kelvin_RealisticWater` | `Shore_M_RealisticWater_GodotInspired` | Kelvin Custom 노드를 삽입한 master material |
| `MI_Kelvin_RealisticWater_Ocean` | `Shore_M_RealisticWater_GodotInspired_Ocean` | 복제 master를 parent로 사용하는 Ocean MI |
| `Kelvin_Waves_RealisticWater` | `Shore_Waves_RealisticWater` | 기존 Gerstner base wave의 안전 복제본 |
| `BP_PlayerShip_Kelvin` | `/Game/New/Ship/Blueprints/BP_PlayerShip` | `AKelvinShip`으로 reparent하여 native wake emitter를 상속한 테스트 선박 |

레벨 `Test_Level_Design.umap`은 복제하거나 저장하지 않았다.

에셋 생성 스크립트:

- `Tools/CreateKelvinM1Assets.py`
- `Tools/ValidateKelvinM1Assets.py`

생성 스크립트는 원본을 duplicate한 뒤 복제 Material Instance의 parent만 복제 master로 변경하고, 복제 Blueprint만 `AKelvinShip`으로 reparent한다.

## 3. 새 런타임 코드

### 3.1 Wake event와 공통 평가기

파일:

- `Source/WaterAndShip/Public/SWShipWakeTypes.h`
- `Source/WaterAndShip/Private/SWShipWakeTypes.cpp`

`FSWShipWakeEvent`가 보관하는 값:

- EventId
- 발생 월드 XY
- 발생 당시 선박 Forward XY
- 서버 시작 시각
- 초기 진폭
- 파장
- 전파/위상 속도
- 수명
- Kelvin half angle

`FSWShipWakeEvaluator`는 다음을 평가한다.

- 선박 앞쪽 제외
- 진행 방향 뒤의 Kelvin wedge 제한
- 약 19.47도 부근의 divergent 성분
- 중심선 부근의 transverse 성분
- 파면 envelope
- 시간/거리 감쇠
- 중첩 높이를 ±100 cm로 제한
- 유한차분 gradient를 통한 GT water normal 보정

이것은 Havelock 적분을 실시간으로 푸는 true Kelvin solver는 아니다. M1용 결정론적 Kelvin-like 근사이며, Froude/Kelvin atlas 보정은 다음 단계다.

### 3.2 Wake subsystem

파일:

- `Source/WaterAndShip/Public/SWShipWakeSubsystem.h`
- `Source/WaterAndShip/Private/SWShipWakeSubsystem.cpp`

역할:

- world별 thread-safe event cache
- 최대 64개의 최근 wake packet 유지
- 만료 후 2초간 Async Physics rollback용 기록 보존
- `GetWakeHeight`와 `GetWakeGradient` 제공
- 클라이언트용 64 x 3 `PF_A32B32G32R32F` event texture 생성
- WaterBody MID에 다음 파라미터 전달

```text
ShipWakeTex
ShipWakeCount
ShipWakeServerTime
```

Dedicated server에서는 texture를 만들지 않고 CPU event cache와 높이 평가만 유지한다.

### 3.3 선박 emitter와 Kelvin 선박 클래스

파일:

- `Source/WaterAndShip/Public/SWShipWakeEmitterComponent.h`
- `Source/WaterAndShip/Private/SWShipWakeEmitterComponent.cpp`
- `Source/WaterAndShip/Public/KelvinShip.h`
- `Source/WaterAndShip/Private/KelvinShip.cpp`

`AKelvinShip`은 `AShip`의 얇은 subclass이며 생성자에서 `USWShipWakeEmitterComponent`를 기본 subobject로 만든다. 복제한 `BP_PlayerShip_Kelvin`이 이 클래스를 parent로 사용하므로 원본 Blueprint 구성은 유지하면서 inherited component가 추가된다.

기본 emitter 값:

| 값 | 기본값 |
|---|---:|
| Hull length | 2400 cm |
| Stern offset | 900 cm |
| 최소 발생 간격 | 0.12 s |
| 발생 거리 | 180 cm |
| 최소 속도 | 250 cm/s |
| 최대 진폭 | 42 cm |
| 수명 | 12 s |
| Kelvin half angle | 19.47 deg |

서버만 event를 생성하며 event는 현재 Reliable NetMulticast로 전달된다. Standalone/Listen/일반 client의 M1 테스트에서는 packet 누락 없이 같은 event를 받는다. Late join history와 대규모 다중 선박 대역폭은 아직 Fast Array가 아니므로 M2 네트워크 보강 항목이다.

## 4. 부력 통합

### 4.1 일반 Water Waves query

수정 파일:

- `Source/WaterAndShip/Private/SWRippleWaterWaves.cpp`

기존 합성:

```text
Base Gerstner height + Ripple height
```

M1 합성:

```text
Base Gerstner height + Ripple height + Ship Wake height
```

`GetWaveHeightAtPosition`과 `GetSimpleWaveHeightAtPosition` 모두 Ship Wake를 더한다. 따라서 현재 `USWRippleWaterWaves` wrapper를 통해 수면을 쿼리하는 수영, 부력 컴포넌트, 발사체 등의 사용자는 wake 높이 영향을 받는다.

### 4.2 AShip Async Network Physics

수정 파일:

- `Source/WaterAndShip/Public/ShipPhysicsAsync.h`
- `Source/WaterAndShip/Private/Ship.cpp`
- `Source/WaterAndShip/Private/ShipPhysicsAsync.cpp`

Game Thread가 `USWShipWakeSubsystem`의 event snapshot을 `FAsyncInputShip`에 복사하고 Physics Thread는 `CachedShipWakeEvents`로 보관한다. pontoon 높이 평가 시 Gerstner와 Ripple 다음에 동일한 `FSWShipWakeEvaluator` 결과를 더한다.

따라서 물리 스레드는 UObject, subsystem, GPU texture를 직접 접근하지 않으며 rollback 시에도 해당 프레임의 snapshot과 simulation time을 사용한다.

## 5. Custom node와 USH

공통 셰이더 파일:

- `Shaders/SWShipWake.ush`

복제 master material에는 Python으로 다음 노드를 추가했다.

### Height Custom node

입력:

```text
WorldPosition
ShipWakeTex
ShipWakeServerTime
ShipWakeCount
HeightScale
Enable
```

코드의 역할:

```hlsl
// Include File Paths: /Project/SWShipWake.ush
float Height;
float Foam;
SW_EVALUATE_SHIP_WAKE(..., Height, Foam);
return float3(0, 0, Height * HeightScale * saturate(Enable));
```

반환값은 기존 WPO와 Add한 뒤 최종 `SetMaterialAttributes`의 `World Position Offset`에 연결했다.

### Foam Custom node

입력:

```text
WorldPosition
ShipWakeTex
ShipWakeServerTime
ShipWakeCount
Enable
```

crest와 divergent/transverse energy에서 foam mask를 만들며 다음에 연결했다.

- 기존 Emissive + `ShipWakeFoamColor * ShipWakeFoamIntensity * FoamMask`
- 기존 Roughness에서 1.0으로 FoamMask Lerp

기존 material graph의 Gerstner, RippleTex, FoamRT, normal, refraction, underwater 경로는 제거하지 않았다. 최종 Material Attributes 직전에 WPO, Roughness, Emissive만 끼워 넣어 노드 변경 범위를 최소화했다.

현재 foam은 별도 advected history texture가 아니라 활성 wake event에서 매 프레임 재계산되는 mask다. 지속/이류 foam field는 M2 후보다.

## 6. 레벨에서 테스트하는 방법

`Test_Level_Design`을 열고 다음 세 가지만 바꾼다.

1. `WaterBodyOcean_0`의 Water Material을 다음으로 변경한다.

   ```text
   /Game/Tests/Landscape/Kelvin/MI_Kelvin_RealisticWater_Ocean
   ```

2. 현재 Water Waves가 사용하는 `SWRippleWaterWaves` wrapper는 유지한다. 그 안의 `BaseWavesAsset`을 다음으로 바꾼다.

   ```text
   /Game/Tests/Landscape/Kelvin/Kelvin_Waves_RealisticWater
   ```

   wrapper 자체를 일반 Gerstner object로 교체하면 Ripple/Ship Wake CPU query 경로를 우회할 수 있으므로 `BaseWavesAsset`만 교체한다.

3. 기존 `BP_PlayerShip`을 레벨에서 제거하고 같은 transform에 다음 Blueprint를 배치한다.

   ```text
   /Game/Tests/Landscape/Kelvin/BP_PlayerShip_Kelvin
   ```

   기존 선박의 Auto Possess, instance override가 있었다면 새 선박에도 같은 값을 확인한다.

처음에는 Standalone PIE로 테스트한다. 수평 속도 250 cm/s 아래에서는 wake가 발생하지 않는다. 테스트할 항목:

- 직진 시 뒤쪽 약 19.47도 양쪽 divergent wave
- 중심선 transverse/stern 성분
- 정지 후 약 12초 감쇠
- 회전 전 wake가 현재 선박과 함께 회전하지 않고 발생 당시 월드 방향을 유지하는지
- 다른 Kelvin/일반 부력 선박 또는 물체가 wake를 지날 때 실제 높이 영향을 받는지
- `ShipWakeHeightScale`, `ShipWakeFoamIntensity`, `ShipWakeFoamColor`, `ShipWakeEnable` MI 파라미터 조정

## 7. 검증 결과

### 성공

- `ArtisticSW2026Editor Win64 Development`: 빌드 성공
- Unreal Automation: `ArtisticSW.Water.ShipWake.EvaluatorShape` 1/1 성공
  - divergent arm 높이 존재
  - 선박 앞쪽 0
  - wedge 바깥 0
  - 만료 후 0
  - gradient 유한값
- Editor Python asset validation 성공
  - `BP_PlayerShip_Kelvin_C`
  - inherited `SWShipWakeEmitterComponent`
  - Kelvin material expression 385개
  - 최종 WPO = Add, Roughness = Lerp, Emissive = Add 연결 확인
  - Kelvin MI parent가 Kelvin master임을 확인
- 2026-08-16 실제 적용 대상 `/Game/New/Water/Realistic_Water/M_Realistic_Water` 재컴파일에서 Kelvin/USH shader error가 기록되지 않음
- 원본 Landscape 에셋 및 `Test_Level_Design.umap`은 git 수정 목록에 없음

### 2026-08-16 실제 적용 경로 전환 및 셰이더 수정

최종 적용 대상이 `/Game/New/Water/Realistic_Water`로 정정되어 다음 구성을 검사했다.

- `Realistic_Water` 레벨의 `WaterBodyOcean_0`
- Water Material: `M_Realistic_Water_Ocean`
- Water Waves wrapper: inline `SWRippleWaterWaves`
- Base Waves Asset: `Waves_Realistic_Water`
- 선박: `BP_PlayerShip_Kelvin`

`M_Realistic_Water`의 최종 연결은 다음과 같이 확인했다.

```text
BreakMaterialAttributes.WorldPositionOffset
    + SWKelvinWakeWPO(float3)
    -> SetMaterialAttributes.World Position Offset
```

따라서 Add 연결과 base waves asset은 원인이 아니었다. 실제 원인은 Custom 노드의 `Include File Paths` 동작 방식이었다. Unreal은 include 파일을 생성된 `CustomExpression` 함수 몸체 안에서 펼친다. 기존 `SWShipWake.ush`가 그 위치에서 전역 HLSL 함수를 다시 정의하여 다음 컴파일 오류가 발생했다.

```text
SWShipWake.ush: function definition is not allowed here
M_Realistic_Water: Failed to compile Material, Default Material will be used in game
```

해결:

- `SWShipWake.ush`를 전역 함수 선언이 없는 macro-only evaluator로 변경
- WPO Custom 노드가 `SW_EVALUATE_SHIP_WAKE(...)`를 호출하도록 변경
- 미사용 중인 Foam Custom 노드도 같은 evaluator를 호출하도록 함께 정정
- Include File Paths는 계속 `/Project/SWShipWake.ush` 사용
- `ShipWakeHeightScale=1`, `ShipWakeEnable=1`과 모든 Custom 입력 연결 확인
- 머티리얼 재컴파일 및 저장 후 `SWShipWake.ush`/`M_Realistic_Water` 컴파일 오류 0건 확인

즉, 사용자가 최종 WPO에 Add한 방식은 맞으며, 이 수정 이후부터 실제 runtime event/texture 경로를 시각적으로 검증할 수 있다.

### 환경/기존 프로젝트 차단 사항

- `ArtisticSW2026Server` 빌드: 설치된 Epic Launcher UE 5.7 배포판이 Server target을 지원하지 않아 시작 단계에서 중단됨. Kelvin 코드 오류가 아님.
- `ArtisticSW2026` Development Game 전체 빌드: Kelvin/WaterAndShip 파일은 컴파일됐으나 기존 `Enemy`와 `NPCDialogue` 모듈의 non-editor `FDataValidationContext`/`IsDataValid` 오류 때문에 전체 target이 실패함. 이번 작업 범위 밖의 기존 오류라 수정하지 않았다.

## 8. M1의 한계와 다음 단계

1. 지금 패턴은 true Havelock/Kelvin 적분이 아니라 Kelvin wedge를 이용한 deterministic 근사다.
2. 최대 64 event를 water vertex와 pixel에서 평가하므로 다수 선박/긴 수명에서는 GPU 비용을 프로파일해야 한다.
3. foam은 persistent/advected field가 아니라 event 기반 crest mask다.
4. Reliable Multicast는 late join history를 주지 않으며 다수 선박에서 최종 네트워크 구조가 아니다.
5. Froude 수는 진폭과 파장 생성에 사용하지만 Python ground truth atlas 보간은 아직 없다.

권장 M2 순서:

1. 실제 레벨 화면에서 선체 길이, stern offset, 진폭, 파장 범위를 먼저 튜닝한다.
2. Unreal Insights/GPU Profile로 1/2/4척과 event 16/32/64개 비용을 측정한다.
3. Python/논문 기반 Froude slice를 만들어 M1 식의 angular/phase 계수를 보정한다.
4. 필요하면 시각 foam만 view-centered compute field에 누적하고 authoritative WPO/부력은 현재 evaluator를 유지한다.
5. Fast Array 또는 서버 frame 기반 wake trail history로 late join/rollback 네트워크를 완성한다.

## 9. 재생성 주의

`CreateKelvinM1Assets.py`는 기존 Kelvin master가 있으면 그래프를 중복 삽입하지 않기 위해 중단한다. 사용자가 Kelvin material을 편집한 뒤 스크립트를 다시 실행해 덮어쓰지 않도록 한 안전장치다.

깨끗하게 다시 생성해야 할 때는 `/Game/Tests/Landscape/Kelvin`의 이번 네 생성 에셋만 명시적으로 백업/삭제한 뒤 스크립트를 실행한다. 원본 Landscape 에셋을 삭제하거나 이동할 필요는 없다.

