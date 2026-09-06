# Area Slow 능력 최종 설계 및 구현안

## 1. 확정 동작

이 능력은 지속형 장판이 아니라 **확정 순간 한 번만 대상을 획득하는 범위 공격**이다.

1. 소유 클라이언트가 전용 능력 키를 누르고 있는 동안 플레이어 전방 사각 Decal을 본다.
2. 이 조준 Decal은 해당 클라이언트에만 존재하며 복제되지 않는다.
3. 키를 놓거나 우클릭하면 비용 없이 취소한다.
4. 좌클릭하면 로컬 조준 Decal을 제거하고 GAS Confirm 이벤트를 서버로 전달한다.
5. 서버는 그 순간의 서버 Player 월드 Transform으로 Oriented Box Overlap을 정확히 한 번 실행한다.
6. Player Character를 항상 제외하고, Data Asset의 Gameplay Tag Query를 만족하는 대상만 고른다.
7. 각 대상 ASC에 Duration Gameplay Effect를 한 번 적용한다.
8. 서버는 판정과 무관한 짧은 수명의 복제 Decal Actor를 생성한다. 모든 클라이언트가 이를 본다.
9. 범위는 즉시 소멸하지만, 맞은 대상은 자신의 감속 지속시간이 끝날 때까지 느려진다.

범위 이탈 추적, 주기적 재검색, 배 상대좌표 변환, 원래 속도 저장/복구는 사용하지 않는다.

## 2. 책임 분리

| 타입 | 책임 | 가지면 안 되는 책임 |
|---|---|---|
| `ABasePlayer` | Enhanced Input 바인딩, GAS 입력 전달, 좌/우 클릭을 Confirm/Cancel로 라우팅 | 범위 계산, 대상 검색, 속도 변경 |
| `UGA_PlayerAreaSlow` | 조준 수명 조율, 서버 Commit/소비, 1회 Overlap, Tag Query, GE 적용 | Tick 기반 검색, Target 속도 직접 변경 |
| `UAreaSlowSkillDataAsset` | 범위, 감속 수치/시간, 허용·차단 Query, Object Type, Decal 설정 | 레벨 Actor 인스턴스 목록 보관 |
| `AAreaSlowTargetingDecal` | 소유 클라이언트의 조준 범위를 플레이어에 맞춰 갱신 | 복제, 판정, GE 적용 |
| `AAreaSlowConfirmedDecal` | 서버가 만든 확정 범위를 모든 클라이언트에 짧게 표시 | Collision, Overlap, 감속 수명 관리 |
| `UAreaSlowGameplayEffect` | 대상별 지속시간, 이동/공격 감속 배율, Slow 상태 Tag, 중첩 정책 | 대상 시스템별 기준 속도 계산 |
| Target 이동/공격 소유자 | `MoveSpeedMultiplier`, `AttackSpeedMultiplier`를 각자의 최종 계산에 반영 | Ability/Decal 상태 의존 |

핵심 경계는 **Ability가 효과를 적용하고, 대상이 자신의 이동·공격 방식을 해석한다**는 것이다. 현재 `ABaseEnemy`, 근접/보스 기본 공격 Ability, 원거리 적의 공격 재생률 계산이 이 계약을 구현한다.

## 3. 네트워크 흐름

```text
Owning Client                         Server                         Other Clients
     |                                  |                                  |
Ability key Started                     |                                  |
     |-- LocalPredicted GA activate ---->|                                  |
     |-- local preview spawn             |                                  |
     |   (bReplicates=false)             |                                  |
     |                                  |                                  |
Left click                              |                                  |
     |-- ASC GenericConfirm ------------>|                                  |
     |   SpecHandle + PredictionKey      |                                  |
     |-- local preview destroy           |                                  |
     |                                  |-- Commit + skill use consume      |
     |                                  |-- server transform snapshot       |
     |                                  |-- oriented overlap exactly once   |
     |                                  |-- tag filter + duration GE        |
     |                                  |-- spawn replicated decal -------->|
     |<-------------------------- short confirmed decal -------------------|
     |                                  |-- ability ends immediately        |
```

좌클릭 확정은 Player Actor의 별도 RPC와 `WaitGameplayEvent` 조합을 사용하지 않는다. ASC의 Generic Confirm/Cancel 이벤트를 사용하므로 Ability Spec Handle과 Activation Prediction Key가 같이 전달된다. 빠른 클릭이 서버 Ability 활성화보다 먼저 도착하는 경우에도 GAS가 해당 활성화에 이벤트를 보관하고 연결한다.

