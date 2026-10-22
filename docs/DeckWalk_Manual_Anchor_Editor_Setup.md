# 갑판 스폰 앵커·보행면 에디터 설정과 검증

최신 대조: 2026-10-08. 저장된 LV_ET와 Lvl_CY를 읽기 전용으로 확인했다. 스폰·풀·복원 동작은 [생명주기 가이드](Deck_Enemy_Spawn_Refactoring_Editor_Test_Guide.md), 구조는 [DeckWalk 구현](DeckWalk_Implementation.md)을 따른다.

## 1. 현재 앵커

BP_EnemyShip 및 두 레벨의 함선은 다음 앵커를 사용한다. ID를 바꾸면 Spawn Plan, 높이 기준과 보스 설정의 참조도 함께 맞춘다.

| 컴포넌트 | ID | 용도 | Can Spawn |
| --- | ---: | --- | --- |
| L_MeleeEnemySpawnPoint_0 | 0 | LowerDeck 일반 스폰·높이 기준 | 켬 |
| L_MeleeEnemySpawnPoint_1 | 1 | LowerDeck 일반 스폰 | 켬 |
| U_MeleeEnemySpawnPoint_0 | 10 | UpperDeck 일반 스폰·높이 기준 | 켬 |
| U_RangedEnemySpawnPoint_1 | 11 | UpperDeck 일반 스폰 | 켬 |
| U_RangedEnemySpawnPoint_2 | 12 | UpperDeck 일반 스폰 | 켬 |
| BossSpawnPoint | 20 | 최초 보스 전용 | 끔 |

과거 기본 5앵커/예비 1·11/보스 12 설정은 현재 저장값과 다르다. 이동용 Waypoint 링크·Combat 플래그는 현재 설정 대상이 아니다. 일반 순찰·전투는 보행면을 사용한다.

## 2. 보행면과 스폰 높이

DeckWalkAreaComponent → Surfaces에서 LowerDeck/UpperDeck, WaypointReference와 기준 ID 0/10을 확인한다. 실제 높이 범위는 **현재 기준 앵커 로컬 Z ± Height Below/Above Reference**다. 앵커 Z를 바꾸면 범위도 바뀌므로 과거 노드 수·높이 표를 고정 정답으로 사용하지 않는다.

Floor Component Names, 충돌 Mesh, Trace Complex, DeckMesh_Complex의 Scale을 확인한다. Spawn Height Offset은 바닥 위 Actor 중심의 오프셋이며 Capsule Half Height를 자동으로 더하지 않는다. C++ 기본은 90cm, 이전 두 레벨 조사에서는 30cm였으므로 해당 에셋의 실제 값을 우선한다. 이번 검증만을 위해 임의로 높이를 바꾸지 않는다.

Validate Deck Waypoints는 고유 ID, 부착, 보행면·스폰 참조와 캡슐 여유를 검사한다. Commandlet에서 바닥 노드가 0이라는 사실만으로 실제 PIE의 보행면 생성 실패를 단정하지 않는다.

## 3. 일반 적 편성과 자동 소환

Enable Spawning과 Spawn Plan은 EditDefaultsOnly다. 레벨마다 다른 편성이 필요하면 자식 BP의 컴포넌트 기본값을 수정한다. 항목마다 정확한 Enemy Class / Stats Row / Spawn Point Id를 지정한다.

- 기본 BP_EnemyShip/Lvl_CY: T1 근접 2명 @0/10.
- LV_ET: 일반함 3명, 특수함 2명. 티어·슬롯 순서는 [레벨 비교](LV_ET_vs_Lvl_CY_EnemySpawn_Audit.md)를 따른다.
- 기본 BP와 T1 파생 BP는 서로 다른 풀 클래스다. 수동/보스 소환 클래스도 준비된 풀과 정확히 일치해야 한다.
- Spawn Start Delay 3초, Sight Activation Delay 0.25초, Activation Interval 0.35초가 기본값이다.
- 자동 소환에는 현재 Player 배 Sight, 함선 Active, 보행면 준비가 필요하다. Wake 또는 Approach/Orbit만으로 소환되지 않는다.
- 거리 휴면은 조우를 리셋하지 않는다. 반복 Sight가 이미 완료한 슬롯을 다시 실행해서는 안 된다.

