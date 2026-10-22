# Samurai Dash Point 실패 후 재배치

2026-10-10. Samurai의 Dash 선택 실패에만 적용한다. 자동화 테스트는 추가하거나 실행하지 않는다.

## 적용 범위

- `T4_BP_ShipBoss_Samurai`의 Combat BehaviorSet이 사용하는 `BT_Subtree_SamuraiBoss_Combat`을 수정했다.
- 정책 컴포넌트는 서버에서, 보스의 실제 Combat subtree가 `Samurai Combat Tree`와 일치할 때만 활성화된다. 공통 보스에 컴포넌트가 존재해도 Rogue 등 다른 Combat BT에서는 동작하지 않는다.
- 공통 `BTT_SelectBossDestinationPoint`의 Walk/Vanish/Dash 실행 방식과 `BTT_MoveToDeckWaypoint`는 유지한다. Samurai 단발 Dash만 별도 선택 Task로 교체했다.
- Point 실패를 공격 쿨다운으로 바꾸지 않는다. 기존 GAS 쿨다운·피격 취소·원자적 공격 수명·보행면 복구 규칙은 유지한다.
- 작업 전부터 있던 `BossDeckRecoveryComponent`, `DeckWalkAreaComponent`, `AbilityTask_BossVanishRelocation` 변경은 보존했다. 이번 기능은 전술 재배치이며 발밑 보행면 복구를 대신하지 않는다.

## 동작

```text
Samurai Combat Selector
├─ RepositionReady [Lower Priority]
│  └─ Reposition Samurai After Dash Failure
│     └─ 새 Walk Point/경로 선택 → 기존 갑판 이동 → 결과/Focus 정리
└─ 기존 Attack Selector [AllowNormalBehavior]
   ├─ BasicAttack [기존 조건 + AllowNormalBehavior]
   ├─ ChainSlashDash [기존 능력 조건 + AllowDash]
   │  └─ Focus → 능력 실행 → Clear Focus
   ├─ DashSlash [기존 능력 조건 + AllowDash]
   │  └─ Focus → Select Samurai Dash Point → 능력 실행 → Clear Focus
   ├─ SpeedUp [AllowNormalBehavior]
   └─ 기존 Move [AllowNormalBehavior]
```

1. 단발 Dash Point 선택은 전체 후보 탐색을 한 번 시도하고, 실패 시 0.25초 간격으로 실제 탐색을 다시 수행한다. 후보 하나의 탈락을 한 번의 실패로 세지 않는다.
2. 단발 BT 선택, 단발 활성화 직전 경로 재검증, 연속 능력 내부의 재등장/돌진 Point 선택 실패가 보스별 카운터를 공유한다. 기본 한도는 3회다.
3. 3회 실패하면 재배치를 요청하고 선택 Task를 `Failed`로 종료한다. 해당 BT 순회에서 남은 Dash/SpeedUp/Move 분기가 실행되지 않도록 각 분기에도 조건을 붙였다.
4. 서버가 Busy가 아니고 이동 가능한 상태이면 최우선 재배치 Task가 실행된다. 새 Walk 목적지와 경로를 선택하며 실패한 Dash 목적지를 재사용하지 않는다.
5. 최소 200cm 떨어진 목적지를 선택하고 최대 3초 동안 이동한다. 일반 전투 이동처럼 목표를 계속 추적 재계산하지 않고 선택된 재배치 위치로 이동한다. 이동 중 함선의 이동·회전은 기존 갑판 로컬 경로가 처리한다.
6. 이동 성공 시 카운터/요청을 초기화하고 즉시 BT를 다시 판단한다. 재배치 뒤 별도의 Wait는 없다.
7. Walk 선택/이동 실패 시 경로·예약·Blackboard·Focus를 정리하고 Dash 선택을 기본 1초 늦춘다. 이 실패는 Dash Point 실패 횟수에 다시 더하지 않는다. 일반 행동은 계속 판단할 수 있다.

실패 횟수는 **실제 Dash 또는 전체 연속 공격이 성공한 경우** 초기화한다. Point 선택만 성공했다가 활성화 직전 경로 검증에서 계속 실패하는 경우에도 누적 실패를 제한하기 위해, 선택 성공만으로는 초기화하지 않는다.

