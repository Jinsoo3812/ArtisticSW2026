# 물폭탄 최소 구현안: Enemy 코드 무수정

## 1. 결론

`Source/Enemy` 아래 파일을 하나도 수정하지 않고 다음 기능을 구현할 수 있다.

- 대포 조종 중에만 `4`로 WaterBomb/Normal 모드 토글
- 물폭탄 모드 좌클릭 발사
- 적 함선 명중 시 해당 함선의 모든 Cannon을 N초 발사 불가 처리
- 명중 순간 해당 함선 위에 서 있거나 부착된 ASC Character 감지
- 감지된 Character가 현재 또는 이후 재생하는 GAS Montage를 N초 동안 감속
- 서버 권위 처리와 Montage Play Rate 복제
- 모드, 발사, 명중, Cannon 차단, Character 감속 로그

이 구현은 Enemy의 Attribute나 공격 Ability를 수정하지 않는다. 따라서 “공격속도 Attribute N% 감소”가 아니라 “공격 Montage 재생률 N% 감소” 구현이다.

현재 적 공격 Behavior Tree의 고정 Wait까지 바꾸지 않으므로 장시간 공격 횟수가 정확히 N% 줄어드는 것은 보장하지 않는다. Montage가 느려져 Ability 완료가 늦어지므로 실제 공격 간격도 늘어나지만, 전체 공격 주기는 다음과 같다.

```text
기존 주기 = 공격 Montage 시간 + 고정 BT Wait
감속 주기 = 공격 Montage 시간 / 감속 배율 + 고정 BT Wait
```

정확한 최종 공격 빈도 N% 감소가 필요할 때만 `GA_BasicAttack`, BT Cooldown 또는 Enemy Attribute를 수정하는 정식안으로 확장한다.

---

## 2. 최소 변경 파일

### 반드시 수정할 기존 파일 3개

1. `Source/WaterAndShip/Public/Cannon.h`
   - WaterBomb 모드 상태와 Projectile Class
   - `4` Toggle Handler/RPC/RepNotify
   - Cannon 임시 발사 차단 API

2. `Source/WaterAndShip/Private/Cannon.cpp`
   - 대포 전용 `4` 입력 바인딩
   - 서버 모드 검증과 로그
   - 모드별 Projectile Class 선택
   - `FireCannon`/`ServerFire` 발사 차단 검사
   - 하차 시 Normal 모드 초기화

3. `Source/WaterAndShip/Public/Cannonball.h`
   - `HandleShipHit(AShip*)`을 `virtual protected`로 변경
   - 파생 Projectile용 `GetLaunchingShip()` protected Getter

`Cannonball.cpp`의 기존 명중 구현은 그대로 둘 수 있다. 선언이 virtual이면 기존 `OnHit()` 호출이 파생 WaterBomb Override로 동적 Dispatch된다.

### 신규 파일 2개

4. `Source/WaterAndShip/Public/WaterBombCannonball.h`
5. `Source/WaterAndShip/Private/WaterBombCannonball.cpp`

두 파일 안에 다음 두 클래스를 함께 둘 수 있다.

- `AWaterBombCannonball : public ACannonball`
- `AWaterBombMontageSlowRuntime : public AActor`

Helper Runtime을 별도 파일로 분리하는 편이 구조상 더 깔끔하지만, “최소 파일 변경” 기준에서는 WaterBomb Projectile 파일에 같이 둔다.

### 자동화 테스트를 추가할 경우 신규 파일 1개

6. `Source/WaterAndShip/Private/Tests/WaterBombTests.cpp`

즉 기능 구현만 보면 기존 3개 + 신규 2개, 테스트까지 포함하면 총 6개 파일이다.

다음 파일은 수정하지 않는다.

- `Source/Enemy/**`
- `Source/GASCore/**`
- `Source/ClassFeature/**`
- `Source/WaterAndShip/Public/Ship.h`
- `Source/WaterAndShip/Private/Ship.cpp`
- `BaseGameplayTags.h/.cpp`

---

## 3. Enemy 타입 없이 갑판 위 Character 감지

`AWaterBombCannonball`이 적 함선 명중 시 서버에서 한 번 World의 `ACharacter`를 순회한다.

판정 순서:

