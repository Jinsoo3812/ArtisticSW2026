# Water Optics Integration Assessment

## 1. 목적

이 문서는 다음 네 대상을 실제 구현 기준으로 대조한다.

1. Unreal Engine 5.7 Water Plugin
2. Unreal Engine 5.7 Single Layer Water renderer/shader
3. 현재 `M_Realistic_Water` 마스터 머티리얼과 연결된 Water material functions 및 Project Custom HLSL
4. Sea of Thieves SIGGRAPH 2018 자료와 `FluidFluxWater.ush`

목표는 기능을 많이 추가하는 것이 아니라 다음을 구분하는 것이다.

- 현재 구조에 그대로 적용할 수 있는 원리
- 수정하면 적용 가능한 원리
- 필요한 원천 데이터가 없어 같은 방식으로는 적용할 수 없는 원리
- 엔진 또는 현재 마스터가 이미 수행하므로 추가하면 충돌하거나 이중 계산되는 원리

Foam 개선안은 이 문서의 범위에서 제외한다.

## 2. 조사 기준과 범위

### 2.1 프로젝트 기준

- 프로젝트: Unreal Engine 5.7, DX12
- `r.ReflectionMethod=1`: Lumen reflection
- `r.Substrate=True`
- `r.Water.SingleLayer.Reflection.DownsampleFactor=1` (엔진 기본값으로 복원)
- `r.Water.SingleLayer.RefractionDownsampleFactor=1` (엔진 기본값으로 복원)
- 마스터: `/Game/Blueprints/Water/M_Realistic_Water`
- 그래프: 248 expressions
- 연결된 Water/Engine material function dependency: 29 functions, 767 expressions
- Material domain: Surface
- Blend mode: Masked
- Shading model: Single Layer Water
- Two Sided: true
- Material Attributes 사용

### 2.2 읽은 엔진 구현의 핵심 파일

Water Plugin:

- `WaterBodyComponent.cpp/.h`
- `WaterZoneActor.cpp/.h`
- `WaterMeshComponent.cpp/.h`
- `WaterMeshSceneProxy.cpp/.h`
- `WaterViewExtension.cpp/.h`
- `WaterInfoRendering.cpp/.h`
- `WaterQuadTree*`
- `WaterVertexFactory*`
- `GerstnerWaterWaves.cpp/.h`
- `GerstnerWaveFunctions.ush`
- `WaterDataFunctions.ush`
- Water Plugin의 현재 마스터 연결 material functions

Single Layer Water:

- `SingleLayerWaterRendering.cpp/.h`
- `SingleLayerWaterShading.ush`
- `SingleLayerWaterCommon.ush`
- `SingleLayerWaterComposite.usf`
- `SingleLayerWaterRefractionCulling.usf`
- `SingleLayerWaterDefinitions.h`
- `MaterialExpressionSingleLayerWaterMaterialOutput.h`
- `MaterialExpressions.cpp`의 Single Layer Water Custom Output compiler

에디터 UI, 에셋 팩토리, spline 편집기처럼 최종 수면 광학에 관여하지 않는 Water Plugin 코드는 기능 목록만 확인하고 광학 판정에서는 제외했다.

## 3. 엔진 Water Plugin이 담당하는 것

Water Plugin과 Single Layer Water는 같은 기능이 아니다.

```text
Water Plugin
├─ WaterBody/WaterZone의 공간 정보
├─ 카메라 중심 Water Mesh quadtree와 LOD
├─ WaterInfo texture의 수면 높이/지면 높이/유속
├─ WaterBody별 MID와 인덱스 설정
├─ Gerstner wave 데이터
└─ Water material functions와 caustics용 입력

Single Layer Water renderer
├─ 물 없는 SceneColor/SceneDepth 복사
├─ 수면 base pass
├─ 굴절과 수중 체적광
├─ Lumen/SSR/reflection capture 반사
└─ 최종 composite
```

### 3.1 WaterBody가 머티리얼에 제공하는 런타임 데이터

