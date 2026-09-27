# Rogue 보스 기본공격 파이프라인 및 설정 계획

작성: 2026-09-12. C++ 소스와 Unreal Python commandlet으로 **디스크에 저장된 자산**을 읽어 확인했다. 실행 중 에디터의 미저장 값과 PIE 동작은 검증하지 않았다. 게임 코드와 콘텐츠는 변경하지 않았다.

## 1. 결론과 확인된 문제

랜덤 선택 구조를 새로 만들 필요는 없다. Rogue의 기본 연결은 이미 구성되어 있다.

| 항목 | 저장된 현재 값 | 판단 |
|---|---|---|
| BP_Ship_Rogue.BasicAttackSet | DA_Rogue_Attacks | 연결됨 |
| StartingAbilities | GA_BossBasicAttack 포함 | 연결됨 |
| BehaviorTree / BehaviorSet | BT_BossBase / DA_Rogue_AI | 연결됨 |
| Combat 동적 서브트리 | AI.Behavior.Combat → BT_Subtree_RogueBoss_Combat | 연결됨 |
| DefaultWeaponTag | Item.EnemyWeapon.Knife | Knife 정의 사용 |
| Weapon Registry | DA_Weapon | 연결됨 |
| Knife WeaponActorClass / EquipSocket | BP_Knife / Knife | 정의됨; 실제 부착 위치 확인 필요 |
| Knife DamageEffectClass | **None** | **공격 실행을 막는 확정적인 설정 누락** |
| Knife AttackRange | 180 cm | 실제 단검 도달 거리와 비교 필요 |
| Attack Set | ShortA → AM_Rogue_Attack1, ShortB → AM_Rogue_Attack2 | ID 중복 없음, 가중치 각각 1 |
| AvoidImmediateRepeat | true | 두 후보가 유효하면 첫 선택 이후 A/B 교대 |
| Attack1 HitScan | 약 0.327~0.665초에 ANS_HitScanWindow | 판정 구간 있음 |
| Attack2 HitScan | 몽타주와 원본 시퀀스 모두 Notify 없음, Timed HitScan도 false | **재생되더라도 해당 공격은 타격 판정이 시작되지 않음** |
| Rogue Mesh Skeleton | SKM_Rogue_Skeleton | 몽타주와 다름 |
| 두 Rogue Montage Skeleton | SK_Mannequin | 호환 설정 또는 리타기팅 검증 필요 |
| Rogue Anim Class / Montage Slot | ABP_Unarmed / DefaultSlot | 실제 출력 경로와 스켈레톤 호환성 확인 필요 |

Knife의 GrantedAbilities와 AttackMontage도 비어 있지만, **현재 보스 전용 경로에서는 이것 자체가 실패 원인은 아니다.** Ability는 보스가 부여하고 몽타주는 Attack Set에서 가져온다. ImpactGameplayCueTag는 선택적인 피드백 설정이다.

## 2. 전체 실행 흐름

```text
BP_Ship_Rogue 생성
  ├─ BaseEnemy: 서버에서 StartingAbilities 부여
  ├─ WeaponComponent: DefaultWeaponTag로 WeaponRegistry 검색
  │    └─ Knife 정의 → BP_Knife 생성·부착 (Equip On Spawn = true)
  └─ AIController: BT_BossBase 실행 + DA_Rogue_AI의 동적 서브트리 주입
       └─ Combat → BT_Subtree_RogueBoss_Combat
            └─ 타깃/무기/거리/쿨다운 조건 충족
                 └─ BTT_ActivateBossAbility(GameplayAbility.BasicAttack)
                      └─ GA_BossBasicAttack을 우선 선택
                           ├─ PrepareAttack: Attack Set 가중치 랜덤 선택
                           ├─ CacheAttackData: 현재 무기 + DamageEffect 필수 검사
                           ├─ Commit: 공통 쿨다운 + 선택 항목 개별 쿨다운
                           ├─ Attack ID로 항목 재조회 → 몽타주 재생
                           ├─ Notify 또는 타이머 → HitScan 시작/종료
                           │    └─ BP_Knife Trace → 대상에게 DamageEffect 적용
                           └─ 몽타주 종료/중단 → 판정 및 공격 상태 정리
```

실제 함수 호출에서는 Attack ID 재조회와 실행 데이터 구성도 Commit 전에 수행된다. 위 도식의 재생 단계는 그 실행 데이터를 사용한다.

