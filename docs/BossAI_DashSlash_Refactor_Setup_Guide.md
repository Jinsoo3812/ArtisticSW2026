# Boss AI / DashSlash 리팩터링 에디터 설정 가이드

## 1. 최종 책임 구조

| 계층 | 책임 |
| --- | --- |
| Blackboard / Behavior Tree | 타깃, 공격 종류, 목적지 Point, 실행 순서 결정 |
| `BTT_SelectBossDestinationPoint` | Dash/Vanish 목적지 선택 및 BB/Boss에 기록 |
| `BTT_ActivateBossAbility` | 태그에 맞는 GA Spec 실행, 종료 대기, Abort 시 취소 |
| Gameplay Ability | Commit/Cooldown, Montage, 원자적 이동, 피해 판정, 취소 정리 |
| GameplayEffect | 서버 권위 피해 적용 |
| GameplayCue | 실제 Health 감소 확정 후 피해자 위치의 VFX/SFX/Camera Shake |

`BTT_ActivateBossAbility`와 공격 시작 시점의 GA는 피해 GameplayCue를 실행하지 않는다. 공격이 만든 Damage Spec에 Impact Cue Tag를 기록하고, 피해자의 Health가 실제로 감소한 뒤 피해자 ASC가 Cue를 실행한다.

## 2. BasicAttack BT 분기 추가

현재 `BT_Subtree_RogueBoss_Combat` 애셋에는 DashSlash, Knockback, Vanish 태그만 있고 BasicAttack 노드가 없다. 다음 분기를 추가해야 BasicAttack Montage가 실행된다.

1. `/Game/GameplayAbilitySystem/Enemy/AI/SubTree/RogueBoss/BT_Subtree_RogueBoss_Combat`를 연다.
2. Combat Selector 아래에 새 Sequence를 만든다.
3. 필요하면 거리 Decorator를 추가한다. 거리는 Sword Definition의 `AttackRange`와 같거나 조금 작게 둔다.
4. `BTD_CanActivateAbilityByTag`를 추가한다.
   - `AbilityAssetTag`: `GameplayAbility.BasicAttack`
5. `BTT_ActivateBossAbility`를 추가한다.
   - `AbilityAssetTag`: `GameplayAbility.BasicAttack`
   - `Prefer Current Weapon Ability`: 체크
   - `Require Preselected Destination`: 체크 해제
   - `Clear Destination When Finished`: 체크
6. 컴파일하고 저장한다.

Task는 같은 태그의 GA가 여러 개면 `SourceObject == CurrentWeapon`인 Spec을 우선한다. 따라서 Sword가 부여한 `BPGA_MeleeAttack`이 선택된다.

## 3. BasicAttack Montage 표시 설정

1. `/Game/GameplayAbilitySystem/Enemy/Weapon/DA_Weapon`을 연다.
2. `Item.EnemyWeapon.Sword` 항목을 확인한다.
3. 다음 값을 확인한다.
   - `GrantedAbilities`: `BPGA_MeleeAttack`
   - `CombatData.AttackMontage`: 사용할 Sword 공격 Montage
   - `CombatData.AttackMontagePlayRate`: `1.0`부터 시작
   - `CombatData.DamageEffectClass`: 유효한 Instant Damage GE
4. 공격 Montage를 열고 Slot이 `DefaultSlot`인지 확인한다.
5. Montage Skeleton이 `BP_Ship_BossEnemy` Mesh와 호환되는지 확인한다.
6. `ABP_Warrior`의 AnimGraph를 연다.
7. Locomotion 결과와 `Output Pose` 사이에 `Slot 'DefaultSlot'` 노드를 둔다.
   - 별도 FullBody Slot을 사용할 경우 Montage와 AnimBP Slot 이름을 동일하게 맞춘다.
8. Montage의 `ANS_HitScanWindow`가 실제 칼날이 통과하는 구간에 있는지 확인한다.
9. AnimBP와 Montage를 컴파일하고 저장한다.

실행 실패 시 Output Log에서 `LogEnemyBasicAttack`을 검색한다. 현재 무기, SourceObject, Montage, PlayRate 또는 누락된 AnimInstance가 출력된다.

## 4. Dash Montage 작성

Dash Montage는 Root Motion이 아닌 in-place FullBody Montage로 만든다. 코드가 서버에서 함선 Deck 로컬 좌표를 기준으로 이동을 담당한다.

1. 사용할 Animation Sequence에서 `EnableRootMotion`을 끄거나 Root Motion이 제거된 Sequence를 준비한다.
2. Montage를 열고 Slot을 BasicAttack과 동일한 FullBody Slot으로 맞춘다.
3. Montage Section을 정확히 다음 이름으로 만든다.
   - `Windup`: Montage 첫 프레임
   - `DashSlash`: 준비가 끝난 뒤 시작하는 전체 검 휘두르기(1회 재생)
   - `DashHold`: 검을 모두 휘두른 마지막 자세를 유지하는 짧은 루프
   - `Recover`: 도착 후 마무리 자세의 첫 프레임
