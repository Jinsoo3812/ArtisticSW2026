# Water Shader Integration 개선 계획서

작성일: 2026-08-23  
대상 프로젝트: `C:\Unreal Projects\ArtisticSW2026`  
현재 마스터 머티리얼: `Content\Blueprints\Water\M_Realistic_Water.uasset`

## 1. 목적

이 문서는 다음 세 자료를 교차 검증하여 현재 물 렌더링의 실제 구성과 개선 방향을 정의한다.

1. SIGGRAPH 2018 논문 *The Technical Art of Sea of Thieves*
2. 프로젝트의 현재 물 셰이더 및 런타임 코드
3. Unreal Python으로 직접 읽은 `M_Realistic_Water.uasset`의 머티리얼 노드 그래프

핵심 목표는 기능을 개별적으로 더 붙이는 것이 아니라, Gerstner 파형, Godot 기반 잔물결, Ripple, Kelvin wake, Ocean Foam, Shore Foam을 하나의 일관된 수면 상태와 거품 수명 주기로 통합하는 것이다.

## 2. 조사 방법과 검증 범위

### 2.1 PDF

입력 문서는 2페이지 분량의 SIGGRAPH Talks 논문이다. 전체 발표 슬라이드가 아니므로 구현 수식이나 전체 렌더 패스는 제공하지 않는다. 이 문서에서 직접 확인할 수 있는 물 렌더링 원칙은 다음과 같다.

- FFT 파형의 horizontal choppiness로 wave peak mask 생성
- peak mask를 거품뿐 아니라 deep/subsurface water color 전환에도 활용
- 카메라 중심 영역에서 depth buffer 비교로 수면 교차 물체의 거품 생성
- 이전 거품 버퍼를 feedback하고 점진적으로 blur하여 퍼짐과 지속성 표현
- 물리적 생성 마스크에 artist-authored texture를 결합
- calm/normal/storm 상태에 따라 거품 생성량, 분산 및 텍스처 혼합 변경
- 얕은 물에서는 GPU surface simulation texture space에 camera depth를 투영하여 물체와 거품을 상호작용시킴

### 2.2 UAsset 그래프 검사

`Scripts\inspect_realistic_water_material.py`를 Unreal Editor 5.7의 Python 환경에서 실행했다. 에셋을 일반 바이너리 파서로 추측하지 않고 Unreal의 에셋 로더와 프로젝트의 `RealisticWaterMaterialPipelineLibrary`를 사용했다.

검사 결과:

- 로드된 표현식 노드: **235개**
- Material Domain: `Surface`
- Blend Mode: `Masked`
- Shading Model: `Single Layer Water`
- Two Sided: 활성
- Use Material Attributes: 활성
- 최종 머티리얼 입력: `MaterialExpressionSetMaterialAttributes_2`
- 별도 `SingleLayerWaterMaterialOutput` 노드가 존재하며 scattering, absorption, phase, caustics 경로가 연결됨

진단 원본은 프로젝트의 `Saved\Diagnostics\M_Realistic_Water_Graph.json`에 생성했다.

## 3. 현재 마스터 머티리얼의 실제 구조

### 3.1 파형 및 WPO

현재 WPO는 다음 순서로 합성된다.

```text
Engine Water/Gerstner WPO
        + RippleRT.Height
        + ShipWakeRT.Height
        -> 최종 World Position Offset
```

확인 근거:

- `ComputeGerstnerWaves`와 `WaterTextureSurface` 함수가 기본 파형을 구성한다.
- Ripple Custom 노드가 `RippleRT`의 R 채널을 height로 읽는다.
- `AppendVector(0, 0, RippleHeight)`가 기존 WPO에 더해진다.
- Kelvin Custom 노드가 `ShipWakeRT`의 R 채널을 height로 읽고 기존 WPO에 추가한다.

따라서 “Ripple과 Kelvin이 시각 normal만 만들고 WPO에는 반영되지 않는다”는 해석은 사실이 아니다. 두 기능 모두 현재 마스터의 WPO에 연결되어 있다.

### 3.2 최종 normal 합성

실제 normal 합성 순서는 다음과 같다.

