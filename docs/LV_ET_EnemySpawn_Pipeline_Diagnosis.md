# LV_ET Enemy 소환 파이프라인과 실패 원인

## 최신 구현 상태 (2026-10-08)

이 문서의 실패 흐름·PIE 수치는 **2026-10-07 리팩터링 전 재현 기록**이다. 현재 거리 휴면은 ResetAfterReturnToSpawn을 호출하지 않고, Fresh Pool은 스탯 적용 없이 재구성 가능 상태로 초기화한다. 자동 큐는 실제 Sight·Active·보행면 준비를 검사하며 준비 → 점유/슬롯 확정 → 공개를 분리한다. 전체 Room 복원 완료 전에 AI/큐를 재개하지 않는다.

현재 구현과 남은 수동 검증은 [스폰 생명주기 가이드](Deck_Enemy_Spawn_Refactoring_Editor_Test_Guide.md), 설계와 구현 계약은 [리팩터링 문서](../Planning/Enemy%20On%20Ship/Deck_Enemy_Spawn_Lifecycle_Refactoring_Plan_2026-10-07.md)를 따른다. 2026-10-08 Editor 빌드는 성공했지만 수정 후 실제 Sight·휴면·저장 복원 PIE는 아직 확인하지 않았다.

확인일: 2026-10-07. 사용자 첨부 로그, 현재 C++ 코드, 별도 서버 PIE의 거리 최적화 비교 실험을 근거로 분석한다. 원본 레벨과 Blueprint는 저장하지 않는다.

## 앞의 배를 처치해야 하는가

일반 EnemyShip의 갑판 적 생성에는 이전 배·편대의 처치 조건이 없다. 각 배가 독립적으로 플레이어 배의 Sight 이벤트를 받아 생성 요청을 한다. Final 편대는 별도의 최종전 스토리 조건을 검사하지만, Normal_1_1을 처치해야 Normal_1_2나 Normal_2_1의 갑판 적이 등장하는 조건은 아니다.

## 리팩터링 전 정상 생성 흐름

1. EnemyShip BeginPlay에서 스폰 포인트를 등록하고 DeckWalkArea를 만든다.
2. Spawn Plan의 각 슬롯으로 적을 미리 생성한 뒤 DeactivateToPool로 숨기고 충돌을 끈다. 이때 ResetBalanceForReuse가 스탯 적용 플래그를 비워 다음 배치를 준비한다.
3. NavalAIController가 Player 태그가 있는 플레이어 배를 Sight로 감지한다.
4. EnemyShip.NotifyPlayerShipSighted가 DeckEnemySpawner.RequestDeployment를 호출한다.
5. 스폰 시작 대기 3초와 감지 반응 대기 0.25초 중 남은 긴 시간을 기다린다.
6. DeployNextEnemy가 슬롯의 적과 포인트를 찾고 위치·예약을 검증한다.
7. ConfigureSpawnBalance로 해당 슬롯의 Stats Row를 지정한다.
8. ActivateFromPool에서 실제 스탯을 적용하고 표시·충돌·AI를 활성화한다.

앞의 배 처치 확인은 이 흐름에 없다. 항해 TargetShip 검색과 Sight 이벤트는 별도이며, 배가 추적한다는 사실만으로 갑판 생성 단계가 성공한 것은 아니다.

## 리팩터링 전 실패 흐름: 먼 배의 풀 초기화와 스탯 재설정 충돌

멀리 있는 배는 ShipSwarmSubsystem이 플레이어 배와의 거리로 최적화 휴면을 적용한다. 현재 저장된 기본 범위는 100,000cm다.

| 단계 | 호출과 상태 |
| --- | --- |
| 휴면 진입 | SetDistanceOptimizationDormant(true) |
| 함선 초기 상태 복구 | ResetAfterReturnToSpawn() |
| 갑판 풀 초기화 | ResetForNewEncounter() → 각 Enemy.ResetToFreshPoolState() |
| 잘못된 순서 | DeactivateToPool() 다음 RestoreForPoolActivation() 호출 |
| 대기 적의 스탯을 미리 적용 | RestoreForPoolActivation → ApplyBaseStatsForSpawn → bBalanceApplied=true, bBalanceReady=true |
| 플레이어 접근 | 휴면에서 깨어나 Sight → RequestDeployment → DeployNextEnemy |
| 실패 조건 | ConfigureSpawnBalance는 bBalanceApplied=true인 적을 거부하고 false 반환 |
| 관측 로그 | 재시도 소진 후 Deployment entry abandoned |