4. `DashSlash`에는 검을 드는 과정부터 휘두르기를 끝내는 과정까지 한 번만 넣고 루프시키지 않는다.
5. `DashHold`는 마지막 자세 주변의 매우 짧은 구간으로 만들고, 반복 경계에서 튀지 않는지 확인한다.
6. `DashSlash` 시작 프레임에 `AN_SendGameplayEvent` Notify를 추가하고 `EventTag`를 `Event.Boss.Dash.Start`로 설정한다.
7. `DashSlash` 마지막 프레임(완전히 휘두른 경계)에 두 번째 Gameplay Event Notify를 추가하고 `EventTag`를 `Event.Boss.Dash.SlashFinished`로 설정한다.
8. Blend In/Out은 먼저 `0.05~0.15초` 범위로 설정한다.
9. 저장한다.

GA는 런타임에 다음 Section 연결을 강제한다.

```text
Windup -> DashSlash (1회)
DashSlash -> DashHold
DashHold -> DashHold
Slash 완료 후 아직 이동 중이면 DashHold 유지
Slash 도중 먼저 도착하면 이동/판정만 종료하고 Slash 끝까지 재생
Slash 완료와 목적지 도착이 모두 충족되면 Recover
Recover 완료 -> GA 종료 -> BTTask 성공
```

Notify가 누락되어도 `WindupDuration` 후 Dash가 시작되지만, 애니메이션과 정확하게 맞추려면 Notify를 사용한다.

## 5. BPGA_SlashDash Class Defaults

`/Game/GameplayAbilitySystem/Ability/Enemy/BPGA_SlashDash`를 열고 Class Defaults에서 설정한다.

| 속성 | 권장 시작값 |
| --- | --- |
| `DashMontage` | 위에서 만든 in-place Montage |
| `WindupSectionName` | `Windup` |
| `DashSlashSectionName` | `DashSlash` |
| `DashHoldSectionName` | `DashHold` |
| `RecoverySectionName` | `Recover` |
| `WindupDuration` | Notify 실패 대비 시간, 예: `0.5` |
| `DashDuration` | 목적지까지 실제 이동 시간, 예: `0.45` |
| `RecoveryTimeout` | Recover 실패 안전장치, 예: `1.5` |
| `DashHitRadius` | 공격 판정 반경, 예: `120` |
| `Damage` | 원하는 Dash 피해량 |
| `DamageEffectClass` | Instant Damage GameplayEffect |
| `StartupGameplayCueTag` | 비워 둠(피해 확정 전 Cue 금지) |
| `ImpactGameplayCueTag` | `GameplayCue.Impact.Boss.DashSlash` |

`DashDuration`과 Montage 길이는 독립적이다. 목적지 도착이 이동 종료의 권위 있는 기준이다.

## 6. DashSlash BT 분기

`GA_BossDashSlash`의 `Dash Acceptance Radius` 기본값은 75cm다. Dash는
목적지의 갑판 로컬 평면 허용 범위에 들어오면 종료되며, 마지막 프레임에
Deck Point의 정확한 좌표로 강제 스냅하지 않는다.

필요한 연출에 따라 DashSlash Sequence의 `BTT_SelectBossDestinationPoint`
앞에 `BTT_BossStrafe`를 배치할 수 있다. 이 Task는 쿨다운 fallback이
아니며, Task 시작 시 Target 반지름의 좌·우 접선 중 하나만 선택한다.
공간 검사나 목적지 도착 판정 없이 기본 0.75초 동안 이동 입력을 주며,
벽에 막혀 실제 이동량이 적어도 성공한 뒤 DashSlash Sequence를 계속한다.

순서는 반드시 다음과 같아야 한다.

1. `BTD_CanActivateAbilityByTag`
   - `GameplayAbility.Boss.DashSlash`
2. `BTT_SelectBossDestinationPoint`
   - Purpose: `Dash`
   - Blackboard Key: `DestinationPointId`
3. `BTT_ActivateBossAbility`
   - Ability Asset Tag: `GameplayAbility.Boss.DashSlash`
   - Require Preselected Destination: 체크
   - Destination Point Key: `DestinationPointId`

`DestinationPointId`의 초기/무효 값은 `-1`이다. Int Key에 `ClearValue`를 사용하면 `0`이 되어 유효한 Point로 오인될 수 있으므로 사용하지 않는다.

## 7. Dash 충돌과 피해

`BP_Ship_BossEnemy`에 컴포넌트를 수동 추가할 필요는 없다. 네이티브 `DashDamageVolume`이 자동 생성된다.

- 평상시: `NoCollision`
- Dash 중 Boss Capsule: `Pawn`을 임시로 `Ignore`
- Dash 중 DamageVolume: `QueryOnly`, Pawn `Overlap`
- 서버: 매 이동 구간마다 Sphere Sweep을 추가 수행하여 고속 이동 터널링 방지
- 피해: `CanEngageActor`를 통과한 Actor마다 한 Dash에 한 번
- 도착/Abort/피격/사망: DamageVolume 비활성화 및 원래 Pawn 응답 복구

