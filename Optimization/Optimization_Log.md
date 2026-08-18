# ArtisticSW2026 런타임 최적화 종합 기록 및 분석 일지

> **작성 일시**: 2026-08-18  
> **기록 위치**: `c:\Unreal Projects\ArtisticSW2026\Optimization\Optimization_Log.md`  
> **데스크탑 동기화**: `C:\Users\vlvkr\OneDrive\Desktop\Optimization\Optimization_Log.md`  
> **프로젝트**: ArtisticSW2026  

---

## 1. 최적화 대화 및 실험 히스토리 (Changelog & Timeline)

### [2026-08-18 12:15] 세션 1: 런타임 게임플레이 프로파일링 캡처 및 1차 전수 분석
* **수행 내용**: `Trace.Start` 및 `csvprofile` 기반 3,194개 프레임 수집 및 스레드별 병목 분석.
* **핵심 결론**: 목표 60 FPS (16.66ms) 대비 **18.12ms (55.2 FPS)**. RenderThread(17.63ms)와 GPU(15.58ms)가 주병목.

### [2026-08-18 12:45] 세션 2: 렌더 파이프라인 및 병목 원리 분석
* **질의응답**: 렌더 스레드 가시성 대기(12.88ms) 및 VRAM 대역폭(276 GB/s) 메모리 바운드 병목 규명.

### [2026-08-18 12:57] 세션 3: PrimitiveStats, ProfileGPU, Quad Overdraw 심층 진단
* **데이터 분석**: 배 6척(`SM_Ship`, `SM_Ship_Rough`)이 씬 전체 삼각형의 **89% (485만 개)**를 점유하고 있음을 확인.

### [2026-08-18 13:07] 세션 4: 선박 블루프린트 검증 및 쿼드 오버드로우 메커니즘 규명
* **검증 내용**: `Ship.cpp` 확인 결과 충돌용 3개 메쉬는 `HiddenInGame` 설정 확인. 단, 시각용 1개 메쉬 자체가 20만 폴리곤.

### [2026-08-18 13:15] 세션 5: VSM 큐 오버플로우 원인 규명 및 4단계 로드맵 수립
* **발견**: VSM Non-Nanite 큐 오버플로우 실측 확인 및 4단계 세분화 태스크 수립.

### [2026-08-18 13:30] 세션 6: [Phase 1: Task 1-1] Nanite 적용 및 C++ 소스 수정
* **수행 작업**:
  1. `SM_Ship`, `SM_Ship_Rough`에 Nanite 활성화 및 `Apply Changes` 적용.
  2. `Source/WaterAndShip/Private/Ship.cpp`에서 `BuoyancyRoot`, `ShipDamageMesh`, `ShipDeckMesh`의 `bDisallowNanite = true;` 하드코딩 제거 완료.
* **실측 확인된 사실 (Facts Only)**:
  1. **`[VSM] Non-Nanite Marking Job Queue overflow` 경고 로그 완전 소멸 확인.**
  2. **Nanite Visualization (Triangles)**에서 배가 정상적으로 멀티컬러 클러스터로 렌더링됨을 확인.
  3. **Quad Overdraw / Shader Complexity 뷰모드**에서 배의 렌더링 부하 색상이 개선됨을 확인.
  4. `.profViz` 파일 구조상 `Nanite::BasePass`와 `Nanite::ShadeBinning` 패스가 신규 활성화됨을 확인.
  *(※ 정확한 ms 단위의 프레임 타임/GPU 타임 단축 수치는 향후 `csvprofile` 재측정을 통해 실측 수치로 기록 예정)*

---

## 2. ProfileGPU (`.profViz`) 파일의 특성 및 한계

* **`.profViz` 파일의 성격**:
  - 언리얼 엔진의 `profilegpu`가 생성하는 `.profViz`는 에디터 시각화 뷰어 전용 바이너리 이벤트 트리 파일입니다.
  - 패스의 실행 구조(`Nanite::BasePass` 활성화 여부 등)는 확인할 수 있으나, **개별 패스의 정확한 소요 밀리초(ms) 수치는 별도의 CSV 프로파일(`csvprofile`)이나 `stat GPU` 실측 덤프를 통해서만 정밀 수치를 추출할 수 있습니다.**
* **수치 벤치마크 원칙**:
  - 추측에 의한 밀리초 수치 기재를 엄격히 배제하며, 실제 `csvprofile`을 실행하여 얻은 프레임 타임(ms) 변화 데이터만 공식 기록합니다.

---

## 3. 세부 단계별 작업 진행 현황 (Task Status)

```
[ Phase 1: 선박 렌더링 & VSM 그림자 최적화 ]
   ├── [완료] Task 1-1. SM_Ship 메쉬 Nanite 활성화 & VSM 큐 오버플로우 해소
   ├── [완료] Task 1-2. Ship.cpp의 bDisallowNanite 강제 비활성화 코드 제거
   └── [대기] Task 1-3. Nanite 적용 후 인게임 csvprofile ms 수치 벤치마크

[ Phase 2: 선박 물리(Chaos) 스파이크 제거 ]
   ├── Task 2-1. BuoyancyRoot, ShipDeckMesh, ShipDamageMesh에 전용 로우폴리 충돌체(100~500 폴리) 할당
   └── Task 2-2. 물리 연산 스파이크 (38ms -> 1ms 이하) 검증

[ Phase 3: 바다(WaterBodyOcean) 테셀레이션 & SLW 최적화 ]
   ├── [다음 진행 추천] Task 3-1. WaterMeshActor의 Tessellation Factor / Tile Size 완화 (동심원 미세 그리드 제거)
   ├── Task 3-2. SingleLayerWater의 루멘 반사 품질 및 버퍼 복사 최적화
   └── Task 3-3. 렌더 스레드 가시성 대기(EventWait/Visibility 12.88ms -> 4ms 이하) 검증

[ Phase 4: 게임 스레드 틱 및 AI 스파이크 안정화 ]
   ├── Task 4-1. Cannon(24개), Camera(38개) 등 불필요 컴포넌트 틱 비활성화
   └── Task 4-2. 적선 AI Perception / BehaviorTree 탐색 인터벌 분산 (640ms 스파이크 제거)
```

---
*(다음 작업: **Phase 3 (Task 3-1: 바다 테셀레이션 그리드 완화)** 또는 **Nanite 적용 상태의 `csvprofile` 재측정**)*
