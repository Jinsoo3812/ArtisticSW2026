# RangedEnemy 1단계 MVP 에디터 설정 및 테스트 가이드

## 1. 구현 범위

`ARangedEnemy`는 지상 또는 움직이는 함선 갑판에서 동일한 공격 루프를 사용하는 고정 원거리 적의 첫 버전이다.

- `ABaseEnemy`의 ASC, 체력, 사망, 드랍, 체력바를 재사용한다.
- `ARangedEnemyAIController`가 AI Sight로 `ABasePlayer`를 찾는다.
- 10Hz 서버 루프에서 표적, 거리, 시야를 다시 검증한다.
- 공격 몽타주가 없으면 즉시 발사한다.
- 몽타주가 있으면 `Event.Montage.FireArrow` 이벤트에서 발사한다.
- 발사체는 자신과 HostShip을 무시하고 `Team.Player` 대상에게만 피해를 준다.
- HostShip은 선택 사항이며 없더라도 감지·GA·발사 로직이 그대로 동작한다.
- Player Actor Collision과 `Team.Player` 누락은 표적 선정을 막지 않는다. 단, 실제 Projectile 피격/피해에는 충돌과 `Team.Player`가 필요하다.
- 갑판 위 이동과 사격 위치 선정은 2단계 범위다.

## 2. 생성할 Blueprint 자산

권장 경로:

```text
Content/GameplayAbilitySystem/Enemy/RangedEnemy/
```

### 2.0 `Projectile` Collision Preset 확인

`Config/DefaultEngine.ini`에 이 MVP가 사용하는 `Projectile` 프리셋이 등록되어 있다.
프리셋 추가 후 이미 에디터가 실행 중이었다면 에디터를 완전히 종료한 뒤 다시 실행한다.

`Project Settings > Engine > Collision > Presets`에서 `Projectile`이 다음 값인지 확인한다.

| 항목 | 값 |
|---|---|
| Collision Enabled | Query Only |
| Object Type | WorldDynamic |
| WorldStatic / WorldDynamic / Pawn | Block |
| Visibility / Camera | Ignore |
| ECC_Interactable / PlayerCannon / EnemyCannon / WeaponAim / ShipDamage | Ignore |

이 프리셋은 캐릭터와 월드 지형에 대한 `OnComponentHit`을 만들고, 조준·함포·함선 피해 채널과는 간섭하지 않도록 구성한다.

### 2.1 `BP_RangedEnemyProjectile`

1. `RangedEnemyProjectile`을 부모로 Blueprint Class를 만든다.
2. `MeshComp`에 화살 Mesh를 지정한다.
3. Mesh의 Forward Axis가 발사 방향인 +X를 향하는지 확인한다.
4. 필요하면 MeshComp의 Relative Rotation만 수정한다.
5. `BoxComp` 크기를 화살 Mesh에 맞춘다.
6. `Collision Presets`를 `Projectile`로 선택한다. 기존 Blueprint가 `Custom` 값을 직렬화했다면 `Reset to Default` 후 다시 선택한다.
7. `ProjectileComp` 설정:
   - Rotation Follows Velocity: true
   - Should Bounce: false
   - Projectile Gravity Scale: 0.0
8. 첫 MVP에서는 `Only Damage Players`를 true로 유지한다.

`ARangedEnemyProjectile`에는 10초 LifeSpan이 있으므로 빗나간 화살도 자동 제거된다.

### 2.2 `BP_RangedEnemy`

1. `RangedEnemy`를 부모로 Blueprint Class를 만든다.
2. 기존 `BP_EnemyBase`와 동일한 Skeletal Mesh, Anim Class, 체력/드랍 설정을 옮긴다.
3. AI Controller Class는 기본값 `RangedEnemyAIController`를 유지한다.
4. Auto Possess AI는 `Placed in World or Spawned`를 유지한다.
5. Behavior Tree는 지정하지 않는다. 1단계는 Controller의 경량 전투 루프를 사용한다.
6. `Ranged Enemy | Combat`을 설정한다.

권장 시작값:

| 속성 | 값 |
|---|---:|
| Min Attack Range | 150 |
| Max Attack Range | 2500 |
| Attack Cooldown | 2.0 |
| Base Damage | 10 |
| Projectile Speed | 2500 |
| Target Aim Height Offset | 60 |
| Projectile Class | `BP_RangedEnemyProjectile` |
| Damage Effect Class | `/Game/GameplayAbilitySystem/GameplayEffect/GE_Instant_Arrow` |

`GE_Instant_Arrow` 대신 다른 GE를 사용한다면 `Data.Damage` SetByCaller 값을 Health 감소 Modifier가 읽도록 구성해야 한다.