대표 일반함을 원거리 휴면 → Wake → 실제 Sight로 접근시켜 계획 수량을 확인한다. 직접 ActivateDeckEnemyAtPoint 성공만으로 자동 큐 검증을 대신하지 않는다.

## 4. 보스를 사용할 때

현재 LV_ET/Lvl_CY는 각각 51척 모두 Boss Encounter Enabled=false다. 보스를 사용할 BP/인스턴스에만 활성화하고 생성 가능한 Boss Class, Boss Stats Row, Boss Spawn Point Id=20과 트리거·스토리 조건을 설정한다.

보스 추가 소환은 기존 풀의 정확한 클래스와 도달 가능한 일반 스폰 앵커를 사용한다. 기존 일반 적의 사망·풀 반환을 기다리고 빈 포인트·거리·바닥·Capsule 조건을 만족해야 한다. 보스 전용 20을 일반 Can Spawn 앵커로 취급하지 않는다. 승무원 전멸로 함선이 소환 불가 상태가 되는 경우도 구분한다.

Test_Level의 T2_BP_ShipBoss_Rogue, BP_DeckMeleeEnemy 재소환, Story.GameStarted/MiddleBoss1Defeated 조건은 2026-10-03 실험 설정이다. 이번 최신 점검은 Test_Level을 불러와 그 설정을 다시 확인하지 않았다.

## 5. 이동·능력·네트워크 확인

서버 PIE에서 층별 보행면 Ready와 실제 스폰 위치를 확인한다. 같은 층의 플레이어 추적·무기 사거리 정지·쿨타임 재배치를 확인하고, 움직이는 배에서도 Movement Base와 발밑 바닥이 유지되는지 본다.

보스 Walk/Strafe/DashSlash/Vanish는 현재 보행면 위치/Revision과 실제 지지 바닥을 사용한다. 목적지 표시 Vector와 실제 유효한 위치 핸들을 구분한다. 이동 중단·사망·Rebuild·풀 반환 시 예약과 이동 잠금이 정리되어야 한다. 층 사이에 연속 지지 바닥과 범위 구성이 없으면 경로가 자동 연결되지 않는다.

Listen Server와 다른 클라이언트에서 인원·Host·활성 세대·표시·충돌·이동 기반이 일치하는지 확인한다. Dedicated Server/Late Join/실제 재접속·지연·손실은 별도 검증이다.

## 6. 실패 구분과 확인 범위

서버 Output Log의 DeckWalk / DeckEnemySpawner / BossEncounter / DeckSpawnAnchorValidation을 검색한다.

| 이유 | 확인할 내용 |
| --- | --- |
| WalkAreaNotReady / NoWalkableSpawnFloor | 높이 기준·Mesh 충돌·Scale·앵커 위치 |
| CapsuleBlocked / PointUnavailable | 현재 충돌·예약 경합 |
| WaitingForSight / SightExpired | 실제 Player 배 감지와 대기 수명 |
| InactiveBalanceAlreadyApplied | 비활성 풀의 스탯 계약과 해당 Generation/Slot |
| MissingOrAbstractBossClass | 보스 Class와 활성화 설정 |
| ReadinessTimeout | 준비 실패와 제한 시간 |

2026-10-08 현재 Editor 빌드는 성공했고 저장된 레벨 설정을 읽었다. 생명주기 변경의 실제 PIE·멀티플레이·Room 복원은 아직 검증하지 않았다. 과거 LowerDeck 302 / UpperDeck 387 노드는 2026-10-03의 BP/Test_Level 실험 기록이며 현재 두 레벨의 보장값이 아니다.
