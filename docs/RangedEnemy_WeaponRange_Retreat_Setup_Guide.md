# RangedEnemy 무기 사거리 후퇴 설정

## 런타임 계약

- 원거리 최대 공격 거리는 장착 무기의 `CombatData.AttackRange`를 우선한다.
- `ARangedEnemy.MaxAttackRange`는 무기 데이터가 없는 경우에만 fallback으로 사용한다.
- `BTT_RetreatToWeaponRange`는 서버에서 Player 반대 방향의 NavMesh 후보를 선택한다.
- Task는 무기 사거리보다 75cm 안쪽을 목표로 하며 Player가 100cm 이상 움직이면 0.2초 간격으로 재탐색한다.
- 모든 후보가 막힌 경우 실패하여 기존 EQS Reposition 분기로 내려간다.

## Combat Subtree

`BT_Subtree_RangedEnemy_Combat`의 Root Selector에서 후퇴 Sequence를 가장 왼쪽에 둔다.

```text
Root Selector
|- Emergency Retreat Sequence
|  |- Target Distance: Within 200, Observer Aborts = Lower Priority
|  |- Set Movement Speed: Run
|  |- Optional SpeedUp Selector
|  |  |- Sequence: Can Activate MoveSpeed -> Activate MoveSpeed
|  |  `- Finish With Result: Succeeded
|  `- Retreat To Weapon Range
|- Ranged Attack Sequence
|- EQS Reposition Sequence
`- Track Target Sequence
```

주의사항:

1. 가까운 거리와 먼 거리 Decorator를 같은 Sequence에 직접 붙이지 않는다. 직접 연결된 Decorator는 AND다.
2. 가까운 거리 Decorator의 Observer Aborts는 `Both`가 아니라 `Lower Priority`다. `Both`이면 200cm를 벗어나는 순간 후퇴 Task가 자기 자신을 중단한다.
3. `Can Activate Ability By Tag`를 Emergency Retreat Sequence 자체에 붙이지 않는다. SpeedUp 쿨다운이 후퇴까지 막게 된다.
4. `Set Movement Speed: Run`을 SpeedUp과 후퇴 Task보다 먼저 실행한다. 기본 속도가 0인 상태에서는 MoveSpeedBonus가 Idle을 우회하지 않는다.
5. 기존 `BTT_MoveToWeaponRange`는 Target에게 접근하는 Task이므로 가까운 거리 후퇴 분기에서 사용하지 않는다.

## 권장 초기값

| 항목 | 값 |
| --- | ---: |
| 가까운 거리 Trigger | 200cm |
| Weapon `AttackRange` | 1000cm 이상, 무기별 설정 |
| Retreat `RangeInset` | 75cm |
| Retreat `MinimumDesiredRange` | 300cm |
| Retreat `AcceptanceRadius` | 50cm |
| Repath Interval | 0.2초 |
| Target Repath Threshold | 100cm |
| Maximum Move Time | 6초 |

## 멀티플레이 확인

Dedicated Server와 Client 2개로 다음을 확인한다.

1. 200cm 진입 시 실행 중인 공격/EQS 분기가 즉시 중단된다.
2. SpeedUp이 쿨다운이어도 기본 속도로 후퇴한다.
3. SpeedUp 사용 가능 시 두 Client에서 동일한 증가 속도가 보인다.
4. Player가 추격하면 서버가 목적지를 다시 선택하고 클라이언트에는 CharacterMovement 결과만 복제된다.
5. Target 상실, BT Abort, 사망 시 이동이 즉시 정지한다.
6. 후퇴 완료 후 `Can Ranged Attack` 분기로 전환된다.
