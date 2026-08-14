# RangedEnemy Bow + GAS + EQS MVP

## MVP 목표

RangedEnemy가 전투 타깃을 인식한 뒤 Behavior Tree에서 다음 루프를 반복한다.

1. 현재 위치에서 사격 가능하면 `BTT_RangedAttack`이 `GA_RangedEnemyAttack`을 실행한다.
2. 사거리 또는 시야 조건이 맞지 않으면 `EQS_RangedEnemy_CombatPosition`을 실행한다.
3. EQS 결과를 Blackboard의 `PointOfInterest`에 기록하고 그 위치로 이동한다.
4. 활의 `Arrow_socket`에서 화살을 생성해 타깃을 향해 발사한다.

이번 MVP에서는 공격 GA만 사용한다. 이동, 조준, 사거리 선택은 BT/EQS가 담당하며 이동용 GA는 만들지 않는다.

## 런타임 Sequence

```text
ABaseEnemy::BeginPlay
  -> Item.EnemyWeapon.Bow 로드아웃 생성 및 즉시 장착
  -> BP_EnemyBow 생성
  -> DA_Weapon의 GrantedAbilities로 GA_RangedEnemyAttack 부여

Perception / CombatTarget
  -> BT_Subtree_RangedEnemy_Combat Selector
     1. Attack: CanRangedAttack 성공
        -> Idle 속도 -> SetFocus -> BTT_RangedAttack -> ClearFocus
     2. Reposition: 공격 불가 + CombatTarget 유효
        -> Run EQS -> Strafe 속도 -> SetFocus -> MoveTo(PointOfInterest) -> ClearFocus
     3. TrackTarget: EQS 이동 실패 시
        -> 이동 속도 설정 -> SetFocus -> Wait -> ClearFocus

BTT_RangedAttack
  -> GameplayAbility.RangedAttack 태그의 정확한 AbilitySpec 탐색
  -> GA_RangedEnemyAttack 실행
  -> Ability 종료까지 Latent 대기

GA_RangedEnemyAttack
  -> 서버에서 타깃, 사거리, LOS 재검증
  -> 공격 몽타주 재생
  -> Event.Montage.FireArrow 수신
  -> 장착된 AEnemyBow의 Arrow_socket Transform 조회
  -> 화살 Spawn + Strength Damage Spec 초기화 + Launch
  -> Ability 종료
```

## 구현 파일

- `AEnemyBow`: `Source/Enemy/Public/Weapon/EnemyBow.h`, `Source/Enemy/Private/Weapon/EnemyBow.cpp`
- 활 에셋: `/Game/GameplayAbilitySystem/Enemy/Weapon/BP_EnemyBow`
- 무기 레지스트리: `/Game/GameplayAbilitySystem/Enemy/Weapon/DA_Weapon`
- 원거리 적: `Source/Enemy/Public/RangedEnemy/RangedEnemy.h`, `Source/Enemy/Private/RangedEnemy/RangedEnemy.cpp`
- 공격 GA: `Source/Enemy/Private/GAS/Ability/GA_RangedEnemyAttack.cpp`
- 자동화 테스트: `Source/Enemy/Private/Tests/RangedEnemyTests.cpp`
- 에셋 재생성 도구: `Tools/CreateRangedEnemyBowAssets.py`

## 활과 Arrow_socket 계약

`AEnemyBow::GetArrowSpawnTransform`은 다음 우선순위를 사용한다.

1. `WeaponMesh`에 실제로 존재하는 `Arrow_socket`
2. 메시 소켓이 아직 없는 MVP 에셋을 위한, 같은 이름의 `ArrowSocketPoint` SceneComponent

실제 활 메시가 확정되면 Static Mesh Editor에서 정확히 `Arrow_socket`이라는 이름의 소켓을 만들고 화살의 노크 위치와 전방 방향을 맞춘다. 코드나 GA에 캐릭터 기준 Muzzle Offset을 별도로 두지 않는다. LOS 검사와 실제 투사체 Spawn 모두 같은 활 소켓 Transform을 사용한다.

