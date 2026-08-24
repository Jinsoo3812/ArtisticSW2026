# Improved Foam Implementation Plan

## 1. 목적과 범위

현재 Ocean Foam은 Gerstner, Kelvin, Ripple이 합성된 최종 WPO 높이와 PixelNormalWS 가파름을 하나의 공통 Bound로 판정한다. 이 구조를 다음 세 항목만 개선한다.

1. Gerstner Foam은 현재 높이/가파름 마스크와 표현을 그대로 유지한다.
2. Kelvin과 Ripple은 각 파형의 고유 좌표계에서 `이번 프레임에 어디에서 얼마나 많은 새 Foam을 방출할 것인가`를 나타내는 정보를 만들고 런타임 파라미터와 결합한다.
3. Kelvin/Ripple Foam에만 생성, 누적, 약한 분산, 잔류, 감쇠 생명주기를 적용하고 Foam 위치 판정의 카메라 의존성을 제거한다.

기존 Gerstner/Kelvin/Ripple WPO와 Normal 생성 로직은 변경하지 않는다. Kelvin 시작점 깜빡임, 회색 물 내부 노출, Shore Foam 및 실패한 Persistent Foam V5는 범위에서 제외한다.

## 1.1 구현 상태 (2026-08-23)

본 문서의 Kelvin/Ripple Improved Foam 핵심 경로는 구현되었다.

- Kelvin Golden RGBA16F의 A 채널에 Froude별 Foam LocationMask를 베이크했다. 기존 R/G/B는 byte 단위 회귀 검사로 보존했다.
- Ripple profile baker와 Golden을 만들었다. 런타임은 같은 cosine phase/steepness 값을 기존 Ripple Compute 안에서 재사용해 LUT fetch를 추가하지 않는다.
- Kelvin/Ripple의 기존 Compute event scan 안에서 R16F Foam Source UAV를 함께 기록한다. 별도 event scan과 CPU 파형 평가는 없다.
- `SWImprovedFoamHistoryCS`가 512×512 world-space ping-pong history를 한 pass로 갱신한다. R=Kelvin, G=Ripple, B=합집합이다.
- 마스터 머티리얼에는 Texture Object Parameter 한 개와 history sample Custom 한 개만 추가했다. 기존 WPO/Normal 합성 경로는 재배선하지 않았다.
- 기존 Ocean Foam Custom 수식은 Git 원본과 대조했다. 두 Bounds mask, slope 제곱, emerald/white layer와 lerp 수식은 동일하며 입력만 Gerstner WPO/Normal로 분리했다.
- 일반 플레이에는 readback이 없다. `-SWFoamDiagnostics`에서만 5초 주기의 blocking readback을 사용한다.

주요 구현 파일:

```text
Shaders/SWShipWake.ush, Shaders/SWShipWakeCS.usf
Shaders/SWRipple.ush, Shaders/SWRippleCS.usf
Shaders/SWImprovedFoamHistoryCS.usf
Source/WaterAndShip/Private/SWImprovedFoamSubsystem.cpp
Scripts/bake_kelvin_foam_masks.py
Scripts/bake_ripple_foam_profile.py
Scripts/integrate_improved_foam_material.py
```

## 2. 확정된 최종 구조

```text
Gerstner 최종 높이/가파름 Bound
└─ 기존 Gerstner Ocean Foam 경로 그대로 유지

Kelvin Golden A: Foam 위치 후보
× Runtime Amplitude/Wavelength/Froude/Fade/Decay
└─ Kelvin Foam Emission
                         ┐
                         ├─ 월드 공간 Foam History
Ripple Profile Foam 위치 │   생성 -> 누적 -> 약한 분산 -> 잔류 -> 감쇠
× Runtime Phase/Amplitude/Wavelength/Envelope/Decay
└─ Ripple Foam Emission  ┘

기존 Gerstner Foam + Kelvin/Ripple History Foam
└─ 기존 SWFlux Web/Froth 및 FluidFlux식 외형 조절
└─ 최종 Ocean Foam
```

WPO와 Normal 경로는 현재 상태를 보존한다.

```text
Gerstner WPO/Normal -> 기존 유지
Kelvin WPO/Normal   -> 기존 Golden Height/Gradient 및 ShipWakeRT 유지
Ripple WPO/Normal   -> 기존 analytic RippleRT 유지
```

Foam 위치 데이터는 WPO나 Normal을 수정하지 않고 Foam emission에만 사용한다.

