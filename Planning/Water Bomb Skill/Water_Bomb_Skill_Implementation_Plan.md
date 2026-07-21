# 물폭탄 대포 스킬 구현 계획

> 구현 기록(2026-07-21): 계획의 Attribute 기반 정식 경로를 구현했다. 실제 구현에서는 모듈 순환 의존성을 피하기 위해 `AttackSpeedMultiplier`를 `UEnemyAttributeSet`가 상속하는 `UBaseAttributeSet`에 배치했다. 지속시간과 배율은 `AWaterBombCannonball` Class Defaults가 GE Spec에 주입한다. 세부 에디터 설정과 테스트 절차는 같은 폴더의 `Water_Bomb_Editor_Setup_and_PIE_Test_Guide.md`를 따른다.

## 1. 문서 목적

대포 조종 중 `4`를 눌러 물폭탄 모드를 켜고, 좌클릭으로 물폭탄을 발사하는 기능의 구현 방향을 정의한다.

물폭탄이 적 함선에 명중하면 다음 효과를 서버 권한으로 적용한다.

- 적 함선의 모든 대포를 `N`초 동안 발사 불가 상태로 만든다.
- 명중 순간 해당 적 함선 위에 있는 적 캐릭터들의 공격속도를 `N%` 감소시킨다.
- `4`를 다시 누르면 일반 포탄 모드로 돌아간다.
- 대포를 조종하지 않는 일반 캐릭터 상태와 함선 조종 상태에서는 물폭탄용 `4` 입력이 아무 동작도 하지 않는다.
- 별도 UI가 생기기 전까지 모드 전환과 피격 결과는 로그로 확인한다.

이 문서는 구현 계획이며, C++/Blueprint/Input Asset/GameplayEffect 자체를 아직 변경하지 않는다.

---

## 2. 조사 결론

### 2.1 현재 함선은 갑판 위 적 캐릭터 목록을 가지고 있지 않다

현재 프로젝트에는 `AEnemyShip -> 탑승 중인 ABaseEnemy 목록`과 같은 명시적 연결이 없다.

- `AEnemyShip`이 보관하는 함선 부속 Actor 목록은 `AttachedCannons`, `ActiveAICannons`뿐이다.
- `AShip::RidingPlayer`는 함선을 직접 조종하는 플레이어 Pawn 한 명을 위한 값이다. 일반 갑판 승객이나 적 선원을 나타내지 않는다.
- `ABaseEnemy`에는 자신이 어느 함선에 타고 있는지 나타내는 포인터, Gameplay Tag, 컴포넌트가 없다.
- `BP_EnemyShip`, `BP_EnemyShip1`, `BP_EnemyShip2`의 자산 참조에서도 `BP_MyEnemy` 또는 다른 적 캐릭터 클래스 참조는 확인되지 않았다.
- 적 함선 시험 맵인 `Test_Level`은 `BP_EnemyShip`을 참조하지만 일반 적 캐릭터 자산은 참조하지 않는다. 반대로 일반 적 캐릭터가 있는 일부 다른 레벨은 적 함선 시험 맵과 분리되어 있다.

따라서 현재 C++/자산 구조만 보면 “적 함선이 적 선원을 생성하고 소유한다”는 게임플레이 시스템은 없다. 적 캐릭터가 에디터에서 함선 위에 단순 배치되어 있다면 함선과의 관계는 명시적인 탑승 관계가 아니라 물리/Character Movement 관계일 가능성이 높다.

### 2.2 그래도 엔진 수준의 연결고리는 생길 수 있다

`ACharacter`가 갑판 위에서 `MOVE_Walking` 상태로 서 있으면 `UCharacterMovementComponent::GetMovementBase()`가 발밑의 갑판 컴포넌트를 가리킨다. 갑판 컴포넌트의 Owner가 해당 `AShip`이면 “현재 이 캐릭터가 이 배 위에 서 있다”는 것을 판정할 수 있다.

또한 Actor가 함선 또는 함선의 Child Actor에 명시적으로 부착되어 있다면 `GetAttachParentActor()` 계층으로도 판정할 수 있다.

정리하면 현재 상태는 다음과 같다.

| 관계 | 현재 존재 여부 | 용도 |
|---|---:|---|
| 함선의 `RidingPlayer` | 있음 | 함선 조종자 한 명 |
| 대포의 `RidingPlayer` | 있음 | 대포 조종자 한 명 |
| 캐릭터의 `MovementBase` | 엔진이 런타임에 설정 | 갑판 위에 서 있는 캐릭터 판정 가능 |
| Actor Attach Parent | 부착했을 때만 있음 | 강제 부착된 승객 판정 가능 |
| 함선의 전체 갑판 탑승자 목록 | 없음 | 신규 API 필요 |
| 적 캐릭터의 소속 함선 포인터 | 없음 | 현재는 사용 불가 |

### 2.3 물폭탄 명중 시 갑판 위 적을 감지할 수 있는가

가능하다. 초기 구현은 명중 시점에 한 번만 전체 `ACharacter`를 순회하고 다음 조건으로 해당 함선의 갑판 탑승자 Snapshot을 만드는 방식을 권장한다.

