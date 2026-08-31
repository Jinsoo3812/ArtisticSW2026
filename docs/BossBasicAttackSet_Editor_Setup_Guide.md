# 보스 가변 기본공격 에디터 설정 가이드

## 1. 책임 구분

- `DA_Weapon > Item.EnemyWeapon.Sword > CombatData.AttackRange`: 모든 보스 기본공격이 공유하는 무기 사거리
- `DA_Weapon > CombatData.DamageEffectClass`: 모든 보스 기본공격이 공유하는 피해 Effect
- `DA_RogueBossBasicAttacks > Attacks`: 보스가 선택할 수 있는 가변 길이 공격 목록
- `UGA_BossBasicAttack`: 사용 가능한 Entry 선택, Montage 재생, 공통/개별 Cooldown 적용

Attack Entry에는 사거리 필드가 없다. Short와 Combo를 몇 개 추가하더라도 모두 현재 장착한 Sword의 동일한 `AttackRange`를 사용한다.

## 2. 공격 Montage 준비

1. 공격에 사용할 Animation Sequence를 준비한다.
2. Sequence에서 `Create > Create AnimMontage`를 선택한다.
3. 생성된 Montage의 Slot을 보스 AnimBP가 사용하는 Slot과 동일하게 맞춘다. 현재 기본값은 `DefaultSlot`이다.
4. 공격 판정 방식 중 하나를 선택한다.
   - 권장: Montage 또는 원본 Sequence에 `ANS_HitScanWindow`를 실제 칼날 타격 구간마다 배치한다.
   - 단일 타격 보조 경로: Attack Entry의 `Use Timed Hit Scan Window`를 켜고 정규화 구간을 입력한다.
5. Combo에서 여러 번 피해를 주려면 각 휘두르기마다 별도의 `ANS_HitScanWindow`를 배치한다. Window가 새로 시작될 때 이미 맞은 대상 목록이 초기화되어 같은 플레이어를 다음 타격에서 다시 맞힐 수 있다.
6. Montage를 저장한다.

`Timed Hit Scan Start Normalized=0.35`, `Duration Normalized=0.25`는 Montage 전체 길이의 35% 지점부터 25% 길이 동안 판정한다는 뜻이다. 즉 35~60% 구간이다. Montage Notify를 사용한다면 `Use Timed Hit Scan Window`를 끈다. 두 방식을 동시에 켜면 중복 판정 구간이 생길 수 있다.

## 3. AttackSet 배열 개수 조절

1. `/Game/GameplayAbilitySystem/Enemy/DA/DA_RogueBossBasicAttacks`를 연다.
2. `Attacks` 배열을 펼친다.
3. `+` 버튼으로 원하는 만큼 Entry를 추가하거나, 배열 요소의 휴지통/삭제 메뉴로 제거한다.
4. 배열에는 최소 1개의 유효한 공격만 있으면 된다.
5. 각 Entry를 아래 기준으로 설정한다.

| 프로퍼티 | 설정 방법 |
| --- | --- |
| `Attack Id` | 배열 안에서 중복되지 않는 이름. 예: `ShortA`, `ShortB`, `ComboA` |
| `Attack Type` | 정리/검사용 분류. `Short` 또는 `Combo` |
| `Attack Montage` | 실행할 Montage |
| `Attack Montage Play Rate` | 이 공격만의 기본 재생속도. 보통 `1.0` |
| `Selection Weight` | 선택 확률 가중치. `0`이면 선택되지 않으므로 보통 양수 사용 |
| `Use Timed Hit Scan Window` | Montage에 Notify가 없을 때만 활성화 |
| `Timed Hit Scan Start Normalized` | 판정 시작 위치, `0~1` |
| `Timed Hit Scan Duration Normalized` | 판정 지속 비율. 시작값과 합이 `1`을 넘으면 안 됨 |
| `Individual Cooldown Tag` | 이 공격만 추가 Cooldown이 필요할 때 설정 |
| `Individual Cooldown Duration` | 개별 Cooldown 시간. Tag와 Duration은 반드시 함께 설정 |

6. 같은 공격의 연속 반복을 줄이려면 AttackSet의 `Avoid Immediate Repeat`를 켠다.
7. 저장 후 Content Browser에서 `Validate Assets`를 실행한다.

