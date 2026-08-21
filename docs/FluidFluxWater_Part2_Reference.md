# FluidFluxWater 물 렌더링 파트 2 — 기술 레퍼런스

> **원저자**: 秋大叔 · **번역**: jplee (techartnomad)
> **원문**: [Zhihu](https://zhuanlan.zhihu.com/p/1942643591932510814)
>
> 이 문서는 블로그 글의 셰이더 코드, 파라미터, 핵심 개념을 구조적으로 정리한 구현 레퍼런스입니다.
> 프로젝트 셰이더 파일: `Shaders/FluidFluxWater.ush`

---

## 전체 셰이딩 아키텍처

Single Layer Water 셰이딩은 PBR 프레임워크 위에 3개의 큰 블록으로 구성.

| 블록 | PBR 역할 | 핵심 내용 |
|------|---------|---------|
| **1** | Direct Specular | GGX BRDF (Fresnel + NDF + Geometry) |
| **2** | Direct Diffuse **대체** | 수중 산란 = 광원 3종 × 감쇠 |
| **3** | Indirect Specular | IBL Cubemap + SSR RT + envBRDF |

> 이 셰이딩에서는 일반적인 디퓨즈(Lambertian 등)가 등장하지 않는다.
> Block 2의 수중 산란이 그 역할을 완전히 대체한다.

---

## Block 1 — 주 광원 GGX 스페큘러

표준 Cook-Torrance BRDF. UE 셰이더에서 포팅.
- `My_D_GGX()` — GGX 법선 분포
- `Vis_SmithJointApprox()` — Smith Joint 가시성 근사
- `My_F_Schlick()` — Schlick 프레넬
- `SpecularGGX()` — 통합 스페큘러

주요 파라미터: `F0` (BaseColor), `Roughness`

---

## Block 2 — 수중 물리 기반 산란/굴절 (핵심)

### 전처리 파라미터 6종

| # | 파라미터 | 의미 |
|---|---------|------|
| 1 | Scattering Coefficient | 산란 색 — 물속 고유색 |
| 2 | Absorption Coefficient | 흡수 계수 — 빛 에너지 전환 감쇠 |
| 3 | PhaseG | 태양 입사각별 산란 방향 분포 |
| 4 | Water Behind | 수중 화면 밝기/색감 보정 |
| 5 | Opacity | 수면 투명도 → WaterVisibility = 1 - opacity |
| 6 | Specular | 수중 산란/굴절 강도 제어 계수 |

### Shoreline 마스크
- SDF 거리 + view depth를 `max()`로 합집합 → 해안선 범위 정의
- `_CoastlineScattringDistance`, `_CoastlineScattringHeight` 제어

### 산란 계수 (-log 트릭)
- shoreline으로 깊은바다/해안 산란색 lerp
- `-log()` 전처리: exp 감쇠 후에도 지정한 색이 그대로 보이게 역변환

### 수중 굴절 왜곡 UV
- 시차(parallax) 기반 UV 왜곡
- `GATHER_TEXTURE2D`로 주변 4 텍셀 depth 확인 → 수면 위 오염 제거

### 수중 3개 광원 강도
1. **태양광**: Schlick Phase 적용
2. **환경광**: Isotropic Phase (1/4π)
3. **수중 SceneColor**: 깊이 기반 Beer-Lambert 감쇠

### 산란/흡수 감쇠 통합
- `Transmittance = exp(-OpticalDepth)`
- `SafeScatteringAmount = σs × (1-T) / (σs + σa)`
- 태양+환경 → `× (1-envBRDF)` 에너지 분배
- SceneColor → `× Transmittance × waterBehind`
- 최종: `WaterVisibility × (ScatteredLuminance + BehindWaterSceneLuminance)`

---

## Block 3 — 환경광 IBL + 거울 반사

- 러프니스 기반 환경맵 mip 샘플링
- SSR RT와 IBL 블렌딩
- `envBRDF` (Pre-integrated BRDF) 적용
- 깊이에 따른 반사 강도 조절
- **단순 add 금지** → 가중치 블렌딩 필수 (밝기 폭발 방지)

---

## 디테일 — 3-Phase Advection

### 디테일 노말
- 3회 샘플링 + cos 가중치 (1/3 주기 오프셋)
- 가중치 합이 상수 → 반복감 제거의 수학적 근거
- RG 2채널 노말 → derivate 변환

### 거품 (Foam)
- 4채널 텍스처: RG=노말, B=soft, A=마스크
- B/A 채널 max 합성
- 바다 거품 / 해안 거품 별도 처리

---

## 파라미터 Quick Reference

### Block 2 수중 산란
| 파라미터 | 설명 | 기본값 |
|---------|------|-------|
| `SurfaceOverlayAlpha` | 수면 투명도 | ~0.01 |
| `CoastlineScattringDistance` | 해안 SDF 거리 범위 | - |
| `CoastlineScattringHeight` | 해안 깊이 범위 | - |
| `SurfaceScattering0` | 깊은바다 산란색 | - |
| `SurfaceScatteringShoreline` | 해안 산란색 | - |
| `SurfaceAbsorption0` | 깊은바다 흡수색 | - |
| `SurfaceAbsorptionShoreline` | 해안 흡수색 | - |
| `PhaseGDeepSunHigh` | PhaseG (태양 높음) | 0.4 |
| `PhaseGDeepSunLow` | PhaseG (태양 낮음) | 0.6 |
| `VolumeDepthStrength` | 통합 감쇠 깊이 강도 | - |
| `DistancePixelToWaterTopStrength` | SceneColor 감쇠 깊이 | - |

### 디테일
| 파라미터 | 설명 |
|---------|------|
| `DetailWaveTimeSpeed` | 디테일 흐름 속도 |
| `DetailWaveTextureUVScale` | UV 스케일 |
| `DetailWaveNormalScale` | 노말 강도 |
| `WaveFoamPow` | 거품 파워 |
| `WaveFoamInstensity` | 거품 강도 |
| `WaveFoamColor` | 거품 색상 |