1. Character의 Movement Base 컴포넌트 Owner가 피격 함선인지 확인한다.
2. Base Owner가 Child Actor라면 Attach Parent 계층을 따라 피격 함선까지 도달하는지 확인한다.
3. Movement Base가 없다면 Character 자체의 Attach Parent 계층이 피격 함선까지 도달하는지 확인한다.
4. 위 관계를 통과한 Actor 중 ASC를 가지고 `IsEnemyCharacterForEffects()`가 참인 적 캐릭터만 반환한다.

물폭탄은 저빈도 단발 이벤트이므로 명중 때의 월드 Character 순회 비용은 작다. 지속 Aura나 매 Tick 판정이 필요해질 때만 함선별 등록 목록으로 최적화한다.

단순한 함선 Bounds/구형 Overlap만으로 “배 위”를 판정하지 않는다. 배 옆에서 수영하거나 공중에 있는 적, 인접한 다른 배의 적까지 잘못 포함할 수 있기 때문이다. 공간 검사는 Movement Base/Attach 관계가 없는 레거시 배치를 진단하는 보조 로그로만 사용한다.

### 2.4 현재 적에게 공격속도 Attribute는 없다

`UEnemyAttributeSet`은 현재 `UBaseAttributeSet`을 상속만 하고 별도 Attribute가 없다. 공통 Attribute도 `Health`, `MaxHealth`, `AttackPower`, `MoveSpeed`뿐이다.

현재 적 공격속도에 영향을 주는 값은 GAS Attribute가 아니라 무기 Data Asset의 고정값이다.

- `FWeaponCombatData::AttackMontagePlayRate`
- `FWeaponCombatData::AttackCooldown`
- Behavior Tree의 고정 `BTTask_Wait::WaitTime`

`UGA_BasicAttack`은 `AttackMontagePlayRate`를 그대로 사용한다. `AttackCooldown`은 현재 C++ 공격 경로에서 소비되지 않고, Behavior Tree 자산에는 별도의 고정 Wait가 있다.

따라서 GameplayEffect를 적용하는 것만으로는 아직 공격속도가 바뀌지 않는다. `AttackSpeedMultiplier` Attribute를 새로 만들고 실제 공격 Montage와 재공격 대기시간이 이 Attribute를 읽도록 연결해야 한다.

---

## 3. 권장 전체 구조

```text
대포 조종 시작
  └─ ACannon이 PlayerController에 Possess됨
      └─ CannonIMC 활성화
          ├─ 좌클릭: 기존 CannonFireAction
          ├─ 취소: 기존 CannonExitAction
          └─ 4: 신규 CannonWaterBombToggleAction

4 Started
  └─ 로컬 모드 즉시 토글 + 서버 RPC
      └─ [WATER-BOMB][MODE] 로그

좌클릭 Started
  └─ ACannon::FireCannon
      ├─ 서버가 피격 함선의 CannonDisabled Tag를 확인
      ├─ 일반 모드: 기존 CannonballClass Spawn
      └─ 물폭탄 모드: WaterBombCannonballClass Spawn

WaterBombCannonball이 적 함선에 명중
  ├─ 서버가 피격 함선 ASC에 N초 Cannon Disable GE 적용
  ├─ 피격 함선의 현재 Deck Occupant Snapshot 조회
  └─ 각 적 Character ASC에 N초 Attack Speed Slow GE 적용
```

### 3.1 해류 발생기에서 재사용할 원칙

물폭탄은 해류 발생기의 다음 구현 원칙을 그대로 따른다.

- 숫자 키로 발사 종류를 선택한 뒤 좌클릭으로 실제 발사하는 2단계 입력 흐름
- 클라이언트는 입력/표시를 담당하고 실제 Projectile Spawn과 피격 효과는 서버만 처리
- Projectile Actor는 복제하고 서버 피격 결과를 권위값으로 사용
- 중복 피격 방지 상태를 Projectile 내부에 보관
- 지속시간, 감속률, Projectile Class를 C++ 기본값으로 고정하지 않고 Blueprint/GE에서 조정 가능하게 노출
- 시작, 명중, 종료를 일관된 로그 Prefix로 기록

다만 해류 발생기의 `UGA_GravityVortexThrow`를 그대로 상속하거나 복제하지는 않는다. 해류 발생기는 플레이어 Character가 입력을 소유하지만, 대포 조종 중에는 PlayerController가 `ACannon` Pawn을 Possess하고 `ACannon::SetupPlayerInputComponent`가 좌클릭을 직접 처리한다.

테스트 단계에서 Character ASC의 장기 실행 GA를 억지로 활성화하면 Cannon 입력을 다시 Character ASC로 중계하고 현재 Cannon 정보를 Payload로 전달해야 한다. 네트워크 소유권과 취소 처리가 불필요하게 복잡해진다. 따라서 권장안은 `ACannon`이 발사 모드를 소유하고, GAS는 피격 후 지속 효과와 추후 비용/쿨다운 검증에 사용한다.