서버가 신뢰하는 값은 서버 Player Transform과 서버 ASC Tag뿐이다. 클라이언트는 범위 Transform이나 Target 배열을 보내지 않는다.

## 4. 입력 수명주기

### 능력 키 Started

- `ABasePlayer::OnAreaSlowSkillPressed()`가 `Key.Skill.AreaSlow` 입력 ID를 누른다.
- `UGA_PlayerAreaSlow`는 `LocalPredicted`, `InstancedPerActor`로 활성화된다.
- `State.Aiming`과 `GameplayAbility.Skill.AreaSlow`를 임시로 소유한다.
- 소유 클라이언트에서만 `AAreaSlowTargetingDecal`을 생성한다.
- 서버 인스턴스나 Simulated Proxy는 조준 Decal을 만들지 않는다.

### 좌클릭

- Area Slow가 활성 상태이면 무기/기본 공격 입력으로 전달하지 않는다.
- `UAbilitySystemComponent::LocalInputConfirm()`을 호출한다.
- 로컬에서는 조준 Decal을 즉시 제거한다.
- 서버에서는 Commit과 스킬 재료 소비가 성공한 경우에만 판정한다.
- 유효 대상이 0명이어도 정상 사용으로 취급한다.

### 우클릭 또는 능력 키 Released/Canceled

- 확정 전이면 Ability를 취소한다.
- 조준 Decal과 임시 상태 Tag를 정리한다.
- 범위 검색, GE 적용, 확정 Decal, 사용 횟수 소비를 하지 않는다.

현재 프로젝트의 공용 Player Skill 정책에 맞춰 Area Slow는 기본 잠금 상태이며, 성공한 사용마다 `RareSkill` 재료 하나를 소비한다. 진행 시스템이 서버의 `UnlockSkill(GameplayAbility.Skill.AreaSlow)`를 호출하거나 개발용 bypass를 켜야 활성화할 수 있다.

## 5. Data Asset 계약

`UAreaSlowSkillDataAsset`의 모든 튜닝 값은 Data Asset 에디터에서 직접 수정할 수 있다.

| 그룹 | 필드 | 기본값 |
|---|---|---:|
| Range | `FrontGap` | 100 cm |
| Range | `RangeLength` | 800 cm |
| Range | `RangeWidth` | 500 cm |
| Range | `RangeHeight` | 300 cm |
| Range | `VerticalOffset` | 0 cm |
| Targeting | `TargetObjectTypes` | Pawn |
| Effect | `MoveSpeedMultiplier` | 0.5 |
| Effect | `AttackSpeedMultiplier` | 0.5 |
| Effect | `SlowDuration` | 4 s |
| Presentation | `DecalProjectionDepth` | 300 cm |
| Presentation | `ConfirmedDecalDuration` | 0.6 s |

기본 Required Query는 다음 세 태그를 모두 요구한다.

```text
ALL(
  Targetable.Skill.AreaSlow,
  Capability.Effect.MoveSpeedMultiplier,
  Capability.Effect.AttackSpeedMultiplier
)
```

기본 Blocked Query는 다음 중 하나라도 있으면 제외한다.

```text
ANY(
  Immunity.Debuff.Slow,
  State.Dead
)
```

`Targetable.Skill.AreaSlow`는 기획자가 명시적으로 선택하는 opt-in 태그다. 두 `Capability.Effect.*Multiplier` 태그는 해당 Actor의 이동 코드와 공격 코드가 공용 배율을 실제로 소비한다는 구현 계약이다. 선택 태그와 구현 태그를 분리하면 “선택 태그는 붙었지만 실제 시스템은 감속을 지원하지 않는” 오설정을 줄일 수 있다. Capability 두 개는 Required Query의 편집 여부와 무관하게 C++에서도 불변 조건으로 검사한다.

## 6. 대상 선택 규칙

후보는 아래 조건을 모두 만족해야 한다.

1. 서버의 1회 Collision Object Query에 검출된다.
2. 시전자와 동일 Actor가 아니다.
3. `ABasePlayer`가 아니다. 다른 플레이어도 항상 제외한다.
4. ASC를 가진다.
5. `UBaseAttributeSet::MoveSpeedMultiplier`와 `AttackSpeedMultiplier`를 모두 가진다.
6. Data Asset의 Required Query를 만족한다.
7. Data Asset의 Blocked Query를 만족하지 않는다.

`ABaseEnemy`는 시작할 때 이동/공격 Capability 태그를 모두 ASC에 자동으로 추가한다. 영향을 받게 할 Enemy Blueprint의 `Effect Target Tags`에 `Targetable.Skill.AreaSlow`를 추가하면 opt-in이 완료된다.

