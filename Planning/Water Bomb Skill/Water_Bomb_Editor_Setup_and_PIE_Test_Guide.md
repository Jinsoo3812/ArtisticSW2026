# 물폭탄 구현 결과와 에디터/PIE 테스트 가이드

## 1. 구현 결과

물폭탄은 다음 흐름으로 구현되어 있다.

1. 플레이어가 대포를 조종하는 동안 `4`를 누르면 일반 포탄/물폭탄 모드가 전환된다.
2. 물폭탄 모드에서 기존 발사 입력(좌클릭)을 누르면 서버가 `AWaterBombCannonball`을 발사한다.
3. 물폭탄이 적함에 맞으면 같은 지속시간 동안 다음 두 효과가 적용된다.
   - 적함 ASC: `State.Ship.CannonDisabled`를 부여하는 Duration GE
   - 해당 적함 위의 살아 있는 적 ASC: `AttackSpeedMultiplier`를 곱연산으로 낮추는 Duration GE
4. 적의 공격 몽타주는 `BaseMontagePlayRate * AttackSpeedMultiplier`로 재생된다.
5. 공격 후 회복은 매 Tick `DeltaSeconds * AttackSpeedMultiplier`만큼 진행된다.
6. 따라서 배율이 `0.5`이면 공격 모션과 다음 공격까지의 회복이 모두 절반 속도로 진행된다.
7. 효과가 공격 도중 시작되거나 끝나도 현재 몽타주 재생률과 남은 회복 속도가 즉시 바뀐다.

추가 공격속도 Attribute는 하나만 사용한다. 몽타주용 Attribute와 쿨다운용 Attribute를 별도로 만들 필요가 없다.

## 2. 적함 위 적 판정 방식

적 캐릭터와 적함 사이에 별도의 승선 목록은 없었다. 현재 판정은 엔진이 이미 관리하는 관계를 사용한다.

- `Character->IsBasedOnActor(HitShip)`
- `APawn::GetMovementBaseActor(Character) == HitShip`
- Character의 Attach Parent 계층이 HitShip으로 연결됨

배의 `ShipDeckMesh`가 Pawn을 Block하므로, 정상적으로 갑판 위에 서 있는 Character는 CharacterMovement의 Movement Base로 해당 배를 갖는다. 반경/Bounds만으로 판정하지 않으므로 바다에 빠진 적이나 바로 옆의 다른 배 위 적은 포함하지 않는다.

WaterAndShip 모듈은 Enemy 모듈을 직접 참조하지 않는다. `ABaseCharacter::IsEnemyCharacterForEffects()`의 기본값은 false이고 `ABaseEnemy`만 true를 반환한다.

## 3. 조절 가능한 값

`AWaterBombCannonball` 또는 그 Blueprint 자식의 Class Defaults에서 조절한다.

| 속성 | 기본값 | 의미 |
|---|---:|---|
| `Effect Duration Seconds` | 5.0 | 대포 봉쇄와 적 공격속도 감소 GE의 공통 지속시간 |
| `Attack Speed Multiplier` | 0.5 | 1.0 정상, 0.7은 30% 감소, 0.5는 50% 감소 |
| `Attack Speed Effect Class` | 네이티브 기본 GE | 필요할 때 Blueprint GE로 교체 가능 |
| `Cannon Disable Effect Class` | 네이티브 기본 GE | 필요할 때 Blueprint GE로 교체 가능 |

같은 대상을 다시 맞히면 기존 `State.Debuff.WaterBomb` 또는 `State.Ship.CannonDisabled` GE를 제거하고 새 Spec을 적용한다. 중첩해서 더 느려지지 않고 지속시간만 처음부터 다시 시작한다.

## 4. 에디터에서 해야 할 일

### 4.1 필수: C++ 빌드

2026-07-21 재검증에서 UE 5.7 Development Editor 전체 빌드가 성공했다. 새 UCLASS/UPROPERTY가 모두 UHT를 통과하고 WaterAndShip/Enemy DLL 링크까지 완료됐다.

현재 Visual Studio 2026 MSVC 14.51은 UE가 표시하는 preferred toolset 14.44보다 새 버전이라는 경고가 있지만 빌드 실패 원인은 아니었다. 팀의 재현 가능한 빌드 환경을 엄격하게 맞춰야 한다면 14.44 toolset 설치를 별도로 고려한다.

