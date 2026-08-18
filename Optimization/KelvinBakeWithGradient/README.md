# Python Kelvin Wake Baker with Analytical Gradients (M7/M4 Optimization)

이 파이프라인은 Kelvin Wake의 수면 높이(Height, $\zeta$)뿐만 아니라, **해석적 기울기(Analytical Gradients: $\partial\zeta/\partial u, \partial\zeta/\partial v$)를 텍스처의 G/B 채널에 미리 구워 넣는(Bake)** 생성기입니다.

---

## 1. 최적화 핵심 배경 및 원리

### 기존 방식의 문제점 (Finite Difference 3중 루프)
기존 셰이더에서는 노멀(Normal)을 구하기 위해 현재 위치(Center)와 주변 2점(PosX, PosY)의 높이를 구하느라 **동일한 1024/N 루프 함수(`SW_M7_EVALUATE_KELVIN`)를 3번 연속 호출**했습니다.

### 개선 방식 (Analytical Gradient Texture Baking)
골든 텍스처를 샘플링할 때 **단 1회의 텍스처 페치(`Texture2DSampleLevel`)**로 높이와 기울기를 동시에 읽어옵니다:

| 채널 (Channel) | 데이터 내용 | 용도 |
|:---|:---|:---|
| **R (Red)** | Normalized Elevation $\zeta / Z_{peak}$ | 파도의 높이 (World Position Offset / Z) |
| **G (Green)** | Normalized Downstream Slope $(\partial\zeta/\partial u) / Z_{peak}$ | 후방 방향(U) 경사도 $\rightarrow$ 노멀 X/Y 변환 |
| **B (Blue)** | Normalized Lateral Slope $(\partial\zeta/\partial v) / Z_{peak}$ | 측면 방향(V) 경사도 $\rightarrow$ 노멀 X/Y 변환 |
| **A (Alpha)** | Validity Mask (1.0) | 유효 마스크 / 패딩 |

---

## 2. 수학적 유도식 (Analytical Derivatives)

Darmon et al. (2013)의 켈빈 수면 변위 적분식:
$$\zeta(u, v) = -2 \int_{0}^{\pi/2} \frac{\hat{P}(\theta, Fr)}{\cos^4\theta} \sin\left(\frac{2\pi u}{\cos\theta}\right) \cos\left(\frac{2\pi v \sin\theta}{\cos^2\theta}\right) d\theta$$

이를 $u$와 $v$에 대해 편미분한 해석적 적분식:
$$\frac{\partial\zeta}{\partial u} = -4\pi \int_{0}^{\pi/2} \frac{\hat{P}(\theta, Fr)}{\cos^5\theta} \cos\left(\frac{2\pi u}{\cos\theta}\right) \cos\left(\frac{2\pi v \sin\theta}{\cos^2\theta}\right) d\theta$$

$$\frac{\partial\zeta}{\partial v} = +4\pi \int_{0}^{\pi/2} \frac{\hat{P}(\theta, Fr)\sin\theta}{\cos^6\theta} \sin\left(\frac{2\pi u}{\cos\theta}\right) \sin\left(\frac{2\pi v \sin\theta}{\cos^2\theta}\right) d\theta$$

---

## 3. HLSL 셰이더 적용 가이드 (수치미분 제거)

### 기존 셰이더 (`SWShipWake.ush`):
```hlsl
// ❌ 기존: 루프를 3번 돌림
SW_M7_EVALUATE_KELVIN(Pos, ..., H_Center);
SW_M7_EVALUATE_KELVIN(Pos + (35, 0), ..., H_PosX);
SW_M7_EVALUATE_KELVIN(Pos + (0, 35), ..., H_PosY);
```

### 새로운 셰이더 (단 1회 루프 / 1회 텍스처 샘플링):
```hlsl
// ✅ 개선: 단 1회 샘플링으로 높이와 기울기를 모두 획득!
const float4 SW_GoldenSample = Texture2DSampleLevel(GOLDEN_TEX, GOLDEN_SAMPLER, SW_GoldenUV, 0.0);
const float SW_GoldenH = SW_GoldenSample.r * SW_StampMask;
const float SW_GoldenGradU = SW_GoldenSample.g * SW_StampMask;
const float SW_GoldenGradV = SW_GoldenSample.b * SW_StampMask;

// 월드 공간 기울기 변환 (Chain rule)
const float SW_dH_dDownstream = (SW_Amp / SW_Length) * SW_GoldenGradU * SW_FadeIn * SW_Decay;
const float SW_dH_dLateral = (SW_Amp / SW_HalfWidth) * SW_GoldenGradV * SW_FadeIn * SW_Decay;

// 선박의 Forward / Right 벡터를 이용해 World dH/dX, dH/dY로 투영
const float2 SW_GradWorld = SW_dH_dDownstream * (-SW_Forward) + SW_dH_dLateral * SW_Right;
SW_TotalGradWorld += SW_GradWorld;

// 최종 노멀 계산
OUT_NORMAL = normalize(float3(-SW_TotalGradWorld.x * NORMAL_STRENGTH, -SW_TotalGradWorld.y * NORMAL_STRENGTH, 1.0));
```

---

## 4. 디렉터리 구성 및 생성 파일

```text
Kelvin Bake with gradient/
├── baker_core.py                  # 수치적분 및 해석적 기울기 코어
├── generate_golden_images.py      # Golden 이미지 & Gradient 맵 생성기 (Fr = 0.30, 0.50, 0.70, 1.00)
├── atlas_baker.py                 # Multi-slice RGBA16F Atlas 생성기
├── verify_parity_and_physics.py   # 수학적/물리적 패리티 및 FP16 검증 스위트
├── run_pipeline.py                # 전체 마스터 실행 스크립트
├── run_all.bat                    # 언리얼 파이썬 기반 원클릭 실행 배치
├── VALIDATION_REPORT.json         # 자동 검증 결과 JSON
├── VALIDATION_SUMMARY.md          # 자동 검증 요약 리포트
├── golden_images/                 # 골든 이미지, 기울기 맵, 바이너리 출력
│   ├── golden_Fr0.50_2D_height.png              # R 채널: 높이맵
│   ├── golden_Fr0.50_2D_gradient_u.png           # G 채널: 후방 기울기맵
│   ├── golden_Fr0.50_2D_gradient_v.png           # B 채널: 측면 기울기맵
│   ├── golden_Fr0.50_2D_gradient_normal_rgb.png  # 직관적 Normal RGB 맵
│   ├── kelvin_wake_golden_fr050_rgba16f.bin      # UE 임포트용 RGBA16F 바이너리
│   └── ...
└── atlas_output/                  # Multi-slice 아틀라스 출력
    ├── kelvin_wake_atlas_gradient_fp16.bin      # 12-Slice RGBA16F 바이너리
    ├── kelvin_wake_atlas_meta.json              # 메타데이터 JSON
    └── KelvinAtlasGradientConstants.h           # C++ / HLSL 통합 헤더
```

---

## 5. 실행 방법

탐색기에서 `run_all.bat`을 더블클릭하거나 PowerShell에서 다음 명령을 실행합니다:
```powershell
& "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\ThirdParty\Python3\Win64\python.exe" run_pipeline.py
```