```text
Engine/Gerstner base normal
    -> BlendAngleCorrectedNormals(Godot detail normal)
    -> BlendAngleCorrectedNormals(Kelvin wake normal)
    -> BlendAngleCorrectedNormals(Ripple normal)
    -> 최종 Normal
```

앞선 코드 단독 분석에서 우려했던 “모든 normal의 단순 덧셈”은 최종 그래프에는 해당하지 않는다. 최종 합성에는 Unreal의 angle-corrected normal blending이 사용된다.

다만 `GodotNormal.ush` 내부에서 Normal A와 B 자체는 여전히 가중 선형 합산된다. 또한 Ripple/Kelvin compute가 만든 world XY 기울기 기반 normal을 tangent-space normal 입력처럼 합성하고 있어, 큰 Gerstner 파형 위에서 좌표 공간이 엄밀히 일치하는지는 별도 수정이 필요하다.

### 3.3 Godot 기반 잔물결

`GodotNormal.ush`가 실제 마스터의 Custom 노드에 include되어 있다.

확인된 파라미터:

- `V3 Godot Base Inv Tile Cm`
- `V3 Godot Sampler Direction`
- `V3 Godot Motion Sampler Scale`
- `V3 Godot UV Warp Strength`
- `V3 Godot Normal A/B Weight`
- `V3 Godot Normal Strength Near/Far`
- `V3 Godot Mip Bias Near/Far`
- `Godot Water Normal A/B`, `Godot Water UV Motion`

코드의 `BaseUV = WorldPosition.xy * BaseTileCm`는 처음에는 단위 오류로 보였지만, 실제 파라미터 이름이 `Inv Tile Cm`이므로 reciprocal scale을 곱하는 의도임이 확인됐다. 수학 자체보다 함수 인자명 `BaseTileCm`이 잘못된 것이다. 코드와 머티리얼에서 이름을 `InvTileCm` 또는 `UVScalePerCm`으로 통일해야 한다.

### 3.4 Ripple

현재 마스터는 이벤트 텍스처를 픽셀 셰이더에서 직접 순회하지 않는다. `RippleSubsystem`의 compute shader가 카메라 추적 `RippleRT`를 만들고, 머티리얼은 이를 한 번 샘플링한다.

- 기본 RT 해상도: 512 x 512
- 기본 커버리지: 20,000 cm
- R: height
- GBA: normal XYZ
- 마스터는 GBA 전체를 사용하고 `RippleNormalStrength`를 적용한다.
- WPO와 최종 normal 모두에 연결된다.

따라서 Ripple의 큰 비용은 물 머티리얼의 픽셀 셰이더가 아니라, 매 프레임 실행되는 compute 단계의 `해상도 x 활성 이벤트 수` 평가에서 발생한다.

### 3.5 Kelvin ship wake

현재 마스터는 `ShipWakeRT`를 샘플링해 WPO와 normal에 사용한다. 기본 런타임 구성은 512 x 512, 30,000 cm 영역이다.

중대한 인코딩 불일치가 확인됐다.

- `SWShipWakeCS.usf` 출력: `R=Height, G=Normal.X, B=Normal.Y, A=Normal.Z`
- 머티리얼 Custom 노드: G/B만 읽고 `float3(G, B, 1.0)`으로 normal 재구성
- 결과: compute에서 계산한 A 채널의 `Normal.Z`가 버려짐

Ripple 경로는 GBA를 모두 사용하지만 Kelvin 경로는 같은 형식이라고 주석을 달아 놓고도 A를 사용하지 않는다. 이 문제는 wake normal 세기와 각도를 왜곡하며 가장 먼저 수정해야 한다.

### 3.6 Ocean Foam

`SWFluxOceanFoam.ush`를 사용하는 Custom 노드가 2개 존재한다. 두 노드는 Froth와 Web 계열 foam texture를 각각 3-phase로 샘플링하고, crest/slope 마스크를 이용해 흰 crest와 emerald web layer를 합성한다.

확인된 문제:

- `FoamORM`과 sampler가 Custom 노드 입력에 연결되지만 `SWFluxOceanFoam.ush` 내부에서는 사용하지 않는다.
- 세 phase의 이동 방향이 본질적으로 같은 대각선 방향이라 장시간 관찰 시 directional sliding이 드러날 수 있다.
- 이 foam은 현재 프레임의 wave height/steepness로 만들어지는 stateless 표현이다.
- `PersistentFoamState` 또는 동등한 상태 텍스처 파라미터가 현재 마스터에는 없다.
- 엔진 기본 `Enable Ocean Foam` static switch 두 개의 기본값은 false이지만, 프로젝트의 두 `SWFluxOceanFoam` Custom 노드는 이 switch 밖에서 최종 foam 합성 노드에 직접 연결된다. 즉 엔진 기본 ocean foam 비활성 여부와 프로젝트 custom ocean foam 활성 여부는 동일한 의미가 아니다.

### 3.7 Shore Foam

`GodotFoam.ush`가 실제 마스터에 연결되어 있다.

- `SceneDepthWithoutWater`와 `PixelDepth` 차이 사용
- `ShoreFoamScale = 4`
- `ShoreFoamDepthThreshold = 50 cm`
- 결과가 기존 water base color와 foam color 사이의 Lerp alpha로 사용됨

이는 얕은 영역을 빠르게 표시하는 기능은 하지만, vertical water depth가 아니라 view-ray 방향의 depth difference를 사용한다. 카메라 각도에 따라 해안 foam 폭이 달라질 수 있으며 이전 프레임 상태, 해안 법선, 파도 진행 방향, 확산이 없다.

### 3.8 Single Layer Water 및 FluidFlux

현재 마스터는 이미 Unreal의 `Single Layer Water`를 사용하며 다음 엔진 경로가 존재한다.

- `WaterAttributes`
- `Water_Underside`
- `WaterCoefficientMask`
- `SingleLayerWaterMaterialOutput`
- scattering, absorption, anisotropy, caustics 파라미터

하지만 `FluidFluxWater.ush`를 include하는 Custom 노드는 없다. 즉 현재 수중 광학은 엔진 Water 함수 기반이며, FluidFlux의 Beer-Lambert 계산, shoreline SDF 혼합, 3-phase detail helper, custom GGX/SSR composite는 사용되지 않는다.

### 3.9 Persistent Foam

프로젝트에는 `ASWPersistentFoamField`와 ping-pong RT 구현이 존재하지만 현재 `M_Realistic_Water` 그래프에는 다음 요소가 없다.

- persistent foam state texture
- previous/current field center
- foam field size
- Ripple/Kelvin/object intersection source injection

따라서 “프로젝트에 persistent foam 구현이 존재한다”는 사실과 “현재 마스터가 persistent foam을 사용한다”는 것은 구분해야 한다. 현재 마스터 기준으로는 사용하지 않는다.

## 4. 이전 분석과의 대조

| 이전 분석 항목 | UAsset 검증 결과 | 판정 |
|---|---|---|
| Godot flow-warp normal이 사용됨 | `GodotNormal.ush` Custom 노드와 관련 파라미터 확인 | 확인됨 |
| Godot tile 계산의 곱셈이 단위 오류일 수 있음 | 실제 파라미터가 `Inv Tile Cm`이므로 곱셈 의도는 맞고 코드 인자명이 부정확 | 수정 필요 |
| Ripple이 별도 RT로 계산됨 | `RippleRT`, GridCenter, GridSize와 Custom sampler 확인 | 확인됨 |
| Kelvin이 별도 RT로 계산됨 | `ShipWakeRT`, GridCenter, GridSize와 Custom sampler 확인 | 확인됨 |
| Ripple/Kelvin height가 WPO에 통합돼야 함 | 이미 둘 다 최종 WPO에 연결됨 | 이미 구현됨 |
| 최종 normal이 단순 덧셈일 수 있음 | Engine/Godot/Kelvin/Ripple은 angle-corrected blend 사용 | 이전 판단 수정 |
| Ocean Foam이 stateless texture 표현임 | 두 3-phase Custom 노드와 crest/slope 합성 확인 | 확인됨 |
| Shore Foam이 depth difference 기반 stateless 마스크임 | `GodotFoam.ush`와 BaseColor Lerp 확인 | 확인됨 |
| Persistent foam이 현재 표면에 통합되지 않음 | 관련 상태 texture/parameter 부재 | 확인됨 |
| FluidFlux가 현재 마스터에는 적용되지 않음 | `FluidFluxWater.ush` include 없음 | 확인됨 |
| Foam source들이 서로 분리됨 | Ripple/Kelvin은 foam source에 연결되지 않고 Gerstner 계열 mask 중심 | 확인됨 |
| Kelvin compute 비용과 event scan 위험 | 머티리얼은 1회 샘플이나 compute는 최대 256 이벤트를 grid 전체에서 평가 | 확인됨 |

