# 갑판 총기 보스 — 에디터 연결 안내

새 C++ 구현을 빌드한 뒤 에디터를 다시 열고 아래 자산을 연결한다. 기존 레벨과 보스 BP는 자동 변경하지 않았다. 자동화 테스트는 추가·실행하지 않는다.

## 1. 필요한 자산 4개

| 자산 예시 | 생성 방법 | 설정 |
| --- | --- | --- |
| `BP_GunnerBoss` | `GunnerBossEnemy`를 부모로 Blueprint 생성 | Mesh, Anim BP, Physics Asset, 체력/Strength 설정, 아래 Combat Data/BT 지정 |
| `BP_GunnerAimLine` | `BossGunAimLine`을 부모로 Blueprint 생성 | Warning Mesh 또는 Warning Effect 지정 |
| `DA_GunnerCombat` | Miscellaneous → Data Asset → `GunnerBossCombatData` 선택 | Fire Socket Name, Aim Line Class, 공격 시간/피해/사거리 |
| `BT_GunnerCombat` | 기존 보스와 동일 Blackboard로 Behavior Tree 생성 | Root → Selector → **Gunner Boss Combat** Task 하나 |

권장 연결 순서: AimLine BP → Combat Data → Combat BT → Boss BP → 함선 Encounter.

## 2. 총구와 조준선

1. 보스 **캐릭터 Skeletal Mesh/Skeleton**의 총구에 소켓을 만들고 `DA_GunnerCombat.Fire Socket Name`에 같은 이름을 넣는다. 초기 이름은 `Gun_Muzzle`. 총이 별도 Mesh여도 조회 원점은 캐릭터 Mesh의 소켓이다. 소켓이 없으면 사격하지 않으며 Actor 위치로 대체하지 않는다.
2. `BP_GunnerAimLine`의 **Warning Mesh**에 Cube처럼 X축으로 길이를 늘릴 수 있는 중심 원점 Mesh를 지정한다. 충돌·그림자는 코드에서 끈다. **Warning Material**에는 빛나는 재질을 넣으면 된다. `BeamColor` Vector Parameter가 있으면 Tracking/Locked Color가 적용된다. 다른 이름이면 **Color Parameter**도 바꾼다.
3. Niagara를 사용할 경우 **Warning Effect**에 Beam을 지정하고 `User.BeamStart`, `User.BeamEnd`를 월드 좌표로 받도록 설정한다. 이름은 BP의 Start/End Parameter에서 변경 가능하다. 선택 항목: `User.BeamColor`, `User.Progress`(현재 단계의 0~1 진행률).
4. **Shot Effect**는 순간 사격선 Niagara로 같은 Start/End Parameter를 받는다. **Muzzle Effect / Impact Effect / Prepare Sound / Lock Sound / Fire Sound**는 선택 사항이다. Muzzle/Impact Niagara는 스스로 종료되는 효과를 사용한다.
5. `DA_GunnerCombat.Aim Line Class`에 `BP_GunnerAimLine`을 지정한다. Warning Mesh와 Warning Effect가 모두 비어 있으면 준비선을 볼 수 없으므로 공격을 시작하지 않는다.

**Trace Channel 기본값은 기존 `WeaponAim`**이다. Player의 기존 CombatHurtbox/Physics Asset과 갑판 난간·벽·엄폐물이 이 채널을 Block해야 한다. 플레이어가 맞지 않으면 Player Mesh의 Hurtbox 초기화와 채널 응답을 먼저 확인한다. 다른 채널로 바꿀 수 있지만 조준선과 실제 발사는 항상 같은 설정을 사용한다.

## 3. 공격 수치 — DA_GunnerCombat 한 곳에서 수정