`UWaterBodyComponent::SetDynamicParametersOnMID`는 다음을 MID에 전달한다.

- `WaterBodyIndex`
- `GlobalOceanHeight`
- `WaterZoneIndex`
- WaterInfo texture array (`WaterVelocityAndHeight`)

`WaterDataFunctions.ush`와 `/Water/Materials/Functions/WaterBodyData`는 이 데이터에서 다음을 복원한다.

- water surface Z
- ground Z
- water depth
- flow magnitude와 direction
- fixed water height/depth/velocity fallback
- WaterZone 위치와 extent

따라서 연안/심해 광학 마스크를 만들 때 별도의 SceneDepth 추정이나 신규 전역 RT보다 WaterInfo의 `water Z - ground Z`를 먼저 사용하는 것이 현재 파이프라인과 가장 잘 맞는다.

### 3.2 Water Mesh와 카메라

Water Mesh는 quadtree와 카메라 중심 LOD를 사용한다. 이 카메라 의존성은 geometry density와 draw tile 선택에 관한 것이며, 수면 광학 계수의 월드 위치를 의도적으로 이동시키는 기능은 아니다.

광학 마스크를 Screen UV, PixelDepth 또는 LOD 보간 결과로 새로 정의하면 카메라 이동에 따라 형태가 바뀔 수 있다. 반면 WaterInfo의 월드 위치 기반 수심이나 Gerstner의 동일 파형 출력을 사용하면 LOD가 바뀌어도 의미상 같은 위치를 유지할 수 있다.

### 3.3 Gerstner가 제공하는 정보

`ComputeGerstnerWaves`는 WaterBodyIndex, time, world position으로 `GetAllGerstnerWavesNew`를 호출한다. 최종 함수 출력은 다음이다.

- WPO XYZ
- wave normal XYZ

Gerstner shader 내부에는 수평 변위가 포함되지만 현재 마스터에 Tessendorf FFT의 독립적인 choppiness/Jacobian texture는 없다. 따라서 Gerstner WPO와 normal로 파두/경사를 근사할 수는 있지만 Sea of Thieves의 FFT choppiness peak mask와 동일한 데이터라고 부를 수는 없다.

## 4. Single Layer Water의 실제 광학 처리

### 4.1 머티리얼이 renderer에 제공하는 네 값

`SingleLayerWaterMaterialOutput`은 다음 네 출력을 컴파일한다.

1. Scattering Coefficients, float3
2. Absorption Coefficients, float3
3. PhaseG, float
4. Color Scale Behind Water, float3

현재 마스터는 모두 연결되어 있다.

```text
Scattering parameter ┐
Optional Foam scatter├─ coefficient conversion
Absorption parameter ┘
        ↓
Water_Underside front/back handling
        ↓
WaterOpacityMaskFromDepth
        ↓
WaterCoefficientMask
        ├─ Scattering × opacity mask
        └─ Absorption × opacity mask
        ↓
SingleLayerWaterMaterialOutput

Anisotropy = 0.1 -> PhaseG
Caustics switch result -> ColorScaleBehindWater
```

현재 coefficient authoring 식은 대략 다음 의미다.

```text
ScatteringCoeff = (Scattering.rgb × Scattering.a
                 + optional FoamScattering.rgb × FoamScattering.a) / 1000

AbsorptionCoeff = 1 / (Absorption.rgb × Absorption.a)
```

그 뒤 `Water_Underside`와 `WaterCoefficientMask`가 면 방향과 opacity mask를 적용한다. 그러므로 FluidFlux의 색→계수 변환식을 그대로 덮어쓰면 기존 MI 파라미터 의미와 단위가 바뀐다.

### 4.2 수중 체적광

`SingleLayerWaterShading.ush::EvaluateWaterVolumeLighting`는 이미 다음을 수행한다.