## 5. 핵심 문제 정의

### P0. Kelvin RT normal 인코딩 불일치

현재 wake RT의 A 채널을 버리므로 compute 결과와 머티리얼 해석이 일치하지 않는다.

권장 수정:

```hlsl
float3 wakeNormal = normalize(float3(
    WakeSample.g * EdgeFade,
    WakeSample.b * EdgeFade,
    lerp(1.0, WakeSample.a, EdgeFade)));
```

더 근본적으로는 두 RT 모두 `R=height, GB=world gradient, A=foam source 또는 validity`로 표준화하고 최종 surface basis에서 normal을 한 번만 재구성하는 편이 낫다.

### P0. Foam source의 분리

현재 custom foam의 주된 생성 조건은 Gerstner 계열 wave height/steepness이며 Ripple과 Kelvin은 WPO/normal에만 관여한다. 충돌 파문과 선박 wake가 수면을 변형하지만 같은 위치에 지속성 foam을 만들지 않는다.

### P0. Compute 비용 구조

기본 설정 기준 최악의 평가량은 다음과 같다.

- Ripple: `512 x 512 x 32` = 약 840만 event evaluation/frame
- Kelvin: `512 x 512 x 256` = 약 6,710만 segment evaluation/frame

early culling이 존재하지만 각 픽셀이 이벤트 목록을 훑는 구조 자체는 유지된다. 특히 Kelvin은 quadratic solver와 여러 texture load를 포함한다.

### P1. Shore foam의 카메라 의존성과 무상태성

view depth difference는 수직 수심이 아니며, feedback/advection/blur가 없어 Sea of Thieves가 설명한 확산 거품 구조와 차이가 크다.

### P1. Normal 좌표 공간의 혼합

compute RT normal은 world XY gradient에서 생성되지만 최종 그래프에서는 tangent-space normal blend 함수에 전달된다. 평평한 수면에서는 근사적으로 맞지만 큰 Gerstner 파형의 crest와 경사면에서는 micro/wake/ripple 방향이 실제 displaced basis와 어긋날 수 있다.

### P1. 물 광학과 파형 상태의 분리

현재 foam은 최종 emissive/base color 쪽에 추가되지만 동일한 crest/source mask가 scattering, roughness, absorption에 일관되게 전달되지 않는다. 논문의 wave peak mask 기반 deep/subsurface color 전환과 차이가 있다.

## 6. 목표 아키텍처

```text
Gerstner height/gradient/choppiness ----+
Ripple height/gradient/curvature -------+--> Unified Surface State
Kelvin height/gradient/wake mask -------+       height + gradient.xy
Shore SDF/depth/normal -----------------+       break/foam source
Object intersection depth -------------+       flow velocity
                                                |
                                                v
Previous Foam State --> Backtrace/Advect --> Decay/Diffuse --> Source Inject
                                                                  |
                                                                  v
                                      Persistent Foam Density/Core/Age
                                                                  |
                                   Artist Froth/Web textures ------+
                                                                  v
                               BaseColor / Roughness / Normal / Scattering
```

중요 원칙:

- 물리적/기하학적 조건이 **거품의 위치**를 결정한다.
- artist texture는 거품의 **모양과 스타일**만 결정한다.
- WPO, normal, foam, roughness, scattering은 동일한 surface state를 공유한다.
- 모든 source를 매 픽셀에서 재계산하지 않고 가능한 경우 compute 단계의 공통 결과를 재사용한다.

## 7. 단계별 개선 계획

