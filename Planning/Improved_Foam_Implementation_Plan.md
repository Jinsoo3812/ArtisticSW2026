# Direct Kelvin/Ripple Foam 구현 문서

## 1. 최종 목표와 범위

Foam의 물리적 생성·이류·잔류를 모사하지 않는다. Kelvin은 Kelvin V자 파형에, Ripple은 동심원 위상에 정확히 붙어 있는 현재 프레임 Foam을 직접 표시한다.

- Gerstner: 기존 높이/가파름 Bounds Foam 유지
- Kelvin: Golden Image A에 미리 구운 Foam 발생 위치와 Kelvin 이벤트의 현재 stamp/envelope/감쇠를 사용
- Ripple: 현재 cosine 위상, crest 위치, radial envelope와 진폭 감쇠를 사용
- Gerstner/Kelvin/Ripple WPO와 Normal 계산은 변경하지 않음

Kelvin/Ripple은 합성된 최종 WPO 높이나 PixelNormalWS를 공통 판정하지 않는다. 따라서 Gerstner 또는 서로의 파도가 겹쳐도 각자의 Foam 발생 위치가 변하지 않는다.

## 2. 최종 데이터 흐름

```text
Kelvin event + Golden A Foam 위치
-> 기존 Kelvin Compute의 동일 event loop
-> SWShipWake_FoamSource R16F
-> 월드 좌표 직접 sample

Ripple event + analytic cosine phase/crest/envelope
-> 기존 Ripple Compute의 동일 event loop
-> SW_Ripple_FoamSource R16F
-> 월드 좌표 직접 sample

Gerstner legacy Foam + Kelvin direct Foam + Ripple direct Foam
-> Ocean Foam Custom에서 비가산 합성
```

현재 프레임 Source가 곧 Foam 밀도다. Kelvin/Ripple 파형이 약해지거나 사라지면 Foam도 함께 약해지거나 사라진다.

## 3. 제거한 생명주기 시스템

다음 요소를 전부 제거했다.

- `SWImprovedFoamHistoryCS`
- `USWImprovedFoamSubsystem`
- 512×512 RGBA16F ping-pong History 두 장
- 이전 Source memory BA 채널
- onset, generation, lifetime, sustain, diffusion
- History world reprojection
- History 진단 readback과 관련 CVar
- 머티리얼의 `SW Improved Foam State` 및 History sample Custom

이 제거로 움직이는 Kelvin Source의 양의 변화분을 매 프레임 신규 Foam으로 증폭하던 진동 원인도 사라졌다.

## 4. Kelvin Foam 원리

Kelvin Foam은 단순 높이 마스크가 아니다.

```text
GoldenImage.A
× Kelvin stamp mask
× 이벤트 fade-in
× Kelvin decay
× Kelvin 고유 steepness qualification
= Kelvin Foam Source
```

Golden A는 Froude 기반 Kelvin 베이크 단계에서 지정된 Foam 발생 위치다. WPO/Normal과 같은 이벤트 좌표, 시간, Froude profile 및 Golden sample을 재사용하므로 V자 형상과 함께 움직인다.

## 5. Ripple Foam 원리

Ripple도 합성 높이를 판정하지 않는다. 기존 Ripple Compute의 analytic 위상을 직접 사용한다.

```text
cosine phase의 crest 위치
× radial envelope
× amplitude decay
× Ripple 고유 steepness qualification
= Ripple Foam Source
```

마루 폭은 world-grid texel footprint보다 좁아지지 않도록 band-limit되어 있다. 따라서 Ripple의 동심원 모양을 유지하면서 sub-texel 점멸을 줄인다.

## 6. 머티리얼 구성

추가 경로는 다음으로 제한한다.

- Texture Object Parameter 2개
  - `SW Kelvin Foam Source`
  - `SW Ripple Foam Source`
- Kelvin/Ripple 직접 월드 샘플 Custom 1개
- WPO를 제외한 Absolute World Position 1개
- 사용자 조절 Scalar Parameter 6개
- 기존 Ocean Foam Custom의 `DirectFoam` 입력 1개

직접 샘플 Custom 출력:

```text
R = Kelvin Foam
G = Ripple Foam
B = Kelvin/Ripple 비가산 union
```

Ocean Foam은 Gerstner legacy 결과와 direct Foam 색상을 `max`로 합성한다.

## 7. 머티리얼 인스턴스 파라미터

