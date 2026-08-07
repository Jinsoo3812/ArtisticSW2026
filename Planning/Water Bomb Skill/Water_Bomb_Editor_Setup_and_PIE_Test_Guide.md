# 물폭탄 GA 설정 및 PIE 테스트 가이드

## 1. 최종 구조

물폭탄 모드는 해류 발생기와 마찬가지로 플레이어 ASC에 부여되는 Gameplay Ability로 구현한다.

1. `ABasePlayer::PossessedBy`가 `WaterBombAbilityClass`를 기본 Ability로 부여한다.
2. 플레이어가 대포를 조종하는 동안 `4`를 누르면 대포가 서버에서 `GameplayAbility.Skill.WaterBomb` 태그의 GA를 활성화한다.
3. `UGA_WaterBombCannonMode`는 플레이어의 Attach Parent에서 현재 탑승한 `ACannon`을 찾고 물폭탄 모드를 켠 채 활성 상태를 유지한다.
4. 물폭탄 모드의 좌클릭 발사는 GA에 설정된 Projectile Class, Effect Duration, Attack Speed Multiplier를 사용한다.
5. 다시 `4`를 누르거나 대포에서 내리면 GA가 취소되고 일반 포탄 모드로 복구된다.

GA는 모드 진입·유지·설정의 소유자이고, 실제 포탄 Spawn은 기존처럼 서버 권한의 `ACannon`이 담당한다. 따라서 일반 포탄의 발사·업그레이드 경로도 그대로 유지된다.

## 2. 에디터에서 할 일

### 2.1 물폭탄 Projectile Blueprint

비주얼이 필요하면 다음 Blueprint를 만든다.

1. `AWaterBombCannonball`을 부모로 `BP_WaterBombCannonball` 생성
2. 상속된 Cannonball Mesh에 Mesh/Material/VFX 지정

네이티브 `AWaterBombCannonball`만 사용해도 기능은 동작하지만 전용 비주얼은 보이지 않을 수 있다.

### 2.2 Water Bomb GA Blueprint

1. `UGA_WaterBombCannonMode`를 부모로 `GA_WaterBombCannonMode_BP` 생성
2. Class Defaults에서 다음 값을 설정

| GA 속성 | 기본값 | 의미 |
|---|---:|---|
| `Projectile Class` | `AWaterBombCannonball` | 물폭탄 모드에서 발사할 투사체. 비주얼 BP를 만들었다면 그 BP를 지정 |
| `Effect Duration Seconds` | 5.0 | 적함 대포 봉쇄와 갑판 위 적 감속의 공통 지속시간 |
| `Attack Speed Multiplier` | 0.5 | 1.0은 정상, 0.7은 30% 감소, 0.5는 50% 감소 |

지속시간과 감속 배율의 최종 조절 위치는 Projectile이 아니라 이 GA이다. GA가 발사 직전에 해당 값을 Projectile 인스턴스에 전달한다.

### 2.3 Player Blueprint

실제 PIE에서 사용하는 Player Blueprint의 Class Defaults에서 `Abilities > Water Bomb`을 설정한다.

- `Grant Water Bomb Ability`: 체크
- `Water Bomb Ability Class`: `GA_WaterBombCannonMode_BP`

C++ 기본값은 네이티브 `UGA_WaterBombCannonMode`라서 이 설정을 생략해도 로직 테스트는 된다. 디자이너 값과 Projectile BP를 사용하려면 반드시 GA Blueprint를 지정한다.

`Default Granted Abilities`나 키 슬롯 Map에 같은 GA를 또 추가할 필요는 없다. 물폭탄은 Player의 입력 슬롯이 아니라 대포가 Ability Tag로 활성화한다.

### 2.4 IA/IMC

현재 C++에는 대포 Pawn에만 적용되는 Keyboard `4` fallback이 있어서 IA 없이도 테스트할 수 있다. 정식 입력 자산 구성은 다음과 같다.

1. `IA_CannonWaterBombToggle` 생성, Value Type은 Digital/Bool
2. `CannonIMC`에 Keyboard `4` 매핑
3. `BP_Cannon`의 `Cannon Water Bomb Toggle Action`에 IA 지정

IA가 지정되면 직접 키 fallback은 등록되지 않는다. Cannon IMC와 Cannon 입력 컴포넌트는 대포 조종 중에만 활성화되므로 일반 캐릭터와 배 조종 상태의 `4`는 물폭탄 GA를 실행하지 않는다.

`BP_Cannon`에는 더 이상 `Water Bomb Cannonball Class`를 지정하지 않는다. Projectile 선택과 수치는 GA가 소유한다.

### 2.5 적 배 위 테스트 적 배치

적은 별도 탑승 목록이 아니라 다음 기존 연결을 이용해 감지한다.

- `Character->IsBasedOnActor(HitShip)`
- `APawn::GetMovementBaseActor(Character) == HitShip`
- Character Attach Parent 체인에 HitShip 존재

테스트 Enemy의 Capsule이 적함 갑판에 닿아 Walking 상태가 되도록 배치한다. 공중에 떠 있거나 갑판 충돌이 Pawn을 받지 않으면 Movement Base가 생기지 않아 감속 대상이 되지 않는다.

