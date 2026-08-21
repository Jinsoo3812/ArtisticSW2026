# V6 FluidFlux 수중 산란 셰이딩 실험

V6은 FluidFlux의 Single Layer Water 수중 산란/굴절 기법을 프로젝트에 적용하는 독립 실험 브랜치입니다.

기존 V5 PersistentAdvectedFoam 머티리얼에는 변경을 가하지 않습니다.

## 핵심 셰이더 파일

`Shaders/FluidFluxWater.ush` — 수중 산란 함수 라이브러리

UE 머티리얼 에디터의 Custom 노드에서 아래와 같이 include:

```hlsl
#include "/Project/FluidFluxWater.ush"
```

## 제공 함수 목록

### Block 1: 주 광원 스페큘러
- `FluidFlux_SpecularGGX()` — GGX BRDF 통합

### Block 2: 수중 산란/굴절
- `FluidFlux_GetShoreline()` — 해안선 마스크
- `FluidFlux_ComputeScatteringCoefficient()` — 산란 계수 (-log 트릭 포함)
- `FluidFlux_ComputeAbsorptionCoefficient()` — 흡수 계수
- `FluidFlux_ComputePhaseG()` — 태양 입사각 위상 함수
- `FluidFlux_SchlickPhase()` — Schlick Phase 함수
- `FluidFlux_ComputeWaterBehind()` — 수중 색조
- `FluidFlux_GetViewportUVDistortion()` — 시차 기반 UV 왜곡
- `FluidFlux_ComputeUnderwaterVolumeLighting()` — 3개 광원 통합 감쇠

### Block 3: 환경광 반사
- `FluidFlux_ComputeEnvSSRColor()` — IBL + SSR 합성
- `FluidFlux_FinalComposite()` — 밝기 폭발 방지 블렌딩

### 디테일
- `FluidFlux_AdvectUV3()` — 3-Phase Advection (반복감 제거)
- `FluidFlux_CalculateDerivate()` — RG 노말 → Derivate 변환
- `FluidFlux_ComputeFoamOpacity()` — 거품 마스크
- `FluidFlux_ComputeShallowFoamMask()` — 해안 거품

## 머티리얼 구성 가이드

1. V5 마스터 머티리얼을 **복사**하여 V6 마스터 생성
2. Custom 노드 추가 → `#include "/Project/FluidFluxWater.ush"`
3. 산란/흡수 파라미터를 MPC (Material Parameter Collection)로 노출
4. 기존 Single Layer Water 셰이딩 모델의 Scattering/Absorption 출력에 연결

## 참고 자료

- 원문: [FluidFluxWater 파트 2 (Zhihu)](https://zhuanlan.zhihu.com/p/1942643591932510814)
- 기술 레퍼런스: `docs/FluidFluxWater_Part2_Reference.md`
- UE 공식: [Single Layer Water Shading Model](https://dev.epicgames.com/documentation/en-us/unreal-engine/single-layer-water-shading-model)

## 적용된 V6 자산

- `V6_M_FluidFluxWater` — V5 마스터의 독립 복사본. 네 Custom 노드가 `/Project/FluidFluxWater.ush`를 Include File Path로 사용합니다.
- `V6_MPC_FluidFluxWater` — 심해/연안 산란색, 흡수색, Phase G, 후면 수중색과 1/cm 단위 변환 스케일을 제공합니다.
- `V6_MI_FluidFluxWater_Base`, `V6_MI_FluidFluxWater_Ocean` — V6 전용 인스턴스 체인입니다.
- `V6_Test_FluidFluxWater` — V6 Ocean 인스턴스를 사용하는 독립 테스트 레벨입니다. V5 Persistent Foam 액터는 포함하지 않습니다.

Custom 노드 출력은 기존 `SingleLayerWaterMaterialOutput`의 `ScatteringCoefficients`, `AbsorptionCoefficients`, `PhaseG`, `ColorScaleBehindWater`에 연결되어 있습니다. V5 자산은 수정하지 않았습니다.