정식 스킬 비용과 보유 여부가 필요해지면 모드 진입 시 대포가 `RidingPlayer`의 ASC에 “물폭탄 사용 가능 여부/비용/쿨다운”을 질의하는 얇은 Ability 계층을 추가할 수 있다. Projectile Spawn 권한은 계속 Cannon 서버에 둔다.

---

## 4. 대포 전용 4번 입력과 모드 상태

### 4.1 입력 Asset

신규 Asset 예시:

- `/Game/New/Cannon/IA_CannonWaterBombToggle`
- 기존 `/Game/New/Cannon/CannonIMC`에서 Keyboard `4`에 매핑
- `BP_Cannon`의 신규 `CannonWaterBombToggleAction` 속성에 할당

현재 `CannonIMC`는 대포가 PlayerController에 Possess되었을 때 `ACannon::OnRep_Controller`에서 추가되고, 대포 조종이 끝나면 제거된다. 이 Context 안에만 `4`를 넣으면 다음 요구사항이 자연스럽게 성립한다.

- 일반 Character 상태: 물폭탄 Toggle Handler가 바인딩되지 않음
- 함선 조종 상태: 물폭탄 Toggle Handler가 바인딩되지 않음
- 대포 조종 상태: 물폭탄 Toggle Handler가 바인딩됨

`ABasePlayer`의 기존 Quick Slot 4 로직과 `AShip`은 수정하지 않는다. 즉 비대포 상태에서 “물폭탄 기능”은 반응하지 않는다. 기존 Quick Slot 4에 아이템이 있다면 그 기존 동작까지 막는 의미는 아니다.

### 4.2 `ACannon` 상태

추가할 주요 값의 예시는 다음과 같다.

```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cannon|Water Bomb")
TSubclassOf<ACannonball> WaterBombCannonballClass;

UPROPERTY(ReplicatedUsing=OnRep_WaterBombMode)
bool bWaterBombMode = false;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cannon|Input")
TObjectPtr<UInputAction> CannonWaterBombToggleAction;
```

필요 함수:

- `HandleToggleWaterBombMode(const FInputActionValue&)`
- `ServerSetWaterBombMode(bool bEnabled)`
- `OnRep_WaterBombMode()`
- `ResetWaterBombMode()`
- `ResolveProjectileClassForCurrentMode()`

규칙:

- 로컬 PlayerController가 실제로 대포를 조종 중이고 `RidingPlayer`가 유효할 때만 토글한다.
- 서버 RPC는 해당 Cannon이 PlayerController에 Possess되어 있는지 다시 검증한다.
- 모드는 한 발 뒤 자동 해제하지 않는다. 사용자가 `4`를 다시 누를 때까지 유지한다.
- 대포 조종을 종료하거나 강제 하차하거나 Cannon이 파괴되면 일반 모드로 초기화한다.
- AI는 Toggle 입력을 가지지 않으므로 항상 일반 포탄을 사용한다.
- 서버가 최종 Projectile Class를 선택한다. 클라이언트 모드 값만 믿고 Spawn하지 않는다.

권장 로그:

```text
[WATER-BOMB][MODE] Cannon=BP_Cannon_C_0 Mode=WaterBomb Controller=...
[WATER-BOMB][MODE] Cannon=BP_Cannon_C_0 Mode=Normal Controller=...
[WATER-BOMB][MODE] Cannon=BP_Cannon_C_0 Reset=ExitCannon
```

---

## 5. 물폭탄 Projectile

### 5.1 기존 `ACannonball` 확장

신규 `AWaterBombCannonball`은 `ACannonball`을 상속하는 것이 가장 안전하다. 그러면 다음 기존 기능을 그대로 사용한다.

- Cannon의 Muzzle 위치와 회전
- 함선 Attribute의 Cannonball Speed
- 발사 함선/Owner/Instigator 설정
- Player/Enemy Cannonball Collision Channel
- 발사 함선 및 Instigator Ignore
- ProjectileMovement의 Sweep 기반 함선 충돌
- 물 진입 Ripple 처리

현재 `ACannonball::HandleShipHit`과 발사 함선 참조는 파생 클래스가 확장하기 어렵게 되어 있다. 다음 중 한 방식으로 최소 확장한다.

- `HandleShipHit(AShip*)`을 `virtual protected`로 변경
- 또는 `OnValidEnemyShipHit(AShip*, const FHitResult&)` 가상 Hook을 새로 추가
- 파생 클래스용 `GetLaunchingShip()` protected Getter 추가

일반 포탄 경로는 기존 구현을 그대로 호출하고, 물폭탄만 Hook을 Override한다.

### 5.2 물폭탄 명중 규칙

서버에서 다음 순서로 처리한다.

1. 이미 처리한 Projectile인지 검사한다.
2. `HitShip != LaunchingShip` 검사한다.
3. `HitShip->IsEnemyShipForEffects()`로 적 함선인지 검사한다.
4. 발사 함선이 Player 진영인지 검사한다.
5. 함선 대포 정지 GE를 적용한다.
6. 해당 함선의 Deck Occupant Snapshot을 구한다.
7. 각 유효한 적 Character ASC에 공속 감소 GE를 적용한다.
8. 명중 위치, 대상 함선, 감지 인원, 지속시간, 감속률을 로그로 남긴다.
9. VFX/SFX/Ripple을 실행하고 Projectile을 Destroy한다.