### 2.3 Muzzle 설정

둘 중 하나를 사용한다.

1. 권장: 캐릭터 Skeletal Mesh에 `ArrowSocket` Socket을 만든다.
2. 임시: Socket 없이 `Muzzle Offset`을 캐릭터 로컬 공간에서 조정한다.

Socket이 존재하면 `Muzzle Offset`보다 Socket 위치가 우선한다.

## 3. 공격 애니메이션 설정

애니메이션 없이 먼저 기능을 확인할 수 있다. `Attack Montage = None`이면 공격 Ability가 즉시 화살을 생성한다.

애니메이션을 연결할 때:

1. 활 발사 Animation Montage를 `Attack Montage`에 지정한다.
2. 활시위를 놓는 정확한 프레임에 `AN_SendGameplayEvent` Anim Notify를 추가한다.
3. Notify의 Event Tag를 `Event.Montage.FireArrow`로 설정한다.
4. Montage Slot이 Anim Blueprint의 Slot 노드와 일치하는지 확인한다.

Notify가 누락되어도 몽타주 종료 시 한 번 발사하는 안전장치가 있다. 하지만 타이밍이 늦어지므로 정상 자산에는 반드시 Notify를 넣는다.

## 4. 지상 또는 EnemyShip 위 배치

### 지상 배치

1. `BP_RangedEnemy`를 NavMesh나 바닥 위에 배치한다.
2. `Host Ship`을 비워 둔다.
3. 자동 탐색이 제한 횟수 안에 Ship을 찾지 못해도 독립 전투 모드로 계속 동작한다.

### EnemyShip 위 배치

1. 테스트 맵에 `BP_EnemyShip`과 `BP_RangedEnemy`를 각각 배치한다.
2. RangedEnemy의 Capsule 바닥이 `ShipDeckMesh` 위에 오도록 놓는다.
3. RangedEnemy를 Ship Root에 Attach하지 않는다.
4. `Host Ship`은 다음 두 방법 중 하나로 설정한다.
   - 가장 안정적: 레벨 인스턴스의 `Host Ship`에 해당 EnemyShip을 직접 지정
   - 자동: 비워두고 Movement Base/Attach Parent/아래 방향 500cm Trace로 자동 탐색
5. 게임 시작 후 CharacterMovement의 Movement Base가 `ShipDeckMesh`인지 확인한다.

1단계에서는 `Max Walk Speed = 0`으로 고정되어 있지만 CharacterMovement는 계속 동작하므로 움직이는 갑판의 Based Movement를 따라간다.

## 5. AI Perception 설정

기본 Controller 값:

| 속성 | 값 |
|---|---:|
| Sight Radius | 3000 |
| Lose Sight Radius | 3500 |
| Peripheral Vision | 80도 |
| Sight Stimulus Max Age | 2초 |
| Combat Update Interval | 0.1초 |

값을 바꾸려면 `RangedEnemyAIController`를 부모로 `BP_RangedEnemyAIController`를 만들고 `BP_RangedEnemy`의 AI Controller Class에 지정한다.

AI Sight가 Player를 감지하려면 Player Character가 AI Stimuli Source로 Sight에 등록되어 있거나 프로젝트의 기본 Pawn 자동 등록 설정이 켜져 있어야 한다.

## 6. 자동화 테스트

에디터의 Session Frontend에서 다음 필터를 실행한다.

```text
ArtisticSW.Enemy.RangedEnemy
```

포함 테스트:

- `Defaults`: Controller, Projectile, Ability 태그 기본 연결
- `ProjectileTeamFilter`: Enemy 아군 피해 차단 및 Player 팀 허용
- `AttackIntegration`: HostShip 없는 독립 공격, 즉시 발사 Projectile 생성, collision-disabled Player 표적 유지, 선택적 HostShip 연결

명령줄:

```powershell
& "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "<ProjectRoot>\ArtisticSW2026.uproject" `
  -Unattended -NullRHI -NoSplash -NoSound -NoP4 `
  -ExecCmds="Automation RunTests ArtisticSW.Enemy.RangedEnemy;Quit" `
  -TestExit="Automation Test Queue Empty"