Busy, 타깃 소실/변경, 보행면 준비 중, 보스의 접지 해석 실패, 타깃 갑판 Anchor 해석 실패, 기존 보행면 복구, 일반 Ability 취소/몽타주 오류/BT Abort는 Point 실패로 합산하지 않는다. 연속 능력의 일반 실행/이동 실패도 `CancelForPointFailure`로 분류된 경우에만 전달한다.

정책은 타깃 문맥 변경, Combat 이탈, 사망, 함선 파괴, EndPlay, InitializeBoss, 룸 복원에서 초기화한다. 재배치 중 타깃이 바뀌면 이동 Task를 중단하고 정리한다. 진행 중인 공격의 목적지는 Busy 보호에 따라 유지한다.

## 파일별 책임

| 파일 | 책임 |
|---|---|
| `Source/Enemy/Public/BossAI/SamuraiDashRecoveryComponent.h` | Samurai Combat 범위, 편집 가능한 실패 한도/간격/시간, 정책 API와 개체별 상태 선언 |
| `Source/Enemy/Private/BossAI/SamuraiDashRecoveryComponent.cpp` | 범위·타깃 문맥 검사, Point 실패 누적, 재배치 요청/완료, 실패 이동 후 Dash 제한. 목적지 선택·실제 이동·능력 취소는 하지 않음 |
| `Source/Enemy/Public/Task/BTT_SelectSamuraiDashPoint.h` | Samurai 전용 Dash 선택 Task 계약 |
| `Source/Enemy/Private/Task/BTT_SelectSamuraiDashPoint.cpp` | NodeMemory로 제한 재시도/전체 대기 상한 관리, 기존 Point Selector 호출, 목적지 저장, 실패/Abort 정리 |
| `Source/Enemy/Public/Task/BTT_RepositionSamuraiAfterDashFailure.h` | 기존 갑판 이동 Task의 Samurai 전용 확장과 Walk 선택 설정 |
| `Source/Enemy/Private/Task/BTT_RepositionSamuraiAfterDashFailure.cpp` | 새 Walk Point/경로 설치, 기존 이동 실행, 타깃 변경 시 중단, 이동 결과 전달 및 경로/Focus 정리 |
| `Source/Enemy/Public/Decorator/BTD_SamuraiDashRecovery.h` | RepositionReady / AllowDash / AllowNormalBehavior 조건 선언 |
| `Source/Enemy/Private/Decorator/BTD_SamuraiDashRecovery.cpp` | 조건 평가와 재배치 분기의 Lower Priority 관찰. 목적지 변경·능력 취소는 하지 않음 |
| `Source/Enemy/Public/BossAI/ShipBossEnemy.h` | 정책 컴포넌트 소유 및 접근자 |
| `Source/Enemy/Private/BossAI/ShipBossEnemy.cpp` | 정책 컴포넌트 생성과 보스 수명/초기화/룸 복원 시 초기화 |
| `Source/Enemy/Private/AI/BaseAIController.cpp` | Combat을 떠날 때 정책 초기화 |
| `Source/Enemy/Public/BossAI/BossDeckPointSelector.h` | 선택 설정에 `MinimumWalkTravelDistance` 추가. 기본 0으로 기존 호출 유지 |
| `Source/Enemy/Private/BossAI/BossDeckPointSelector.cpp` | Walk 후보에 선택적으로 최소 이동 거리 적용. Samurai 재배치 Task만 기본 200cm 사용 |
| `Source/Enemy/Private/GAS/Ability/Boss/GA_BossDashSlash.cpp` | 활성화 직전 부적합 Point/경로 실패와 실제 단발 성공을 정책에 전달 |
| `Source/Enemy/Private/GAS/Ability/Boss/GA_BossChainSlashDash.cpp` | 명시적인 내부 Point 실패와 전체 연속 공격 성공을 정책에 전달 |
| `Source/Enemy/Private/Task/BTT_ActivateBossAbility.cpp` | Samurai Dash 능력 실패/Abort 후 Focus 정리. 진행 중인 Busy 공격과 다른 보스는 제외 |
| `Source/Enemy/Public/AI/SamuraiDashAuthoringCommandlet.h` | Samurai BT만 다루는 에디터 연결 도구 선언 |
| `Source/Enemy/Private/AI/SamuraiDashAuthoringCommandlet.cpp` | 기존 BT 조건/설정 보존, 분기 및 Task 교체, 연속 공격 사전 선택 제거, 편집 그래프 재구성/저장, 읽기 전용 구조 확인 |
| `Content/GameplayAbilitySystem/Enemy/AI/SubTree/SamuraiBoss/BT_Subtree_SamuraiBoss_Combat.uasset` | 실제 Samurai Combat 분기 연결. Blackboard는 기존 `BB_RogueBoss` 유지 |