초기 물폭탄은 직접 피해를 주지 않는 것을 기본안으로 한다. 향후 피해도 필요하면 별도의 `WaterBombDamage`를 추가하되 기존 Cannon Damage를 묵시적으로 재사용하지 않는다.

---

## 6. 함선의 갑판 탑승자 조회 API

### 6.1 `AShip`에 추가할 API

WaterAndShip 모듈이 Enemy 모듈에 의존하면 순환 의존이 생긴다. 따라서 반환 타입은 `ABaseEnemy*`가 아니라 일반 `AActor*` 또는 `ACharacter*`로 둔다.

예시:

```cpp
void GetDeckOccupants(TArray<AActor*>& OutOccupants) const;
bool IsActorStandingOnThisShip(const AActor* Actor) const;
```

`IsActorStandingOnThisShip`의 판정 순서:

1. Actor가 `ACharacter`이면 Character Movement의 Movement Base를 조회한다.
2. Base Component의 Owner가 이 Ship이면 참이다.
3. Base Owner의 Attach Parent/GetParentActor 계층에 이 Ship이 있으면 참이다.
4. Actor 자신의 Attach Parent/GetParentActor 계층에 이 Ship이 있으면 참이다.
5. 어느 관계도 없으면 거짓이다.

`GetDeckOccupants`는 서버에서만 호출하고, World의 Character를 순회하여 위 판정을 통과한 Actor를 반환한다. `TWeakObjectPtr` 장기 저장을 하지 않으므로 하선/사망/Destroy 때 별도 정리 누수가 없다.

### 6.2 적 필터

`AShip`은 갑판 관계만 판정한다. 적인지, ASC가 있는지는 물폭탄 효과 적용 계층에서 검사한다.

- Actor가 `IAbilitySystemInterface`를 구현하고 유효한 ASC를 반환해야 한다.
- `ABaseCharacter`에 `virtual bool IsEnemyCharacterForEffects() const { return false; }`를 추가한다.
- `ABaseEnemy`만 이를 `true`로 Override한다. 이는 기존 `AShip::IsEnemyShipForEffects()`와 같은 의존성 역전 패턴이다.
- 물폭탄은 `ABaseEnemy`를 직접 Cast하지 않고 이 공통 API로 필터한다.
- 향후 Team/Faction 인터페이스가 추가되면 이 임시 식별 API의 내부 구현을 교체한다.
- 사망 Tag가 있거나 Health가 0인 적은 제외한다.

### 6.3 관계가 전혀 없는 레거시 적의 처리

적을 단순히 함선 근처 월드 좌표에 Spawn했는데 Character Movement가 Movement Base를 잡지 못하고 Attach도 하지 않았다면 정확한 함선 소속 판정은 불가능하다.

구현 전 실제 “적 함선 위 적” 테스트 배치를 만들고 다음 값을 로그로 확인한다.

- Enemy 이름
- `MovementMode`
- `MovementBase` 이름과 Owner
- Attach Parent
- 판정된 Ship

관계가 비어 있다면 적 Spawn/Boarding 경로에서 다음 중 하나를 명시해야 한다.

- Character가 갑판에 착지하여 Movement Base를 정상 획득하게 Spawn 높이와 Collision을 수정
- 고정 선원이라면 Ship 컴포넌트에 Attach
- 향후 이동형 선원 등록 시스템이 필요하면 `RegisterDeckOccupant/UnregisterDeckOccupant`를 추가

초기 물폭탄 구현에서 Bounds Overlap만으로 이 결함을 숨기지 않는다.

---

## 7. 함선 대포 N초 정지

### 7.1 Gameplay Tag와 GameplayEffect

신규 Tag 예시:

- `State.Ship.CannonDisabled`
- `State.Debuff.WaterBomb`
- `Data.Effect.Duration`
- `Data.Effect.AttackSpeedMultiplier`

신규 Duration GameplayEffect 예시:

- `GE_WaterBomb_CannonDisable`
  - Duration: SetByCaller `Data.Effect.Duration`
  - Granted Tag: `State.Ship.CannonDisabled`
  - Stack Limit: 1
  - 재명중 정책: 지속시간 Refresh

`ACannon::FireCannon`과 `ACannon::ServerFire_Implementation` 양쪽에서 `GetOwningShip()->GetAbilitySystemComponent()`의 `State.Ship.CannonDisabled`를 검사한다.

서버 검사가 최종 권위다. 클라이언트 검사도 넣어 불필요한 RPC와 로컬 발사 Cooldown 시작을 막는다.

이 방식은 `AEnemyShip::TickAIAimingAndFiring`을 별도로 중지하지 않아도 된다. AI와 Player Cannon이 모두 공통 `ACannon::FireCannon`을 사용하므로 함선 ASC의 Tag 하나로 함선에 붙은 모든 대포가 차단된다.