**적이 비활성 풀 상태인데 스탯 적용 플래그는 이미 활성화된 상태**가 된다. 자동 배치 큐는 스탯을 지정하기 전에 이 상태를 거부한다. 같은 적을 재시도해도 플래그를 초기화하지 않으므로 각 슬롯이 반복 실패한다.

처음부터 플레이어 가까이에 있는 BP_ES_Normal_1_1/2/3은 이 휴면 초기화 경로를 거치지 않아 정상 생성될 수 있다. 나머지 먼 배는 처음에 휴면에 들어갔다가 접근 시 깨어나므로 문제를 겪는다. 따라서 티어나 앞의 배 처치 순서보다 **휴면을 한 번 거쳤는지**가 증상을 설명한다. 전투 종료 후 원위치 복귀도 같은 풀 초기화 경로를 사용하므로 이후 재조우에도 영향을 줄 수 있다.

## 첨부 로그가 보여주는 내용

- 로그 2892~2895행: BP_ES_Normal_2_C_1의 LowerDeck 267개, UpperDeck 387개, Ready=1. 이 배의 초기 보행면 생성은 성공했다.
- 로그 8597~8598행: BP_ES_Normal_2_C_2와 _C_1의 첫 슬롯 ID 10이 재시도를 소진한다.
- 로그 8624행: BP_ES_Normal_RRR_C_1의 원거리 적도 ID 10에서 동일하게 실패한다. 근접 적 클래스에만 국한되지 않는다.
- 로그 8935~8937행: 같은 배들의 ID 11도 실패한다.
- 로그 9208~9210행: ID 12도 실패한다.
- `Invalid stats` 또는 `Invalid combat row`가 아닌 `Deployment entry abandoned`다. ConfigureSpawnBalance의 이미 적용된 상태 거부는 자체 원인 로그를 남기지 않기 때문에 기존 로그에서 실패 원인이 명시되지 않는다.
- 시작부의 aqProf/Vtune DLL 로드 경고는 이 C++ 생성 거부 조건과 별개다.

첨부 로그의 내부 이름 BP_ES_Normal_2_C_1/2는 앞선 저장값 조사에서 Outliner의 BP_ES_Normal_1_4/5였다. Blueprint 클래스 이름·내부 액터 이름과 Outliner Label을 구분해야 한다.

## 이전 조사와의 관계

이전 조사는 공개 ActivateDeckEnemyAtPoint로 적을 직접 활성화했다. 이 함수는 DeployNextEnemy의 ConfigureSpawnBalance 단계를 거치지 않으므로, 직접 활성화 성공만으로 자동 소환 큐의 정상 동작을 판정할 수 없다.

보스 클래스 누락은 사람 보스 생성에 대한 별도 문제다. Spawn Height Offset 30cm는 두 레벨이 공유한 설정이고, 이전 통제 실험에서 갑판 적 활성화·보행이 성공했다. 이번 일반 갑판 적 실패의 핵심은 풀 리셋 뒤 스탯 적용 플래그와 자동 배치 큐 사이의 충돌이다.

## 서버 PIE 재현 결과

동일한 N2/BP_ES_Normal_MMR 클래스의 두 배를 비교했다. BP_ES_Normal_1_4(내부 _2_C_1)는 테스트 메모리에서만 거리 최적화를 껐고, BP_ES_Normal_1_5(내부 _2_C_2)는 원래 설정을 유지했다. 앞의 배를 처치하지 않고 게임 시간 약 4.25초에 플레이어 배를 두 배 사이로 이동시켜 실제 Sight → RequestDeployment → DeployNextEnemy 경로를 실행했다. 공개 직접 활성화 함수로 우회하지 않았다.