## 3. 기존 구조에서 달라지는 점

### 3.1 기존 구조

```text
WaveHeight
    = Gerstner + Kelvin + Ripple이 합성된 최종 WPO 높이

WaveSteepness
    = 1 - 최종 PixelNormalWS.Z

공통 Height/Slope Bounds
    -> 모든 Ocean Foam 생성
```

문제점:

- 서로 다른 진폭과 파장을 가진 세 파도에 동일한 Bound를 적용한다.
- 다른 파도의 높이와 기울기가 Kelvin/Ripple의 원래 대칭 형태를 상쇄하거나 변형할 수 있다.
- PixelNormalWS는 Water Mesh LOD와 픽셀 보간에 영향을 받으므로 카메라 이동 시 마스크 위치가 달라질 수 있다.
- 현재 프레임만 평가하므로 Kelvin/Ripple Foam의 생성과 소멸 사이에 시간적 상태가 없다.

### 3.2 개선 구조

```text
Gerstner
└─ 기존 공통 마스크 중 Gerstner Foam 결과를 유지

Kelvin
└─ Golden에 구운 위치 후보 × 런타임 생성 강도

Ripple
└─ Profile에 구운 위치 후보 × 런타임 생성 강도

Kelvin/Ripple Emission
└─ 월드 공간 History에 주입
```

핵심 변화는 높이 판정을 없애는 것이 아니다. Kelvin/Ripple을 최종 합성 픽셀에서 추측하지 않고, 다른 파도와 섞이기 전의 고유 파형 데이터에서 독립적으로 판정하는 것이다.

## 4. Foam Emission의 정의

`FoamEmission`은 최종 Foam 색이나 완성된 Foam 무늬가 아니다.

```text
FoamEmission(x,t)
= 이번 프레임에 월드 위치 x에서 새 Foam History를 얼마나 추가할지 나타내는 0~1 스칼라
```

다음 두 정보를 분리한다.

```text
LocationMask
= 파형 고유 좌표에서 Foam이 발생할 수 있는 위치

GenerationStrength
= 현재 진폭, 파장, Froude, 감쇠 상태에서 실제로 방출할 양

FoamEmission = LocationMask × GenerationStrength
```

## 5. Gerstner Foam

Gerstner는 수정하지 않는다.

```text
HeightMask = smoothstep(HeightMin, HeightMax, WaveHeight)
SlopeMask  = smoothstep(SlopeMin,  SlopeMax,  WaveSteepness)
GerstnerFoam = HeightMask × SlopeMask
```

- 기존 Bound 파라미터 유지
- 기존 Foam 모양과 색상 유지
- History 적용 안 함
- 새로운 Source RT 생성 안 함
- Tessendorf/Jacobian 판정 추가 안 함

머티리얼 통합 시 기존 Gerstner Foam 경로를 보존하고 Kelvin/Ripple 개선 Foam을 추가 합성한다.

## 6. Kelvin Foam Emission

### 6.1 기존 Golden 데이터

현재 Kelvin Golden Baker는 Froude별 다음 값을 계산한다.

```text
R = ζ(u,v,Fr)    Height
G = ∂ζ/∂u        Downstream gradient
B = ∂ζ/∂v        Lateral gradient
A = 1.0          현재 고정값
```

A를 `KelvinFoamLocationMask`로 변경한다.

```text
R = 기존 Height, 변경 금지
G = 기존 dZ/du, 변경 금지
B = 기존 dZ/dv, 변경 금지
A = 신규 Kelvin Foam 위치 후보
```

따라서 기존 Height/Gradient texture fetch에서 Foam 위치도 함께 얻으며 추가 Golden texture fetch는 없다.

### 6.2 위치 후보 생성

Kelvin Golden은 파형 전체를 미리 알고 있으므로 오프라인 Baker에서 양의 파두 위치를 안정적으로 결정한다.

기본 입력:

```text
NormalizedHeight = ζ / FroudeSlicePeak
GradientU        = ∂ζ/∂u
GradientV        = ∂ζ/∂v
```

초기 LocationMask는 다음을 기준으로 만든다.

```text
양의 정규화 높이
× 파두 주변의 제한된 폭
× 기존 Kelvin domain/stamp mask
```

필요하면 1차/2차 미분은 얇은 ridge 선택과 마스크 폭 조절에만 사용한다. 이를 물리적 surface folding이나 실제 쇄파 판정이라고 부르지 않는다.

Golden mask는 다음을 보장해야 한다.

