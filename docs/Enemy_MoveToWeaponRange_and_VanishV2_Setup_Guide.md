# MoveToWeaponRange / Vanish V2 에디터 연결 가이드

## MoveToWeaponRange

`BTT_MoveToWeaponRange`는 서버에서 현재 장착 무기의 `GetCurrentAttackRange()`를 읽어
`AttackRange - AcceptanceRangeInset`까지 `TargetActor`를 추적한다. 기본 Inset은 15cm이고,
Task가 성공하거나 중단되면 경로 이동과 남은 이동 속도를 즉시 정지한다.

Basic Attack Sequence는 다음 순서로 구성한다.

1. 필요하면 `Activate Enemy Ability By Tag`
   - `Ability Asset Tag`: `GameplayAbility.Enemy.Buff.MoveSpeed`
2. `Move To Weapon Range`
   - Blackboard Key: `TargetActor`
   - `Acceptance Range Inset`: 우선 15cm
   - `Minimum Acceptance Range`: 우선 25cm
3. 기존 Basic Attack Task

무기별 거리는 `DA_Weapon`의 `CombatData.AttackRange`만 조정한다. BT에 같은 거리를
중복 입력하지 않는다. 공격 거리가 매우 짧아도 계산된 Acceptance Range가 실제 공격
거리를 넘지 않으며, 무기나 유효한 적대 Target이 없으면 Task는 안전하게 실패한다.

## DashSlash / Vanish 목적지 관계

`BTT_SelectBossDestinationPoint`의 `Destination Relation`으로 Target 기준 방향을 선택한다.

| 분기 | Purpose | Destination Relation |
| --- | --- | --- |
| DashSlash | `Dash` | `Behind Target` |
| 기존 Vanish | `Vanish` | `Behind Target` |
| Vanish V2 | `Vanish` | `In Front Of Target` |

기존 BT Task의 기본값은 `Behind Target`이므로 저장된 DashSlash와 Vanish 동작은 유지된다.
Vanish V2 분기에서만 `In Front Of Target`으로 바꾼다.

Vanish V2 Sequence의 능력 실행 Task는 다음처럼 설정한다.

1. `BTT_SelectBossDestinationPoint`
   - Purpose: `Vanish`
   - Destination Relation: `In Front Of Target`
2. `Activate Boss Ability`
   - Ability Asset Tag: `GameplayAbility.Boss.VanishV2`
   - Require Preselected Destination: 체크

네이티브 `AShipBossEnemy`는 `UGA_BossVanishV2`를 기본 부여한다. Boss Blueprint가
`Starting Abilities` 배열을 직접 Override하고 있다면 해당 배열에도 `GA_BossVanishV2`를
추가해야 한다.

## Vanish 네트워크 확인

Vanish는 서버 전용 GA에서 다음 순서를 보장한다.

```text
숨김 복제 -> 이동/충돌 정지 -> 숨김 대기 -> 숨겨진 상태로 순간이동
-> 위치 복제 안정화 대기 -> Walking/Base 복구 -> 표시
```

Dedicated Server와 Client 2개로 PIE를 실행하고 다음을 확인한다.

1. 사라지기 전에 공중 이동 또는 Falling 포즈가 노출되지 않는다.
2. 기존 Vanish는 Player 뒤, V2는 Player 앞에서 나타난다.
3. 나타나는 순간 Boss가 갑판을 Base로 사용하고 `Walking` 상태다.
4. Ability Abort, 피격 취소, 사망 시에도 Boss가 숨은 채 남지 않는다.
5. 모든 Client에서 숨김, 위치, 재등장 순서가 동일하다.