## 3. 자동화 검증 결과

2026-07-21 기준 Development Editor 빌드가 성공했고 `ArtisticSW.WaterBomb` 자동화 테스트 5개가 모두 통과했다.

- `EffectConfiguration`
- `GameplayAbilityCannonIntegration`
- `GameplayAbilityConfiguration`
- `GameplayEffectApplication`
- `ProjectileHitIntegration`

검증 범위에는 GA의 Asset Tag·기본 Projectile·지속시간·배율·Player 기본 부여 설정, GE 적용/복구, 실제 적함 명중, 갑판 위 적 감속, 적함 대포 봉쇄가 포함된다.

명령행 실행:

```powershell
& 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'C:\Unreal Projects\ArtisticSW2026\ArtisticSW2026.uproject' `
  -unattended -nop4 -NullRHI `
  '-ExecCmds=Automation RunTests ArtisticSW.WaterBomb;Quit' `
  '-TestExit=Automation Test Queue Empty' -log
```

## 4. PIE 확인 순서

에디터를 완전히 종료한 뒤 Development Editor 빌드하고 다시 실행하는 것을 권장한다. 새 UCLASS/UPROPERTY가 포함되어 있어 Hot Reload만으로는 Blueprint 기본값이 오래된 상태일 수 있다.

1. 일반 캐릭터 상태에서 `4`: 물폭탄 관련 로그가 없어야 한다.
2. 배 조종 상태에서 `4`: 물폭탄 관련 로그가 없어야 한다.
3. 대포에 탑승하고 `4`: 아래 GA 활성/모드 로그가 출력되어야 한다.
4. 좌클릭: `GA_WaterBombCannonMode_BP`에 지정한 Projectile Class가 발사되어야 한다.
5. 적함 명중: 대포 봉쇄와 갑판 위 적 감속 로그를 확인한다.
6. 다시 `4`: GA가 취소되고 `Cannon mode: NORMAL`이 출력되어야 한다.
7. WATER BOMB 상태에서 대포 하차 후 재탑승: NORMAL 상태여야 한다.

정상 활성/발사 로그 예시:

```text
[WaterBomb] GA configured cannon=..., projectile=..., duration=5.00s, attack-speed multiplier=0.50
[WaterBomb] GA active: avatar=..., cannon=...
[WaterBomb] Cannon mode: WATER BOMB
[WaterBomb] Fired: cannon=..., owning-ship=..., projectile=..., class=...
```

적함 명중 로그 예시:

```text
[WaterBomb] Slowed onboard enemy=..., movement-base=..., multiplier=0.50
[WaterBomb] Hit enemy ship=..., duration=5.00s, attack-speed multiplier=0.50, cannons disabled=YES, onboard enemies slowed=1
```

`onboard enemies slowed=0`이면 GA나 Projectile 문제가 아니라 Enemy와 적함 사이의 Movement Base/Attach 관계부터 확인한다.

## 5. 공격 모션과 공격 주기 확인

확인이 쉽도록 GA의 `Effect Duration Seconds=10`, `Attack Speed Multiplier=0.5`로 테스트한다.

1. 명중 전 공격 Montage 길이와 공격 시작 간격을 측정한다.
2. 명중 후 새 공격 Montage가 약 2배 길어지는지 확인한다.
3. BT 공격 회복 구간도 약 2배 길어지는지 확인한다.
4. 공격 Montage 재생 중 명중해도 현재 Montage가 즉시 느려지는지 확인한다.
5. GE 만료 시 현재 Montage와 이후 공격 주기가 정상 속도로 복구되는지 확인한다.

관련 로그:

```text
[WaterBomb] AttackSpeedMultiplier changed: owner=... 1.00 -> 0.50
[WaterBomb] AttackSpeedMultiplier changed: owner=... 0.50 -> 1.00
```

`BT_EnemyBase`에서 `BTT_EnemyBasicAttack` 뒤에 기존 고정 `BTTask_Wait`가 있다면 제거하거나 Wait Time을 0으로 만든다. 공격 주기 회복은 이제 `BTT_EnemyBasicAttack` 자체가 `AttackCooldown * AttackSpeedMultiplier` 진행률로 처리한다.

## 6. 주요 코드 위치

- `Source/ClassFeature/Public/Attacker/GA_WaterBombCannonMode.h`
- `Source/ClassFeature/Private/Attacker/GA_WaterBombCannonMode.cpp`
- `Source/ClassFeature/Public/BasePlayer.h`
- `Source/ClassFeature/Private/BasePlayer.cpp`
- `Source/ClassFeature/Private/Tests/WaterBombAbilityTests.cpp`
- `Source/WaterAndShip/Public/Cannon.h`
- `Source/WaterAndShip/Private/Cannon.cpp`
- `Source/WaterAndShip/Public/WaterBombCannonball.h`
- `Source/WaterAndShip/Private/WaterBombCannonball.cpp`
- `Source/WaterAndShip/Public/WaterBombEffects.h`
- `Source/WaterAndShip/Private/WaterBombEffects.cpp`
- `Source/Enemy/Private/Tests/WaterBombProjectileIntegrationTests.cpp`