다른 개발 머신에서는 Epic Launcher의 UE 5.7 옵션에 Windows Target Platform이 켜져 있는지 확인하고, Visual Studio Installer에서 다음 항목을 설치한다.

- Desktop development with C++
- Game development with C++
- MSVC v143 이상
- Windows 10 또는 Windows 11 SDK
- Visual Studio Tools for Unreal Engine(권장)

그 뒤 에디터를 완전히 재시작한다. 이 변경에는 새 UCLASS/UPROPERTY가 포함되어 있어 Hot Reload보다 정상 종료 후 Development Editor 빌드를 권장한다.

### 4.2 필수: Behavior Tree의 기존 고정 Wait 제거

`/Game/GameplayAbilitySystem/Enemy/AI/BT_EnemyBase`를 열어 `BTT_EnemyBasicAttack` 뒤의 기존 `BTTask_Wait`를 제거하거나 Wait Time을 0으로 만든다.

이제 `BTT_EnemyBasicAttack` 자체가 현재 무기의 `AttackCooldown`과 `AttackSpeedMultiplier`를 읽어 회복을 기다린다. 기존 Wait가 남으면 새 동적 회복 뒤에 고정 대기시간이 한 번 더 붙는다.

각 적 무기의 `FWeaponCombatData.AttackCooldown`도 의도한 기본 회복시간인지 확인한다.

### 4.3 권장: 정식 IA/IMC 연결

기능 확인만 할 때는 C++ fallback이 숫자 `4`를 직접 바인딩하므로 IA/IMC가 없어도 된다. 이 fallback은 Cannon Pawn이 실제로 빙의된 동안에만 존재한다.

정식 입력 구성은 다음과 같다.

1. `/Game/New/Cannon/IA_CannonWaterBombToggle` Input Action 생성
2. Value Type을 Digital/Bool로 설정
3. 기존 `/Game/New/Cannon/CannonIMC`에서 Keyboard `4`에 매핑
4. `/Game/New/Cannon/BP_Cannon`의 `Cannon Water Bomb Toggle Action`에 새 IA 지정

IA가 지정되면 C++ 직접 키 fallback은 등록되지 않으므로 이중 토글은 발생하지 않는다.

일반 Character와 Ship에는 이 Action을 추가하지 않는다. CannonIMC도 대포 빙의 중에만 활성화되므로 일반 이동/배 조종 중 `4`는 물폭탄 코드에 반응하지 않는다.

### 4.4 권장: 물폭탄 Blueprint와 비주얼

네이티브 `AWaterBombCannonball`이 `BP_Cannon`의 기본 물폭탄 클래스로 이미 설정되어 있어 로직 테스트에는 Blueprint가 필수가 아니다. 다만 네이티브 기본 클래스에는 전용 Mesh/VFX가 없으므로 다음 구성을 권장한다.

1. `AWaterBombCannonball`을 부모로 `BP_WaterBombCannonball` 생성
2. 상속된 Cannonball Mesh에 물폭탄 Mesh/Material 지정
3. Class Defaults에서 Duration과 Attack Speed Multiplier 조정
4. `/Game/New/Cannon/BP_Cannon`의 `Water Bomb Cannonball Class`에 이 BP 지정

기본 네이티브 GE 두 개가 이미 동작하므로 GE Blueprint를 별도로 만들 필요는 없다. 디자이너 GE로 바꿀 경우 물폭탄 코드가 Duration을 덮어쓰고 `Data.Effect.AttackSpeedMultiplier` SetByCaller 값을 전달한다.

### 4.5 필수: 승선 적 테스트 배치

현재 `Test_Level`에는 적함은 있지만 일반 적 캐릭터가 없는 것으로 확인됐다.

1. `BP_MyEnemy`를 적함의 `ShipDeckMesh` 위에 배치한다.
2. Capsule 바닥이 갑판에 닿게 하고 공중에 띄우지 않는다.
3. PIE 시작 뒤 적이 갑판 위에서 Walking 상태인지 확인한다.
4. 적이 공격할 Player/Target을 인식할 수 있는 거리와 AI 조건을 맞춘다.