1. `CharacterMovement->GetMovementBase()`의 Owner가 피격 Ship인지 검사
2. Base Owner가 Child Actor이면 Attach Parent 계층을 따라 피격 Ship인지 검사
3. Character 자체가 피격 Ship 또는 그 Child Actor에 Attach되어 있는지 검사
4. `IAbilitySystemInterface`를 통해 유효한 ASC가 있는지 검사
5. PlayerController가 조종하는 Character와 발사 Instigator는 제외
6. Health가 0 이하이면 제외

WaterBomb 코드가 `ABaseEnemy`를 Cast하지 않으므로 Enemy 모듈 의존성이 생기지 않는다.

현재 프로젝트에는 공용 Faction 인터페이스가 없으므로 “Player가 아닌 ASC Character”를 임시 적 판정으로 사용한다. 현재 적 선원 테스트에는 충분하지만, 향후 아군 NPC가 적 함선에 올라갈 수 있으면 그 NPC도 감속될 수 있다. 정확한 진영 분리는 공용 Team/Faction 시스템이 생긴 뒤 교체한다.

Bounds/Radius만으로 배 위를 판정하지 않는다. 바다에 빠진 적과 인접 함선의 적이 포함될 수 있기 때문이다.

---

## 4. Enemy 코드 없이 Montage 감속

### 4.1 사용할 엔진 API

적 ASC에는 다음 public API가 이미 있다.

```cpp
UAnimMontage* UAbilitySystemComponent::GetCurrentMontage() const;
void UAbilitySystemComponent::CurrentMontageSetPlayRate(float InPlayRate);
```

서버에서 `CurrentMontageSetPlayRate()`를 호출하면 ASC가 Replicated Montage Data를 갱신하므로 Simulated Proxy Client에도 재생률이 전달된다.

### 4.2 Runtime Actor 동작

`AWaterBombMontageSlowRuntime`은 감지한 Character 하나를 N초 동안 추적한다.

- Target ASC와 만료 서버 시간을 저장
- 약 20Hz로 `ASC->GetCurrentMontage()` 확인
- 새 Montage가 시작되면 현재 원본 Play Rate를 한 번 저장
- `원본 Play Rate * SlowMultiplier`를 `CurrentMontageSetPlayRate()`로 적용
- Montage가 끝나면 추적 상태를 비움
- 같은 Montage가 다시 시작되면 새 인스턴스로 보고 원본 Rate를 다시 읽음
- 효과 종료 시 아직 같은 Montage가 재생 중이면 원본 Rate 복구
- Target 사망/Destroy/ASC 무효 시 즉시 종료

예를 들어 30% 감속은 `SlowMultiplier=0.7`이다.

같은 Target이 다시 물폭탄을 맞으면 Runtime Actor를 중복 생성하지 않고 기존 Actor의 만료 시간을 Refresh한다. 초기 정책은 중첩 없음, 가장 느린 배율 유지다.

공격 Montage 외의 GAS Montage가 효과 중 재생되면 같이 느려질 수 있다. Enemy 코드를 전혀 수정하지 않고 “공격 Montage만” 완전히 구분할 공용 정보가 현재 없기 때문이다. 현재 적 전투 경로에서는 ASC의 주 Montage가 기본 공격 Montage이므로 테스트에는 사용할 수 있다.

---

## 5. EnemyShip 코드 없이 함선 Cannon 정지

물폭탄이 적 Ship에 명중하면 World의 `ACannon`을 한 번 순회한다.

```text
Cannon->GetOwningShip() == HitShip
```

인 Cannon마다 `ApplyTemporaryFireDisable(Duration)`를 호출한다.

`ACannon`은 `FireDisabledUntilServerTime`을 보관하고 다음 두 경로에서 검사한다.

- `FireCannon()`의 로컬 조기 차단
- `ServerFire_Implementation()`의 최종 권위 차단

AI도 `ACannon::FireCannon()`을 사용하므로 `AEnemyShip::TickAIAimingAndFiring()`을 수정할 필요가 없다.

연속 명중 시 `FireDisabledUntilServerTime = Max(기존 만료, 새 만료)`로 갱신한다. 별도 Tick은 필요하지 않고 현재 서버 시간이 만료값보다 작을 때만 발사를 거부한다.

---

## 6. 입력과 에디터 작업

### 6.1 기능 테스트만 할 때: IA/IMC 불필요

`ACannon::SetupPlayerInputComponent()`에 임시 Fallback을 둘 수 있다.

```cpp
if (CannonWaterBombToggleAction)
{
    EnhancedInput->BindAction(...);
}
else
{
    PlayerInputComponent->BindKey(EKeys::Four, IE_Pressed, ...);
}
```

