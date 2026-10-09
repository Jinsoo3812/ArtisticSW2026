# PCG_Biome Collision Profile 영향 검토

최신 대조: 2026-10-08. 현재 C++ 필터는 구현되어 있고, 저장된 LV_ET Brush에는 PCGVolumeBounds가 적용되어 있다. **Lvl_CY Brush는 현재 Custom이며 추가 적용이 필요하다.** 두 레벨 각각 3,886개 인스턴스를 확인했다. 아래 채널/그래프 상세 근거는 2026-10-07 조사 기록이며 [최신 적용 상태와 절차](PCG_Combat_Fix_Editor_Check.md)를 우선한다.

## 현재 판단

`PCG_Biome`의 기존 충돌 응답을 그대로 `PCGVolumeBounds` Profile로 등록하고, WorldStatic 장애물 검사를 사용하는 공통 Resolver에서 해당 Profile의 컴포넌트만 제외한다. 플레이어/적 근접·보스 호출이 이 검사를 사용하며 화살은 추가 검사를 끈다. 이전 PCG 클래스/Brush/IgnoreAll 검사와 GASCore의 PCG 모듈 의존성은 현재 구현에 없다.

2026-10-07 실제 저장 레벨을 다시 읽은 결과, `LV_ET`와 `Lvl_CY` 모두 **Arrow=Block이고 나머지 등록 채널=Ignore**였다. 이전 보고서의 “모든 채널 Ignore” 표현은 현재 프로젝트의 사용자 정의 채널까지 확인하지 못한 설명이었다. 이 차이가 이전 필터가 작동하지 않은 원인이다.

## 보존하는 설정

| 항목 | 적용 전후 동일 값 |
| --- | --- |
| Collision Enabled | Query Only |
| Object Type | WorldStatic |
| Arrow | Block |
| 그 외 등록된 기본/사용자 채널 | Ignore |
| Generate Overlap Events | False |
| PCG 그래프 | /Game/Tools/PCG_Biome |

변경하는 것은 Brush의 Profile 이름 `Custom` → `PCGVolumeBounds`와 근접 피해 판정의 해당 Profile 제외 조건이다. Profile을 실제 생성 메시에는 지정하지 않는다. 화살에 대한 기존 Block 응답도 유지한다.

Profile 등록만으로는 WorldStatic Object Query에서 PCG Brush가 빠지지 않는다. 공통 피해 판정에서 Profile을 명시적으로 제외해야 한다. 엔진 5.7의 `PhysicsEngine/CollisionQueryFilterCallback.cpp`는 Object Multi Query에서 일치하는 객체를 모두 수집하도록 처리한다. 이 결과에서 PCG Profile만 제외하면 그 뒤의 실제 벽도 차단 대상으로 남는다.

## PCG 생성 관련 근거와 범위

공유 그래프의 SurfaceSampler는 Unbounded=False이고 PCG 볼륨 형상으로 생성 범위를 제한한다. 연결된 `ScaleByDensity` BP는 점의 밀도를 크기로 변환하며, 앞선 검사에서는 Brush Trace/Overlap 제어를 발견하지 않았다.

엔진의 PCGVolumeData는 Brush BodySetup으로 별도의 내부 물리 형상을 만들고 범위 내 점을 검사한다. NoCollision Profile은 에디터 밖에서 정상 동작하지 않을 수 있다는 경고 경로가 있으므로 이번 변경은 Query Only와 기존 형상을 유지한다.

Profile 적용 스크립트는 변경 전 레벨을 백업하고, 저장 전후 및 다시 읽은 뒤 등록 채널 응답, PCG 그래프 참조/Generated/DirtyGenerated, 컴포넌트의 상대 Transform, 생성 인스턴스 수, 생성 메시 충돌 설정을 비교한다. PCG Generate/Cleanup을 호출하지 않는다.

이 확인은 기존 데이터 보존을 확인하는 것이며 PCG 재생성·패키지 실행 검증은 포함하지 않는다. 현재 충돌 응답 보존은 새 채널을 나중에 추가했을 때까지 보장하지 않으므로 그때 Profile 응답을 함께 검토한다. 기존 LevelSnapshot을 복원하면 Custom Profile이 돌아올 수 있으므로 복원 후 Brush Profile을 재확인한다.

## 검사 자료

- `Saved/PCG_Profile_Before.json`: 이번 변경 전 실제 설정.
- `Saved/PCG_Profile_Migration/report.json`: Profile 적용/저장/재조회 확인 결과.
- `Saved/PCG_Dependency_Audit/report.json`, `PCG_Biome.copy`, `ScaleByDensity.copy`: 앞선 그래프/BP 정적 분석 자료.
- [PCG 적용 및 PIE 가이드](PCG_Combat_Fix_Editor_Check.md): 최신 디스크 적용 상태와 수동 검증 절차.