정상 감지 시 다음 로그의 `movement-base`가 적함 이름이고 `onboard enemies slowed`가 1 이상이어야 한다.

## 5. 자동화 테스트

추가된 테스트:

- `ArtisticSW.WaterBomb.EffectConfiguration`
- `ArtisticSW.WaterBomb.GameplayEffectApplication`
- `ArtisticSW.WaterBomb.ProjectileHitIntegration`

검증 내용:

- 느려짐 GE가 Duration + Multiplicative modifier인지
- 대상 Attribute가 `AttackSpeedMultiplier`인지
- 기본 지속시간/배율이 5초/0.5인지
- 실제 ASC에 GE 적용 시 Attribute가 1.0에서 0.5로 바뀌는지
- Debuff 태그가 부여되는지
- GE 제거 시 Attribute와 태그가 원복되는지
- 대포 봉쇄 GE가 `State.Ship.CannonDisabled`를 부여하고 제거하는지
- 실제 Source Ship, Enemy Ship, BaseEnemy, WaterBomb Projectile을 생성한 명중 경로
- 적함 위/부착 적 탐지와 감속, 적함 대포 봉쇄, 명중 후 발사체 파괴

빌드가 성공한 머신에서 Session Frontend > Automation에서 `ArtisticSW.WaterBomb`을 검색해 실행한다. 명령행은 다음과 같다.

```powershell
& 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'C:\Unreal Projects\ArtisticSW2026\ArtisticSW2026.uproject' `
  -unattended -nop4 -NullRHI `
  '-ExecCmds=Automation RunTests ArtisticSW.WaterBomb;Quit' `
  '-TestExit=Automation Test Queue Empty' -log
