# Weapon Sound and Trail Editor Setup Guide

## 1. 구현 구조

공격 몽타주는 실제 Sound/Niagara Asset을 직접 소유하지 않는다.

1. `AN_WeaponSwingSound`가 현재 보이는 장착 무기의 `UWeaponFeedbackComponent`를 찾는다.
2. Component가 등록된 `UWeaponFeedbackDataAsset`에서 Sound Set을 골라 재생한다.
3. `ANS_WeaponTrail`의 Begin/Tick/End가 동일한 Component의 Trail을 시작, 갱신, 종료한다.
4. 플레이어 `ABaseItem` 계열과 적 `ABaseWeapon` 계열 모두 같은 Component와 Data Asset을 사용한다.
5. Dedicated Server에서는 Sound와 Niagara를 생성하지 않는다.

검은 `ASwordItem`의 기존 `TraceStartPoint`와 `TraceEndPoint`를 Endpoint Parameter 방식의 Trail 위치로 자동 사용한다.

## 2. Content 폴더 준비

새 프로젝트 전용 Asset은 다음 경로 구성을 권장한다.

```text
/Game/Combat/WeaponFeedback/
    Data/
    Audio/
    Niagara/
```

외부 팩 원본인 `/Game/SoundEffect`와 `/Game/SwordTrailVFX`는 그대로 두고, 프로젝트에서 수정할 Sound Cue, Niagara System, Material Instance만 위 폴더로 복제해 사용하면 팩 업데이트와 충돌하지 않는다.

## 3. Weapon Feedback Data Asset 생성

1. Content Browser에서 `/Game/Combat/WeaponFeedback/Data`로 이동한다.
2. 빈 공간 우클릭 후 `Miscellaneous > Data Asset`을 선택한다.
3. 클래스 선택 창에서 `WeaponFeedbackDataAsset`을 선택한다.
4. 이름을 `DA_WF_Sword_Default`로 지정한다.

이 Data Asset 하나가 검의 Swing Sound와 Trail 설정을 함께 보관한다.

## 4. Swing Sound 등록

`DA_WF_Sword_Default`의 `Weapon Feedback > Sound > Swing Sound Sets`에 원소를 추가한다.

### 가장 단순한 설정

```text
Sound Set Name: Default
Sounds:
    MOVEActv_Short_HA_WHOOSH_01_Cue
    MOVEActv_Short_HA_WHOOSH_08_Cue
    MOVEActv_Short_HA_WHOOSH_11_Cue
Volume Multiplier: 1.0
Pitch Range: X=0.96, Y=1.04
```

현재 가져온 Cue는 `/Game/SoundEffect/WhooshSFXPackLite/Cues` 아래에 있다. Wav보다 Cue를 우선 등록하면 Cue 내부 설정도 함께 사용할 수 있다.

### 콤보마다 다른 소리를 사용할 경우

다음과 같이 여러 Sound Set을 추가한다.

```text
Combo1: Short/Standard 계열
Combo2: Medium/Standard 계열
Combo3: Medium/Heavy 계열
Combo4: Long/Heavy 계열
```

`Sound Set Name`은 몽타주의 `Weapon Swing Sound` Notify에 입력하는 이름과 대소문자까지 같아야 한다. 요청한 이름이 없으면 `Default` Set으로 폴백한다.

`Attenuation Settings`와 `Concurrency Settings`는 선택 사항이다. 멀티플레이에서는 별도의 Sound Attenuation과 Sound Concurrency Asset을 만들어 등록하는 것을 권장한다.

## 5. Trail Niagara 등록

`DA_WF_Sword_Default`의 `Weapon Feedback > Trail`을 설정한다.

### 방법 A: 가져온 SwordTrailVFX 팩 사용

현재 `/Game/SwordTrailVFX/VFX`에 `NS_Trail_01`부터 `NS_Trail_18`까지 존재한다.

```text
Niagara System: 원하는 NS_Trail_xx
Placement Mode: Attached System
Attach Socket Name: None
Component Scale: (1, 1, 1)
Relative Location: (0, 0, 0)
Relative Rotation: (0, 0, 0)
```

`Attached System`은 Niagara를 장착된 무기의 Root에 붙인다. 외부 팩의 방향이나 크기가 현재 검 Mesh와 다르면 `Relative Location`, `Relative Rotation`, `Component Scale`로 조정한다.

### 방법 B: 검날 양 끝을 사용하는 커스텀 Niagara

직접 만든 Niagara Ribbon이 두 위치를 입력받도록 설계된 경우 사용한다.

```text
Placement Mode: Endpoint Parameters
Start Position Parameter: User.TrailStart
End Position Parameter: User.TrailEnd
```

Niagara System 안에는 반드시 다음 Position 또는 Vector User Parameter가 있어야 한다.

```text
User.TrailStart
User.TrailEnd
```

두 값은 World Space로 전달된다. Emitter의 `Local Space`를 끄고 두 Parameter를 Ribbon 생성 위치 계산에 사용한다. 플레이어 검은 Blueprint의 `TraceStartPoint`와 `TraceEndPoint` 위치가 자동 연결되므로 별도 Mesh Socket이 필요 없다.

검 Blueprint에서 다음 위치를 확인한다.

- `TraceStartPoint`: 손잡이 바로 위 검날 시작점
- `TraceEndPoint`: 검 끝점