### Phase 0. 기준선 확보와 명확한 오류 수정

작업:

1. 현재 마스터와 MI의 렌더 기준 스크린샷/GPU profile 저장
2. Kelvin A(normal Z) 사용 수정
3. `BaseTileCm` 명칭을 `InvTileCm`으로 통일
4. `SWFluxOceanFoam`의 사용하지 않는 ORM 인자를 제거하거나 실제 roughness/AO/detail mask에 사용
5. Ripple/Kelvin RT 채널 의미를 코드와 머티리얼 주석에서 동일하게 정의

완료 조건:

- Kelvin normal의 compute 결과와 material sample 결과가 수치적으로 일치
- 기존 WPO, buoyancy, replication, underwater volume 동작에 회귀 없음
- 각 RT 채널을 debug view에서 독립 검증 가능

### Phase 1. Surface State와 normal 공간 표준화

작업:

1. Ripple/Kelvin compute 출력의 normal 대신 `gradient.xy` 저장 검토
2. Gerstner, Ripple, Kelvin gradient를 더해 macro world normal 생성
3. displaced macro normal로 tangent/bitangent basis 구성
4. Godot detail normal을 slope/derivative로 변환해 해당 basis에 적용
5. 최종 단계에서 한 번 normalize

완료 조건:

- 큰 파도 crest에서도 잔물결과 wake normal이 표면에 붙어 움직임
- 각 normal source의 활성/비활성 전환 시 밝기 에너지 급변이 없음
- 원거리에서 micro normal aliasing과 반짝임이 증가하지 않음

### Phase 2. Unified Foam Source 구축

Foam source를 다음과 같이 정의한다.

```text
FoamSource =
    GerstnerBreak
  + RippleBreak
  + KelvinWakeMask
  + ShoreBreak
  + ObjectIntersection
```

권장 입력:

- Gerstner: crest height + slope + 가능하면 horizontal choppiness/Jacobian
- Ripple: `abs(height)`, gradient, analytic curvature, event age
- Kelvin: atlas A mask 또는 height/gradient 기반 white-water mask
- Shore: vertical depth + shoreline SDF + incoming wave direction
- Object: camera-centered intersection/depth mask

완료 조건:

- 파문과 선박 wake가 정확히 변형 위치에서 foam을 생성
- source별 strength를 독립 디버그 및 조절 가능
- source를 꺼도 다른 source의 수명과 advection에 영향 없음

### Phase 3. Persistent Foam Field 통합

기존 `ASWPersistentFoamField`의 ping-pong 구조를 현재 마스터에 연결하되, Gerstner 전용 입력에서 unified source 입력으로 확장한다.

상태 채널 권장안:

- R: foam density
- G: dense core 또는 compressed age
- B: edge/detail seed
- A: validity/coverage

업데이트 순서:

1. 이전 frame state를 flow velocity로 backtrace
2. exponential decay
3. 저비용 확산/blur
4. unified source injection
5. camera-centered field 이동 보정
6. 현재 state를 물 머티리얼에 전달

완료 조건:

- 거품이 생성 즉시 사라지지 않고 흐르며 분산
- field 이동 시 seam/teleport가 보이지 않음
- calm/normal/storm lifetime 변경이 frame-rate 독립적

### Phase 4. Ocean Foam을 스타일 레이어로 전환

현재 Froth/Web 3-phase texture는 거품 존재 여부를 결정하는 주 source에서 persistent density의 breakup/detail layer로 역할을 변경한다.

작업:

- wind/current basis로 phase 방향 회전
- 동일 대각선 이동 대신 서로 다른 방향과 스케일 사용
- foam age에 따라 white core -> translucent filament -> 소멸 변화
- foam이 BaseColor만이 아니라 Roughness, Normal, Scattering에도 영향
- ORM을 실제로 사용할 경우 roughness/AO mask 의미를 명시

완료 조건:

- foam texture가 수면 전체를 미끄러지는 느낌이 없음
- density가 0인 곳에서는 artist texture만으로 foam이 생기지 않음
- 원거리 mip에서도 깜빡임과 패턴 반복이 억제됨

### Phase 5. Shore/Object Interaction 개선

작업:

1. scene depth에서 world position 복원
2. water surface와의 수직 거리 또는 water-local depth 계산
3. coastline SDF가 있으면 depth와 결합
4. SDF gradient로 shore normal 계산
5. wave direction과 shore normal의 dot product로 breaking strength 계산
6. 움직이는 물체 intersection source를 shore source와 분리
7. 두 source를 persistent field에 주입

완료 조건:

- 카메라 고도/각도 변화에도 shore foam 폭이 안정적
- 해안 방향으로 들어오는 파도에서 foam이 강하고 빠져나가는 파도에서는 약함
- 물체 주변 foam과 해안 foam을 서로 다른 파라미터로 조절 가능

### Phase 6. Ripple/Kelvin compute 최적화

우선 Kelvin부터 최적화한다.

선택지:

1. Event-centric stamping: 각 이벤트의 영향 rectangle/annulus만 dispatch
2. Tile binning: 16 x 16 tile별 활성 event list 생성
3. Hybrid: 소수 이벤트는 stamp, 밀도가 높을 때 tile list

추가 작업:

- 화면 기여도 기준 event compaction
- 오래되고 약한 wake segment 병합/제거
- 선박의 연속 segment를 spline/polyline 단위로 묶기
- Ripple/Kelvin/foam이 동일 grid center와 snap policy를 공유할 수 있는지 검토
- 품질 단계별 256/512/1024 해상도와 event capacity 정의

완료 조건:

- GPU capture에서 전체-grid full event scan이 주요 병목이 아님
- 최악의 다중 선박/다중 Ripple 장면에서도 목표 frame budget 유지
- grid edge와 center snap에서 height/normal/foam seam이 없음

### Phase 7. FluidFlux 광학의 선택적 적용

`FluidFluxWater.ush` 전체를 한 번에 붙이지 않고 현재 Single Layer Water 경로와 역할이 겹치지 않는 부분부터 적용한다.

우선 적용 후보:

- shoreline SDF + depth 결합
- deep/shore scattering coefficient 전환
- foam density에 따른 scattering 증가
- derivative 기반 detail normal helper
- refraction contamination 방지

후순위/검증 필요:

- custom GGX가 엔진 Single Layer Water specular와 중복되는지 확인
- custom IBL/SSR composite의 에너지 이중 계산 여부 확인
- Beer-Lambert 단위가 UE centimeter 기준과 일치하는지 검증
- 고정 tangent/bitangent basis 제거

완료 조건:

- 기존 `SingleLayerWaterMaterialOutput`과 중복 계산 없음
- 수심 단위 테스트에서 scattering/absorption이 예측 가능
- crest/foam/shore mask가 water color와 일관되게 반응

### Phase 8. Sea State와 품질 프리셋

논문의 calm/normal/storm 제어를 프로젝트 파라미터 계층으로 만든다.

상태별 제어 대상:

- Gerstner break threshold
- Ripple/Kelvin foam injection strength
- foam lifetime/diffusion
- artist texture scale/contrast
- surface roughness와 micro normal strength
- deep/shore scattering
- compute resolution/event capacity

네트워크로 동기화해야 하는 것은 sea state와 gameplay 영향 파형뿐이며, persistent foam history 자체는 client-local visual state로 유지할 수 있다.

## 8. 구현 우선순위

| 우선순위 | 항목 | 이유 |
|---|---|---|
| P0 | Kelvin normal Z 인코딩 수정 | 명확한 현재 오류이며 범위가 작음 |
| P0 | RT 채널/좌표 공간 표준화 | 이후 통합의 전제 조건 |
| P0 | Unified Foam Source | 기능 간 시각적 단절 해결 |
| P0 | Kelvin event scan 최적화 | 최악 비용이 가장 큼 |
| P1 | Persistent Foam 통합 | Sea of Thieves 핵심 feedback/dispersion 구현 |
| P1 | Shore vertical depth/SDF | 카메라 의존성 제거 |
| P1 | Ocean Foam 스타일 레이어화 | 떠다니는 텍스처 인상 제거 |
| P2 | FluidFlux deep/shore optics | 표면 통합 이후 광학 품질 개선 |
| P2 | Calm/Normal/Storm 프리셋 | 아트 디렉션과 성능 스케일링 |