| 항목 | 기본 공격 Basic | 능력 1 Burst |
| --- | --- | --- |
| Prepare Time: 조준선 표시부터 발사까지 | 2.0초 | 각 발 1.0초 |
| Lock Time: 준비에 포함되는 마지막 고정 구간 | 0.6초 | 각 발 0.35초 |
| Recovery Time: 마지막 발 이후 정지 후딜 | 1.2초 | 2.5초 |
| Damage Coefficient: Strength 피해 계수 | 1.0 | 0.7 |
| Cooldown: 공격 Commit 시 시작 | 0초 | 12초 |
| Post Attack Gap: 후딜 종료 후 모든 사격의 추가 대기 | 0.6초 | 0초 |
| Shot Gap: 발 사이 대기 | 단발에는 미사용 | 0.2초 |

- **Burst Shot Count** 기본 3, **Attack Range** 기본 1,600cm, **Aim Update Interval** 기본 0.05초.
- `0 < Lock Time <= Prepare Time`. 준비와 후딜 중에는 이동·다른 공격이 금지된다. Post Attack Gap에는 이동할 수 있다.
- `Damage Coefficient` 두 값은 독립적이다. Basic을 바꾸면서 Burst를 기본 공격의 70%로 유지하려면 Burst도 함께 조정한다. `Impact Cue`에는 기존 피해 확인용 Impact GameplayCue를 선택할 수 있다.
- **Target Aim Offset**은 Player Actor 원점 기준 로컬 오프셋. 기본 `(0,0,0)`이며 실제 모델의 몸통 위치에 맞춰 조절한다.
- 각 Pattern의 **Montage**는 선택 사항이다. 없으면 조준선·판정·타이밍은 동작하지만 사격 애니메이션은 재생하지 않는다.
- Montage를 쓰면 in-place FullBody Slot을 사용하고 `Aim`, `Fire`, `Recover` 섹션을 만든다. 이름은 Pattern의 Prepare/Fire/Recovery Section에서 변경 가능하다. 코드가 Aim/Recover를 유지 루프로 설정하고 서버 타이머로 전환한다. 마지막 Fire는 자연스럽게 Recover로 이어진다. Fire 구간은 Recovery Time보다 짧게 만든다. 연사의 Fire 구간은 Shot Gap 안에 들어오도록 설정한다.
- **AnimNotify에 실제 발사/피해 이벤트를 넣지 않는다.** 발사 시각은 서버 타이머가 소유한다. 일반 피격 반응은 공격 중 차단하고 기존 스턴·사망은 공격을 취소한다.

## 4. BP_GunnerBoss와 Behavior Tree

- `BP_GunnerBoss.Combat Data` → `DA_GunnerCombat`.
- 기존 보스의 Mesh/Anim BP/Physics Asset, 체력·Strength용 **Default Stats Row** 등을 모델에 맞게 설정한다. CharacterMovement의 이동 Base는 기존 갑판 보스 구조를 사용한다.
- Starting Abilities에는 기본 제공되는 **GA_BossGunAttack / GA_BossGunBurst**를 유지한다. 새 GunnerBossEnemy BP 기본값에서는 기존 검 공격·대시·순간이동이 빠져 있고 자동 검 장착도 꺼져 있다. 총 외형은 BP에서 부착한다.
- 기존 공통 Root BT를 쓰면 새 **EnemyBehaviorSet**을 만들거나 기존 것을 복제해 **Combat**의 Subtree만 `BT_GunnerCombat`으로 바꾸고 Boss BP의 Behavior Set에 연결한다. 다른 기존 보스가 공유하는 BehaviorSet을 수정하지 않는다.
- 별도 전체 BT를 쓰는 경우에도 기존 Dead/Frozen/Combat 상태 라우팅과 동일 Blackboard를 유지하고 Combat 분기에 **Gunner Boss Combat** Task만 넣는다. Combat 상태 Decorator는 Self Abort로 상태 변경을 반영한다.
- 기존 컨트롤러가 요구하는 **DestinationLocation(Vector)** 키를 유지한다. 새 Task는 추가 Blackboard 키가 필요 없다. 이동/공격용 구형 Boss Task와 Focus Service는 새 Combat 분기에서 함께 실행하지 않는다.
- 새 Task 자체가 `거리 조절 → Burst 우선 → Basic → 짧은 재판단`을 처리한다. 별도 거리·사거리·쿨다운 Decorator를 복사해 넣을 필요가 없다. 첫 전투에서는 사용 가능한 Burst가 먼저 실행된다.
- Actor를 레벨에 직접 배치한 경우에도 **HostShip/초기 갑판 위치**가 필요하다. 정상 연결은 기존 BossEncounter에서 스폰하는 방식이다. 테스트 Blueprint에서는 서버에서 기존 `InitializeBoss`로 HostShip과 유효한 Spawn Point ID를 지정한다.