- `Extinction = Scattering + Absorption`
- `OpticalDepth = Extinction × 물속 이동 거리`
- `Transmittance = exp(-OpticalDepth)`
- 해석적인 single scattering 적분
- Schlick phase를 사용한 주 방향광 산란
- isotropic ambient scattering
- 수중 SceneColor에 light-to-surface transmittance 적용
- EnvBRDF를 이용한 반사/투과 에너지 분배
- `ColorScaleBehindWater`의 얕은 경계 fade

즉 Beer-Lambert, 산란/흡수 합성, 태양/환경/SceneColor의 체적 합성은 현재 renderer의 본체다.

### 4.3 굴절

엔진은 다음 절차를 이미 수행한다.

1. 물의 Specular에서 IOR을 계산
2. material normal로 distortion UV 계산
3. 물 두께가 얕으면 distortion을 약화
4. refraction mask의 2×2 footprint 검사
5. distorted SceneDepth 네 샘플 중 하나라도 수면보다 앞이면 원래 UV로 fallback
6. 물 없는 SceneColor를 안전한 UV로 sample

이것은 `FluidFlux_ShouldCancelDistortion`이 의도한 foreground contamination 방지를 더 완전한 형태로 이미 포함한다.

### 4.4 반사

현재 프로젝트는 Lumen reflection을 사용한다. Single Layer Water renderer는 설정에 따라 다음을 선택한다.

- Lumen reflection
- SSR + temporal history/TAA
- reflection capture + skylight
- reflection disabled

composite에서는 reflection capture/sky와 SSR을 합성하고 EnvBRDF/Fresnel을 적용한다. 프로젝트의 reflection buffer는 엔진 기본값인 full resolution으로 복원했다.

### 4.5 수중 시점과 Snell's Window

두 층에서 이미 처리한다.

- `/Water/Materials/Functions/Water_Underside`
  - `Critical Angle Dot = 0.225`
  - `Critical Angle Width = 0.01`
  - underside specular/opacity/refraction 전환
  - lip refraction과 bottom refraction
- `SingleLayerWaterShading.ush`
  - air-to-water relative IOR
  - `WaterRefract`
  - total internal reflection 시 굴절 실패 처리
  - underwater camera 전용 산란/반사 분기

따라서 Sea of Thieves의 Snell's Window를 신규 Custom 광학으로 다시 만드는 것은 현재 구조와 충돌한다. 현재 기능을 튜닝하는 것이 올바른 접근이다.

## 5. 현재 마스터 머티리얼의 추가 구조

### 5.1 파형과 normal

```text
Engine Gerstner WPO/Normal
  + Ripple RT WPO/Normal
  + Kelvin RT WPO/Normal
  + Godot detail normal
  -> angle-corrected normal chain
  -> V2 dynamic roughness/specular override
```

현재 최종 Material Attributes는 엔진 Water base attributes를 유지하면서 다음을 덮어쓴다.

- final WPO
- combined normal
- dynamic roughness
- specular
- Ocean Foam의 base color 계열 결과

SLW의 scattering/absorption/PhaseG/ColorScaleBehindWater 출력은 별도 체인으로 유지된다.

### 5.2 현재 광학 관련 특징

- Water Plugin의 `WaterAttributes`가 roughness, specular, refraction을 제공
- 프로젝트 V2가 roughness와 specular를 다시 제어
- `Water_Underside`가 임계각과 수중 면을 처리
- `WaterCoefficientMask`가 coefficient를 opacity mask에 맞게 제한
- caustics 함수 결과가 `ColorScaleBehindWater`로 들어감
- Godot, Ripple, Kelvin normal이 굴절과 반사의 입력 normal에 영향을 줌
- `FluidFluxWater.ush`는 어떤 Custom node에서도 include/call되지 않음

따라서 `FluidFluxWater.ush`는 현재 실행 중인 코드가 아니라 비교용 라이브러리다.

## 6. Sea of Thieves에서 차용할 수 있는 것

Sea of Thieves 자료의 ocean 광학 핵심은 다음이다.

