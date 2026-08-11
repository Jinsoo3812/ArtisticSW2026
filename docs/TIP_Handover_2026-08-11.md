# ArtisticSW2026 — Turn In Place (TIP) 인수인계

작성일: 2026-08-11  
프로젝트: `ArtisticSW2026` / Unreal Engine 5.7  
대상 범위: `ABP_Player`의 Turn In Place(TIP) 재생, 재선택, 실제 Actor yaw 적용

> 이 문서는 과거 대화의 추측을 재사용하지 않는다. 아래 내용은 현재 워킹 트리의 코드와 에디터 연결을 기준으로 한다.

## 1. 프로젝트의 애니메이션 계약

이 프로젝트는 **항상 Strafe**다. Project_J의 비전투 OTM(Orient To Movement) 구조를 복사하면 안 된다. Project_J의 방향 양자화, delta-yaw, Chooser/Blend Stack 사용 방식은 참고하되, 최종 판단 기준은 Artistic의 Strafe 입력·Control yaw·Actor yaw이다.

별도의 UE `State Controller` AnimGraph 노드나 컴포넌트는 사용하지 않는다. 코드에 남은 `StateController` 이름은 GASP의 presentation-state 개념을 C++로 구현한 레거시 명칭이다.

```text
ABasePlayer
  └─ ULocomotionAnimStateComponent
       - 입력/속도/공중/착지/Turn In Place 요청을 수집
       - DesiredFacingDeltaYaw = ControlYaw - ActorYaw

UMotionMatchingAnimInstance (ABP_Player의 부모)
  └─ C++ presentation state + Chooser 평가
       - Idle/Locomotion: Motion Matching PSD
       - Start/Stop/Pivot/Jump/FallOff/Land/TIP: Chooser -> Blend Stack one-shot

ABP_Player AnimGraph
  └─ Blend Stack one-shot pose <-> Motion Matching pose
       - Blend Poses by Bool
       - Active Value = GetThreadSafeShouldOverrideMotionMatching()
```

One-shot state는 `EStateControllerPresentationState`로 표현된다.

```text
IdleLoop, TransitionToStart, LocomotionLoop, TransitionToStop,
TransitionToPivot, TransitionToJump, TransitionToLand, TurnInPlace
```

## 2. 관련 파일과 역할

| 파일 | 역할 |
|---|---|
| `Source/ClassFeature/Private/Animation/MotionMatchingAnimInstance.cpp` | presentation state, TIP Chooser 선택, one-shot hold, worker-thread 전달값, Chooser 결과 |
| `Source/ClassFeature/Public/Animation/MotionMatchingAnimInstance.h` | Thread-safe getter, TIP tuning 값, runtime state 선언 |
| `Source/ClassFeature/Private/BasePlayer.cpp` | `ApplyCombatTurnInPlaceRotation()`에서 Blend Stack TIP 시퀀스 root yaw를 직접 추출·Actor에 적용 |
| `Source/ClassFeature/Public/BasePlayer.h` | TIP 함수, yaw RPC 선언 |
| `Source/ClassFeature/Private/Animation/LocomotionAnimStateComponent.cpp` | 이동 상태 및 rotation policy. TIP 중 controller desired rotation을 끄는 정책이 있음 |
| `Source/ClassFeature/Public/Animation/LocomotionAnimStateComponent.h` | `DesiredFacingDeltaYaw`, `bShouldTurnInPlace`, `TurnInPlaceRootYawDelta` 등 데이터 |
| `Content/Anim_Logic/ABP_Player.uasset` | Blend Stack, Motion Matching, Blend Poses by Bool, OnUpdate_0 Anim Node Function |
| `Content/Anim_Logic/Choosers/CHT_Player_TurnInPlace.uasset` | TIP 90/180 L/R 애셋을 고르는 Chooser |

참조 프로젝트:

```text
C:\Users\I\Documents\GitHub\Project_J
```