```

## 7. PIE 수동 테스트 루프

### 루프 A: 기능 확인

1. `Attack Montage`를 비운 상태로 시작한다.
2. Player가 2500cm 바깥에 있으면 발사하지 않는지 확인한다.
3. 사거리 안으로 들어가면 Enemy가 Player를 바라보고 2초마다 발사하는지 확인한다.
4. 화살이 Enemy와 발사한 EnemyShip에 즉시 충돌하지 않는지 확인한다.
5. Player 명중 시 Base Damage만큼 Health가 감소하는지 확인한다.

### 루프 B: 차폐와 표적 상실

1. Player와 Enemy 사이에 Visibility를 Block하는 벽을 둔다.
2. 차폐 중에는 발사하지 않는지 확인한다.
3. 벽을 벗어나면 다시 발사하는지 확인한다.
4. Player Collision을 끄거나 실제 조타 상태에 진입해도 GA/발사가 계속되는지 확인한다. 이 상태의 실제 명중은 별도 피격 프록시가 없으면 실패할 수 있다.

### 루프 C: 움직이는 함선

1. EnemyShip을 정지, 직진, 회전, 급선회 상태로 각각 시험한다.
2. RangedEnemy가 갑판을 따라가며 Capsule이 밀어내는 물리력을 배에 전달하지 않는지 확인한다.
3. 화살 Spawn 위치가 매 발사 시 현재 `ArrowSocket` 위치와 일치하는지 확인한다.
4. EnemyShip이 Destroy되면 RangedEnemy도 제거되는지 확인한다.

### 루프 D: 멀티플레이

1. Dedicated Server 또는 Listen Server + Client 2명으로 실행한다.
2. 서버에서만 표적 선정, Ability 실행, Projectile 생성, 피해 적용이 일어나는지 확인한다.
3. 두 Client에서 Enemy 회전, Montage, Projectile 위치와 피격 결과가 동일한지 확인한다.
4. 한 Player가 사망하면 다른 유효 Player로 표적이 전환되는지 확인한다.

### GA가 실행되지 않을 때 확인 순서

AI Sight 성공은 공격 가능 판정과 별개다. 서버 디버거에서 아래 순서대로 처음 `false`가 되는 지점을 찾는다.

1. `ARangedEnemyAIController::OnRangedTargetPerceptionUpdated`
   - `HasAuthority()`가 true인지 확인한다.
   - `Stimulus.WasSuccessfullySensed()`와 `IsValidRangedTarget(SeenTarget)`가 모두 true인지 확인한다.
   - 처리 후 `Enemy->GetCombatTarget()`이 Player인지 확인한다.
2. `ARangedEnemy::IsValidCombatTarget`
   - 대상이 `ABasePlayer`인지, 사망하지 않았는지 확인한다.
   - Player ASC에 `Team.Enemy`가 잘못 들어 있지 않은지 확인한다.
   - `Team.Player`와 Actor Collision은 공격 시작을 막지 않지만 Projectile 피해/피격에는 영향을 준다.
3. `ARangedEnemy::CanAttackTarget`
   - 3D 거리가 `MinAttackRange` 이상, `MaxAttackRange` 이하인지 확인한다.
   - `Draw Attack Line Of Sight`를 켜고 초록 선인지 확인한다. 빨간 선이면 Visibility Trace의 `Hit Actor`를 조사한다.
   - 자기 함선을 `Host Ship`에 지정하면 LOS Trace와 Projectile 이동에서 해당 함선을 무시한다.
4. `ARangedEnemy::TryStartRangedAttack`
   - Enemy ASC가 존재하고 `State.Attacking`이 남아 있지 않은지 확인한다.
   - `TryActivateAbilitiesByTag` 직전 Ability Spec의 Asset Tags에 `GameplayAbility.RangedAttack`이 있는지 확인한다.
5. `UGA_RangedEnemyAttack::ActivateAbility`
   - 진입하지 않으면 BP GA의 Asset Tags, Activation Required/Blocked Tags, Cost GE, Cooldown GE를 확인한다.
   - 진입 직후 종료되면 `CanAttackCurrentTarget(true)` 또는 `CommitAbility`가 실패한 것이다.
6. 첫 격리 테스트에서는 `Attack Montage`를 비운다. 이 경우 GA가 성공하면 즉시 Projectile을 생성한다. 즉시 발사는 되지만 Montage를 지정하면 안 된다면 AnimBP Slot과 `Event.Montage.FireArrow` Notify 설정을 조사한다.

## 8. 1단계 완료 기준

- Player 캐릭터만 감지하고 Enemy/함선에는 개인 무기 피해를 주지 않는다.
- 거리와 Visibility LOS가 모두 유효할 때만 발사한다.
- 서버에서만 Projectile과 피해를 생성한다.
- 함선 이동 중에도 발사 원점이 현재 Socket 위치를 따른다.
- Player 사망 상태에서는 공격을 중단한다. 조타 중 collision-disabled Player도 공격 대상으로 유지한다.
- HostShip 제거 시 dangling reference나 계속되는 AI 타이머가 없다.
- 자동화 테스트 세 개와 Dedicated Server 수동 루프를 통과한다.