- deep water color와 sub-surface water color를 혼합
- view angle, sun direction, wave peak mask를 혼합 기준으로 사용
- FFT choppiness offset에서 wave peak mask 생성
- 낮은 태양을 위한 넓은 area specular
- 수중에서 Snell's Window 표현

### 6.1 그대로 또는 안전하게 차용 가능

#### A. 파두에서 수면 아래 색/산란을 더 보여주는 미술 원리

적용 가능하다. 단, view angle과 sun direction까지 Custom에서 다시 계산하지 않는다.

권장 구조:

```text
Gerstner-only crest mask
-> Deep/Peak scattering coefficient lerp
-> 기존 Water_Underside
-> 기존 WaterCoefficientMask
-> 기존 SingleLayerWaterMaterialOutput
```

Renderer가 이미 시선각 Fresnel과 태양 방향 phase scattering을 계산하므로, 새 로직은 파두에 따른 coefficient 변화만 제공해야 한다.

파두 mask 후보는 다음 우선순위다.

1. Gerstner 함수의 동일 WPO/normal에서 만든 안정적인 crest mask
2. 기존 Gerstner Ocean Foam의 height/slope mask를 낮은 강도로 재사용
3. 별도의 미술 조절 curve

Kelvin/Ripple까지 합성한 PixelNormalWS를 쓰면 다른 파형과 화면 미분에 다시 의존한다. Sea of Thieves의 기본 ocean 파두 효과를 옮기는 목적이라면 Gerstner-only가 맞다.

#### B. calm/normal/storm에 따른 광학 preset

적용 가능하다. Foam 수명주기가 아니라 다음 계수만 Sea State로 묶는다.

- deep scattering/absorption
- shore scattering/absorption
- crest subsurface strength
- roughness 범위
- refraction strength
- caustics strength

### 6.2 조건부 적용

#### C. 낮은 태양의 넓은 area specular

시각 목표는 차용할 수 있지만 Sea of Thieves의 closest-point-on-sphere highlight를 곧바로 더하면 현재 Lumen/SLW directional specular와 에너지가 중복된다.

먼저 Directional Light source angle, 현재 dynamic roughness, Lumen reflection 결과를 조정한다. 그 후에도 스타일상 부족할 때만 다음 조건으로 별도 lobe를 고려한다.

- 태양 방향은 MPC 또는 명시적 런타임 파라미터로 전달
- 기존 specular와 에너지 보존 방식으로 혼합
- emissive 단순 가산 금지
- 낮은 태양 고도에서만 활성
- Lumen/SSR on/off A/B 검증

### 6.3 같은 방식으로는 적용 불가능

#### D. FFT choppiness peak mask의 직접 사용

현재는 FFT displacement/choppiness field가 없다. Gerstner WPO에는 수평 변위가 있지만 Tessendorf FFT의 choppiness texture/Jacobian과 동일하지 않다.

따라서 가능한 것은 Gerstner crest approximation이며, Sea of Thieves와 동일한 FFT peak mask를 얻는 것은 아니다.

### 6.4 이미 구현되어 추가하면 충돌

#### E. Snell's Window 신규 구현

`Water_Underside`의 critical-angle graph와 renderer의 IOR/TIR 처리가 이미 존재한다. 신규 Snell Custom은 specular, opacity, refraction을 다시 바꾸므로 이중 임계각과 경계 band를 만들 수 있다.

결론: 새 기능이 아니라 현재 underside 파라미터를 노출하고 검증한다.

## 7. FluidFluxWater.ush 판정표