Ship에는 별도 하드 제외 분기가 없다. 현재 Ship 물리 코드가 이 공용 배율을 소비하지 않으므로 기본 상태에서는 opt-in 태그를 주지 않는다. 나중에 Ship 감속을 허용하려면 Ship 물리 입력에 배율을 연결하고 Capability와 Targetable 태그를 모두 부여한다. Ability 코드는 변경하지 않는다.

Pawn이 아닌 Actor를 지원할 때는 Data Asset의 `TargetObjectTypes`에도 해당 Collision Object Type을 추가해야 한다. Collision은 broad phase이고 Gameplay Tag Query가 최종 allow-list다.

## 7. 한 번만 캡처하는 사각 범위

범위는 Player Actor Yaw만 사용한다.

```text
Forward       = YawRotation.Vector
BoxExtent     = (Length / 2, Width / 2, Height / 2)
Center        = PlayerLocation
              + Forward * (FrontGap + Length / 2)
              + Up * VerticalOffset
BoxRotation   = (Pitch 0, PlayerYaw, Roll 0)
```

Hold 중 로컬 Decal과 Confirm 시 서버 Overlap은 동일한 Data Asset 수식으로 계산한다. 서버는 `OverlapMultiByObjectType`을 한 번 호출하고, 여러 Component로 중복 검출된 Actor는 `TSet<AActor*>`로 한 번만 처리한다.

Ability는 판정 후 즉시 종료하므로 다음 항목이 존재하지 않는다.

- 지속형 Field Actor
- Begin/End Overlap 대상 수명 관리
- Timer 또는 Tick 재검색
- 범위 이탈 시 즉시 복구
- Player/Ship 상대좌표로 범위를 계속 추적하는 로직

## 8. 감속과 자동 복구

`UBaseAttributeSet`의 공용 Attribute 기본값은 다음과 같다.

```text
MoveSpeedMultiplier = 1.0
AttackSpeedMultiplier = 1.0
Clamp = [0.1, 3.0]
Replication = RepNotify Always
```

`UAreaSlowGameplayEffect`는 다음 정책을 가진다.

```text
DurationPolicy = HasDuration
Duration       = Data Asset SlowDuration
Modifier 1     = MoveSpeedMultiplier / Multiplicative
Magnitude 1    = SetByCaller(Data.Effect.MoveSpeedMultiplier)
Modifier 2     = AttackSpeedMultiplier / Multiplicative
Magnitude 2    = SetByCaller(Data.Effect.AttackSpeedMultiplier)
Granted Tag    = State.Debuff.Slow
Stacking       = AggregateByTarget, Limit 1, Duration Refresh
```

`ABaseEnemy`의 최종 공식은 다음과 같다.

```text
BuffedSpeed = BaseMovementSpeed * SpawnMovementSpeedMultiplier
             + max(0, MoveSpeedBonus)

FinalSpeed  = BuffedSpeed * clamp(MoveSpeedMultiplier, 0.1, 3.0)
```

GE가 끝나면 GAS Aggregator가 두 Modifier와 `State.Debuff.Slow`를 함께 제거한다. Enemy Attribute Change Delegate가 그 시점의 Jog/Run 기준값으로 `MaxWalkSpeed`를 다시 계산하고, 실행 중인 기본 공격 몽타주도 Attribute 변화 비율로 즉시 정상 속도에 복귀한다. 따라서 원래 속도를 별도로 저장하거나 두 타이머를 따로 관리하지 않는다.

동일 능력에 다시 맞으면 감속을 곱해서 누적하지 않고 한 스택의 지속시간을 갱신한다.

## 9. Decal 정책

### Hold Preview

- 클래스: `AAreaSlowTargetingDecal`
- 생성 위치: 소유 클라이언트
- 복제: 꺼짐
- Tick: 켜짐, Player 전방 범위만 갱신
- Collision/Gameplay: 없음
- 수명: 키 해제, 우클릭, 좌클릭, Ability 종료 시 즉시 파괴

### Confirmed Visual

- 클래스: `AAreaSlowConfirmedDecal`
- 생성 위치: 서버
- 복제: 켜짐, 짧은 수명 동안 Always Relevant
- Tick: 꺼짐
- 복제 값: Box Extent, Projection Depth, Material, 초기 Spawn Transform
- Collision/Gameplay: 없음
- 수명: `ConfirmedDecalDuration` 후 서버 파괴