AI가 매 Tick 발사를 시도하므로 차단 시도마다 로그를 찍지 않는다. GE 적용/해제 시점과 Cannon별 첫 차단 시도만 Rate Limit하여 기록한다.

`CannonFireCooldown`을 매우 큰 값으로 바꾸는 방식은 사용하지 않는다. 그것은 완전한 발사 금지가 아니고, 기존 Cooldown Timer와 효과 종료 시점을 맞추기 어렵다.

---

## 8. 적 캐릭터 공격속도 N% 감소

### 8.1 신규 Attribute

`UEnemyAttributeSet`에 다음 Attribute를 추가한다.

```cpp
UPROPERTY(BlueprintReadOnly, Category="Attributes|Combat", ReplicatedUsing=OnRep_AttackSpeedMultiplier)
FGameplayAttributeData AttackSpeedMultiplier;
```

규칙:

- 기본값: `1.0`
- 최소값: 예시 `0.1`
- 최대값: 초기에는 `3.0`
- RepNotify와 `DOREPLIFETIME_CONDITION_NOTIFY` 추가
- `PreAttributeChange`에서 Clamp

Attribute는 GE가 명중 때 새로 “부여”하는 값이 아니다. 모든 적의 `UEnemyAttributeSet`에 기본값 `1.0`으로 항상 존재하고, 물폭탄 Duration GE가 N초 동안 그 Aggregated 값을 수정한다. GE가 만료되면 GAS가 자동으로 이전 값으로 복구한다.

공격속도 `N%` 감소의 배율은 다음과 같이 해석한다.

```text
SlowRatio = Clamp(N / 100, 0, 0.9)
AttackSpeedMultiplier = 1 - SlowRatio
```

예를 들어 30% 감소면 배율은 `0.7`이다. “Cooldown 30% 증가”로 해석하지 않는다. 정확한 공격 빈도를 70%로 만들려면 시간 값은 `BaseTime / 0.7`로 계산해야 한다.

### 8.2 GameplayEffect

- `GE_WaterBomb_AttackSpeedSlow`
  - Duration: SetByCaller `Data.Effect.Duration`
  - `AttackSpeedMultiplier`에 Multiplicative Modifier
  - 배율: SetByCaller `Data.Effect.AttackSpeedMultiplier`
  - Granted Tag: `State.Debuff.WaterBomb`
  - Stack Limit: 1
  - 재명중 정책: 지속시간 Refresh

초기에는 같은 물폭탄 Slow가 중첩되지 않게 한다. 서로 다른 공속 Buff/Debuff와의 최종 Aggregation 규칙은 별도 전투 시스템 정책으로 확장한다.

### 8.3 새로 시작하는 공격 Montage 연결

`UGA_BasicAttack`에서 Montage 재생률을 다음처럼 계산한다.

```text
EffectiveMontagePlayRate = BaseAttackMontagePlayRate * AttackSpeedMultiplier
```

Montage Notify에 연결된 Hit Scan 구간도 Montage와 함께 느려지므로 공격 동작 전체가 일관되게 느려진다.

이 처리는 `UGA_BasicAttack::PlayAttackMontage` 한 곳에 넣는다. 현재 무기의 Data Asset 기본 Play Rate는 유지하고 Attribute를 배율로 곱하므로 무기별 고유 공격속도를 잃지 않는다.

### 8.4 이미 재생 중인 공격 Montage 자동 동기화

`UGA_BasicAttack`에서만 Attribute를 읽으면 물폭탄이 명중하기 전에 시작한 공격은 느려지지 않는다. 이를 위해 `UEnemyAttributeSet::PostAttributeChange`에서 `AttackSpeedMultiplier` 변경을 감지하여 현재 기본공격 Montage에도 즉시 비율 변화를 적용한다.

서버 처리 개념은 다음과 같다.

```text
if ChangedAttribute == AttackSpeedMultiplier
  ASC가 Authority인지 확인
  ASC->GetAnimatingAbility()의 Asset Tag가 GameplayAbility.BasicAttack인지 확인
  CurrentMontage와 현재 실제 PlayRate 조회
  RateRatio = NewAttackSpeedMultiplier / OldAttackSpeedMultiplier
  ASC->CurrentMontageSetPlayRate(CurrentPlayRate * RateRatio)
```

예시:

- 기본 무기 Montage Rate `1.2`, Attribute `1.0 -> 0.7`: 현재 Rate `1.2 -> 0.84`
- GE 만료로 Attribute `0.7 -> 1.0`: 현재 Rate `0.84 -> 1.2`

절대값을 `NewAttribute`로 덮지 않고 기존 Rate에 `New/Old` 비율을 곱한다. 그래야 무기 Data Asset의 기본 Play Rate와 다른 시스템의 배율을 보존할 수 있다.

`CurrentMontageSetPlayRate()`는 서버에서 호출하면 ASC의 Replicated Montage Data를 갱신하므로 Client Simulated Proxy도 같은 속도로 보인다. Attribute RepNotify가 실행되는 Client에서는 다시 Montage Setter를 호출하지 않도록 Authority Guard를 둔다.