| 관측 | 휴면을 거치지 않은 _2_C_1 | 휴면을 거친 _2_C_2 |
| --- | --- | --- |
| 준비된 풀 | 3명 | 3명 |
| 접근 전 비활성 근접 적 Balance Ready | false | true |
| 접근 전 동일 Stats Row의 ConfigureSpawnBalance | true, 두 적 모두 | false, 두 적 모두 |
| 플레이어 접근 후 | 3명 활성화, Completed | 0명 활성화 |
| 자동 큐 로그 | 이 배의 abandoned 없음 | ID 10·11·12 모두 abandoned |

약 15.38초에도 _2_C_1은 3명이 모두 활성 상태였다. _2_C_2는 3명 모두 숨겨진 비활성 상태, Balance Ready=true이며, 첫 실패 후 Sight 재요청에 의한 Deploying 상태였다. 같은 조건에서 옆의 BP_ES_Normal_RRR_C_1도 ID 10·11·12에 같은 abandoned를 보고했다. 실험의 Python 오류는 없었다.

첨부 로그에 없는 세부 상태를 이 실험에서 직접 확인했으므로, ConfigureSpawnBalance의 적용 플래그 거부와 휴면 초기화의 인과관계를 재현한 결과다. 접근 전 설정 함수 호출은 기존 슬롯과 같은 Row를 사용했고, 이미 적용된 적은 false를 반환해 상태를 바꾸지 않았다.

## 당시 수정 후보와 현재 적용

ResetToFreshPoolState가 체력·사망·연출 상태를 복구하더라도, 비활성 적은 다음 슬롯 설정을 받을 수 있어야 한다. 당시 최소 수정 후보는 RestoreForPoolActivation 이후 ResetBalanceForReuse였으나, 현재 구현은 Fresh 초기화에서 RestoreForPoolActivation 자체를 호출하지 않는다. 실제 슬롯 스탯 적용과 공개는 ConfigureSpawnBalance → PreparePoolActivation → CommitPoolActivation으로 분리했다.

ConfigureSpawnBalance의 이미 적용된 상태 보호를 무조건 제거하면 살아 있는 적의 스탯 변경까지 허용할 수 있으므로 적절한 해결책이 아니다. 거리 최적화를 모든 배에서 끄면 이번 휴면 경로를 피할 수 있지만 복귀 후 풀 초기화 문제는 남는다.

수정 후 검증에는 처음 가까운 배, 처음 멀리 있다 접근하는 배, 귀환·재조우, Stats Row 오버라이드, 보스 추가 소환을 포함한다. 이 진단 당시에는 원인 분석과 재현만 수행했고 후속 리팩터링이 현재 C++에 반영되어 있다.

## 수정 전 코드 근거

아래 행 번호는 당시 코드 기준이다. 최신 코드에서는 위 링크 문서와 함수 이름으로 찾는다.

- Source/Enemy/Private/ShipAI/ShipSwarmSubsystem.cpp:193 — 거리 최적화 평가
- Source/Enemy/Private/ShipAI/EnemyShip.cpp:948 — 휴면 진입 때 ResetAfterReturnToSpawn 호출
- Source/Enemy/Private/ShipAI/EnemyShip.cpp:853 — ResetForNewEncounter 호출
- Source/Enemy/Private/DeckAI/DeckEnemySpawnerComponent.cpp:770 — 풀 초기화
- Source/Enemy/Private/DeckAI/DeckRangedEnemy.cpp:251 — ResetToFreshPoolState
- Source/Enemy/Private/DeckAI/DeckRangedEnemy.cpp:462 — 복구 중 스탯 적용
- Source/Enemy/Private/BaseEnemy.cpp:635 — 적용 플래그 true
- Source/Enemy/Private/BaseEnemy.cpp:555 — 이미 적용된 상태의 설정 거부
- Source/Enemy/Private/DeckAI/DeckEnemySpawnerComponent.cpp:665 — 자동 배치 큐에서 스탯 설정 실패 처리

재현 스크립트: Saved/CodexDiagnostics/ProbeLVETDormancyDeployment.py. 결과: Saved/CodexDiagnostics/LV_ET_DormancyDeployment.json 및 .log. 테스트용 옵션과 플레이어 배 위치는 별도 세션의 메모리에서만 변경한다.