Project_J는 MMORPG 구조이며 비전투 OTM과 전투 Strafe가 공존한다. Artistic은 항상 Strafe이므로 Project_J의 **전투 Strafe** 방향/회전 부분만 동등 비교 대상으로 삼는다.

## 3. 현재 ABP_Player 연결 (필수 유지)

### 3.1 메인 one-shot / Motion Matching 분기

```text
Get Thread Safe State Controller Selected Animation
  -> Blend Stack.Animation Asset

Get Thread Safe State Controller Selected Animation Start Time
  -> Blend Stack.Animation Time

Get Thread Safe State Controller Selected Animation Should Loop
  -> Blend Stack.Loop

Get Thread Safe State Controller Selected Animation Blend Time
  -> Blend Stack.Blend Time

Get Current Active Pose Search Database Thread Safe
  -> Motion Matching.Database

Get Thread Safe Should Override Motion Matching
  -> Blend Poses by Bool.Active Value
```

`True Pose`는 Blend Stack(one-shot), `False Pose`는 Motion Matching이다. 전환 시간만 키워서 one-shot의 재선택·Actor 회전 문제를 해결하려 하면 안 된다.

### 3.2 Blend Stack의 `OnUpdate_0` (이미 연결됨)

Blend Stack node Details의 **On Update**는 `OnUpdate_0`여야 한다. 함수 그래프는 다음과 같이 연결돼 있다.

```text
OnUpdate_0.Node
  -> Convert to Blend Stack Node
      Succeeded -> Branch
        Condition = Get Thread Safe State Controller Force Blend Stack On Next Update
        True -> Force Blend On Next Update(변환된 Blend Stack Node)
        False -> Return
      Failed -> Return
```

`Context`는 이 호출에는 필요 없다. `Blend To`를 On Update에서 매 프레임 호출하면 재생 시간이 계속 초기화되므로 절대 대체하지 말 것.

UE 5.7 엔진 API 위치:

```text
Engine/Plugins/Animation/BlendStack/Source/Runtime/Public/BlendStack/BlendStackAnimNodeLibrary.h
```

이 API의 `Force Blend On Next Update`는 같은 `UAnimationAsset`이 다시 선택되었을 때 Blend Stack이 기존 시간 커서를 유지하지 않고 새 player를 만들게 한다.

### 3.3 후처리 노드의 원칙

- Offset Root Bone, Orientation Warping, Steering은 전체 locomotion 품질 보정용이다.
- TIP의 gameplay/capsule yaw는 **Offset Root Bone이 아니라 C++의 직접 Actor rotation**이 소유한다.
- TIP 중 `bUseControllerDesiredRotation` 또는 `bOrientRotationToMovement`가 Actor yaw를 다시 덮어쓰지 않도록 `ULocomotionAnimStateComponent`에서 rotation policy를 제어한다.
- Root Motion Mode를 `Root Motion from Everything`으로 바꾸지 말 것. 현재 C++에서 root transform을 직접 추출하여 적용하고 있으므로 중복 회전 위험이 있다.

## 4. TIP 선택과 재선택 계약

### 4.1 최초 TIP 선택

`ABasePlayer::ApplyCombatTurnInPlaceRotation()`이 매 tick 실행되고, 정지/지상/비행동 상태에서 `abs(ControlYaw - ActorYaw) > 75`이면:

```text
AnimStateComponent->bShouldTurnInPlace = true
AnimStateComponent->DesiredFacingDeltaYaw = ControlYaw - ActorYaw
```

`UMotionMatchingAnimInstance::EvaluateStateControllerPresentationState()`가 `TurnInPlace`를 요구하고, `EvaluateStateControllerPlaybackHold()`가 `CHT_Player_TurnInPlace`를 평가한다.

현재 TIP index 계약:

| Index | 방향 | 요구 semantic yaw |
|---:|---|---:|
| 0 | 없음 / 임계값 미만 | 0 |
| 1 | Left 90 | -90 |
| 2 | Left 180 | -180 |
| 3 | Right 90 | +90 |
| 4 | Right 180 | +180 |