프로젝트의 Player와 Boss가 모두 Pawn 채널을 사용하므로 Dash 중에는 모든 Pawn을 관통한다. Player만 관통하고 다른 NPC와는 충돌해야 할 때만 별도 `PlayerPawn` Object Channel 도입을 고려한다.

## 8. GameplayCue 설정

### 선택적 공격 연출 Cue

`/Game/GameplayCues/Boss/GCN_Boss_Attack`은 Telegraph 전용으로 남아 있지만 현재 피해 Ability에서는 자동 실행하지 않는다. 피격 Camera Shake/VFX로 사용하지 않는다.

- GameplayCue Tag: `GameplayCue.Boss.Attack`
- Camera Shake Class: 약한 Shake 애셋
- Camera Shake Scale: `0.25`
- Recipient: `All Local Players In Radius`
- Inner Radius: 예 `300`
- Outer Radius: 예 `2200`
- Sound: Boss 공격 시작 SFX
- Niagara: 일반 공격 시작 VFX가 필요할 때만 지정

### Boss가 플레이어에게 피격된 경우의 공통 Cue

`/Game/GameplayCues/Boss/GCN_Boss_Hit`

- GameplayCue Tag: `GameplayCue.Boss.Hit`
- Camera Shake Class: 강한 Shake 애셋
- Camera Shake Scale: `1.0`
- Recipient: `Instigator Local Player`
- Niagara System: 피격 VFX
- Sound: 피격 SFX

Boss HealthComponent는 서버에서 실제 Health 감소가 확인된 뒤 Cue를 실행한다. Damage EffectContext에 HitResult가 있으면 ImpactPoint/ImpactNormal에서 VFX가 재생된다. DashSlash도 Overlap/Sweep HitResult를 Damage Context에 포함한다.

### Boss 공격에 플레이어가 피격된 경우의 Impact Cue

`DA_Weapon`의 `CombatData.ImpactGameplayCueTag`가 Damage Spec에 기록된다.

| 무기 | 확정 피격 Cue |
| --- | --- |
| Hand | `GameplayCue.Impact.Weapon.Hand` |
| Sword | `GameplayCue.Impact.Weapon.Sword` |
| Bow | `GameplayCue.Impact.Weapon.Bow` |

각 Cue는 `Target Local Player` Camera Shake와 고유 Niagara/SFX를 갖는다. Cue는 피해자 ASC에서 실행되므로 `MyTarget`, AttachComponent, Impact 위치가 모두 피해자 기준이다. 무적/방어 등으로 Health가 감소하지 않으면 Cue도 실행되지 않는다.

`Instigator Local Player`는 Boss를 때린 플레이어에게 강한 타격감을 주는 설정이다. “Boss에게 맞은 피해자 플레이어”의 카메라를 흔들려면 플레이어 전용 Hit Cue를 만들고 `Target Local Player`를 사용한다.

## 9. 멀티플레이 PIE 검증

1. PIE 설정을 `Number of Players = 2`로 설정한다.
2. 가능하면 `Net Mode = Play As Client` 또는 Dedicated Server를 사용한다.
3. 다음을 확인한다.
   - Server와 두 Client 모두 BasicAttack Montage가 보인다.
   - Dash에서 Boss와 Player Capsule이 밀어내지 않는다.
   - 두 Player가 경로에 있으면 각각 한 번씩만 피해를 받는다.
   - 동일 Player가 DamageVolume에 오래 머물러도 중복 피해가 없다.
   - 목적지 도착 시 즉시 Recover로 넘어간다.
   - BT Abort, Boss 피격, 사망 시 충돌이 원래대로 돌아온다.
   - Montage 시작만으로 피격 Camera Shake/VFX가 실행되지 않는다.
   - 실제 Health 감소와 같은 프레임에 피해자의 로컬 카메라와 ImpactPoint에서 Cue가 실행된다.
   - Boss 피격 VFX/SFX가 각 Client에서 한 번씩 보이고 들린다.

## 10. 자동 검증

Editor 빌드:

```powershell
& "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" `
  ArtisticSW2026Editor Win64 Development `
  "C:\Users\wonkii\Documents\GitHub\ArtisticSW2026\ArtisticSW2026.uproject" `
  -WaitMutex -NoHotReloadFromIDE
```

Boss 테스트:

```powershell
& "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "C:\Users\wonkii\Documents\GitHub\ArtisticSW2026\ArtisticSW2026.uproject" `
  -Unattended -NoSplash -NullRHI `
  -ExecCmds="Automation RunTests ArtisticSW.Enemy.BossMVP;Quit" `
  -TestExit="Automation Test Queue Empty" -log
```

`Defaults` 테스트의 `Combat subtree activates BasicAttack` 실패는 2장의 BasicAttack BT 분기가 아직 애셋에 추가되지 않았다는 뜻이다.
