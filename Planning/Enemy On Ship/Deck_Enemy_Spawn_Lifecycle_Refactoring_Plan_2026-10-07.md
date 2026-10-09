# 함선 휴면·갑판 Enemy 소환 생명주기: 구현 상태와 검증 계획

작성일: 2026-10-07 / 구현 대조: 2026-10-08
대상: `AEnemyShip`, `UDeckEnemySpawnerComponent`, `ADeckEnemy`, 네트워크 복제, Room Snapshot

핵심 생명주기와 저장 마이그레이션은 현재 C++에 구현되어 있다. 이 문서는 구현된 계약과 남은 검증을 구분한다. 에디터 설정과 수동 확인 순서는 [검증 가이드](../../docs/Deck_Enemy_Spawn_Refactoring_Editor_Test_Guide.md)를 따른다.

## 1. 수정한 원인

이전 구현은 거리 휴면에서 `ResetAfterReturnToSpawn → ResetForNewEncounter`를 호출했다. 비활성 풀 초기화가 활성화용 스탯까지 적용하면서 `bBalanceApplied=true`가 되었고, 자동 큐의 `ConfigureSpawnBalance`가 재구성을 거부했다. [리팩터링 전 재현 기록](../../docs/LV_ET_EnemySpawn_Pipeline_Diagnosis.md)에 근거가 있다.

현재 거리 휴면은 조우·체력을 초기화하지 않는다. 명시적인 귀환 완료만 새 조우를 초기화한다. `ResetToFreshPoolState`는 비활성 상태를 정리하고 `ResetBalanceForReuse`를 호출하며, 스탯 적용은 슬롯 활성화 준비에서 수행한다. 이미 활성화된 갑판 적의 재구성 보호는 유지한다.

## 2. 구현된 책임과 상태

| 담당 | 현재 책임 |
| --- | --- |
| `AEnemyShip` | 충돌·물리·AI 적용 후 유효 상태 발행, 휴면 허용 여부, Story Gate·사망·복원 차단 |
| `ANavalAIController` | Sight 시작/종료 전달, 현재 감지한 유효 Player 배 선택 |
| `UDeckEnemySpawnerComponent` | 단일 요청·슬롯 결과·지연·제한 재시도·예약·활성화 확정 |
| `UDeckWalkAreaComponent` | 보행면 준비/Revision 변경 알림, 바닥과 위치 검증 |
| `ADeckEnemy` | 비활성 초기화, 준비/공개 분리, 풀 상태 복제와 클라이언트 재조정 |
| `USWRoomSnapshotSubsystem` | 전체 Finalize 성공 후 복원 장벽 해제와 완료 알림 |
| `UBossEncounterComponent` | 기존 조우 조건 유지, 함선 Active 복귀 후 현재 Sight 재평가 |

`EnemyShipRuntimeState.h`의 `FEnemyShipRuntimeState`는 `Phase`, `BlockingReasons`, `Revision`을 갖는다.

- Phase: Restoring / Dormant / Active / Terminal.
- 차단 비트: Distance / Story / Restore / Terminal / CrewDefeated.
- 동일 Phase·차단 이유는 Revision을 반복 증가시키지 않는다.
- 서버의 `OnRuntimeStateChanged(Previous, Current)`는 전환 적용 후 발행한다.
- 클라이언트는 복제된 RuntimeState와 `OnRuntimePresentationChanged`로 표시를 복구한다. Delegate 자체를 네트워크로 보내지 않는다.
- `CanDeployDeckEnemies()`는 Active 외에도 서버 권한, 전환·복원·휴면·사망·침몰·승무원 전멸 조건을 확인한다.

전환 중 별도의 Phase 비트는 없으며 `bApplyingRuntimeState`가 실행을 차단한다. 원 설계의 이름 후보 State/StateRevision/TransitionInProgress와 구분한다.

## 3. 자동 소환 요청

기존 `EDeckEnemyDeploymentState`와 별도로 `EDeckEnemySpawnRequestState`의 None / PendingReadiness / Running / Finished / Cancelled를 사용한다. 슬롯 결과는 0=대기, 1=활성화 확정, 2=영구 실패다.

`EncounterGeneration`은 새 조우를, `RequestId`는 요청을, `ExecutionEpoch`는 오래된 콜백을 구분한다. 상태·보행면·복원 완료 알림은 다음 Tick의 단일 재평가로 모은다. 반복 요청은 기존 요청을 병합하며 지연과 완료 슬롯을 초기화하지 않는다.

자동 소환 시작에는 **현재 Sight + 함선 Active + 보행면 준비**가 필요하다. 항해의 TargetShip/Approach/Orbit는 재평가만 요청하며 Sight를 대신하지 않는다. 선호 배가 여전히 감지되면 유지하고, 아니면 거리·StableId 기준으로 감지 대상을 선택한다.

| 설정 | 기본값 / 의미 |
| --- | --- |
| Spawn Start Delay | 3초, 함선 BeginPlay 기준 최소 대기 |
| Sight Activation Delay | 0.25초, 첫 유효 요청의 반응 지연 |
| Activation Interval | 0.35초, 슬롯 간 간격 |
| Pending Sight Lifetime | 5초, 시작 전 Sight 소실·유효 대상 소멸 대기 |
| Readiness Timeout | 15초, 실행 준비 대기 제한 |
| Max Spawn Retries / Spawn Retry Interval | 3회 / 0.5초, 일시 충돌 등의 제한 재시도 |

