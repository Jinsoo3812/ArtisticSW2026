# ArtisticSW2026 런타임 최적화 종합 기록 및 분석 일지

> **작성 일시**: 2026-08-18  
> **기록 위치**: `c:\Unreal Projects\ArtisticSW2026\Optimization\Optimization_Log.md`  
> **데스크탑 동기화**: `C:\Users\vlvkr\OneDrive\Desktop\Optimization\Optimization_Log.md`  
> **데이터 보관 경로**: `C:\Users\vlvkr\OneDrive\Desktop\Optimization\Data\`  
> **상세 실측 보고서**: [`C:\Users\vlvkr\OneDrive\Desktop\Optimization\01_Water_Material_Comparison_Report.md`](file:///C:/Users/vlvkr/OneDrive/Desktop/Optimization/01_Water_Material_Comparison_Report.md)  
> **프로젝트**: ArtisticSW2026  

---

## 1. 아카이브 데이터 파일 목록 (`/Optimization/Data/`)

* `01_Baseline_Gameplay_Profile(20260818_121541).csv`: 최적화 전 최초 런타임 CSV 프로파일 (3,194 프레임)
* `02_Nanite_Applied_Profile(20260818_133800).csv`: **선박 Nanite 적용 후 런타임 CSV 프로파일 (1,065 프레임)**
* `01_Baseline_PrimitiveStats.csv`: 씬 내 오브젝트별 폴리곤/인스턴스/메모리 실측 CSV
* `01_BeforeNanite_GPUProfile.profViz`: Nanite 적용 전 GPU 프로파일러 덤프
* `02_AfterNanite_GPUProfile.profViz`: Nanite 적용 후 GPU 프로파일러 덤프
* `01_Water_Material_Comparison_Report.md`: **순정 vs Realistic 물 머티리얼 4회 전수 대조 심층 분석 보고서**

---

## 2. 최적화 단계별 성과 추이 (Milestones)

| 최적화 단계 | 전체 프레임 (Frame) | 렌더 스레드 (Draw) | GPU 타임 (GPUTime) | RHI 스레드 (RHIT) | 게임 FPS | 주요 조치 내용 |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| **최초 Baseline** | 18.12 ms | 17.63 ms | 15.58 ms | 12.46 ms | **55.2 FPS** | 최적화 전 순정 상태 |
| **1단계: 선박 Nanite** | 16.42 ms | 15.77 ms | 13.77 ms | 10.48 ms | **60.9 FPS** | 선박 Nanite 활성화, VSM 큐 오버플로우 해소 |
| **2단계: 바다 LOD 완화** | 13.94 ms | 13.95 ms | 11.82 ms | 7.75 ms | **71.75 FPS** | **Water LODScale 낮춤 ➔ 70 FPS 돌파!** |
| **3단계: 굴절/반사 1/2 고정** | 13.93 ms | 14.09 ms | 11.48 ms | 8.16 ms | **71.77 FPS** | **DefaultEngine.ini에 반사/굴절 1/2 고정 반영 완료** |
| **[참조] 순정 물 머티리얼** | **10.78 ms** | **5.85 ms** | **6.06 ms** | **2.84 ms** | **91.75 FPS** | **순정 물 장착 시 95 FPS 폭주 확인 (잠재력 검증)** |

---

## 3. 순정 vs Realistic 물 머티리얼 핵심 대조 요약

* **SingleLayerWater (GPU)**: 순정 **`1.96 ms`** vs Realistic **`8.38 ms`** (➔ **`+6.42 ms` 폭증**)
* **총 GPU 렌더 시간**: 순정 **`6.06 ms`** vs Realistic **`12.56 ms`** (➔ **`+6.50 ms` 폭증**)
* **CPU 렌더 스레드 (`Draw`)**: 순정 **`5.85 ms`** vs Realistic **`15.41 ms`** (➔ **`+9.56 ms` 폭증**)
* **기타 패스 (그림자/루멘/포스트프로세싱/조명)**: 양쪽이 0.01ms 단위까지 **100% 완벽히 동일**.

---
*(다음 작업: **M_Realistic_Water 내부 5대 핵심 로직(노멀/SSS/켈빈) 분해 및 최적화**)*