- 좌우 대칭
- Froude별 Kelvin V자와 cusp 구조 보존
- 음의 골 제거
- 시작 apex 부근의 의도하지 않은 포화 방지
- 아트 파라미터로 폭, 임계값, 감마 조정 가능

### 6.3 런타임 생성 강도

Golden A는 위치 후보일 뿐이며 실제 생성량은 이벤트 상태를 결합해 계산한다.

```text
RuntimeSteepness = AmplitudeCm / max(WakeLengthCm, Epsilon)

GenerationStrength =
    smoothstep(KelvinBreakingMin, KelvinBreakingMax, RuntimeSteepness)
    × EventFadeIn
    × EventDecay
    × FroudeFoamScale

KelvinFoamEmission = Golden.A × GenerationStrength × StampMask
```

정확한 길이 기준은 현재 Kelvin 좌표 변환과 Golden normalization을 대조해 결정한다. 모든 파라미터는 로그에 기록한다.

### 6.4 런타임 출력

`SWShipWakeCS`의 기존 이벤트 순회와 Golden sample을 재사용한다.

```text
기존 OutWakeTexture
R   = Height
GBA = Normal XYZ

신규 OutKelvinFoamSource
R   = KelvinFoamEmission
```

`ShipWakeRT` 인코딩은 바꾸지 않는다. 별도 `R16F` UAV를 사용한다.

여러 이벤트의 emission은 포화 합성한다.

```text
CombinedKelvin = 1 - product(1 - EventEmission)
```

## 7. Ripple Foam Emission

### 7.1 기존 Ripple 식

```text
Radius       = WaveSpeed × Age
SignedOffset = DistanceFromOrigin - Radius
Phase        = SignedOffset / WaveLength × 2π
Height       = InitialAmplitude × Decay × cos(Phase) × Envelope
```

현재 Ripple은 수직 높이와 radial gradient만 제공한다. Tessendorf가 요구하는 수평 choppiness displacement `D`는 없으므로 Jacobian/folding 판정은 사용하지 않는다.

### 7.2 Ripple Profile Bake

Ripple은 모든 이벤트가 동일한 정규화 cosine 구조를 사용하므로 1D Profile LUT를 만든다.

```text
R = NormalizedHeight
G = NormalizedRadialDerivative
B = FoamLocationMask
A = EnvelopeShape
```

`FoamLocationMask`는 양의 cosine crest ring의 위치와 폭을 저장한다.

```text
FoamLocationMask = pow(saturate(cos(ProfilePhase)), CrestSharpness)
```

이 텍스처는 Foam을 완성해서 저장하는 것이 아니라, Ripple 위상 안에서 Foam을 방출할 위치를 저장한다. Profile Golden image와 수치 데이터를 함께 생성한다.

성능 비교에서 LUT fetch가 기존 analytic 계산보다 불리하면 같은 식을 Compute에서 직접 평가하되, Profile LUT를 Golden reference로 사용한다. 어느 경로든 결과 위치는 동일해야 한다.

### 7.3 런타임 생성 강도

```text
EffectiveAmplitude = InitialAmplitude × exp(-DecayRate × Age)
WaveNumber         = 2π / max(WaveLength, Epsilon)
RuntimeSteepness   = WaveNumber × EffectiveAmplitude

GenerationStrength =
    smoothstep(RippleBreakingMin, RippleBreakingMax, RuntimeSteepness)
    × Envelope

RippleFoamEmission = ProfileFoamLocation × GenerationStrength
```

이 결과는 Ripple의 동심원 위상, 진폭 감소 및 wave front envelope를 따른다.

### 7.4 런타임 출력

`SWRippleCS`의 기존 이벤트 순회와 phase/envelope 중간값을 재사용한다.

```text
기존 RippleRT
R   = Height
GBA = Normal XYZ

신규 RippleFoamSourceRT
R   = RippleFoamEmission
```

기존 WPO/Normal output은 변경하지 않는다.

## 8. Tessendorf의 역할과 적용 경계

### 8.1 Tessendorf가 제공하는 정보

Tessendorf의 choppy wave는 높이 스펙트럼에서 수평 변위장 `D(x,t)`를 만들고 다음 수평 매핑을 사용한다.

```text
x' = x + λD(x,t)
```

그 미분 행렬에서 다음 정보를 얻을 수 있다.

- Jacobian determinant: 수면 mapping의 겹침 영역
- minimum eigenvalue: folding 발생 여부와 압축 정도
- minimum eigenvector: folding 방향