기존 이동 Task가 이동·진행 정체·타임아웃·Abort를 처리하고, 기존 Point Selector가 갑판 안전성·점유·경로 조건을 처리한다. 정책 컴포넌트에 동일한 판단을 중복 구현하지 않는다. 발밑 보행면 복구의 수명과 예약은 기존 `BossDeckRecoveryComponent`가 계속 소유한다.

## Editor 설정

새 UCLASS/컴포넌트가 추가되었으므로 에디터를 닫고 `ArtisticSW2026Editor / Win64 / Development` 전체 빌드 후 다시 연다. Live Coding만으로 신규 컴포넌트를 갱신하지 않는다.

현재 에셋의 BT 연결은 이미 적용했다. 테스트 전에 아래 항목을 확인한다.

1. 실제 스폰 클래스가 `T4_BP_ShipBoss_Samurai`이고, Behavior Set의 Combat 항목이 `BT_Subtree_SamuraiBoss_Combat`을 가리키는지 확인한다. 저장된 실제 Samurai BP의 연결도 확인했다.
2. Samurai BP의 **상속된 `SamuraiDashRecoveryComponent`**를 선택한다. 새 컴포넌트를 중복 추가하지 않는다. `Samurai Combat Tree`가 해당 Combat BT인지 확인한다. BT를 복제해서 테스트한다면 이 속성도 복제한 BT로 명시적으로 변경한다.
3. BT의 Blackboard가 `BB_RogueBoss`인지 확인한다. `TargetActor`는 Object, `DestinationLocation`은 Vector이며 목적지는 선박 로컬 좌표다. 새 Blackboard 키는 필요 없다.
4. 최우선 `Reposition Samurai After Dash Failure` Task의 조건이 `RepositionReady`, Observer Aborts가 `Lower Priority`인지 확인한다. `AllowDash`/`AllowNormalBehavior`는 `None`이다. 새 조건 노드를 수동 생성했다면 Condition과 Observer Aborts를 함께 설정한다.
5. 단발 분기의 Task가 `Select Samurai Dash Point (Bounded Retry)`인지 확인한다. Selection Purpose는 Dash, 출력 키는 DestinationLocation, Target Actor Key는 TargetActor다. 단발 Activate Boss Ability는 `GameplayAbility.Boss.DashSlash`, Require Preselected Destination 켜기, Prefer Current Weapon Ability 끄기, Clear Destination When Finished 켜기다.
6. 연속 분기에는 별도 Select Point가 없어야 한다. Activate Boss Ability는 `GameplayAbility.Boss.ChainSlashDash`, Require Preselected Destination 끄기, Prefer Current Weapon Ability 끄기, Clear Destination When Finished 켜기다. 능력 자체의 내부 Point 검증은 유지한다.
7. 기존 단발/연속 Ability BP가 Starting Abilities에 포함되어 있고, 갑판 Walk Area·연결 경로·바닥 충돌이 준비된 상태에서 테스트한다. 안전한 Walk 후보가 전혀 없다면 재배치가 물리적으로 성공할 수 없으며 실패 정리/재시도 제한이 동작한다.

| 위치 | 기본값 | 의미 |
|---|---:|---|
| 컴포넌트: Maximum Point Failures | 3 | 공유 실패 한도 |
| 컴포넌트: Selection Retry Interval | 0.25초 | 단발 Point 전체 탐색 간격 |
| 컴포넌트: Maximum Selection Time | 0.75초 | 한 번의 단발 선택 Task 실행 대기 상한 |
| 컴포넌트: Failed Move Retry Delay | 1초 | 재배치 실패 후 Dash 진입 제한 |
| 재배치 Task: Walk Selection Settings / Minimum Walk Travel Distance | 200cm | 의미 있는 위치 변경의 최소 거리 |
| 재배치 Task: Walk Selection Settings / Ideal Walk Range | 500cm | 플레이어와의 이동 후 희망 거리 |
| 재배치 Task: Maximum Move Time | 3초 | 재배치 이동 상한 |
| 재배치 Task: Progress Timeout | 1초 | 진행 정체 감지 |