Chooser에는 이 계약과 일치하는 4개 계열의 Sequence가 반드시 있어야 한다. 에셋 명칭의 L/R 및 root motion 부호도 실제 root track과 검증할 것.

### 4.2 같은 애셋 재선택

이미 TIP 중인 상태에서 다음 중 하나가 성립하면 재선택한다.

- 반대 방향 회전 요청이 `StateControllerTurnInPlaceReverseDirectionReselectAngle` 이상
- 같은 방향 요청이 최초 선택 yaw보다 `StateControllerTurnInPlaceSameDirectionReselectAngle` 이상 더 커짐
- 클립 말미까지 왔는데 같은 방향 요청이 남음

현재 기본 tuning:

```text
StateControllerTurnInPlaceReselectMinElapsed = 0.20 s
StateControllerTurnInPlaceSameDirectionReselectAngle = 40 deg
StateControllerTurnInPlaceReverseDirectionReselectAngle = 55 deg
StateControllerTurnInPlaceDefaultBlendTime = 0.06 s
```

같은 sequence가 또 선택될 때는 C++가 한 AnimGraph update 동안 `bForceBlendStackOnNextUpdate=true`를 publish한다. 위의 `OnUpdate_0`가 이를 소비한다.

**중요:** 이 값은 한 프레임 pulse다. 에디터 Blueprint pin watch에서 계속 false로 보이는 것은 정상이다. `[SC_TIP_FORCE] Emit=1` 로그가 진짜 판정이다.

최근 검증 결과:

```text
[SC_TIP_FORCE] ... Emit=1 StateChanged=0 Previous=TurnInPlace Desired=TurnInPlace
```

따라서 Blend Stack 재시작 연결은 정상이다. 남은 핵심 버그는 실제 Actor yaw가 90도까지 도달하지 않는 문제다.

## 5. 현재 TIP Actor yaw 구현

구현 위치: `ABasePlayer::ApplyCombatTurnInPlaceRotation()` (`BasePlayer.cpp` 약 2352행).

Blend Stack 재생은 일반 Montage처럼 root motion을 CharacterMovement에 자동 적용하지 않는다. 그래서 선택된 `UAnimSequence`의 root transform을 직접 샘플링한다.

```text
Selected Sequence + StartTime + StateController elapsed time
  -> ExtractRootMotionFromRange(Start, Previous)
  -> ExtractRootMotionFromRange(Start, Current)
  -> cumulative root yaw 차이
  -> Chooser semantic yaw(-90/-180/+90/+180)에 맞춘 scale
  -> 현재 FacingDelta를 넘지 않도록 clamp
  -> SetActorRotation(ActorYaw + ClampedRootYawDelta)
  -> autonomous client는 같은 delta를 Server RPC로 전송
```

이 방식은 애니메이션 curve를 임의로 해석하는 방식보다 낫다. 실제 root track의 누적 delta를 읽으므로 90/180마다 제작된 root motion을 그대로 사용한다. 다만 현재 90도 도달 실패는 아래 항목 중 하나일 수 있으므로 로그 없이 root-motion mode나 Offset Root Bone 구조를 바꾸지 말 것.

1. 선택된 시퀀스의 root yaw가 예상 부호/총량과 다름
2. `StateControllerPlaybackHoldElapsed`와 Blend Stack의 실제 player time이 어긋남
3. `FacingDeltaYaw` clamp가 너무 이르게 0에 가까워짐
4. `SetActorRotation` 후 CharacterMovement / Controller rotation / 네트워크 보정이 yaw를 덮어씀
5. 재선택이 너무 잦아 clip cursor가 중간 전에 계속 재시작됨

## 6. 진단 방법 (먼저 실행, 바로 구조 변경 금지)

콘솔:

```text
a.StateControllerDebug 1
```

90도 TIP를 한 번 발동하고 마우스를 멈춘 뒤 아래 로그를 같은 시간 순서로 확보한다.