| FluidFlux 기능 | 판정 | 이유와 처리 방향 |
|---|---|---|
| `GetShoreline` | 수정 후 적용 가능 | SDF/SceneDepth 식을 그대로 쓰지 말고 WaterInfo의 실제 water depth를 기본 입력으로 사용 |
| deep/shore scattering 전환 | 적용 가능 | SLW output 앞단에서 두 coefficient set을 lerp하면 엔진 체적광을 그대로 활용 가능 |
| deep/shore absorption 전환 | 적용 가능 | 현재 coefficient 단위를 유지한 두 set을 lerp해야 함 |
| sun elevation 기반 `PhaseG` | 조건부 적용 | SLW PhaseG 입력에 연결 가능. 태양 방향은 MPC 등 안정적인 입력이 필요 |
| `ComputeWaterBehind` | 수정 후 적용 가능 | 현재 `ColorScaleBehindWater`에 대응. 기존 caustics 결과와 곱/블렌드해야 함 |
| 3-phase advection | 대체안으로 가능 | Godot/WaterTextureSurface detail normal을 대체할 때만 비교. 추가 적층은 비용과 고주파 alias 증가 |
| derivative normal 변환 | 코드 수정 필수 | 현재 함수의 고정 T/B가 UE의 수평 XY 접평면과 맞지 않으며 실제 surface basis도 무시함 |
| custom GGX | 충돌 | Water base pass/SLW가 이미 BRDF specular를 계산하고 V2가 roughness/specular도 제어함 |
| custom Schlick Fresnel/horizon | 충돌 | EnvBRDF/Fresnel, reflection capture/Lumen/SSR composite와 이중 계산 |
| `SpecularTranslucent` depth fit | 충돌/퇴행 가능 | 엔진은 실제 SceneDepth 기반 물 두께와 exponential transmittance를 사용 |
| Schlick phase 함수 | 이미 구현 | 엔진 `ParticipatingMediaCommon`/SLW가 같은 역할 수행 |
| isotropic ambient phase | 이미 구현 | SLW ambient scattering에서 사용 |
| underwater volume lighting | 강한 충돌 | 엔진 `EvaluateWaterVolumeLighting`과 기능 및 식이 거의 동일 |
| mean transmittance to light | 이미 구현 | 엔진이 scene pixel-to-water-top 거리로 `exp(-distance×extinction)` 계산 |
| refraction UV distortion | 이미 구현 | material refraction과 world normal을 사용한 엔진 distortion path 존재 |
| distortion cancellation | 이미 더 완전하게 구현 | refraction mask와 2×2 SceneDepth gather/fallback 포함 |
| EnvBRDF approximation | 충돌 | 엔진 preintegrated EnvBRDF와 composite가 이미 사용 |
| IBL + SSR blend | 강한 충돌 | 현재 프로젝트는 Lumen reflection이며 SLW renderer가 reflection 방법을 소유 |
| final reflection/refraction composite | 강한 충돌 | renderer pass 밖에서 재현하면 SceneColor 및 에너지 중복 |
| `WaterRefract` | 이미 구현 | SLW가 relative IOR, refracted ray, TIR을 처리 |

## 8. FluidFlux에서 실제로 차용할 광학 구조

함수를 통째로 include하는 방식보다 파라미터 구조만 가져오는 것이 안전하다.

### 8.1 권장 coefficient 구조

```text
WaterInfo world-space depth
-> ShoreMask

Deep Scattering Coeff  ┐
Shore Scattering Coeff ├─ lerp by ShoreMask
                       └─ optional Gerstner Crest modifier

Deep Absorption Coeff  ┐
Shore Absorption Coeff ┴─ lerp by ShoreMask

-> Water_Underside
-> WaterCoefficientMask
-> SingleLayerWaterMaterialOutput
-> 엔진 Beer-Lambert/phase/refraction/reflection
```

기존 `Scattering`과 `Absorption`의 parameter convention을 유지해야 기존 MI가 깨지지 않는다. 신규 deep/shore 색 파라미터도 먼저 현재 방식으로 coefficient로 변환한 뒤 lerp한다.

### 8.2 ColorScaleBehindWater 병합

현재 fourth SLW output은 caustics가 사용한다. 수중 색조 기능을 추가할 경우 다음처럼 병합한다.

```text
FinalColorScaleBehindWater
= ExistingCausticsColorScale × Deep/ShoreBehindWaterTint
```