- `BossBasicAttackSet::SelectAttack`: 몽타주 없음, 가중치 0 이하, 개별 쿨다운 중인 항목을 제외한다. 후보가 둘 이상이고 반복 방지가 켜졌으면 직전 ID를 제외한 뒤 가중치를 추첨한다.
- `GA_BossBasicAttack::ResolveAttackExecutionData`: 몽타주/재생 속도는 Set, 데미지 GE/Impact Cue는 현재 WeaponDefinition에서 가져온다. DamageEffectClass가 None이면 false를 반환한다.
- `GA_BasicAttack::ActivateAbility`: 준비 또는 무기 데이터 검증에 실패하면 **Commit과 몽타주 재생 전에 종료**한다.
- 공통 쿨다운은 `Cooldown.Enemy.BasicAttack`, 네이티브 기본 지속시간은 2초다. 개별 쿨다운은 추가로 적용된다.
- `AttackType=Combo`만 지정한다고 다단 히트가 자동 생성되지는 않는다. 실제 타격 횟수는 HitScan 구간 구성으로 정한다.
- `ABaseWeapon`은 서버에서 TraceStartPoint~TraceEndPoint 사이 Sphere Trace를 수행하고 한 구간에서 이미 맞힌 대상을 중복 처리하지 않는다. 새 구간에서는 다시 맞힐 수 있다.

## 3. 설정 적용 순서

### 단계 1 — Knife 데미지 정의 완성

1. `/Game/GameplayAbilitySystem/Enemy/Weapon/DA_Weapon`을 연다.
2. Weapon Definitions에서 `WeaponTag = Item.EnemyWeapon.Knife` 항목을 선택한다.
3. `Combat Data > Damage Effect Class`를 지정한다. 최초 연결 검증에는 현재 Sword/Hand가 사용하는 `GE_Hand_Attack`을 재사용할 수 있다. 최종 단검 데미지가 달라야 하면 효과 내용을 확인하고 전용 GE로 분리한다.
4. `Weapon Actor Class = BP_Knife`, `Equip Socket Name = Knife`, Attack Range 180을 확인한다. 사거리는 플레이 테스트에서 조정한다.
5. Rogue BP의 WeaponComponent가 같은 DA_Weapon을 참조하는지, 배치 인스턴스나 스폰 초기화가 값을 덮어쓰지 않는지 확인한다.

### 단계 2 — 애니메이션 재생 연결 검증

1. Rogue 메시와 두 몽타주의 Skeleton이 다르므로, 현재 Compatible Skeleton 설정과 실제 재생 가능 여부를 확인한다. 이름 차이만으로 비호환을 확정하지 않는다.
2. 호환이 성립하지 않으면 Rogue용 Skeleton으로 리타기팅한 애니메이션/몽타주와 대응 AnimBP를 사용한다. Skeleton만 임의로 교체하지 않는다.
3. 실제 AnimInstance가 생성되고, `DefaultSlot`이 최종 포즈에 반영되는 AnimGraph 경로에 있는지 확인한다.
4. 각 몽타주를 단독으로 재생해 동작·블렌딩·무기 부착을 먼저 검증한다.

### 단계 3 — 두 번째 공격의 판정 추가

1. `AM_Rogue_Attack2`의 실제 칼날 접촉 구간에 `ANS_HitScanWindow`를 추가한다.
2. 현재 Attack1에는 이미 약 0.327~0.665초 구간이 있으므로 실제 동작과 맞는지만 확인한다.
3. 두 항목의 `Use Timed Hit Scan Window = false`를 유지한다. Notify 대신 타이머를 선택한다면 해당 항목에만 타이머를 설정하고 동일 타격의 Notify와 겹치지 않게 한다.
4. `BP_Knife`의 TraceStartPoint/TraceEndPoint, TraceRadius, TraceObjectTypes를 확인하고 디버그 Trace가 칼날을 따라가는지 확인한다. Trail 효과는 데미지 판정을 대체하지 않는다.

### 단계 4 — Attack Set의 선택 정책 결정

| 설정 | Attack1 | Attack2 |
|---|---|---|
| Attack ID | ShortA | ShortB |
| Montage | AM_Rogue_Attack1 | AM_Rogue_Attack2 |
| Play Rate | 1.0 | 1.0 |
| Selection Weight | 1.0 | 1.0 |
| Individual Cooldown | 빈 태그 / 0초 | 빈 태그 / 0초 |
| 판정 방식 | ANS_HitScanWindow | ANS_HitScanWindow 추가 |