`ACannon` Pawn만 이 키를 바인딩하므로 다음 상태에서는 물폭탄 반응이 없다.

- 일반 Player Character 조종
- Ship 조종
- AI Cannon

따라서 첫 기능 테스트는 사용자가 IA/IMC를 만들지 않아도 가능하다.

### 6.2 정식 입력으로 정리할 때: IA/IMC 필요

최종적으로는 다음 Editor 작업을 권장한다.

1. `/Game/New/Cannon/IA_CannonWaterBombToggle` 생성
2. Value Type을 Digital/Bool로 설정
3. 기존 `/Game/New/Cannon/CannonIMC`에 Keyboard `4` 매핑
4. `BP_Cannon`의 `CannonWaterBombToggleAction`에 새 IA 할당

IA가 할당되면 C++ Fallback `BindKey`는 실행하지 않으므로 이중 Toggle이 발생하지 않는다.

### 6.3 Projectile Blueprint는 선택 사항

C++ `AWaterBombCannonball`을 `ACannon`의 기본 WaterBomb Class로 설정하면 기능 테스트에는 별도 BP가 필요 없다. 대신 Mesh/VFX가 없어 로그와 Debug Draw로만 보일 수 있다.

비주얼 테스트에는 다음 작업이 필요하다.

1. `BP_WaterBombCannonball` 생성
2. 부모를 `AWaterBombCannonball`로 설정
3. Mesh/Material/VFX/SFX 설정
4. `BP_Cannon`의 `WaterBombCannonballClass`에 할당

---

## 7. 구현 후 테스트 가능 범위

### 7.1 자동화 가능한 항목

- WaterBomb/Normal 모드 토글과 서버 상태
- 모드별 Projectile Class 선택
- Friendly Ship 피격 거부
- 적 Ship 명중 시 해당 Ship Cannon만 발사 차단
- 연속 명중 Duration Refresh
- Movement Base/Attach Parent 기반 Deck Character 판정
- ASC 없는 Character 제외
- Montage 시작 감지 후 Play Rate 변경
- 효과 종료 후 원본 Play Rate 복구
- Target/Cannon/Ship Destroy 안전성

### 7.2 Headless/로그 Smoke Test

`UnrealEditor-Cmd` 또는 Dedicated Server에서 WaterBomb을 적 Ship에 직접 Spawn/충돌시켜 다음 로그를 검증할 수 있다.

```text
[WATER-BOMB][MODE]
[WATER-BOMB][FIRE]
[WATER-BOMB][HIT]
[WATER-BOMB][CANNON-DISABLE]
[WATER-BOMB][DECK-TARGET]
[WATER-BOMB][MONTAGE-SLOW]
[WATER-BOMB][MONTAGE-RESTORE]
```

`Test_Level`에는 적 함선은 있지만 일반 적 Character가 포함되어 있지 않다. 갑판 감지와 Montage 감속까지 검증하려면 다음 중 하나가 추가로 필요하다.

- PIE에서 `BP_MyEnemy`를 적 함선 갑판 위에 임시 배치
- 자동 테스트가 `BP_MyEnemy`를 동적으로 Spawn하여 갑판에 착지시킴

### 7.3 최종 수동 PIE 확인

다음은 실제 Input Asset과 Blueprint 설정을 포함한 PIE 확인이 가장 확실하다.

1. 일반 상태 `4`: 물폭탄 로그 없음
2. Ship 조종 상태 `4`: 물폭탄 로그 없음
3. Cannon 조종 상태 `4`: WaterBomb 모드 로그
4. 좌클릭: WaterBomb Projectile 발사
5. 적 Ship 명중: Cannon 정지
6. 갑판 적 공격 Montage가 느려짐
7. N초 후 Cannon과 Montage Rate 원복
8. `4` 재입력: 일반 포탄 모드

---

## 8. 권장 진행 순서

1. IA 없이 C++ Fallback `4` 입력으로 기능 구현
2. C++ WaterBomb 기본 Projectile과 Debug/Log로 발사 및 명중 검증
3. 적 Ship Cannon 차단 자동 테스트
4. `BP_MyEnemy` 갑판 배치 후 Montage Slow/Restore 검증
5. 기능 확인 후 사용자가 IA/IMC와 WaterBomb BP 비주얼 연결
6. 정확한 공격 빈도 N% 감소가 필요해질 때만 Enemy Attribute/GA/BT 수정안 적용