이는 `어디서, 얼마나 강하게, 어느 방향으로 수평 folding이 발생하는가`에 대한 정보다.

### 8.2 Sea of Thieves에서 확인되는 활용

Sea of Thieves 문서에 직접 명시된 흐름은 다음이다.

```text
FFT choppiness vertex offsets
-> Wave Peak Mask
-> 파두 색상 혼합
-> Foam 생성 위치/양을 제공하는 mask
```

생성된 Foam은 별도의 feedback buffer, progressive blur와 artist-authored texture로 관리한다. 문서에는 Tessendorf의 eigenvector 방향을 Foam 확산 방향으로 사용했다는 내용이 없고, Jacobian이 Foam 수명이나 외형을 결정했다는 내용도 없다.

따라서 이 프로젝트에서 대응시킬 역할은 `완성된 Foam`이 아니라 `Foam emission scalar field`다.

### 8.3 Kelvin/Ripple에 직접 적용하지 않는 이유

- Kelvin/Ripple의 현재 WPO에는 Tessendorf식 수평 변위장이 없다.
- Height gradient는 수평 변위 gradient와 다른 데이터다.
- 가상 수평장을 Foam 판정에만 사용하면 실제 렌더 표면과 folding 판정이 불일치한다.
- 현재 Kelvin/Ripple 모델은 실제 비선형 쇄파를 시뮬레이션하지 않는다.

따라서 Tessendorf Jacobian을 흉내 내지 않는다. 대신 각 파형이 실제로 제공하는 고유 높이/위상에서 안정적인 LocationMask를 만들고 런타임 steepness를 생성 강도로 사용한다.

## 9. Foam 생명주기

Sea of Thieves에서 차용하는 핵심 구조다.

```text
Emission
-> World-space History에 누적
-> 약한 분산
-> 일정 시간 잔류
-> 시간 감쇠
```

Kelvin과 Ripple에만 적용하며 Gerstner에는 적용하지 않는다.

### 9.1 상태식

```text
HistoryK(t+1) =
    Reproject(HistoryK(t)) × exp(-dt/LifetimeK)
    + KelvinEmission × KelvinRate × dt

HistoryR(t+1) =
    Reproject(HistoryR(t)) × exp(-dt/LifetimeR)
    + RippleEmission × RippleRate × dt
```

History는 Unreal의 안정적인 UAV 경로를 위해 ping-pong `RGBA16F`를 사용한다.

```text
R = Kelvin History
G = Ripple History
B = 두 채널의 포화 합집합
A = valid/reserved
```

플랫폼 UAV 제약이나 추가 진단 채널이 필요하면 `RGBA16F`로 확장한다.

### 9.2 최종 합성

```text
ImprovedFoam = 1 - (1-HistoryK) × (1-HistoryR)
FinalFoam    = 1 - (1-GerstnerFoam) × (1-ImprovedFoam)
```

### 9.3 분산 제한

Foam은 Kelvin/Ripple의 마루 후보에서 생성되고 주변으로 약하게 퍼지면서 사라진다. 과도한 등방성 blur는 Kelvin V자와 Ripple 동심원을 둥근 얼룩으로 바꿀 수 있다.

- 첫 구현은 diffusion 0으로 시작한다.
- History와 감쇠가 검증된 뒤에만 약한 4-neighbor diffusion을 추가한다.
- Ripple diffusion은 Kelvin보다 작게 시작한다.
- 분산은 파형 전파가 아니라 Foam 경계를 부드럽게 하는 역할만 한다.
- 여러 번의 blur pass는 사용하지 않는다.

## 10. 카메라 독립성과 거리 LOD

가장 중요한 성공 조건이다.

### 10.1 생성과 저장

- Kelvin/Ripple Foam emission은 Compute에서 월드 XY 위치로 계산한다.
- History는 월드 공간 grid에 저장한다.
- PixelNormalWS, screen UV 및 화면 해상도를 생성 판정에 사용하지 않는다.
- 카메라 중심 grid를 사용하더라도 GridCenter를 texel 크기로 snap한다.
- GridCenter가 이동하면 이전/현재 center를 이용해 History를 월드 위치 기준으로 재투영한다.

```text
Foam 위치와 큰 형태
= Kelvin/Ripple 고유 위치 데이터 + WorldXY

화면 표시 품질
= 거리 Fade + texture filtering + material LOD
```

### 10.2 허용되는 거리 변화