Animating Ability의 Asset Tag가 `GameplayAbility.BasicAttack`일 때만 변경하여 Equip, Hit Reaction, Death Montage가 물폭탄 때문에 느려지지 않게 한다.

### 8.5 공격 주기 연결

추가 Attribute 없이 같은 `AttackSpeedMultiplier`를 재공격 대기에도 사용한다.

```text
EffectiveAttackCooldown = BaseAttackCooldown / AttackSpeedMultiplier
```

공격속도 배율을 `S`라고 하면 다음 두 항목을 같은 배율로 늘린다.

```text
Montage 실제 시간 = BaseMontageTime / S
공격 후 회복 시간 = BaseAttackCooldown / S
전체 공격 주기 = (BaseMontageTime + BaseAttackCooldown) / S
```

따라서 `S=0.7`이면 공격 모션과 공격 후 회복시간이 모두 약 `1.4286`배 길어지고, 장시간 공격 횟수는 기준의 약 70%가 된다.

현재 `FWeaponCombatData::AttackCooldown`은 C++ 공격 흐름에서 사용되지 않고 Behavior Tree의 고정 `BTTask_Wait`가 대기시간을 담당한다. 이 고정 Wait는 Attribute 변화에 반응하지 않으므로 다음과 같이 바꾼다.

1. `UBTT_EnemyBasicAttack`은 공격 Tag가 제거될 때 즉시 완료하지 않고 Recovery 단계로 전환한다.
2. Recovery 중 매 Tick 다음 Progress를 누적한다.

```text
RecoveryProgress += DeltaSeconds * CurrentAttackSpeedMultiplier
```

3. `RecoveryProgress >= WeaponDefinition.AttackCooldown`이 되면 Task를 완료한다.
4. 기존 Behavior Tree의 뒤쪽 고정 `BTTask_Wait`는 제거하거나 WaitTime을 0으로 만든다.

이 Progress 방식은 단순히 `BaseCooldown / S` Timer를 한 번 설정하는 것보다 정확하다.

- 물폭탄 GE가 이미 진행 중인 회복시간 도중 적용되어도 남은 회복 진행이 즉시 느려진다.
- GE가 회복시간 도중 만료되면 남은 진행이 즉시 정상 속도로 돌아온다.
- Buff와 Debuff가 도중에 바뀌어도 Timer를 취소하고 다시 계산할 필요가 없다.
- 물폭탄 GE의 실제 N초 범위를 넘어 감속된 Cooldown Snapshot이 잔류하지 않는다.

현재 적 기본공격은 `UBTT_EnemyBasicAttack`을 통해서만 시작되므로 Task가 Recovery까지 점유하는 방식이면 중복 활성화도 막을 수 있다. 향후 BT 외부에서도 기본공격을 직접 활성화하는 경로가 생기면 그때 공용 GAS Cooldown Tag를 추가한다.

### 8.6 이 방식의 최소 Enemy 변경 파일

공격 모션과 공격 주기를 모두 연결할 때 Enemy 쪽 변경은 다음 5개 파일이다.

- `Enemy/Public/GAS/EnemyAttributeSet.h`: Attribute, RepNotify 선언
- `Enemy/Private/GAS/EnemyAttributeSet.cpp`: 기본값, 복제, Clamp, `PostAttributeChange` Montage 동기화
- `Enemy/Private/GAS/Ability/GA_BasicAttack.cpp`: 새로 시작하는 Montage의 Rate에 Attribute 배율 적용
- `Enemy/Public/Task/BTT_EnemyBasicAttack.h`: Recovery 단계와 Tick 선언
- `Enemy/Private/Task/BTT_EnemyBasicAttack.cpp`: Attribute 기반 Recovery Progress 누적

다음 Enemy 파일은 건드리지 않아도 된다.

- `BaseEnemy.h/.cpp`
- `WeaponDataAsset.h`

Behavior Tree Asset에서는 기존 고정 `BTTask_Wait`를 제거하거나 0으로 만드는 Editor 변경 한 번이 필요하다.

---

## 9. 모듈 의존성과 클래스 배치

### 9.1 권장 클래스 위치

ClassFeature 모듈:

- `Public/Projectiles/WaterBombCannonball.h`
- `Private/Projectiles/WaterBombCannonball.cpp`

WaterAndShip 모듈:

- 기존 `Public/Cannon.h`, `Private/Cannon.cpp`
- 기존 `Public/Cannonball.h`, `Private/Cannonball.cpp`
- 기존 `Public/Ship.h`, `Private/Ship.cpp`

GASCore/Enemy 모듈:

- 기존 `Enemy/Public/GAS/EnemyAttributeSet.h`
- 기존 `Enemy/Private/GAS/EnemyAttributeSet.cpp`
- 기존 `Enemy/Private/GAS/Ability/GA_BasicAttack.cpp`
- 기존 `Enemy/Public/Task/BTT_EnemyBasicAttack.h`
- 기존 `Enemy/Private/Task/BTT_EnemyBasicAttack.cpp`
- 선택 사항: 정확한 갑판 적 식별이 필요하면 `GASCore/Public/BaseCharacter.h`와 `Enemy/Public/BaseEnemy.h`에 `IsEnemyCharacterForEffects()` 추가

