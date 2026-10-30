# GunnerBoss 에디터 설정 — 빨간 Ray + 기존 무기 연결

2026-10-13 변경: 준비 중에는 각 클라이언트가 빨간 선을 직접 표시하고, Prepare Time이 끝나면 서버가 RayCast로 피해를 판정한다. Warning Mesh / Warning Effect / AimLine BP는 필요 없다. 기본 공격·연사·거리 유지·후딜 구조는 유지한다.

## 1. 총 BP 만들기

- Blueprint Class → All Classes → **BaseWeapon** → `BP_GunnerGun` 생성. 기존 `Source/Enemy/Public/Weapon/BaseWeapon.h`의 무기 Mesh·복제·수명 구조를 그대로 사용한다.
- BP의 **WeaponMesh → Static Mesh**에 기존 `/Game/Fab/Vetterli_M78_Rifle/Rifle` 또는 `m78`을 넣는다. Collision은 **NoCollision**, Simulate Physics는 끈다.
- 발사 Event나 Projectile은 넣지 않는다. 공격과 피해는 보스의 GA가 처리한다. BaseWeapon의 TraceStartPoint / TraceEndPoint는 이 총 공격에 사용하지 않는다.

## 2. 기존 무기 등록 구조에 연결

기존 `/Game/GameplayAbilitySystem/Enemy/Weapon/DA_Weapon`을 **복제**하여 `DA_GunnerWeapons`를 만들고, Weapon Definitions에 항목을 하나 추가한다.

| 항목 | 넣을 값 |
| --- | --- |
| Weapon Tag | `Item.EnemyWeapon.Gun` — 코드에 등록되어 있음 |
| Weapon Actor Class | `BP_GunnerGun` |
| Socket Data → Equip Socket Name | `Gun_Grip` 또는 사용하는 손 소켓 이름 |
| Socket Data → Back Socket Name | 사용하는 등 소켓 이름; 즉시 장착에서는 사용하지 않음 |
| Ability Data → Granted Abilities | **비움** — 보스가 사격 GA를 이미 가지고 있음 |
| Stats → Strength Bonus | 기본 0; 필요하면 변경 |

이 방식은 기존 **BaseWeaponComponent**가 서버에서 총을 생성하고 손에 부착하며, 클라이언트 복제와 제거를 담당한다. Gunner의 시간·사거리·피해 계수는 아래 Combat Data에서 설정한다. 무기 Registry의 Combat Data를 복사해서 맞출 필요는 없다.

## 3. 캐릭터 소켓 2개

보스의 **캐릭터 Skeleton/Mesh**에서 설정한다.

- **Gun_Grip**: 손 본에 만들고 총이 손에 맞도록 위치·회전을 조절한다. 소켓 Preview Asset에 Rifle을 넣어 확인할 수 있다.
- **Gun_Muzzle**: 같은 손 본에 만들고 장착한 총의 총구 위치에 맞춘다. 사격과 빨간 선의 시작점이다.

`Gun_Grip`은 총 부착용, `Gun_Muzzle`은 Ray 원점용이다. Fire Socket은 **캐릭터 Mesh**에서 조회하므로 총 Static Mesh에만 소켓을 만들면 동작하지 않는다.

## 4. Combat Data

Data Asset → **GunnerBossCombatData** → `DA_GunnerCombat` 생성 또는 기존 자산 수정.

| 설정 | 기본값 / 권장 |
| --- | --- |
| Fire Socket Name | `Gun_Muzzle` |
| Attack Range / Trace Channel | 1,600cm / `WeaponAim` |
| Basic → Prepare Time / Recovery Time | 2.0초 / 1.2초 |
| Burst → Prepare Time / Recovery Time / Cooldown | 발당 1.0초 / 2.5초 / 12초 |
| Burst Shot Count / Burst → Shot Gap | 3발 / 0.2초 |
| Basic / Burst → Damage Coefficient | 1.0 / 0.7 |
| Basic / Burst → Lock Time | **0이면 발사 직전까지 Player 추적**. 기존 기본값 0.6 / 0.35초는 마지막 조준 고정 구간 |
| Ray → Color / Thickness | 빨강 / 2px |
| Ray → Shot Thickness / Shot Lifetime | 발사선 4px / 0.15초 |

`0 <= Lock Time <= Prepare Time`. 준비·연사·후딜 중에는 이동이 멈추고 피해를 받는다. 조준선은 엄폐물에서 끝나며 실제 피해도 같은 충돌 설정을 사용한다. Player Hurtbox와 갑판 난간·벽은 **WeaponAim을 Block**해야 한다.

Montage는 선택 사항이다. 연결하면 in-place FullBody의 `Aim`, `Fire`, `Recover` 섹션을 만들거나 Data의 Section 이름을 실제 섹션에 맞춘다. 서버 타이머가 발사하므로 AnimNotify에 별도 발사/피해 처리를 넣지 않는다.

## 5. 보스 BP·BT·함선

1. **GunnerBossEnemy** 기반 `BP_GunnerBoss`: Mesh / Anim BP / Physics Asset / 체력·Strength용 Default Stats Row 설정.
2. **Combat Data** = `DA_GunnerCombat`, **Default Weapon Tag** = `Item.EnemyWeapon.Gun`, **Equip Weapon On Spawn** = 켬.
3. **WeaponComponent → Weapon Registry** = `DA_GunnerWeapons`. Starting Abilities의 **GA_BossGunAttack / GA_BossGunBurst** 유지.
4. 기존 보스 Blackboard를 쓰는 `BT_GunnerCombat`: **Root → Selector → Gunner Boss Combat Task**. 기존 Root BT를 유지하고 보스 전용 BehaviorSet의 **Combat Subtree**에 연결. Blackboard의 `DestinationLocation(Vector)` 유지.
5. **GunMovement**에서 후퇴/선호/접근 거리 = 400 / 700 / 1,000cm, Move Speed = 220cm/s. 필요하면 조절.
6. 함선 **BossEncounterComponent → Boss Class** = `BP_GunnerBoss`, 유효한 Spawn Point와 기존 DeckWalkArea 설정 유지. 소환을 원치 않으면 Encounter Balance Row / Summoned Enemy Class를 비운다.

준비선용 BP나 Niagara/Material 자산을 지정할 항목은 없다. 기존 자산을 사용 중이었다면 에디터를 재시작하고 변경된 C++ Defaults를 확인한다.

## 수동 확인

2인 PIE에서 양쪽 화면의 빨간 선, Prepare Time 이후 발사, 총 부착, 준비/후딜 중 정지, 엄폐물 차단, 스턴·사망 후 선 제거를 확인한다. 자동화 테스트는 생략한다.

검증: 2026-10-13 `ArtisticSW2026Editor Win64 Development` 빌드 성공. BP 생성·자산 연결 및 PIE 확인은 에디터에서 진행한다.

## 파일별 역할

`GunnerBossCombatData`: 공격·선 설정 / `BossGunAimLine`: 클라이언트 선 그리기 / `BossGunCombatComponent`: 소켓·RayCast·이동 잠금 / `GA_BossGunAttack`: 단계·발사·피해 / `GunnerBossEnemy`: 기존 무기 장착 연결. 총 BP는 기존 `BaseWeapon`, `BaseWeaponComponent`, `WeaponDataAsset`을 재사용한다.