| 로그 | 의미 |
|---|---|
| `[SC_TIP_RESELECT]` | 재선택 gate가 왜 열렸는지 |
| `[SC_TIP_FORCE]` | `Force Blend On Next Update` pulse 발행 여부 |
| `[SC_TIP_ROOT]` | root 누적 yaw, scale, clamp, Actor 적용 전/후 |
| `[SC_TIP_NET]` | client/server yaw delta 적용 |
| `[SC_TIP]` | Chooser asset/index 및 presentation 상태 |

`SC_TIP_ROOT`에서 우선 비교할 값:

```text
Seq, Clock, Prev, Curr, Len, Start,
Index, Semantic, AuthoredTotal, Scale,
CumPrev, CumCurr, RawDelta, ScaledDelta,
FacingBefore, Clamped, ActorBefore, ActorAfter, Remaining, Cannot
```

판정표:

| 관찰 | 다음 조치 |
|---|---|
| `RawDelta`/`ScaledDelta`가 계속 0 | 시퀀스 root track 또는 시계(clock) 문제. 해당 애셋 root motion을 검사 |
| `ScaledDelta`는 있는데 `Clamped=0` | facing delta 산출/회전 정책이 잘못됨 |
| `ActorAfter`는 바뀌는데 다음 샘플의 `ActorBefore`가 되돌아감 | CharacterMovement, Controller, replication 중 덮어쓰기 주체를 찾기 |
| `ActorBefore/After`가 정상 누적되지만 화면만 어색함 | mesh/OffsetRootBone/Steering 시각 보정 문제를 별도 조사 |
| `SC_TIP_FORCE`가 과도하게 연속 발생 | 재선택 threshold/clip-finished 조건을 완화하고 재선택 반복을 멈춤 |

## 7. 현재 코드 변경 상태

아래 변경은 아직 워킹 트리에 있으며, 사용자 asset 변경과 섞여 있다. 무단 reset/revert/checkout 금지.

```text
Content/Anim_Logic/ABP_Player.uasset                 (사용자 에디터 연결)
Content/Anim_Logic/Choosers/CHT_Player_TurnInPlace.uasset
Content/New/Level/Test_Level.umap
Source/ClassFeature/Private/Animation/MotionMatchingAnimInstance.cpp
Source/ClassFeature/Private/BasePlayer.cpp
Source/ClassFeature/Public/Animation/MotionMatchingAnimInstance.h
Source/ClassFeature/Public/BasePlayer.h
```

2026-08-10에 UBT 빌드 성공 확인:

```text
ArtisticSW2026Editor Win64 Development
```

## 8. 안전한 다음 작업 순서

1. `SC_TIP_ROOT` + `SC_TIP_NET` 로그로 90도 누락의 정확한 층을 확정한다.
2. 하나의 원인에만 최소 수정한다. 재선택 문제와 rotation 문제를 동시에 바꾸지 않는다.
3. 90L/90R/180L/180R, 재회전, 멀티플레이 클라이언트/서버를 각각 확인한다.
4. Unreal Editor/Live Coding/UBT가 실행 중이면 다른 build를 시작하지 않는다.
5. 빌드는 직접 UBT로 실행한다.

```powershell
& 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe' `
  ArtisticSW2026Editor Win64 Development `
  '-Project=C:\Users\I\Documents\GitHub\ArtisticSW2026\ArtisticSW2026.uproject' -WaitMutex
```

## 9. 하지 말아야 할 것

- Notify로 TIP/land/start/stop 전환을 구현하지 말 것.
- 모든 one-shot을 Motion Matching PSD로 되돌리지 말 것.
- always-Strafe 프로젝트에 OTM direction 규칙을 이식하지 말 것.
- `Root Motion from Everything`을 검증 없이 켜지 말 것. C++ root yaw가 중복 적용될 수 있다.
- `Blend To`를 Blend Stack `OnUpdate`에서 매 프레임 호출하지 말 것.
- pulse bool이 핀 watch에서 false라는 이유만으로 Force 연결을 제거하지 말 것.
- 사용자 asset/uasset 및 unrelated 변경을 reset/revert/checkout하지 말 것.