검이 아닌 공통 무기 클래스가 Endpoint Component를 제공하지 않으면 Data Asset의 `Start Socket Name`, `End Socket Name`에 Static/Skeletal Mesh Socket을 등록한다.

## 6. 검 Blueprint에 Data Asset 등록

1. `/Game/GameplayAbilitySystem/Weapon/BP_BaseSword`를 연다.
2. Components 패널에서 상속된 `WeaponFeedbackComponent`를 선택한다.
3. Details의 `Weapon Feedback > Feedback Data`에 `DA_WF_Sword_Default`를 지정한다.
4. Compile 후 Save한다.
5. 실제 Item Definition의 `Spawn Class`가 다른 검 Blueprint를 사용한다면 그 Blueprint에도 같은 작업을 한다.

현재 `BP_BaseSword`와 `BP_BaseSword1`이 모두 존재하므로 플레이 중 실제 생성되는 Spawn Class를 확인해야 한다. 두 Blueprint가 서로 다른 검이라면 각각 `DA_WF_Sword_A`, `DA_WF_Sword_B`처럼 별도 Data Asset을 등록할 수 있다.

적 검은 `/Game/GameplayAbilitySystem/Enemy/Weapon/BP_Sword`의 `WeaponFeedbackComponent`에 동일한 방식으로 등록한다.

## 7. 공격 몽타주에 Sound Notify 배치

1. `/Game/Sword_Anims/.../ComboAttack/AM_SwordCombo`를 연다.
2. Notify Track을 하나 추가하고 이름을 `WeaponSound`로 지정한다.
3. 각 Combo Section에서 검의 속도가 가장 빨라지는 프레임을 찾는다.
4. 해당 위치를 우클릭하고 `Add Notify > Weapon Swing Sound`를 선택한다.
5. Notify를 선택하고 Details에서 `Sound Set Name`을 지정한다.

예시:

```text
첫 번째 Section: Combo1
두 번째 Section: Combo2
세 번째 Section: Combo3
마지막 Section: Combo4
```

Data Asset에 `Default`만 만들었다면 모든 Notify의 이름을 `Default`로 둔다.

기존 `Play Sound` Notify가 같은 위치에 있다면 제거하거나 비활성화해서 중복 재생을 막는다.

## 8. 공격 몽타주에 Trail Notify State 배치

1. 같은 몽타주에 Notify Track을 추가하고 이름을 `WeaponTrail`로 지정한다.
2. 우클릭 후 `Add Notify State > Weapon Trail`을 선택한다.
3. State 시작점을 기존 `Hit Scan Window`보다 1~2프레임 앞에 둔다.
4. State 종료점을 `Hit Scan Window`보다 1~2프레임 뒤에 둔다.
5. 모든 Combo Section에 각각 배치한다.

권장 순서:

```text
Trail Begin
    -> Swing Sound
        -> Hit Scan Window Begin
        -> Hit Scan Window End
Trail End
```

기존 `Timed Niagara Effect`가 같은 구간에 있다면 제거해야 Trail이 두 번 생성되지 않는다.

## 9. PIE 확인 순서

1. Standalone PIE에서 검을 장착한다.
2. 빗나가는 공격에서도 Swing Sound와 Trail이 나오는지 확인한다.
3. 콤보 입력마다 Sound가 정확히 한 번씩 재생되는지 확인한다.
4. 공격 취소 또는 장비 해제 시 Trail이 남지 않는지 확인한다.
5. Listen Server 2 Players로 실행한다.
6. 각 플레이어가 자기 공격과 상대 공격의 Sound/Trail을 모두 보는지 확인한다.
7. Dedicated Server에서는 cosmetic component가 생성되지 않는지 로그와 프로파일러로 확인한다.

## 10. 문제 해결

### Sound가 재생되지 않음

- 검 Blueprint의 `WeaponFeedbackComponent > Feedback Data`가 비어 있지 않은지 확인한다.
- Notify의 `Sound Set Name`과 Data Asset 이름이 같은지 확인한다.
- 장착된 검 Actor가 Hidden 상태가 아닌지 확인한다.
- 몽타주 미리보기에서는 장착 무기 Actor가 없으므로 PIE에서 확인한다.

### Trail이 보이지 않음

- `Niagara System`이 등록되어 있는지 확인한다.
- 가져온 팩은 먼저 `Attached System`으로 시험한다.
- `Endpoint Parameters` 사용 시 Niagara에 두 User Parameter가 실제 존재하는지 확인한다.
- `TraceStartPoint`, `TraceEndPoint`가 같은 위치에 놓여 있지 않은지 확인한다.
- Niagara Emitter의 Local Space 설정을 확인한다.

### Trail 방향이 틀어짐

- `Attached System`의 `Relative Rotation`을 조정한다.
- 필요하면 외부 Niagara System을 프로젝트 폴더로 복제한 뒤 축 방향을 현재 검 Mesh 기준으로 수정한다.

### 멀티플레이에서 두 번 들림

- 같은 프레임에 기존 `Play Sound`와 `Weapon Swing Sound`가 함께 있는지 확인한다.
- 공격 Animation Sequence와 이를 사용하는 Montage 양쪽에 동일 Notify가 중복되어 있지 않은지 확인한다.