## 에디터 확인 항목

### BP_EnemyBow

- Parent Class: `AEnemyBow`
- `WeaponMesh`: 활 메시
- 메시 Socket: `Arrow_socket`
- `ProjectileClass`: `ARangedEnemyProjectile` 파생 클래스
- `ProjectileSpeed`: MVP 권장값 `2500 cm/s`

### DA_Weapon의 Bow Definition

- `WeaponTag`: `Item.EnemyWeapon.Bow`
- `WeaponActorClass`: `BP_EnemyBow`
- `GrantedAbilities`: `GA_RangedEnemyAttack`, Level 1
- `BackSocketName`: `BackSocket`
- `EquipSocketName`: `EquipSocket`
- `CombatData.AttackRange`: `2000`
- 공격 몽타주를 사용할 경우 `AttackMontage`와 `AttackMontagePlayRate` 설정

### 공격 몽타주

활시위가 놓이는 프레임에 Gameplay Event Notify를 추가한다.

- Event Tag: `Event.Montage.FireArrow`

몽타주가 없으면 GA는 즉시 화살을 발사한다. 몽타주가 있지만 Notify가 빠졌다면 Ability가 교착되지 않도록 몽타주 완료 시 한 번 발사하는 fallback이 있다. 최종 연출에서는 반드시 release 프레임 Notify를 사용한다.

### BT와 EQS

- `Run EQS Query` 결과 Key: `PointOfInterest` (Vector)
- `Move To` 대상 Key: `PointOfInterest`
- 공격 분기는 Selector에서 재배치 분기보다 먼저 평가
- 공격 분기 Decorator: `Can Ranged Attack`
- 재배치 분기 Decorator: `Combat Target State = IsSet`
- EQS는 명시적인 Context 중심의 Circle/Donut 후보를 생성
- 후보는 Querier로부터 양의 이동거리를 가져야 하며 Pathfinding과 LOS 필터를 통과해야 함

이 구조에서는 사격 가능 여부가 EQS 실행 자체를 막지 않는다. 첫 번째 공격 분기가 실패하면 Selector가 즉시 재배치 분기로 내려가고, MoveTo가 끝난 다음 루프에서 공격 조건을 다시 평가한다.

## MVP 수용 기준

- 전투 시작 시 RangedEnemy가 `BP_EnemyBow`를 장착한다.
- 장착 직후 ASC에 `GameplayAbility.RangedAttack` GA가 존재한다.
- 사격 불가능한 위치에서는 EQS 결과가 `PointOfInterest`에 기록되고 적이 해당 위치로 이동한다.
- 이동 중 적이 CombatTarget을 향해 회전한다.
- 사격 가능 시 BT가 GA를 한 번 실행하고 Ability 종료까지 기다린다.
- 생성된 화살 위치가 활의 `Arrow_socket` 월드 위치와 일치한다.
- 화살이 타깃 방향으로 발사되고 자기 자신, 장착 활, HostShip을 충돌 대상에서 제외한다.
- Notify가 중복되거나 몽타주 완료 callback이 이어져도 공격 한 번당 화살은 최대 한 발만 생성된다.

## 자동화 검증

에디터 타깃을 빌드한 후 다음 필터를 실행한다.

```text
Automation RunTests ArtisticSW.Enemy.RangedEnemy
```

`AttackIntegration` 테스트는 임시 RangedEnemy와 활 메시를 만들고 실제 `Arrow_socket`을 추가한다. 그 뒤 GA를 활성화하여 화살 개수가 한 개 증가하고 Spawn 위치가 소켓 위치와 일치하는지 검사한다. `CombatTreeContract`와 `EQSAssetContract`는 BT의 Selector 순서, Blackboard Key, EQS 이동/경로/LOS 계약을 검사한다.