## 수동 PIE 확인

자동화 테스트 대신 아래 상황을 Editor에서 확인한다. 이 문서 작성 시 실제 PIE 플레이는 수행하지 않았다.

- **탐색 실패 유도:** 단발 선택 Task의 Selection Settings / Maximum Dash Distance를 임시로 100cm로 설정한다. 기본 최소 Dash 거리가 500cm이므로 후보가 선택되지 않는다. 연속 능력은 회차의 Distance Beyond Player를 임시로 5000cm로 설정하면 기본 최대 경로 거리 1200cm 안의 재등장/돌진 후보를 찾지 못하게 할 수 있다. 몽타주/타이밍 등 나머지 설정은 유효하게 유지한다. 테스트 후 원래 값을 복구한다.
- **3회 실패 후 전환:** 컴포넌트 Runtime의 Dash Point Failure Count, Needs Reposition, Repositioning과 BT Debugger를 관찰한다. 실패 한도에 도달하면 SpeedUp/다른 Dash/기존 Move를 건너뛰고 우선 재배치 Task가 실행되어야 한다.
- **이동 후 복귀:** 유효한 Walk 경로에서 실제로 위치가 바뀌고, 이동 종료 후 카운터와 요청이 초기화되는지 확인한다. 임시 실패 유도 값을 복구한 뒤 Dash가 다시 선택되는지도 확인한다.
- **이동도 실패:** Walk 경로를 막았을 때 경로·예약·DestinationLocation·Focus가 남지 않고, 1초 동안 두 Dash 분기의 재진입이 제한되는지 확인한다.
- **이동 중 조건 변경:** 플레이어가 움직이거나 Dash 쿨다운이 끝나도 재배치가 즉시 공격으로 끊기지 않아야 한다. 타깃 소실/변경, 사망, 전투 상태 이탈은 정상적으로 중단·초기화되어야 한다.
- **기존 보행면 복구:** 은신 재등장 후 발밑 복구가 진행되는 동안 전술 재배치가 목적지/예약을 덮어쓰지 않아야 한다.
- **동시 보스/다른 보스:** Samurai 두 개체의 카운터가 독립적인지, Rogue의 Walk/Vanish/Dash는 기존 동작을 유지하는지 확인한다.
- **움직이는 함선/2인 PIE:** 함선 이동·회전에 경로가 따라가고, 서버에서 선택/이동이 진행되며 클라이언트에서 중복 실행되지 않는지 확인한다.

Output Log에서 `LogSamuraiDashRecovery`로 필터하면 실패 횟수, 재배치 시작, 성공/실패/중단 결과를 볼 수 있다.

## 검증과 연결 도구

- `ArtisticSW2026Editor / Win64 / Development` 빌드 성공.
- Samurai Combat BT 저장 및 새 Editor 프로세스에서 재로드 구조 확인: 최우선 재배치, 두 Dash gate, 일반 분기 gate, 단발 전용 선택 Task, 연속 사전 선택 제거, BB_RogueBoss, 실제 Samurai BP의 BehaviorSet 경로.
- 자동화 테스트 추가/실행 없음. 수동 PIE 및 네트워크 플레이는 미실행.
- Editor 연결 도구는 `-run=SamuraiDashAuthoring`이다. 이미 마이그레이션된 BT는 재생성하지 않고 연결만 확인한다. `-AuditOnly`는 읽기 전용 확인이다. `-RepairBindings`는 Samurai Blackboard/gate/편집 그래프를 복구하여 저장하므로 특별히 복구가 필요한 경우에만 사용한다. 일반 실행에서는 그래프 배치나 이후 수동 조정을 덮어쓰지 않는다.

에디터 그래프는 새 분기를 반영하도록 재구성했으므로 노드 배치가 달라질 수 있다. 기존 기본 공격 거리, SpeedUp, 일반 Move의 설정과 우선순위는 보존했다.