```

2026-07-21 실행 결과 세 테스트 모두 성공했고 commandlet 종료 코드는 0이었다. 자동화 테스트는 GE 구성/적용/원복과 발사체의 유효 적함 명중 처리까지 검증한다. 실제 Input Asset, 물리 충돌 이벤트, 갑판의 실제 Movement Base, 애니메이션 에셋 재생은 아래 PIE 테스트에서 검증한다.

## 6. PIE 테스트 순서

### 6.1 입력과 모드

1. 일반 Character 상태에서 `4`: `[WaterBomb] Cannon mode` 로그가 없어야 한다.
2. Ship 조종 상태에서 `4`: 같은 로그가 없어야 한다.
3. Cannon 조종 상태에서 `4`: `Cannon mode: WATER BOMB` 로그가 나와야 한다.
4. 다시 `4`: `Cannon mode: NORMAL` 로그가 나와야 한다.
5. WATER BOMB 상태로 대포에서 내린 뒤 다시 탑승: NORMAL이어야 한다.

### 6.2 발사와 적함 명중

1. WATER BOMB 모드에서 좌클릭한다.
2. 적함 명중 시 다음 형식의 로그를 확인한다.

```text
[WaterBomb] Hit enemy ship=..., duration=5.00s, attack-speed multiplier=0.50, cannons disabled=YES, onboard enemies slowed=1
[WaterBomb] Slowed onboard enemy=..., movement-base=..., multiplier=0.50
```

3. 플레이어 함선 또는 발사한 함선에 맞을 때는 효과 로그가 없어야 한다.
4. NORMAL 모드 좌클릭은 기존 일반 Cannonball과 기존 피해 경로를 그대로 사용해야 한다.

### 6.3 적함 대포 봉쇄

1. 물폭탄 명중 전 적함 대포가 정상 발사하는지 확인한다.
2. 명중 직후 Duration 동안 적함의 모든 `ACannon::FireCannon()` 호출이 거부되는지 확인한다.
3. `Cannon fire blocked` 또는 `Server rejected cannon fire` 로그를 확인한다.
4. Duration 경과 후 별도 조작 없이 다시 발사되는지 확인한다.
5. 효과 중 다시 명중시킨 뒤 두 번째 명중 시점부터 Duration이 다시 계산되는지 확인한다.

### 6.4 공격 모션과 공격 주기

테스트 값을 `AttackSpeedMultiplier=0.5`, `Duration=10`으로 두면 육안과 시간 측정이 쉽다.

1. 효과 전 기본 공격 몽타주 시간과 공격 시작 간격을 3회 측정한다.
2. 물폭탄 명중 후 몽타주가 대략 2배 길어지는지 확인한다.
3. 무기의 `AttackCooldown` 회복도 대략 2배 길어지는지 확인한다.
4. 결과적으로 공격 시작 간격 전체가 대략 2배가 되는지 확인한다.
5. 적이 공격 몽타주를 재생 중일 때 물폭탄을 맞혀 현재 모션이 즉시 느려지는지 확인한다.
6. GE가 몽타주 도중 끝날 때 현재 모션이 즉시 정상 속도로 복구되는지 확인한다.
7. 다음 로그에서 적용/원복을 확인한다.

```text
[WaterBomb] AttackSpeedMultiplier changed: owner=... 1.00 -> 0.50
[WaterBomb] AttackSpeedMultiplier changed: owner=... 0.50 -> 1.00
```

### 6.5 오탐 방지

1. 적함 바로 옆 바다에 적을 둔다: 감속되지 않아야 한다.
2. 다른 적함 갑판에 적을 둔다: 맞은 배 위 적만 감속되어야 한다.
3. 맞은 배 위에서 점프해 Movement Base와 Attach 관계가 모두 끊긴 정확한 순간의 적은 감속되지 않는 것이 현재 의도다.
4. 죽은 적과 Health 0 적은 감속 대상에서 제외되어야 한다.

### 6.6 멀티플레이

PIE Number of Players를 2, Net Mode를 Play As Listen Server로 두고 다음을 확인한다.

- 클라이언트가 `4`를 눌러도 서버의 모드와 일치함
- 서버가 선택한 물폭탄 클래스가 모든 클라이언트에 복제됨
- 적의 몽타주 재생률 변경이 두 화면에서 같음
- 서버의 GE 만료 뒤 양쪽 Attribute/태그가 원복됨
- 클라이언트가 봉쇄 태그 복제보다 먼저 발사를 시도해도 서버가 최종 거부함

## 7. 완료 판정

다음을 모두 만족하면 기능 테스트 완료다.

- C++ Development Editor 빌드 성공
- `ArtisticSW.WaterBomb` 자동화 테스트 3개 통과
- Cannon 조종 중에만 `4` 모드 전환
- 물폭탄/일반 포탄 클래스 전환 정상
- 적함 명중 시 모든 대포가 설정 시간 동안 발사 불가
- 맞은 적함 위의 살아 있는 적만 감지
- 공격 모션과 공격 회복 주기가 같은 Attribute 배율로 느려짐
- 효과 만료와 재명중 Refresh 정상
- Listen Server 2인 PIE에서 서버/클라이언트 결과 일치

## 8. 이번 구현에서 수정/추가된 주요 파일

공통 GAS:

- `Source/GASCore/Public/BaseAttributeSet.h`
- `Source/GASCore/Private/BaseAttributeSet.cpp`
- `Source/GASCore/Public/BaseCharacter.h`
- `Source/ArtisticSWCore/Public/BaseGameplayTags.h`
- `Source/ArtisticSWCore/Private/BaseGameplayTags.cpp`

Enemy의 최소 변경:

- `Source/Enemy/Public/BaseEnemy.h`: 적 판별 override
- `Source/Enemy/Private/GAS/Ability/GA_BasicAttack.cpp`: 몽타주 배율 연결
- `Source/Enemy/Public/Task/BTT_EnemyBasicAttack.h`
- `Source/Enemy/Private/Task/BTT_EnemyBasicAttack.cpp`: 동적 공격 회복

대포/물폭탄:

- `Source/WaterAndShip/Public/Cannon.h`
- `Source/WaterAndShip/Private/Cannon.cpp`
- `Source/WaterAndShip/Public/Cannonball.h`
- `Source/WaterAndShip/Public/WaterBombEffects.h`
- `Source/WaterAndShip/Private/WaterBombEffects.cpp`
- `Source/WaterAndShip/Public/WaterBombCannonball.h`
- `Source/WaterAndShip/Private/WaterBombCannonball.cpp`
- `Source/WaterAndShip/Private/Tests/WaterBombEffectTests.cpp`
- `Source/Enemy/Private/Tests/WaterBombProjectileIntegrationTests.cpp`

