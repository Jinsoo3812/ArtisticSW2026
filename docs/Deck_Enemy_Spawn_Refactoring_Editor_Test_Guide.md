# Deck Enemy Spawn 리팩터링: 에디터 설정과 간단한 수동 검증

작성일: 2026-10-07 / 구현 대조: 2026-10-08. 이번 스폰 생명주기 변경에는 자동화 테스트를 추가하거나 실행하지 않았다. 근접 공격 변경에는 별도 테스트가 있으며 [피격 진단](Deck_Melee_Hit_Diagnosis.md)에 기록한다. 아래 절차는 아직 수행하지 않은 수동 PIE 검증이다.

## 적용된 동작

- 함선이 충돌·물리·감지를 복구한 다음 상태 Delegate를 발행한다. Spawner는 다음 Tick에 알림을 모아 조건을 재검사한다.
- 거리 휴면은 조우와 체력을 초기화하지 않는다. 명시적인 원위치 귀환 완료가 새 조우를 초기화한다.
- 실제 Sight와 Active/갑판 준비 조건이 충족되어야 자동 소환한다. Approach/Orbit의 항해 대상만으로 소환하지 않는다.
- Fresh Pool 초기화에서는 활성화용 스탯을 적용하지 않는다. 소환할 슬롯을 정한 뒤 스탯을 구성·적용한다.
- 자동·수동·보스의 갑판 풀 활성화는 같은 예약/스탯/확정 경로를 사용한다. 표시·충돌·AI는 포인트 점유와 슬롯 기록 확정 뒤 활성화한다.
- 서버만 소환을 결정한다. 함선 상태와 Enemy의 활성/Host/Point/활성화 세대는 복제된 현재 상태로 전달한다.
- 전체 Room 복원 완료 전에는 큐와 AI를 재개하지 않는다. 저장된 체력·사망·처치 기록을 보존한다.

**Blueprint에서 Delegate를 추가로 연결할 필요는 없다.** 기존 Spawn Plan과 에셋 설정을 사용한다.

## 1. 에디터 다시 열기

이번 변경은 USTRUCT/복제 필드와 C++ 클래스 선언을 포함한다. 빌드된 모듈을 적용하려면 에디터를 다시 열고 `LV_ET`를 연다. 검증용 위치/타이밍을 바꿀 경우 레벨 복사본을 사용하면 기존 배치를 보존할 수 있다.

먼저 `BP_ES_Normal_1_5`처럼 이전에 실패했던 **일반 편대 배 한 척**을 대상으로 잡는다. Final 편대는 스토리 조건이 추가되므로 첫 검증에서 제외한다.

## 2. 확인할 설정

| 위치 | 항목 | 첫 검증에서 사용할 값/조건 |
| --- | --- | --- |
| EnemyShip 인스턴스 → Ship / Optimization | Enable Distance Optimization | 켬 |
| 같은 위치 | Distance Optimization Range | 기존 값 유지. 기본 100,000cm = 1km |
| EnemyShip → Squad ID | 편대 | 일반 편대. Final 사용하지 않음 |
| 해당 자식 BP의 Components → DeckEnemySpawnerComponent | Enable Spawning | 켬 |
| 같은 컴포넌트의 Spawn Plan | Enemy Class / Stats Row / Spawn Point Id | 기존 3개 슬롯을 유지. 클래스와 Row가 유효하고 PointId가 중복되지 않음 |
| Spawner → Timing | Spawn Start Delay | 3초 권장. 0으로 바꾸지 않아도 검증 가능 |
| 같은 위치 | Sight Activation Delay / Activation Interval | 기존 값 유지. C++ 기본값 0.25초 / 0.35초 |
| 같은 위치 | Pending Sight Lifetime / Readiness Timeout | 신규 기본값 5초 / 15초 유지 |
| 해당 배의 DeckWaypointComponent | Waypoint Id / Can Spawn Enemy | Spawn Plan의 PointId와 일치하고 스폰 가능 |
| DeckWalkAreaComponent | Surfaces / Floor Component Names | 기존 LowerDeck/UpperDeck 설정 유지 |
| 플레이어 배 Actor → Tags | Player / Enemy | Player 포함, Enemy 미포함 |

`Enable Spawning`, `Spawn Plan`, 새 대기 설정 등은 **EditDefaultsOnly**다. 레벨 인스턴스에서 변경할 수 없는 항목은 해당 자식 Blueprint의 컴포넌트 기본값에서 확인한다. `Spawn Start Delay`와 함선의 거리 범위는 인스턴스에서도 설정할 수 있다.

