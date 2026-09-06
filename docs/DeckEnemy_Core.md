# Deck Enemy Core Architecture Guide

이 문서는 움직이는 EnemyShip 위에서 동작하는 `DeckEnemy`의 공통 구조를 빠르게 파악하기 위한 요약본이다.  
세부 에디터 수치, Boss Encounter 연출, 디버깅 절차와 초기 MVP 기록은 제외하고 핵심 책임만 정리한다.

---

## 1. 전체 구조

```text
AEnemyShip
├─ ShipDeckMesh
├─ DeckWaypointComponent[]
├─ UDeckNavigationComponent
└─ UDeckEnemySpawnerComponent
        |
        ├─ Enemy Pool / Spawn
        ├─ Point Occupancy / Reservation / Claim
        └─ DeckEnemy 활성화
                |
                v
            ADeckEnemy
            ├─ CharacterMovement Base = ShipDeckMesh
            ├─ UDeckEnemyNavigationComponent
            └─ Base Enemy State / Perception / GAS 재사용
                    |
                    v
              Deck 전용 BT Subtree
```

핵심 원칙:

- `DeckEnemy`는 일반 Enemy의 State / Perception / GAS 구조를 재사용한다.
- 차이는 **움직이는 갑판 위 위치와 이동을 Ship-local Waypoint 그래프로 처리한다는 것**이다.
- Enemy를 함선에 고정 Attach하여 이동시키지 않고 `CharacterMovement`의 Movement Base로 갑판을 따른다.
- Spawn, Path 선택, Reservation, AI 판단은 서버 권위다.

---

## 2. ADeckEnemy 역할

`ADeckEnemy`는 갑판 전용 Enemy의 공통 기반이다.

```text
ADeckEnemy
├─ Base Enemy 기능
├─ Deck Combat Role
├─ Host Ship / Deck 관계
├─ Pool 활성/비활성
└─ Deck Navigation 연동
```

`Deck Combat Role`에 따라 Combat Point 선택 정책만 달라진다.

```text
Melee
-> Target에 가까운 유효 Combat Point 선호

Ranged
-> 무기 사거리에 적합한 Combat Point 선호
```

기존 `ADeckRangedEnemy`는 자산 호환용 파생 클래스로 유지할 수 있으며,
새 공통 Deck 기능은 `ADeckEnemy`에 두는 것이 기본 구조다.

---

## 3. Moving Deck 계약

DeckEnemy는 `ShipDeckMesh`를 `CharacterMovement Base`로 사용한다.

따라서 함선이:

```text
이동
회전
Pitch / Roll
```

하더라도 Enemy는 갑판 기준 위치를 유지한다.

중요한 규칙:

```text
월드 좌표를 한 번 저장한 MoveTo
X

현재 DeckWaypoint의 live transform 추적
O
```

움직이는 배에서는 시작 시 계산한 World Vector가 곧 오래된 위치가 되므로,
Deck 이동 Task는 Waypoint의 최신 Ship-relative 위치를 계속 조회해야 한다.

---

## 4. Deck Waypoint Graph

Waypoint는 별도 Actor가 아니라 `AEnemyShip` 내부의 `DeckWaypointComponent`다.

각 Point는 핵심적으로 다음 정보를 가진다.

```text
WaypointId
LinkedWaypointIds
CanSpawn
CanPatrol
CanUseInCombat
```

### ID / Link 계약

- Point ID는 함선 내부에서 고유해야 한다.
- 링크는 실제 이동 가능한 이웃만 연결한다.
- 이동 가능한 관계라면 기본적으로 양방향 링크를 유지한다.
- Spawn Point는 안전한 위치이며 Combat 사용 가능 상태와 유효한 연결을 가져야 한다.

자동 생성 Point와 수동 Point를 함께 사용할 수 있지만,
최종 이동 그래프는 반드시 Validation을 통과해야 한다.

---

## 5. Navigation 책임 분리

Deck 이동은 세 계층으로 분리한다.

### `UDeckNavigationComponent`

```text
EnemyShip 단위
-> Ship-local Waypoint Graph 관리
-> 그래프 연결 / 경로 탐색
```

### `UDeckEnemyNavigationComponent`

```text
Enemy 단위
-> 현재 이동 상태
-> Combat 후보 산출
-> Target 변화에 따른 재탐색
```

### BT Tasks

```text
Select Deck Waypoint
-> 목적/Role에 맞는 경로와 목표 선택

Move To Live Deck Waypoint
-> 선택된 경로를 따라 live Point 추적
```

즉:

```text
Graph 자체
!=
Enemy별 전술 판단
!=
실제 이동 Task
```

로 책임을 분리한다.

---

## 6. Spawn / Reservation 구조

`UDeckEnemySpawnerComponent`가 서버에서 다음 상태를 단독 관리한다.

```text
Enemy Pool
Spawn Plan
Waypoint Occupancy
Next-hop Reservation
Final Combat Point Claim
```

Spawn Plan:

```text
Enemy Class + Spawn Point Id
```