- 매번 독립적으로 랜덤 선택하고 AA/BB 반복도 허용하려면 `Avoid Immediate Repeat = false`.
- 반복 금지가 목표라면 true 유지. **지금처럼 두 후보뿐이면 A→B→A→B가 정상 동작**이다.
- 추가 항목은 고유하고 비어 있지 않은 ID를 사용한다. 선택 후 ID로 첫 일치 항목을 재조회하므로 중복 ID는 잘못된 몽타주 재생을 유발한다.
- 개별 쿨다운을 추가할 때는 태그와 양수 지속시간을 함께 설정한다. 모든 항목이 동시에 쿨다운에 묶이지 않도록 일반 공격 후보를 남긴다.

### 단계 5 — BT와 실행 검증

1. 실행 중 `BP_Ship_Rogue`가 `BT_BossBase`를 사용하고 Combat에 Rogue 서브트리가 주입되는지 BT 디버거로 확인한다.
2. 기본공격 노드는 `BTT_ActivateBossAbility`, 태그는 `GameplayAbility.BasicAttack`, `Require Preselected Destination = false`로 확인한다. `Prefer Current Weapon Ability`는 기존 true 설정을 사용할 수 있다. 이 노드는 보스 기본공격 GA를 무기 GA보다 먼저 선택한다.
3. `BTT_EnemyBasicAttack`은 현재 무기에서 부여한 Ability를 찾는 별도 경로다. Knife는 GrantedAbilities가 비어 있으므로 이 노드로 교체하면 안 된다.
4. 거리 조건 또는 `Move To Weapon Range`가 Knife 사거리로 진입시키는지 확인한다. `Is Melee Attack Ready` 자체는 타깃과 장착/쿨다운 등을 확인하며 실제 거리를 검사하지 않는다.
5. 최초에는 공격 1개씩 확인한 뒤 2개를 함께 활성화한다. 이동·공격 시작·몽타주·실제 HP 감소를 단계별로 확인한다.

## 4. 완료 기준과 진단

- 각 몽타주가 단독으로 재생되고, 각각의 타격 구간에서 대상 HP가 1회 감소한다.
- 두 항목 활성화 시 선택 정책에 맞게 두 동작이 나온다. 반복 방지 true일 때 교대는 정상이다.
- 피격/사망 중단 뒤 HitScan과 Busy/Attacking 상태가 남지 않는다.
- 서버와 클라이언트에서 두 동작이 보이고 데미지가 중복 적용되지 않는다.
- Output Log에서 `Log LogEnemyBasicAttack Verbose`로 재생 로그를 활성화하면 실제 Ability/몽타주를 추적할 수 있다.
- `Basic attack has no valid weapon damage data`는 무기 데이터뿐 아니라 PrepareAttack 실패에도 출력된다. 이 메시지만으로 원인을 단정하지 말고 Set 후보와 무기 GE를 함께 확인한다.

## 5. 참고 및 후속 개선

- 기존 `Scripts/setup_boss_basic_attacks.py`는 옛 `DA_RogueBossBasicAttacks` 경로와 공통 보스 BP를 대상으로 Samurai 몽타주를 설정한다. 현재 Rogue 설정용으로 그대로 실행하지 않는다.
- `docs/BossEnemy_Attacks_Validated.md`의 기존 자산명보다 현재 저장된 `DA_Rogue_Attacks` / `DA_Samurai_Attacks` 구성을 기준으로 작업한다.
- 이번 수정은 우선 에디터 데이터 설정만으로 진행한다. 후속 코드 개선이 필요하면 Attack Set 선택 실패와 Weapon GE 누락 로그를 분리하고, 보스 BP·무기 정의·Set을 함께 검사하는 검증 기능을 추가한다.

근거 소스: `Source/Enemy/Private/BossAI/BossBasicAttackSet.cpp`, `Source/Enemy/Private/GAS/Ability/Boss/GA_BossBasicAttack.cpp`, `Source/Enemy/Private/GAS/Ability/GA_BasicAttack.cpp`, `Source/Enemy/Private/Task/BTT_ActivateBossAbility.cpp`, `Source/Enemy/Private/Weapon/BaseWeapon.cpp`, `Source/Enemy/Private/AI/BaseAIController.cpp`.

저장 자산 조사 로그: `Saved/RogueAttackAudit3.log`. 조사 스크립트: `Saved/audit_rogue_readonly.py` (읽기 전용, 자산 저장 호출 없음).