## 9. 테스트 계획

### 기능 장면

- 잔잔한 대양: micro normal과 far-field 안정성
- 폭풍 대양: crest foam 생성량과 지속성
- 단일/다중 Ripple 충격: ring WPO, normal, foam 정렬
- 직진/선회/정지 선박: Kelvin height, normal, foam 연속성
- 완만/급경사 해안: 카메라 각도별 shore foam 폭
- 수면 교차 물체: 이동 물체 주변 foam 생성과 trail
- 수중 시점: Single Layer Water underside, scattering, Snell 계열 표현 회귀

### 디버그 뷰

- Gerstner/Ripple/Kelvin height
- source별 gradient/normal
- source별 foam injection
- persistent density/core/age
- shore depth/SDF/normal
- final roughness/scattering contribution
- event-per-tile 및 compute occupancy

### 성능 검증

- Unreal Insights CPU/GPU trace
- RenderDoc 또는 PIX compute dispatch 검사
- 256/512/1024 RT 비교
- 1/8/32 Ripple, 1/4/다중 선박 wake 부하 비교
- 메모리: Ripple RT + Wake RT + foam ping-pong RT 총량 기록

## 10. 완료 정의

통합 작업은 다음 조건을 모두 만족해야 완료로 본다.

- Gerstner, Ripple, Kelvin이 동일한 표면 normal 공간에서 합성됨
- Ripple과 Kelvin이 파형 위치에 맞는 foam source를 생성함
- shore/object foam이 이전 frame 상태에 누적되어 흐르고 분산됨
- artist foam texture가 foam 존재 위치를 임의로 만들지 않음
- foam이 BaseColor, Roughness, Normal, Scattering에 일관되게 기여함
- 카메라 이동과 grid snap에서 seam이 없음
- 기존 buoyancy, Water Body, underwater volume, Ripple replication 동작에 회귀가 없음
- 목표 플랫폼별 GPU budget을 충족함

## 11. 관련 파일

- PDF: `C:\Users\vlvkr\OneDrive\Desktop\2018-Talks-Ang_The-Technical-Art-of-Sea-of-Thieves.pdf`
- 마스터 머티리얼: `Content\Blueprints\Water\M_Realistic_Water.uasset`
- 그래프 검사 스크립트: `Scripts\inspect_realistic_water_material.py`
- 그래프 진단 결과: `Saved\Diagnostics\M_Realistic_Water_Graph.json`
- Godot normal: `Shaders\GodotNormal.ush`
- Shore foam: `Shaders\GodotFoam.ush`
- Ripple: `Shaders\SWRipple.ush`, `Shaders\SWRippleCS.usf`
- Kelvin wake: `Shaders\SWShipWake.ush`, `Shaders\SWShipWakeCS.usf`
- Ocean foam: `Shaders\SWFluxOceanFoam.ush`
- FluidFlux reference: `Shaders\FluidFluxWater.ush`
- Persistent foam runtime: `Source\WaterAndShip\Private\SWPersistentFoamField.cpp`

## 12. 최종 결론

현재 마스터 머티리얼은 단순한 프로토타입보다 훨씬 통합되어 있다. Gerstner, Godot detail, Ripple, Kelvin이 실제 WPO와 angle-corrected normal chain에 연결되어 있고 Single Layer Water 광학도 유지된다.

그러나 foam은 아직 같은 수준으로 통합되지 않았다. 현재 foam은 Gerstner crest/slope와 depth band 중심이며 Ripple/Kelvin/object interaction 및 persistent history가 빠져 있다. 따라서 다음 개발의 중심은 새로운 foam texture 추가가 아니라 **surface state 표준화, unified foam source, persistent feedback/advection, compute event 최적화**가 되어야 한다.

가장 안전한 착수점은 Kelvin normal Z 오류 수정과 RT 채널 표준화이다. 그 뒤 unified foam source를 만들고 기존 persistent foam field에 연결하면, Sea of Thieves 논문의 핵심 구조를 현재 시스템을 버리지 않고 단계적으로 구현할 수 있다.