스폰 높이·Capsule 크기·Stats Row를 이번 검증을 위해 임의로 바꿀 필요는 없다. 먼저 기존 콘텐츠 값으로 확인한다.

## 3. 가장 간단한 1인 PIE 검증

Play 설정은 `Number of Players = 1`, `Net Mode = Play Standalone`으로 시작한다. 서버와 클라이언트 간 복제 확인은 다음 절차에서 한다.

1. 플레이어 배가 대상 EnemyShip의 거리 최적화 범위 밖에 있는 상태로 시작한다. 기본 범위라면 XY 거리 120,000cm 이상을 권장한다. 대상 배가 항해의 DetectionDistance 밖에도 있어야 한다.
2. 3~5초 기다린 뒤 F8로 Eject하여 **PIE 월드의** EnemyShip을 선택한다. 휴면 여부를 확인한다. 에디터 원본 Actor와 실행 중 Actor를 구분한다.
3. 대상 배의 `Ship / Optimization → Distance Optimization Dormant = true`, `Ship / Runtime → Runtime State.Phase = Dormant`인지 확인한다.
4. 플레이어 배를 거리 최적화 범위 안으로 접근시킨다. 먼저 약 20,000cm 거리처럼 Sight보다 먼 위치에서 확인한다. 함선 Phase는 Active가 되어도 소환되지 않아야 한다.
5. 실제 플레이어 **배**를 Sight 범위 안으로 접근시킨다. Native Sight 기본값은 10,000cm = 100m이므로 약 8,000cm를 기준으로 삼되, 해당 BP의 Sight 설정을 우선한다. 캐릭터만 이동시키는 것으로 대체하지 않는다.
6. 반응 시간과 슬롯 간격을 기다린다. 3슬롯 기본 구성은 시작 지연이 지난 경우 대략 1~2초 안에 완료 상태를 확인할 수 있다. 충돌 경합이 있으면 제한 재시도 시간이 추가된다.
7. 다시 F8로 해당 배의 Spawner를 선택해 `Deployment State = Completed`, `Spawn Request State = Finished`인지 확인한다. 갑판에 계획된 3명이 표시·충돌·이동을 하는지도 확인한다.
8. 다시 감지되거나 Approach/Orbit가 바뀌어도 적이 6명으로 늘어나지 않아야 한다.

위치 이동 시간을 줄이려면 서버 PIE에서 Eject 후 플레이어 배의 런타임 위치를 변경할 수 있다. 변경 시 수면 높이와 회전은 유지한다. 물리/네트워크 제어로 위치가 되돌아오는 경우에는 직접 항해로 접근한다. 거리 기준은 PlayerStart나 캐릭터가 아니라 실제 Player 배 Actor다.

휴면이 확인되지 않으면 현재 항해 TargetShip/Override, DetectionDistance, 다른 플레이어 배, 살아 있는 활성 승무원 또는 예약이 있는지 확인한다. 실행 중 소환 큐와 승무원이 있는 배는 일반 거리 휴면을 거부하도록 변경했다.

테스트용 거리 범위를 줄일 수도 있지만 **시작 거리가 거리 최적화 범위와 항해 DetectionDistance 모두보다 커야 한다.** 범위만 줄이고 가까운 배가 이미 항해 대상을 잡게 두면 휴면 검증이 되지 않는다. 편대 지시가 개입하면 검증용 배에 별도의 일반 Squad ID를 사용한다.

## 4. 간단한 2인 네트워크 확인

1. Play 설정을 `Number of Players = 2`, `Net Mode = Play As Listen Server`, 별도 PIE 창으로 설정한다.
2. 처음에는 모든 Player 배를 대상 배의 거리 범위 밖에 둔다. 공동 함선을 쓰는 모드라면 그 한 척을 기준으로 한다. Player 배가 여러 척이면 어느 한 척이라도 가까울 때 휴면 조건이 성립하지 않는다.
3. 플레이어 배 한 척을 접근시켜 같은 Wake → Sight 절차를 수행한다.
4. 서버와 다른 클라이언트 창에서 같은 3명이 같은 갑판에 나타나는지 확인한다. 클라이언트 쪽에서 추가 Enemy가 생성되면 실패다.
5. 움직이고 회전하는 EnemyShip 위에서 적이 갑판을 따라 이동하는지 확인한다. 보이지 않는 적의 충돌만 남거나, 갑판 아래로 떨어지거나, 라그돌 상태로 재등장하면 실패다.
6. 필요하면 PIE의 Network Emulation을 켜고 지연·손실 프리셋으로 같은 동작을 한 번 더 확인한다. 실제 사용한 프리셋/수치는 기록한다.