## 5. 이동과 Encounter

`BP_GunnerBoss`의 **GunMovement** 컴포넌트를 선택해 수정한다.

| 설정 | 기본값 |
| --- | --- |
| Retreat Enter Distance / Preferred Distance / Approach Enter Distance | 400 / 700 / 1,000cm |
| Return Band Tolerance | 100cm: 이동 종료 구간 600~800cm |
| Move Speed | 220cm/s |
| Replan Interval / Target Move Threshold | 0.5초 / 150cm |
| Maximum Move Time / Progress Timeout | 6초 / 1초 |

거리 순서는 `후퇴 진입 < 복귀 구간 하한 < 선호 거리 < 복귀 구간 상한 < 접근 진입 < Attack Range`. 안전한 후퇴 경로를 찾지 못해도 현재 사격이 가능하면 제자리에서 공격한다. 경로가 막히면 재시도 간격을 두며 갑판 밖으로 걸어 나가지 않는다.

함선의 **BossEncounterComponent**에서 기존 Boss Class를 `BP_GunnerBoss`로 지정하고, 유효한 Spawn Point와 활성화/상자 설정을 유지한다. 함선의 기존 DeckWalkArea 바닥·장애물 설정이 필요하다. 순수 총기 보스는 **Encounter Balance Row / Summoned Enemy Class**를 비워 소환 패턴을 연결하지 않는다. 머리 피격/체력 임계 스턴은 기존 Boss Status 설정을 사용하며 원치 않으면 해당 Stun Effect Class를 비운다.

## 6. 연결 후 수동 확인

2인 PIE에서 준비선 표시, 고정 후 옆 이동으로 회피, 준비/후딜 중 근접 공격, 엄폐물 차단, 배 이동·회전, 스턴/사망 후 지연 발사 제거를 확인한다. 코드 컴파일 성공은 자산 연결과 실제 플레이 검증을 대신하지 않는다.

검증 기록: 2026-10-12 `ArtisticSW2026Editor Win64 Development` 최종 빌드 성공. 자동화 테스트는 추가·실행하지 않았으며 BP/연출 자산 연결과 PIE 검증은 에디터에서 진행해야 한다.

## 파일별 책임

| 파일 (.h/.cpp) | 책임 |
| --- | --- |
| `GunnerBossEnemy` | 기존 encounter/사망 상속, 컴포넌트 구성, 기본 능력 목록, 수명·룸 복원 정리 |
| `GunnerBossCombatData` | 공격 설정과 자산 참조, 잘못된 시간/필수 표시 자산 검증 |
| `BossGunCombatComponent` | 소켓·사거리·사선·RayCast 질의, 갑판 기준 조준 고정, 이동/Root Motion 잠금, 표시 Actor 수명 |
| `BossGunAimLine` | 클라이언트 Mesh/Niagara/음향, 단계·발사 정보 복제, 배 좌표계 반영 |
| `GA_BossGunAttack` | Basic/Burst의 서버 단계 타이머, 쿨다운, 발별 GAS 피해 창, 취소 정리 |
| `BossGunMovementComponent` | 거리 구간·안전 목적지 선택, 기존 보행 경로 추종, 제한된 재계획 |
| `BTT_GunnerBossCombat` | 위치 조절과 공격 우선순위, 실행 중 공격 유지, 재판단 간격 |
| `GunnerBossTags` | 신규 사격/연사와 쿨다운 Native Gameplay Tags |