ArtisticSWCore 모듈:

- 기존 `BaseGameplayTags.h/.cpp`에 상태/SetByCaller Tag 추가

### 9.2 순환 의존 방지

- `WaterAndShip`은 `Enemy`를 참조하지 않는다.
- `AShip::GetDeckOccupants`는 일반 Actor/Character만 반환한다.
- `AWaterBombCannonball`은 Actor의 `IAbilitySystemInterface`를 통해 ASC를 얻는다.
- 적 캐릭터 여부는 GASCore의 `ABaseCharacter::IsEnemyCharacterForEffects()`로 확인하며 Enemy 타입을 직접 include하지 않는다.
- 공속 감소 Attribute 자체는 GE Asset이 참조하므로 ClassFeature C++이 `UEnemyAttributeSet`을 직접 include할 필요가 없다.
- `ACannon`은 `TSubclassOf<ACannonball>`만 알고 실제 `AWaterBombCannonball` 클래스에는 의존하지 않는다.

기존 의존성 방향인 `ClassFeature -> WaterAndShip`, `Enemy -> ClassFeature/WaterAndShip`을 뒤집지 않는다.

---

## 10. Blueprint와 데이터 Asset

신규/수정 Asset 예시:

- `/Game/New/Cannon/IA_CannonWaterBombToggle`
- `/Game/New/Cannon/CannonIMC` 수정
- `/Game/New/Cannon/BP_Cannon` 수정
- `/Game/Blueprints/Skills/WaterBomb/BP_WaterBombCannonball`
- `/Game/Blueprints/Skills/WaterBomb/GE_WaterBomb_CannonDisable`
- `/Game/Blueprints/Skills/WaterBomb/GE_WaterBomb_AttackSpeedSlow`

`BP_WaterBombCannonball`에서 조정할 값:

- Disable Duration
- Attack Speed Slow Percent
- Projectile Mesh/Material
- Collision Radius
- Water Ripple Amplitude
- 직접 피해 여부와 피해량
- Impact VFX/SFX

기존 `Item.Id.Skill.WaterBomb` Gameplay Tag는 이미 정의되어 있다. 테스트 Toggle에는 바로 필요하지 않지만, 정식 보유/소모/쿨다운 시스템을 붙일 때 식별자로 재사용한다.

---

## 11. 구현 단계

### M0. 실제 적 승선 관계 계측

- 적 함선 위에 일반 `ABaseEnemy`를 둔 테스트 배치를 만든다.
- 서버에서 Movement Base, Base Owner, Attach Parent, 판정 Ship을 로그로 출력한다.
- 갑판 위/바다/인접 배/점프 중 판정 결과를 확인한다.

완료 조건: 현재 콘텐츠에서 갑판 위 적을 Movement Base 또는 Attach 계층으로 안정적으로 구분할 수 있다.

### M1. 대포 모드 입력

- Cannon 전용 Input Action과 `4` 매핑 추가
- `ACannon` 모드 상태, RPC, RepNotify, 로그 추가
- 하차/강제 하차/Destroy 시 모드 Reset
- 좌클릭 시 서버가 모드에 맞는 Projectile Class 선택

완료 조건: Cannon 조종 중에만 `4`가 Normal/WaterBomb을 토글하며, 일반/함선 조종 상태에서는 물폭탄 로그가 전혀 나오지 않는다.

### M2. 물폭탄 Projectile과 함선 정지

- `ACannonball` 확장 Hook 추가
- `AWaterBombCannonball` 구현
- 적 함선 명중 검증
- 함선 Disable GE와 `ACannon` Tag Gate 추가

완료 조건: 적 함선 명중 후 N초 동안 해당 함선의 모든 AI Cannon이 발사하지 않고, 효과 종료 직후 다시 발사할 수 있다.

### M3. 갑판 탑승자 감지

- `AShip::IsActorStandingOnThisShip`
- `AShip::GetDeckOccupants`
- `IsEnemyCharacterForEffects()`와 ASC/사망 필터
- 명중 로그에 감지 인원과 Actor 이름 기록

완료 조건: 피격 순간 해당 배 위의 적만 검출하고, 물/다른 배/공중의 적은 제외한다.

### M4. 공격속도 Attribute와 GE

- `UEnemyAttributeSet::AttackSpeedMultiplier` 추가/복제/Clamp
- Slow GE 추가
- `UGA_BasicAttack` Montage Play Rate 연결
- `UBTT_EnemyBasicAttack` Recovery Progress를 같은 Attribute에 연결
- `BT_EnemyBase`의 기존 고정 Wait 제거 또는 0 설정

완료 조건: 30% Slow 테스트에서 Montage 재생률과 장시간 공격 횟수가 기준 대비 약 70%가 되고, N초 후 정확히 원래 값으로 복귀한다.

### M5. 네트워크 및 회귀 검증