FluidFlux tint로 caustics 연결을 대체하면 기존 caustics가 사라진다.

### 8.3 PhaseG

현재 `Anisotropy=0.1`은 이미 PhaseG에 연결되어 있다. FluidFlux식 sun elevation 변화는 이 scalar의 대체 입력으로 넣을 수 있다.

주의:

- phase 함수 자체를 Custom으로 재계산하지 않음
- `PhaseG` 값만 renderer에 전달
- 게임의 태양 방향을 MPC/Subsystem에서 한 번 갱신
- 물 픽셀마다 별도 light search 금지

## 9. 현재와 충돌하는 대표 조합

### 9.1 Custom GGX + 현재 roughness/specular

현재는 `WaterAttributes`의 roughness/specular 위에 V2 dynamic roughness/specular override가 있고, renderer가 이를 BRDF에 사용한다. FluidFlux GGX를 색에 다시 가산하면 동일 normal과 광원으로 specular가 두 번 생긴다.

### 9.2 Custom Fresnel/SSR + Lumen Single Layer Water

프로젝트 reflection method는 Lumen이다. FluidFlux의 SSR color input과 custom composite는 현재 reflection routing을 우회하거나 중복한다. SSR 전용 구현으로 바꾸면 off-screen 반사와 Lumen reflection을 잃을 수 있다.

### 9.3 Custom volume lighting + SLW coefficient output

SLW에 scattering/absorption을 전달하면서 Custom에서 다시 Beer-Lambert와 scene color 합성을 하면 두 번 감쇠된다. 결과는 과도하게 어둡거나 채도가 높고, 깊이 변화에 비선형적인 band가 생긴다.

### 9.4 FluidFlux refraction + Water_Underside/SLW refraction

현재 normal은 Gerstner, Godot, Kelvin, Ripple을 모두 포함하고 엔진 refraction이 이를 소비한다. Custom distortion을 추가하면 normal distortion이 이중 적용되며 foreground leak 방지 로직도 우회할 수 있다.

### 9.5 수중 tint가 caustics를 덮는 문제

`ColorScaleBehindWater`는 비어 있지 않다. Caustics static switch가 이미 연결되어 있으므로 신규 tint는 반드시 기존 결과와 병합해야 한다.

## 10. 적용 우선순위

### P0. 현재 엔진 경로의 품질 기준 확정

기능 추가 전에 다음 두 설정을 1과 2로 A/B 비교한다.

- `r.Water.SingleLayer.Reflection.DownsampleFactor`
- `r.Water.SingleLayer.RefractionDownsampleFactor`

두 값은 조사 당시 2였으나 엔진 기본값인 1로 복원했다. 반사/굴절의 흐림, 얇은 geometry 누출, 먼 거리 shimmer가 절반 해상도에서 왔는지 full-resolution 상태에서 다시 확인한다.

동시에 다음을 debug view로 분리한다.

- final normal
- roughness/specular
- scattering coefficient
- absorption coefficient
- water depth/shore mask
- ColorScaleBehindWater/caustics
- reflection only
- refraction/volume only

### P1. WaterInfo 기반 deep/shore coefficient

가장 충돌이 적고 효과가 큰 후보다.

1. `WaterBodyData`의 water depth 사용
2. world-space smoothstep으로 shore mask 생성
3. deep/shore scattering을 현재 단위로 변환 후 lerp
4. deep/shore absorption을 현재 단위로 변환 후 lerp
5. 기존 `Water_Underside -> WaterCoefficientMask -> SLW Output` 유지

SceneDepth는 보조 fallback으로만 사용한다. 카메라 ray depth를 주된 연안 마스크로 쓰지 않는다.

### P2. Sea of Thieves식 Gerstner crest subsurface 강조

1. Gerstner-only WPO/normal에서 crest mask 생성
2. peak scattering/tint 강도를 작게 제한
3. P1의 deep/shore coefficient 이후에 적용
4. renderer의 view angle/sun phase/Fresnel은 그대로 사용