- 멀어질수록 opacity가 감소함
- 텍스처 filtering으로 작은 세부가 합쳐짐
- 고정 History texel 크기보다 작은 디테일이 사라짐

### 10.3 허용되지 않는 거리 변화

- V자 또는 동심원 중심이 월드에서 이동함
- 카메라 이동에 따라 다른 마루에 Foam이 새로 선택됨
- LOD 전환 때 전체 Foam 패턴이 다른 위치로 점프함
- 멀어질수록 생성 마스크 자체가 사라짐

이 요구사항은 설계만으로 완료 처리하지 않고 서로 다른 카메라 거리에서 월드 좌표 기준 Golden 비교로 검증한다.

## 11. FluidFlux와 SWFlux의 역할

`FluidFluxWater.ush`는 Foam 생성 위치나 생명주기를 계산하지 않는다. 앞 단계에서 만들어진 `WaveFoamMask`와 Foam texture 채널을 결합해 최종 opacity를 만든다.

```text
FoamHeightAffect = WaveFoamHeight × WaveFoamMask × 1.6
FoamSoftAffect   = WaveFoamSoft × WaveFoamMask
FoamOpacity      = pow(max(FoamHeightAffect, FoamSoftAffect), FoamPow)
                   × FoamIntensity
```

프로젝트의 `SWFluxOceanFoam.ush`는 현재 마스터에서 Web/Froth 외형 텍스처를 제공한다.

개선 구조에서는:

```text
FinalFoam density
-> FluidFlux식 pow/intensity shaping
-> 기존 SWFlux Web/Froth texture
-> Foam 색상과 opacity/scattering 표현
```

## 12. 자료별 차용, 변형, 미사용 정리

### 12.1 Sea of Thieves

#### 직접 차용

- 파형 분석 결과를 Foam emission mask로 사용하는 역할 분리
- Foam feedback buffer
- 생성 후 잔류와 감쇠
- progressive blur를 이용한 부드러운 분산 개념
- artist-authored Foam texture와 emission/history mask 결합

#### 프로젝트에 맞게 변형

- FFT wave-peak mask 대신 Kelvin Golden/Ripple Profile LocationMask 사용
- 공통 Foam buffer 대신 Kelvin/Ripple 채널을 분리해 서로 다른 수명 적용
- 강한 progressive blur 대신 형태 보존형 약한 diffusion
- 카메라 중심 buffer에 world reproject와 texel snap을 명시적으로 추가

#### 사용할 수 없음

- Tessendorf horizontal displacement Jacobian을 Kelvin/Ripple에 직접 적용
- Tessendorf folding direction을 Kelvin/Ripple Foam 방향으로 사용

현재 Kelvin/Ripple은 필요한 수평 변위 데이터를 제공하지 않는다.

#### 이번에 사용하지 않음

- 물체와 수면 교차 depth buffer Foam
- calm/normal/storm에 따른 전체 날씨별 Foam 시스템
- Gerstner Foam 생명주기 변경

### 12.2 Tessendorf

#### 참고하는 것

- 파형 생성 데이터에서 Foam emission 정보를 분리해 추출한다는 사고방식
- Foam 후보는 최종 색이 아니라 후속 생명주기의 입력이라는 역할
- FFT 기반 Sea of Thieves peak mask의 근거

#### 사용하지 못하는 것

- Kelvin/Ripple의 Jacobian folding mask
- minimum eigenvalue/eigenvector 기반 압축 강도와 방향

#### 사용하지 않는 이유

- 현재 Kelvin/Ripple에 수평 displacement field와 그 derivative가 없음
- WPO를 유지한 채 가상 수평장만 판정에 사용하면 렌더 표면과 불일치
- 목표가 WPO/Normal 개선이 아니라 Foam 구조 개선으로 제한됨

### 12.3 FluidFluxWater.ush

#### 직접 차용

- Foam soft/height texture 채널 결합
- Foam mask의 power/intensity shaping
- Foam color, opacity 및 scattering 표현에 density를 연결하는 방식

#### 사용할 수 없음

- Kelvin/Ripple Foam 위치 판정: 해당 로직이 파일에 없음
- Foam feedback, 누적, 수명 및 감쇠: 해당 로직이 파일에 없음

#### 이번에 사용하지 않음

- Shallow Foam
- shoreline/depth 기반 Foam
- 전체 Water BRDF 및 scattering 파이프라인 교체

### 12.4 현재 프로젝트 자원

#### 재사용