확정 Decal 수명과 Target 감속 수명은 완전히 분리된다. Decal이 0.6초 후 사라져도 Target의 4초 감속은 계속된다.

## 10. 실패 및 보안 정책

- Data Asset, 범위, GE Class, Object Type, Required Query가 잘못되면 fail closed로 실행을 거부한다.
- 서버만 Commit, 재료 소비, Overlap, 대상 필터, GE 적용을 수행한다.
- 클라이언트 조준 Decal은 시각 정보일 뿐 판정 권한이 없다.
- 대상마다 Multicast로 속도를 직접 변경하지 않는다.
- 활성 Effect Handle이나 Target 포인터를 Ability 종료 뒤 보관하지 않는다.
- Player 제외는 Team Tag에 의존하지 않고 타입으로 보장한다.

## 11. 구현 파일

### ArtisticSWCore

- `BaseGameplayTags.h/.cpp`: Ability, 입력, opt-in, capability, immunity, slow state, SetByCaller 태그

### GASCore

- `BaseAttributeSet.h/.cpp`: 이동/공격 배율과 복제/Clamp, 실행 중 공격 재생률 갱신
- `Effects/AreaSlowGameplayEffect.h/.cpp`: Duration/Stack/Multiplier GE

### ClassFeature

- `Skills/AreaSlowSkillDataAsset.h/.cpp`: Data Asset과 범위 순수 계산
- `Skills/AreaSlowDecalActors.h/.cpp`: 로컬 Preview와 복제 Confirmed Visual
- `Skills/Abilities/GA_PlayerAreaSlow.h/.cpp`: 입력 수명, 서버 1회 판정, GE 적용
- `BasePlayer.h/.cpp`: 전용 IA 바인딩과 GAS Confirm/Cancel 라우팅
- `Skills/PlayerSkillComponent.cpp`: 스킬/아이템/소모 재료 정의

### Enemy

- `BaseEnemy.h/.cpp`: opt-in Tag 설정, 이동/공격 Capability 선언, 공용 이동 배율 소비
- `RangedEnemy.cpp`: 감속 상태에서 새로 시작하는 원거리 기본 공격의 재생률에 공격 배율 반영
- `Tests/EnemyAbilityInfrastructureTests.cpp`: 감속 포함 최종 속도 공식 검증

## 12. 검증 기준

- Hold Preview는 소유 클라이언트에만 보인다.
- 좌클릭 후 Confirmed Decal은 모든 클라이언트에 짧게 보인다.
- Release/우클릭 취소는 비용과 판정을 발생시키지 않는다.
- 서버 Overlap은 사용당 한 번이다.
- 확정 이후 들어온 Actor는 적용되지 않는다.
- 범위를 벗어나도 대상별 Duration 동안 감속된다.
- opt-in과 capability를 모두 가진 대상만 적용된다.
- 모든 Player Character는 잘못 Tag를 붙여도 제외된다.
- 감속은 기존 Enemy 버프/Spawn 배율 및 다른 공격 속도 효과와 곱연산으로 조합된다.
- GE 제거/만료 시 이동 속도, 공격 속도, Slow 상태 Tag가 함께 복구된다.
- 동일 Actor의 여러 Collision Component가 중복 적용을 만들지 않는다.
- 클라이언트는 대상 목록이나 판정 Transform을 정할 수 없다.

## 13. 에디터에서 남은 콘텐츠 연결

C++는 특정 물리 키와 프로젝트 아트 Material을 임의로 선택하지 않는다. 따라서 아래 콘텐츠 연결은 프로젝트 기획 값으로 설정한다.

1. `UAreaSlowSkillDataAsset` 기반 `DA_AreaSlow` 생성
2. Targeting/Confirmed Decal Material 지정
3. `UGA_PlayerAreaSlow` Blueprint 자식 생성 후 `SkillData = DA_AreaSlow`
4. `BP_Player.AreaSlowAbilityClass`에 해당 GA Blueprint 지정
5. Area Slow용 Digital Input Action 생성
6. 원하는 물리 키로 `DefaultIMC`에 매핑
7. `BP_Player.AreaSlowSkillAction`에 해당 Input Action 지정
8. 영향을 받을 Enemy Blueprint의 `Effect Target Tags`에 `Targetable.Skill.AreaSlow` 추가
9. Pawn 이외 대상을 쓸 경우 Data Asset Object Types 확장 및 이동 배율 adapter 구현

구체적인 에디터 체크리스트는 같은 폴더의 `Area_Slow_Editor_Setup.md`를 따른다.