를 Enemy 한 명당 하나씩 지정한다.

지정한 Point를 사용할 수 없다고 임의의 다른 Point로 대체하지 않는다.

### Point 경쟁 방지

여러 Enemy가 같은 위치를 선택하는 문제는 다음처럼 분리한다.

```text
Final Combat Point
-> Soft Claim

다음 이동 Point
-> Hard Reservation

실제 도착
-> Occupancy로 Commit
```

전체 경로를 한 번에 예약하지 않아 서로 다른 AI의 교차 경로가 장시간 막히는 것을 줄인다.

이동 실패, BT Abort, 사망, Pool 반환 시 관련 Reservation / Claim을 함께 해제한다.

---

## 7. Passive Behavior

Deck 전용 Passive Subtree의 기본 형태:

```text
Select Deck Waypoint
  Selection Mode = Patrol
        |
        v
Move To Live Deck Waypoint
        |
        v
Wait At Deck Waypoint
        |
        `-> 반복
```

Patrol은 `CanPatrol` Point만 사용한다.

일반 NavMesh `Move To(Vector)`가 아니라 Deck 전용 live waypoint 이동을 사용한다.

---

## 8. Combat Behavior

Deck Combat의 기본 구조:

```text
Root Selector
├─ Attack
└─ Reposition
```

### Attack

```text
Can Attack
-> Set Focus(TargetActor)
-> 역할별 Attack
```

공격 가능한 상태가 가장 높은 우선순위다.

### Reposition

```text
Select Deck Waypoint
  Selection Mode = Combat
-> Move To Live Deck Waypoint
```

이동 중에도 공격 가능 여부를 다시 확인한다.

```text
이동 중 Can Attack = true
-> 이동 종료
-> 다음 BT 평가
-> Attack Branch
```

Target이 크게 움직이거나 기존 경로가 더 이상 유효하지 않으면 다시 Combat Point를 선택한다.

---

## 9. Base Enemy 구조와의 관계

DeckEnemy가 새로 구현할 필요가 없는 것:

```text
EEnemyAIState
Sight / Hearing / Damage
TargetActor
BT_EnemyBase State Router
Gameplay Ability
Weapon
Health / Death
```

DeckEnemy가 특화해야 하는 것:

```text
Passive 이동
Combat Reposition
Ship-local Waypoint
Pool / Spawn
Point Reservation
Moving Deck Movement
```

따라서 구조적으로는:

```text
Base Enemy
+ Deck 전용 Navigation
+ Deck 전용 Passive / Combat Subtree
```

라고 이해하면 된다.

---

## 10. Pool & Network 정책

DeckEnemy Pool은 서버에서 미리 Actor를 준비하고 필요할 때 활성화한다.

비활성 상태에서는 불필요한:

```text
Tick
Collision
Brain
Movement Update
```

를 정지시키고 Dormancy를 사용할 수 있다.

네트워크에서 복제하는 것은 주로:

```text
활성 Enemy Actor 상태
현재 Waypoint 관련 최소 상태
CharacterMovement 결과
```

이다.

복제하지 않는 것:

```text
AI Controller
Blackboard
Behavior Tree
Waypoint Graph 자체
Reservation / Claim 내부 상태
```

즉 모든 AI와 경로 판단은 서버에서 실행하고,
Client는 최종 Actor 이동과 Gameplay 결과를 받는다.

---

## 11. 현재 BP_EnemyShip Waypoint 상태 주의

제공된 `BP_EnemyShip` snapshot 기준으로 현재 저장된 Waypoint에는 정리가 필요한 부분이 있다.

- 수동 Point 25개가 모두 `WaypointId = 0`
- 수동 Point의 `LinkedWaypointIds`가 비어 있음
- 자동 생성 Point 중 `10060`, `10120`은 고립 상태

따라서 현재 snapshot 그대로라면 수동 Point는 고유 ID / 링크 계약을 만족하지 않는다.

DeckEnemy 시스템을 실제 적용하기 전:

```text
Unique Waypoint ID
-> Bidirectional Link
-> Spawn / Patrol / Combat Flag
-> Isolated Point 제거
```

순으로 그래프를 정리하는 것이 필요하다.

---

## 12. 핵심 구조 요약

```text
Base Enemy
        |
        v
ADeckEnemy
        |
        +----------------------+
        |                      |
        v                      v
Deck Navigation         DeckEnemy Spawner
        |                      |
 Ship-local Graph       Pool / Reservation
        |                      |
        +----------+-----------+
                   |
                   v
          Deck Behavior Tree
          ├─ Patrol
          ├─ Attack
          └─ Combat Reposition
                   |
                   v
       CharacterMovement on ShipDeckMesh
```

DeckEnemy의 핵심은 새로운 공격 시스템을 만드는 것이 아니라,

**기존 Enemy AI를 유지한 채, 움직이는 함선에서 Spawn·Waypoint·경로·Point 경쟁을 안정적으로 처리하는 Deck 전용 이동 계층을 추가하는 것**이다.