- Kelvin Froude Golden Python Baker
- Kelvin RGBA16F Golden loader와 기존 Golden image/metadata
- 기존 Golden Height와 analytical gradient
- 기존 Kelvin event texture, Golden sample, fade 및 decay
- 기존 Ripple event, phase, envelope, decay 및 analytic gradient
- 기존 `ShipWakeRT`, `RippleRT` WPO/Normal 경로
- 기존 `SWFluxOceanFoam.ush` Web/Froth texture
- Unreal Water Body MID parameter binding과 RDG Compute 패턴

#### 추가

- Kelvin Golden A Foam LocationMask
- Ripple 1D Profile LUT와 Golden reference
- Kelvin/Ripple Foam Source RT
- Kelvin/Ripple History ping-pong RT
- 카메라 grid world reprojection
- 진단 readback과 Golden comparison

## 13. 구현 단계

### Phase 0 - 기존 자원 보존과 기준선

1. Kelvin 기존 Golden R/G/B checksum과 시각 Golden을 보존한다.
2. Ripple 기존 Height/Normal Golden을 생성한다.
3. 현재 Gerstner Foam 머티리얼 결과를 기준 이미지로 저장한다.
4. Foam 변경 전 Kelvin/Ripple WPO와 Normal GPU 출력을 저장한다.

완료 조건:

- 이후 변경에서 Kelvin Golden R/G/B가 허용 오차 내 동일하다.
- Kelvin/Ripple WPO와 Normal이 변경되지 않았음을 비교할 수 있다.
- Gerstner Foam이 변경되지 않았음을 비교할 수 있다.

### Phase 1 - Kelvin Foam Golden

1. Kelvin Baker에 Foam LocationMask 생성 파라미터를 추가한다.
2. Golden A를 1.0에서 Foam LocationMask로 변경한다.
3. Froude별 mask preview, coverage와 histogram을 출력한다.
4. 좌우 대칭, NaN/Inf, 범위 및 checksum 자동 검사를 추가한다.
5. R/G/B 기존 데이터 회귀 테스트를 수행한다.

### Phase 2 - Ripple Foam Profile

1. 정규화 Ripple Profile LUT 생성 스크립트를 추가한다. 런타임은 동등한 analytic phase를 재사용한다.
2. Height, radial derivative, Foam LocationMask, envelope를 저장한다.
3. LUT와 기존 analytic Ripple의 height/gradient 패리티를 검증한다.
4. 동심원 방사형 대칭 Golden test를 추가한다.

### Phase 3 - Kelvin/Ripple Foam Source Compute

1. `SWShipWakeCS`가 기존 Golden A와 이벤트 파라미터로 Kelvin emission을 출력한다.
2. `SWRippleCS`가 Profile/phase와 이벤트 파라미터로 Ripple emission을 출력한다.
3. 기존 pass와 event scan을 재사용하며 두 번째 이벤트 순회를 만들지 않는다.
4. Source RT는 UAV 정밀도와 범용 RHI 지원을 위해 R16F를 사용한다.
5. 기존 WPO/Normal output hash와 Golden을 재검증한다.

### Phase 4 - Kelvin/Ripple History Compute

1. 512×512, 30000 cm 기준의 world-space ping-pong RGBA16F History를 생성한다.
2. 이전/현재 GridCenter 재투영과 texel snap을 구현한다.
3. Kelvin/Ripple별 lifetime, generation rate와 diffusion을 적용한다.
4. 초기 diffusion은 0으로 설정한다.
5. Water Body MID에 History texture, center, size와 enable을 바인딩한다.

### Phase 5 - 마스터 머티리얼 통합

1. 기존 Gerstner Foam 경로를 보존한다.
2. History RG에서 Kelvin/Ripple ImprovedFoam을 만든다.
3. 기존 Gerstner Foam과 채널별 max 합성해 동일 Foam 층의 중복 가산을 막는다.
4. 기존 SWFlux Web/Froth 외형을 유지한다.
5. FluidFlux식 density shaping 파라미터를 연결한다.
6. 거리 Fade는 opacity에만 적용하고 History와 emission에는 적용하지 않는다.

### Phase 6 - 테스트 구동과 조정

1. Kelvin 단독, Ripple 단독, 동시 활성, Gerstner 동시 활성 장면을 실행한다.
2. 카메라 거리와 각도만 바꾼 Golden frame을 비교한다.
3. Kelvin 6척 조건에서 GPU 비용과 RT coverage를 측정한다.
4. diffusion 0에서 형태를 검증한 뒤 필요한 최소값만 적용한다.
5. 로그와 readback에서 이상이 없을 때 머티리얼 기본값을 확정한다.