실행 중 유효한 Trigger 배는 일시 Sight 소실만으로 취소하지 않는다. 대상이 소멸·침몰하면 현재 Sight 대상으로 교체하거나 제한 시간 기다린다. 준비 시간 초과 및 영구 설정 실패는 같은 조우에서 무한 재시작하지 않는다.

일반 거리 휴면은 실행 중 큐, 활성 승무원, 예약, 유효 대기 요청 또는 현재 Sight가 있으면 거부한다. 종료·Story Gate·복원·보행면 재생성에서는 타이머와 티켓을 중단하거나 무효화한다.

## 4. 공통 활성화 트랜잭션

자동 큐·수동·보스 추가 소환은 `ActivateSpecificEnemyAtReservation`을 공유한다. 수동/보스 경로에 자동 요청의 Sight 정책을 강제하지 않지만 함선·복원·풀·스탯·포인트 준비 조건은 공유한다.

1. 권한·함선 상태·시작 지연·비활성 적·정확한 PointId 예약을 확인한다.
2. 현재 보행면 바닥과 Capsule 여유를 검사한다. 고정 포인트를 다른 ID로 바꾸지 않는다.
3. `ConfigureSpawnBalance` 후 `PreparePoolActivation`으로 스탯과 이동 기반을 준비한다.
4. Epoch·Generation·WalkAreaRevision과 Host 상태를 다시 확인한다.
5. 포인트 점유, 활성 Enemy 집합, 자동 슬롯 결과·StableId를 기록한다.
6. `CommitPoolActivation`에서 표시·충돌·이동·AI를 공개한다.

준비/확정 실패 시 예약·점유·슬롯 기록을 롤백하고 적을 비활성 풀로 돌린다. `InactiveBalanceAlreadyApplied`, `CapsuleBlocked`, `ObsoleteActivation`, `ActivationCommitFailed` 등의 이유를 로그로 구분한다.

## 5. 복제와 저장 계약

`FDeckEnemyPoolNetState`는 Active / Dead / Host / PointId / ActivationGeneration / Revision을 한 스냅샷으로 복제한다. 기존 bPoolActive와 InitialSpawnPointId는 표시용 로컬 필드다. Host 참조·무기·이동 상태가 늦게 도착하면 클라이언트가 재조정한다. 경로·예약·AI 판단은 서버 소유다.

| 데이터 | 현재 기록 버전 | 이전 읽기 |
| --- | --- | --- |
| EnemyShip Enemy Domain | v4, DeckSpawner LifecycleVersion=1 | v2/v3 |
| BaseEnemy Enemy Domain | v2, 스탯 선택·적용 상태와 전투 속성 추가 | v1 |
| DeckEnemy Spawner Domain | v1, 활성/Host/Point/세대/풀 반환 남은 시간 | 이전 Enemy v1 저장의 도메인 추가 마이그레이션 |

요청·슬롯 결과/Enemy StableId·계획 지문·남은 지연/TTL을 저장한다. 계획이 바뀌었거나 이전 부분 실패 슬롯을 구분할 수 없으면 복원을 차단한다.

Restore의 `bRestoring`은 Deserialize 이후에도 유지된다. 모든 Actor Finalize가 성공한 `CompleteRestore`만 장벽을 내리고 `OnRestoreCompleted`를 발행한다. 풀 초기화로 저장된 활성 적의 체력·사망·효과를 덮어쓰지 않는다. 실패 시 완료 알림과 자동 재개가 없다.

## 6. 검증 상태와 남은 작업

2026-10-08 `ArtisticSW2026Editor / Win64 / Development` 빌드는 성공했다. 두 저장 레벨의 함선·앵커·편성을 읽기 전용으로 확인했다. **이번 생명주기 변경의 자동화 회귀 테스트, 수동 PIE, 실제 네트워크·저장 복원은 아직 검증하지 않았다.** 근접 공격 테스트 결과와 구분한다.

남은 검증:

- 근거리/원거리 휴면 후 실제 Sight 소환, Wake 단독 소환 0회.
- 반복 감지·오래된 타이머·부분 실패·보행면 Rebuild에서 중복/예약 누수 없음.
- 귀환·재조우, Story Gate, 승무원 전멸과 보스 추가 소환.
- Listen/Dedicated Server, Late Join, 지연·손실에서 세대·Host·충돌·이동 기반 수렴.
- 대기/부분 소환/사망 후 저장과 v2/v3 실제 저장 샘플 복원.
- 51척 규모의 반복 전환 비용과 복제량 측정.

기존 ShipSwarmSubsystem은 함선의 휴면 허용 계약을 사용하며 이번 변경에서 직접 수정하지 않았다. Spawn Height Offset·함선 티어/편성·보스 Class 등 콘텐츠 설정은 별도 에셋 변경이며 [레벨 비교](../../docs/LV_ET_vs_Lvl_CY_EnemySpawn_Audit.md)에 최신 저장값을 기록한다.