## 4. 개수별 예시

공격 1개만 사용하는 경우:

```text
Attacks[0] = ShortA
```

기본 요구 구성:

```text
Attacks[0] = ShortA, Weight 1.0
Attacks[1] = ShortB, Weight 1.0
Attacks[2] = ComboA, Weight 0.6, Combo Cooldown 6초
```

공격을 5개로 확장하는 경우:

```text
ShortA, ShortB, ShortC, ComboA, ComboB
```

`Selection Weight`는 상대값이다. `1, 1, 0.5`라면 Cooldown이 하나도 없을 때 대략 `40%, 40%, 20%` 비율이다. 실제 후보에서는 현재 개별 Cooldown 중인 Entry와 직전 반복 방지로 제외된 Entry가 먼저 제거된 뒤 가중치를 다시 계산한다.

## 5. Cooldown 설정

모든 Entry에는 `UGA_BossBasicAttack`의 공통 `Cooldown.Enemy.BasicAttack`이 자동 적용된다. 현재 기본값은 2초다.

Combo를 덜 자주 사용하려면 다음 값을 추가한다.

```text
Individual Cooldown Tag      = Cooldown.Boss.BasicAttack.Combo
Individual Cooldown Duration = 6.0
```

여러 Combo Entry에 같은 Tag를 넣으면 ComboA 사용 후 ComboB도 함께 6초 동안 후보에서 제외된다. Combo마다 독립 Cooldown이 필요하면 별도의 Native Gameplay Tag를 추가해야 한다.

Short 공격에는 보통 개별 Cooldown Tag와 Duration을 모두 비워 둔다.

## 6. 보스 Blueprint 연결 확인

1. `/Game/GameplayAbilitySystem/Enemy/BP_Ship_BossEnemy`를 연다.
2. Class Defaults에서 `Boss > Combat > Basic Attack Set`이 `DA_RogueBossBasicAttacks`인지 확인한다.
3. `Starting Abilities`에 `GA_BossBasicAttack`이 포함되어 있는지 확인한다.
4. Weapon Component의 `Weapon Registry`는 기존 `DA_Weapon`을 유지한다.
5. `Default Weapon Tag`는 `Item.EnemyWeapon.Sword`를 유지한다.
6. 컴파일하고 저장한다.

## 7. Behavior Tree 확인

기존 BasicAttack 분기는 그대로 사용할 수 있다.

```text
거리 조건 확인
-> BTD_CanActivateAbilityByTag(GameplayAbility.BasicAttack)
-> BTT_ActivateBossAbility(GameplayAbility.BasicAttack)
```

거리 조건은 공격 시작 전까지만 유효하다. Montage가 Commit된 후 플레이어가 Sword 사거리 밖으로 나가 BT 분기가 Abort되어도 보스 기본공격 GA는 취소되지 않고 Montage를 끝까지 실행한다. 피격 또는 사망에 의한 GAS 취소는 유지된다.

## 8. PIE 확인 항목

1. 보스가 AttackSet에 등록된 모든 Montage를 장시간 플레이 중 실제로 선택하는지 확인한다.
2. `Selection Weight`를 크게 바꿨을 때 선택 빈도가 달라지는지 확인한다.
3. Combo 사용 직후 Short 공격은 가능하지만 Combo는 개별 Cooldown 동안 나오지 않는지 확인한다.
4. 공격 시작 후 플레이어가 즉시 사거리 밖으로 이동해도 Montage가 끝까지 재생되는지 확인한다.
5. Short 타격이 한 Window에서 같은 플레이어에게 한 번만 적용되는지 확인한다.
6. Combo의 각 HitScan Window에서 같은 플레이어가 다시 피해를 받을 수 있는지 확인한다.
7. 피격/사망으로 GA가 취소되면 HitScan Timer와 무기 Trace가 즉시 정리되는지 확인한다.

## 9. 초기화 스크립트 주의

`Scripts/setup_boss_basic_attacks.py`는 최초 예시 애셋을 ShortA, ShortB, Combo 3개 구성으로 생성하거나 다시 덮어쓰는 초기화 도구다. 에디터에서 배열을 원하는 구성으로 수정한 뒤에는 의도적으로 기본 3개 구성으로 되돌릴 때만 이 스크립트를 다시 실행한다.