- Listen Server + Client
- Dedicated Server + Client
- 동일 함선 연속 명중
- 서로 다른 함선 동시 명중
- 효과 중 함선/적/Cannon Destroy
- 효과 중 적 하선
- 대포 조종 중 강제 하차

완료 조건: 서버와 모든 Client에서 Projectile/모드/Attribute가 일관되고 Timer, GE, Actor 참조 누수가 없다.

---

## 12. 테스트 시나리오

### 입력/모드

1. 일반 Character 상태에서 `4`: 물폭탄 로그 없음.
2. 함선 조종 상태에서 `4`: 물폭탄 로그 없음.
3. 대포 조종 상태에서 `4`: `Mode=WaterBomb` 로그 1회.
4. 좌클릭: 일반 포탄 대신 물폭탄 Spawn.
5. 다시 `4`: `Mode=Normal` 로그 1회.
6. 좌클릭: 일반 포탄 Spawn.
7. WaterBomb 모드에서 대포 하차 후 재탑승: Normal 모드.

### 함선 대포 정지

1. 물폭탄이 Player Ship/발사 Ship에 맞아도 효과 없음.
2. 적 함선 명중 시 그 함선에 붙은 모든 Cannon 정지.
3. 다른 적 함선 Cannon은 계속 발사.
4. N초 경과 후 정지 함선 Cannon 재개.
5. 효과 중 재명중 시 Stack은 늘지 않고 Duration만 Refresh.

### 갑판 적 판정

1. 피격 함선 갑판 위 적: Slow 적용.
2. 다른 함선 갑판 위 적: 적용 안 됨.
3. 피격 함선 옆 바다의 적: 적용 안 됨.
4. 피격 순간 점프하여 Movement Base가 끊긴 적: 기본안에서는 적용 안 됨.
5. 함선에 명시적으로 Attach된 적: 적용.
6. 피격 후 하선한 적: 이미 받은 N초 GE는 남은 시간 동안 유지.

### 공격속도

1. 기본 배율 1.0 확인.
2. 30% Slow 시 Attribute 0.7 확인.
3. Montage Play Rate가 Base의 0.7배인지 확인.
4. Attack Cooldown 시간이 Base/0.7인지 확인.
5. 효과 종료 후 Attribute와 공격주기가 원복되는지 확인.
6. 사망/부활 또는 Enemy 재사용 시 Debuff가 남지 않는지 확인.

---

## 13. 로그 규격

```text
[WATER-BOMB][MODE] Cannon=... Mode=WaterBomb
[WATER-BOMB][FIRE] Cannon=... Ship=... Projectile=...
[WATER-BOMB][HIT] TargetShip=... Duration=5.00 SlowPct=30.0 CrewDetected=3
[WATER-BOMB][CREW] Enemy=... MovementBase=... AppliedMultiplier=0.70
[WATER-BOMB][SHIP-DISABLE] Ship=... Applied Duration=5.00
[WATER-BOMB][SHIP-DISABLE] Ship=... Removed
[WATER-BOMB][REJECT] Reason=FriendlyShip|NoASC|NoProjectileClass|NotControllingCannon
```

AI의 발사 차단 로그는 매 Tick 출력하지 않는다. `Verbose` 또는 Rate Limit 로그로 제한한다.

---

## 14. 완료 기준

- 대포 조종 상태에서만 `4`로 WaterBomb/Normal 모드를 토글할 수 있다.
- 모드 상태가 로그에 명확히 표시되고 하차 시 Normal로 초기화된다.
- 물폭탄 Projectile은 서버에서 Spawn되고 모든 Client에 정상 복제된다.
- 적 함선에 명중한 경우에만 해당 함선 대포가 N초 동안 완전히 발사 불가가 된다.
- 같은 함선 위에 있다고 Movement Base/Attach 관계로 판정된 적 캐릭터만 N초 공속 감소를 받는다.
- 공속 감소는 Attribute + GameplayEffect로 구현되고 Montage와 재공격 Cooldown 양쪽에 반영된다.
- 효과 종료 후 함선 대포와 적 공격속도가 자동 복구된다.
- 일반 Character/함선 조종 상태에는 물폭탄 전용 `4` 반응이나 로그가 없다.
- WaterAndShip과 Enemy/ClassFeature 사이에 신규 순환 모듈 의존성이 없다.

---

## 15. 구현 전 확정할 튜닝값

기능 구조와 무관하므로 다음 값은 Blueprint 기본값으로 시작하고 기획 확정 후 조정한다.

- 대포 정지 지속시간 `N`초
- 적 공격속도 감소율 `N%`
- 같은 대상 재명중 시 Duration Refresh 여부
- 물폭탄 직접 피해 유무
- 물폭탄 Projectile 속도가 함선 Cannonball Speed를 그대로 사용할지 별도 배율을 둘지
- 점프 중이라 잠시 Movement Base가 없는 적도 “갑판 위 적”으로 포함할지

초기 권장 테스트값은 `Duration=5초`, `AttackSpeedSlow=30%`, 직접 피해 `0`, 중첩 없음/지속시간 Refresh이다.