이는 Sea of Thieves의 시각 원리를 차용하지만 FFT choppiness mask를 재현한다고 주장하지 않는다.

### P3. ColorScaleBehindWater tint와 PhaseG

- deep/shore behind-water tint는 기존 caustics와 곱해서 fourth SLW output에 연결
- 필요하면 sun elevation에 따라 PhaseG 값만 변화
- 광학 함수 자체는 엔진을 사용

### P4. 넓은 저고도 태양 반사

Directional Light source angle과 기존 roughness/Lumen 결과가 부족한 것이 확인된 뒤에만 별도 스타일 lobe를 실험한다. 기본 구현 항목이 아니다.

### 보류. 3-phase detail normal

현재 Godot flow normal 및 WaterTextureSurface와 동일한 역할 영역이다. 별도 quality branch에서 교체 비교할 수는 있지만 동시에 중첩하지 않는다. `FluidFlux_DerivateConvertNormal`은 고정 basis를 먼저 수정해야 한다.

## 11. 최종 분류

### 적용할 것

- WaterInfo 기반 deep/shore scattering·absorption 전환
- Gerstner-only crest에 따른 약한 subsurface/scattering 강조
- 기존 caustics와 병합되는 behind-water tint
- 선택적 sun-elevation PhaseG 값 조절
- calm/normal/storm 광학 preset

### 수정 후 적용할 것

- FluidFlux shoreline mask: WaterInfo depth 중심으로 재정의
- FluidFlux color→coefficient: 기존 프로젝트 단위/MI convention 유지
- FluidFlux 3-phase normal: 현재 normal의 대체 quality path 및 올바른 surface basis 필요
- Sea of Thieves area sun lobe: 엔진 조명으로 부족함이 확인된 경우만 에너지 보존 방식으로 추가

### 같은 방식으로 적용할 수 없는 것

- Sea of Thieves의 FFT choppiness peak mask 자체
- Tessendorf FFT/Jacobian 기반 데이터가 필요한 optical/crest 판정
- FluidFlux 코드가 전제하는 SDF가 프로젝트에서 제공되지 않을 때의 SDF shoreline 항

### 적용하지 말아야 할 것

- FluidFlux custom GGX
- custom Fresnel/horizon reflection의 직접 가산
- custom underwater volume lighting
- custom Beer-Lambert final color 계산
- custom EnvBRDF/SSR/Lumen 대체 composite
- custom refraction distortion/cancellation
- 별도 Snell's Window
- 별도 Schlick/isotropic phase 함수 평가

이 항목들은 UE 5.7 Single Layer Water가 이미 더 깊은 renderer 접근권한으로 수행하고 있다.

## 12. 결론

현재 마스터의 광학 기반은 비어 있지 않다. Water Plugin의 WaterInfo/Gerstner 데이터, `Water_Underside`, `WaterCoefficientMask`, caustics, Single Layer Water의 체적 산란·흡수·굴절·Lumen reflection이 이미 연결되어 있다.

따라서 다음 개선은 `FluidFluxWater.ush`를 통째로 붙이는 작업이 아니다.

```text
새로운 공간 정보
= WaterInfo 기반 deep/shore mask

새로운 파형-광학 결합
= Gerstner crest coefficient modifier

기존 renderer
= 그대로 유지
```

이 구조가 Sea of Thieves의 스타일 원리를 차용하면서도 Unreal의 Single Layer Water와 충돌하지 않는 최소 경로다.

## 13. 근거 자료

- `2018-Talks-Ang_The-Technical-Art-of-Sea-of-Thieves.pdf`
- `Shaders/FluidFluxWater.ush`
- Unreal Engine 5.7 Water Plugin source/shaders
- Unreal Engine 5.7 Single Layer Water renderer/shaders
- `Content/Blueprints/Water/M_Realistic_Water.uasset`
- `Saved/Diagnostics/M_Realistic_Water_Graph.json`
- `Saved/Diagnostics/M_Realistic_Water_Functions.json`