이 절차는 Listen Server의 기본 복제 확인이다. Dedicated Server, Late Join, 실제 재접속, 장시간 지연/손실은 별도의 수동 검증 범위로 남는다.

## 5. 상태와 로그로 실패 구분

Output Log에서 `DeckEnemySpawner`를 검색한다. 새 로그는 Generation / Request / Epoch / Slot / Reason을 포함한다.

| 관측 | 의미와 확인 위치 |
| --- | --- |
| Dormant → Active, 소환 없음, Sight 밖 | 정상. Wake만으로 생성하지 않음 |
| Preparing + PendingReadiness / WaitingForSight | 실제 Sight 확인을 기다림. 유효 기한 내 감지가 돌아와야 시작 |
| SpawnDelay | 시작 또는 감지 반응 지연 중 |
| WalkAreaNotReady | 갑판 준비/전환을 기다림. Surfaces와 Floor Component Names 확인 |
| StoryGateClosed | Final 편대의 스토리 조건이 닫힘 |
| CapsuleBlocked / PointUnavailable | 해당 고정 스폰 지점 충돌/예약 경합. 주변 액터 확인 |
| InvalidSpawnPlan / InvalidStatsRow / MissingSpawnPoint | 클래스/Row/Waypoint 설정 오류. 다음 PIE 전에 수정 |
| InactiveBalanceAlreadyApplied | 비활성 풀의 스탯 계약 위반. Ship/Generation/Slot과 최초 실패 로그를 보관 |
| ReadinessTimeout | 준비가 제한 시간 내 끝나지 않음. 일반 재감지로 무한 재시작하지 않음 |
| CompletedWithFailures | 일부 슬롯만 성공. 실패 Reason을 확인 |
| Result와 Activated=3 Failed=0 | 3슬롯 자동 큐 완료. 실제 클라이언트 표시/이동도 함께 확인 |

Spawner에서 `Get Spawn Request State`, `Get Spawn Wait Reason`, `Get Deployment State`, `Get Alive Deployed Enemy Count`를 BP 디버그 표시로 조회할 수도 있다. 생성/타이머/예약은 서버 소유이므로 상태 진단은 서버 창을 기준으로 한다.

`ActivateDeckEnemyAtPoint`를 직접 호출해 성공한 것만으로 자동 소환 경로를 검증하지 않는다. 위 절차에서는 실제 Sight가 들어와야 한다.

## 6. 저장 복원 확인을 추가할 때

프로젝트의 실제 Room 저장/이어하기 흐름으로 소환 대기 중 또는 일부 적이 등장한 시점에 저장한다. 다시 이어서 이미 등장한 적의 체력과 처치 상태가 유지되고 미완료 슬롯만 실행되는지 확인한다. PIE 재실행만으로는 Room 저장 복원을 검증할 수 없다.

새 저장 계약은 EnemyShip Enemy Domain v4, BaseEnemy Enemy Domain v2, DeckEnemy Pool Spawner Domain v1이다. 기존 EnemyShip v2/v3 및 BaseEnemy v1을 읽는 변환을 제공한다. 이전 저장의 부분 실패 결과가 슬롯 단위로 모호하면 `Legacy partial deck deployment has ambiguous slot results`로 복원을 차단한다. 새 검증은 별도 신규 Room 저장으로 시작하는 편이 원인 구분에 쉽다.

## 확인 범위

2026-10-08 `ArtisticSW2026Editor / Win64 / Development` 빌드는 성공했다. 스폰 생명주기 변경의 자동화 회귀, 수동 PIE·실제 멀티플레이·Room 저장 복원은 아직 검증하지 않았다. 이번 커밋 묶음에는 기존 BP_EnemyShip 수정, Final 티어/편성 BP, LV_ET 레벨과 별도의 근접 공격 테스트도 포함된다. 생명주기 C++ 변경 자체는 BP Delegate의 수동 연결을 요구하지 않는다. 빌드 결과는 런타임 검증을 대신하지 않는다.

두 저장 레벨을 읽기 전용으로 확인한 결과 각각 함선 51척, 보스 앵커 ID=20, Boss Encounter Enabled=false다. LV_ET의 3슬롯 일반함과 2슬롯 특수함을 구분하고, Lvl_CY의 기본 T1 근접 2슬롯 @0/10에서는 기대 인원도 2명으로 바꿔 확인한다. 레벨 로딩에서는 기존 `GCNS_Rogue_Arrival` BP 컴파일 오류가 발견되어 에셋 전체 검증 성공으로 보고하지 않는다.
