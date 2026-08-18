# ArtisticSW2026 런타임 최적화 종합 기록 및 분석 일지

> **작성 일시**: 2026-08-18  
> **기록 위치**: `c:\Unreal Projects\ArtisticSW2026\Optimization\Optimization_Log.md`  
> **데스크탑 동기화**: `C:\Users\vlvkr\OneDrive\Desktop\Optimization\Optimization_Log.md`  
> **데이터 보관 경로**: `C:\Users\vlvkr\OneDrive\Desktop\Optimization\Data\`  
> **프로젝트**: ArtisticSW2026  

---

## 1. 아카이브 데이터 파일 목록 (`/Optimization/Data/`)

* `01_Baseline_Gameplay_Profile(20260818_121541).csv`: 최적화 전 최초 런타임 CSV 프로파일 (3,194 프레임)
* `02_Nanite_Applied_Profile(20260818_133800).csv`: **선박 Nanite 적용 후 런타임 CSV 프로파일 (1,065 프레임)**
* `01_Baseline_PrimitiveStats.csv`: 씬 내 오브젝트별 폴리곤/인스턴스/메모리 실측 CSV
* `01_BeforeNanite_GPUProfile.profViz`: Nanite 적용 전 GPU 프로파일러 덤프
* `02_AfterNanite_GPUProfile.profViz`: Nanite 적용 후 GPU 프로파일러 덤프

---

## 2. 최적화 단계별 성과 추이 (Milestones)

| 최적화 단계 | 전체 프레임 (Frame) | 렌더 스레드 (Draw) | GPU 타임 (GPUTime) | RHI 스레드 (RHIT) | 게임 FPS | 주요 조치 내용 |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| **최초 Baseline** | 18.12 ms | 17.63 ms | 15.58 ms | 12.46 ms | **55.2 FPS** | 최적화 전 순정 상태 |
| **1단계: 선박 Nanite** | 16.42 ms | 15.77 ms | 13.77 ms | 10.48 ms | **60.9 FPS** | 선박 Nanite 활성화, VSM 큐 오버플로우 해소 |
| **2단계: 바다 LOD 완화** | 13.94 ms | 13.95 ms | 11.82 ms | 7.75 ms | **71.75 FPS** | **Water LODScale 낮춤 ➔ 70 FPS 돌파!** |
| **3단계: 굴절/반사 1/2 고정** | **13.93 ms** | **14.09 ms** | **11.48 ms** | **8.16 ms** | **71.77 FPS** | **DefaultEngine.ini에 반사/굴절 1/2 고정 반영 완료** |

---

## 3. 프로젝트 설정(`DefaultEngine.ini`) 상시 적용 내역

[`Config/DefaultEngine.ini`](file:///c:/Unreal%20Projects/ArtisticSW2026/Config/DefaultEngine.ini)의 `[/Script/Engine.RendererSettings]`에 아래 2개 공인 최적화 설정을 영구 고정했습니다:

```ini
[/Script/Engine.RendererSettings]
r.Water.SingleLayer.Reflection.DownsampleFactor=2
r.Water.SingleLayer.RefractionDownsampleFactor=2
```

* **`r.Water.SingleLayer.Reflection.DownsampleFactor=2`**:
  - 수면 루멘/SSR 반사 연산을 1/2 해상도로 처리하여 반사 비용을 절반으로 감축.
* **`r.Water.SingleLayer.RefractionDownsampleFactor=2`**:
  - 물속 투과 굴절 연산 및 VRAM 복사 대역폭을 1/2 해상도로 처리하여 **GPU 타임 -1.26ms 즉시 절감**.

---
*(다음 작업: **M_Realistic_Water 노멀/SSS 픽셀 셰이더 경량화 검토**)*