```text
SW Kelvin Foam Enabled        0.0
SW Ripple Foam Enabled        0.0
SW Kelvin Foam Intensity      1.0
SW Kelvin Foam Display Min    0.02
SW Kelvin Foam Display Max    0.60

SW Ripple Foam Intensity      1.0
SW Ripple Foam Display Min    0.02
SW Ripple Foam Display Max    0.60
```

- `Kelvin Foam Enabled`: 0이면 Kelvin WPO/Normal은 유지하면서 머티리얼 Source sample, Compute의 Foam 수식과 Foam UAV write를 건너뜀
- `Ripple Foam Enabled`: 0이면 Ripple WPO/Normal은 유지하면서 머티리얼 Source sample, Compute의 Foam 수식과 Foam UAV write를 건너뜀
- `Intensity`: 최종 Kelvin/Ripple Foam 강도
- `Display Min`: Source가 보이기 시작하는 값. 낮추면 약한 꼬리까지 표시
- `Display Max`: 완전한 Foam 강도에 도달하는 값. 낮추면 더 넓고 강하게 표시

두 파형은 독립 파라미터를 사용한다. Source Texture Object는 런타임 서브시스템이 설정하므로 MI에서 직접 바꾸지 않는다.

## 8. 카메라와 LOD

두 Source RT는 각 파형 Compute의 snapped world grid에 저장된다. 머티리얼은 WPO 적용 전 Absolute World Position과 각 Source의 GridCenter/GridSize로 UV를 계산한다.

Foam 발생 판정에는 camera distance, screen UV, PixelNormalWS 또는 Gerstner/Ripple/Kelvin 합성 결과가 들어가지 않는다. 카메라 이동은 grid coverage를 옮길 뿐 Kelvin/Ripple의 기준 월드 위치를 선택하지 않는다.

## 9. 비용

- 제거: RGBA16F History RT 2장 약 4 MiB
- 제거: History Compute dispatch 1회/프레임
- 제거: History용 source sampling, decay, diffusion 및 reprojection
- 유지: Kelvin/Ripple 기존 Compute와 각 R16F Source RT
- 머티리얼 추가 비용: R16F texture sample 2회와 간단한 UV/remap 연산

Kelvin/Ripple Source는 기존 WPO/Normal event loop에서 동시에 기록하므로 추가 event scan이나 CPU 파형 계산은 없다.

## 10. 구현 파일

```text
Shaders/SWShipWake.ush
Shaders/SWShipWakeCS.usf
Shaders/SWRipple.ush
Shaders/SWRippleCS.usf
Source/WaterAndShip/Private/SWShipWakeSubsystem.cpp
Source/WaterAndShip/Private/RippleSubsystem.cpp
Scripts/integrate_improved_foam_material.py
Content/Blueprints/Water/M_Realistic_Water.uasset
```

삭제 파일:

```text
Shaders/SWImprovedFoamHistoryCS.usf
Source/WaterAndShip/Private/SWImprovedFoamSubsystem.cpp
Source/WaterAndShip/Public/SWImprovedFoamSubsystem.h
```

## 11. 검증 결과

- Development Editor C++ 빌드 성공
- 마스터 머티리얼 검사: 직접 Source Custom 1개, History Custom 0개, History State Parameter 0개
- D3D12 SM6 실제 실행 및 Ripple 주입 성공
- Material/Compute shader 오류, ensure, assertion, GPU crash 없음
- `ArtisticSW.Water.ShipWake` 자동화 테스트 2/2 성공

실제 운항 Kelvin과 Ripple의 최종 시각 강도는 MI의 각 `Intensity/Display Min/Display Max`로 승인한다.

## 12. Sea of Thieves/Tessendorf/FluidFlux 적용 범위

차용:

- 파형 생성과 Foam 발생 위치 정보를 분리한다는 원칙
- Kelvin Golden에 Foam 위치를 사전 베이크하는 방식
- Foam 결과를 파형별 독립 채널로 다루는 방식
- 월드 공간에서 카메라와 무관하게 위치를 결정하는 방식

사용하지 않음:

- Sea of Thieves의 Foam 생명주기, 이류, 잔류, 확산
- Tessendorf FFT horizontal displacement Jacobian/folding 판정
- FFT peak mask
- FluidFlux의 유체 simulation/shoreline accumulation 경로

현재 Kelvin/Ripple은 FFT 수평 변위장을 제공하지 않고 목표도 실제 쇄파 시뮬레이션이 아니므로 위 기능들은 복잡도만 늘린다.

Foam 외 광학, 반사/투과, 구름, Shore Foam과 Kelvin 시작점 생성 문제는 이 구현 범위에 포함하지 않는다.