## 14. 진단 로그와 Shader 검증

GPU Material/Compute shader는 일반 `UE_LOG`를 직접 호출할 수 없다. 진단 모드에서 Source와 History RT를 낮은 주기로 CPU readback하여 셰이더 결과를 로그로 남긴다.

명령행:

```text
-SWFoamDiagnostics
```

로그 태그:

```text
[SW-FOAM][INIT]
[SW-FOAM][GOLDEN]
[SW-FOAM][KELVIN-SOURCE]
[SW-FOAM][RIPPLE-SOURCE]
[SW-FOAM][HISTORY]
[SW-FOAM][GRID]
[SW-FOAM][MATERIAL]
[SW-FOAM][PERF]
[SW-FOAM][ERROR]
```

기록 항목:

- RT 크기, format, UAV 생성 성공 여부
- Kelvin Golden R/G/B 회귀 checksum과 A mask checksum
- Ripple LUT/analytic 패리티 오차
- Kelvin Source min/mean/max/coverage와 좌우 대칭 오차
- Ripple Source min/mean/max/coverage와 방사형 대칭 오차
- History R/G min/mean/max/coverage와 frame decay
- GridCenter, texel snap, world reprojection offset
- Water MID 수와 parameter binding 상태
- dispatch 해상도, event 수와 GPU pass timing
- NaN, Inf, 예상 밖 포화 및 비어 있는 source 경고

readback은 진단 모드에서만 실행하며 일반 플레이에서는 비활성화한다.

## 15. Golden Image와 성공 기준

### 15.1 Golden 자원

1. 기존 Kelvin Height/Gradient
2. 신규 Kelvin Foam LocationMask
3. Kelvin runtime Foam Source
4. 기존 Ripple Height/Normal
5. Ripple Profile Foam LocationMask
6. Ripple runtime Foam Source
7. History Kelvin/Ripple 채널
8. ImprovedFoam
9. 기존 Gerstner와 합성된 최종 Foam
10. 최종 Water Material

### 15.2 성공 기준

1. Gerstner Foam 결과와 파라미터 반응이 변경 전과 동일하다.
2. Kelvin Foam이 Golden의 좌우 대칭과 V/cusp 형태를 따른다.
3. Ripple Foam이 동심원 위상과 중심을 유지한다.
4. Kelvin/Ripple WPO와 Normal이 변경 전 Golden과 동일하다.
5. Kelvin/Ripple Foam이 생성 후 서로 다른 수명으로 잔류하고 감쇠한다.
6. 카메라 거리와 Water Mesh LOD가 바뀌어도 Foam의 기준 월드 위치와 큰 형태가 이동하지 않는다.
7. 멀어질 때는 opacity와 작은 세부만 감소한다.
8. 진단 로그에 NaN, Inf, 비정상 포화와 parameter binding 실패가 없다.
9. 화면 내 Kelvin 6척 조건이 확정 GPU budget 안에 들어온다.

## 16. 성능 원칙

- Kelvin/Ripple의 기존 Compute pass와 event scan을 재사용한다.
- Kelvin Golden A는 기존 sample에 포함하므로 추가 Golden fetch를 만들지 않는다.
- Ripple은 기존 phase/envelope 중간값을 재사용하거나 Profile LUT 한 번으로 제한한다.
- Source RT는 R16F다. History는 Unreal의 안정적인 UAV/readback 경로를 위해 RGBA16F를 사용한다.
- 512×512 RGBA16F ping-pong History 두 장은 약 4 MiB다. Kelvin/Ripple Source 두 장은 합계 약 1 MiB다.
- History는 한 번의 Compute dispatch에서 Kelvin/Ripple 채널을 병렬 갱신한다.
- 여러 blur pass를 사용하지 않는다.
- RDG event name을 추가해 Unreal Insights/RDG Insights에서 pass 비용을 분리한다.
- Source/History readback은 진단 실행에서만 사용한다.

### 16.1 실제 검증 결과 (2026-08-23)

