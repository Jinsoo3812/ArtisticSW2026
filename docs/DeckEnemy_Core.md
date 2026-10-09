# Deck Enemy Core Architecture Guide

구현 대조: 2026-10-08. 현재 C++와 저장된 LV_ET/Lvl_CY 설정을 기준으로 한다. 에디터 절차는 [스폰 생명주기 검증](Deck_Enemy_Spawn_Refactoring_Editor_Test_Guide.md), 보행면 구조는 [DeckWalk 구현](DeckWalk_Implementation.md), 전투 BT는 [Combat 구현 및 설정](DeckEnemy_Combat_BT_Design_2026-10-04.md)을 따른다.

## 1. 공통 구조

`ADeckEnemy`는 `ARangedEnemy`의 ASC·상태·무기·감지 기능을 재사용하며, Melee/Ranged 역할을 갑판 전투 컴포넌트에서 구분한다. `ADeckRangedEnemy`는 기존 BP 참조를 보존하는 파생 클래스다.

| 담당 | 책임 |
| --- | --- |
| AEnemyShip | 수동 앵커, 함선 유효 상태, 승무원과 휴면 정책 |
| UDeckEnemySpawnerComponent | Spawn Plan, 풀, 요청/슬롯 결과, 스폰 포인트 예약·점유 |
| UDeckWalkAreaComponent | 충돌 Mesh 기반 함선 로컬 보행면 그래프, 경로·위치 예약 |
| UDeckWalkRouteComponent | 개별 적의 목표·이동·재탐색 |
| UDeckEnemyNavigationComponent | BT 목적지 질의와 전투 이동 연동 |
| UDeckEnemyCombatComponent | 공격 거리·쿨타임·LOS·재배치 |
| UDeckCombatTargetResolverComponent | 대상의 현재 보행면·갑판 위치 증거 |
| ADeckEnemy | 풀 준비/공개/반환, 복제 상태와 갑판 Movement Base |

Waypoint는 소수의 **스폰/높이 기준 앵커**다. 이동 목적지는 보행면에서 선택한다. 삭제된 Waypoint 링크 그래프, Next-hop 예약, Combat 플래그를 새 에셋 설정에 사용하지 않는다. 경로와 AI 판단은 서버에서 수행하고, 캐릭터는 CharacterMovement의 Movement Base로 배를 따른다.

## 2. 소환과 풀 생명주기

Spawn Plan은 적 한 명당 Enemy Class + Stats Row + Spawn Point Id 한 항목이다. 최대 32명이며 고정 ID를 다른 앵커로 대체하지 않는다. 명시 Row가 비어 있으면 적 CDO의 DefaultStatsRow를 사용한다.

함선 상태는 Restoring / Dormant / Active / Terminal이다. 자동 큐는 실제 Player 배의 Sight, 함선 Active, 보행면 준비와 지연을 함께 검사한다. Approach/Orbit는 준비 재평가만 요청한다. Wake 자체는 소환 명령이 아니다.

`ResetToFreshPoolState`는 스탯을 미리 적용하지 않는 비활성 초기화다. 실제 활성화는 슬롯 스탯 구성 → PreparePoolActivation → 점유/슬롯 확정 → CommitPoolActivation으로 진행한다. 자동·수동·보스 추가 소환이 같은 내부 트랜잭션을 사용한다. 사망/반환 시 타깃·AI·효과·예약을 정리하고 재구성 가능한 비활성 풀로 돌린다.

Spawner는 None / PendingReadiness / Running / Finished / Cancelled 요청 상태와 EncounterGeneration / RequestId / ExecutionEpoch를 관리한다. 이미 활성화된 슬롯은 반복 Sight로 다시 실행하지 않는다. 일시 충돌은 제한 재시도하고 영구 오류는 원인을 남긴다.

거리 휴면은 조우나 체력을 초기화하지 않는다. 큐·활성 승무원·예약·유효 감지가 있으면 일반 휴면을 거부한다. 명시적인 귀환 완료만 새 조우를 시작한다.

## 3. 이동과 전투

보행면의 NodeIndex / SurfaceId / LocalFloor / Revision으로 위치를 식별한다. Rebuild 후 예전 Revision의 경로와 위치를 다시 사용하지 않는다. 순찰과 전투는 실제 지지 바닥과 연결 영역에서 목적지를 선택한다.

일반 갑판 적은 자신과 대상의 보행면 증거를 확인하고 같은 Surface에서 기존 무기 거리·LOS 조건으로 공격한다. Melee는 장착 무기 사거리, Ranged는 원거리 전투 정책을 사용한다. 경보/조사/쿨타임 재배치 BT의 세부 값은 Combat 문서에서 관리한다.

스폰 포인트 점유와 전투 위치 예약은 구분한다. 이동 시작, BT Abort, 이동 실패, 사망, 풀 반환에서 관련 예약을 해제한다. 계단 등 실제 연결 바닥이 없는 서로 다른 면 사이에 경로가 자동 생성되지는 않는다.

## 4. 복제와 저장

서버는 RuntimeState와 PoolNetState의 최종 상태를 복제한다. PoolNetState에는 Active / Dead / Host / PointId / ActivationGeneration / Revision이 있다. 클라이언트는 늦은 Host·무기·이동 상태 수신과 새 활성화 세대에 맞춰 표시·충돌·Movement Base를 재조정한다.

AIController, Blackboard, BT, 보행면 그래프와 예약 내부 상태는 복제하지 않는다. 기존 CharacterMovement와 전투 연출 복제를 사용한다.

Room 저장은 EnemyShip Enemy v4, BaseEnemy Enemy v2, DeckEnemy Spawner v1을 사용한다. 전체 Finalize 성공 후 복원 완료 알림이 와야 큐/AI를 재개한다. 슬롯 결과·계획 지문·StableId·남은 타이머를 복원하며 저장된 체력·사망 상태를 새 풀 초기화로 덮어쓰지 않는다.

## 5. 현재 콘텐츠와 검증 범위

2026-10-08 저장 레벨 확인 결과 두 레벨은 각각 EnemyShip 51척이다. 기본 BP_EnemyShip 앵커는 아래층 0/1, 위층 10/11/12, 보스 전용 20이다. Lvl_CY는 T1 근접 2명 @0/10, LV_ET는 Final 폴더의 티어/편성별 자식 BP를 사용한다. 과거의 “25개 Point가 모두 ID 0” 설명은 현재 설정에 적용되지 않는다.

현재 Editor 대상 빌드는 성공했다. 생명주기 변경의 실제 PIE·멀티플레이·저장 복원은 남은 검증이며 [검증 가이드](Deck_Enemy_Spawn_Refactoring_Editor_Test_Guide.md)에 절차를 기록한다.