- Development Editor C++ 빌드 성공.
- `ArtisticSW.Water.ShipWake` 자동화 테스트 2/2 성공. Kelvin Golden A의 유효 mask와 좌우 대칭 오차 `< 0.002`를 검사한다.
- D3D12 SM6 실제 RHI에서 Test_Level을 360프레임 이상 실행했다. 세 Compute Shader와 마스터 머티리얼 컴파일 오류, ensure, GPU crash가 없었다.
- 테스트 주입 시 Kelvin Source `Max=0.687988`, Ripple Source `Max=0.894531`을 확인했다.
- 다음 진단 시 Kelvin Source가 0이 된 후에도 Kelvin History `Max=0.780762`가 남아 source 종료 뒤 잔류/감쇠가 동작함을 확인했다.
- 동일 640×360 오프스크린 장면의 300프레임 CSV 비교에서 History ON/OFF GPU 중앙값은 2.75/2.72 ms였다. 관측 차이 약 0.03 ms는 단일 실행 기준이며 플랫폼별 재측정이 필요하다.
- Game Thread 중앙값은 ON/OFF 2.39/2.37 ms였다. 정상 실행에서는 이벤트 재순회와 readback이 없어 CPU 증가는 tick/dispatch 제출 수준이다.

검증 산출물:

```text
Saved/Diagnostics/SWFoam/Automation/index.html
Saved/Diagnostics/SWFoam/Kelvin/
Saved/Diagnostics/SWFoam/Ripple/
Saved/Profiling/CSV/Profile(20260823_184311).csv  # History ON
Saved/Profiling/CSV/Profile(20260823_184343).csv  # History OFF
```

아직 수동 승인이 필요한 항목은 실제 게임 카메라의 여러 거리/각도 Golden 비교와 화면 내 운항 Kelvin 6척 부하 측정이다. 구현상 emission과 history에는 camera distance, screen UV, PixelNormalWS가 들어가지 않으며, grid 이동은 이전/현재 world center로 재투영한다. 따라서 거리 변화는 Foam 월드 위치 판정에 관여하지 않는다. 실제 Water Mesh LOD별 시각적 안정성은 위 Golden 비교로 최종 승인한다.

### 16.2 런타임 조절값

```text
sw.Foam.HistoryResolution          512
sw.Foam.Kelvin.Lifetime            6.0
sw.Foam.Ripple.Lifetime            2.5
sw.Foam.Kelvin.GenerationRate      2.0
sw.Foam.Ripple.GenerationRate      2.5
sw.Foam.Kelvin.Diffusion           0.0
sw.Foam.Ripple.Diffusion           0.0
sw.Foam.Kelvin.SteepnessMin        0.0015
sw.Foam.Kelvin.SteepnessMax        0.0060
sw.Foam.Ripple.SteepnessMin        0.10
sw.Foam.Ripple.SteepnessMax        0.45
sw.Foam.Ripple.CrestSharpness      6.0
```

`Diffusion=0`이 기본값이다. Kelvin V자와 Ripple 동심원을 보존한 상태로 확인한 뒤 필요한 경우에만 최소값을 올린다.

## 17. 참고 자료

- Nigel Ang et al., *The Technical Art of Sea of Thieves*, SIGGRAPH Talks 2018.
- Jerry Tessendorf, *Simulating Ocean Water*: https://people.computing.clemson.edu/~jtessen/reports/papers_files/coursenotes2004.pdf
- Darmon, Benzaquen, Raphael, *Kelvin wake pattern at large Froude numbers*, JFM 738 R3, arXiv:1309.6751.
- Unreal Engine Render Dependency Graph: https://dev.epicgames.com/documentation/unreal-engine/render-dependency-graph-in-unreal-engine
- 프로젝트 `FluidFluxWater.ush`, `SWFluxOceanFoam.ush`, `SWRipple.ush`, `SWShipWake.ush`.
- 프로젝트 Kelvin Golden Python Baker와 기존 Golden image/metadata.

## 18. 아직 고려하지 않은 비-Foam 기술

Sea of Thieves 자료에는 view angle, sun direction와 wave peak mask를 이용한 deep/subsurface water color 혼합, scattering approximation, 넓은 저각도 태양 반사를 위한 closest-point-on-sphere 근사, 수중 Snell's Window가 있다. 또한 별도의 geometry 기반 cloudscape, 저해상도 blur와 depth compositing 기술도 설명한다. `FluidFluxWater.ush`에는 Fresnel/specular, scattering coefficient, absorption/transmittance, shoreline/depth 및 BRDF 관련 함수가 있다. 이 항목들은 Foam 개선 범위가 아니므로 아직 프로젝트 적용 가능성, 비용 및 현재 마스터와의 충돌 여부를 검토하지 않았으며 후속 광학/구름 연구 문서에서 별도로 다룬다.
