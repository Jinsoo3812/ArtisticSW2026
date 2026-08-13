# ArtisticSW2026 — Project_J식 TIP 위상 이식 기록

작성일: 2026-08-13  
범위: 항상 Strafe인 `ABP_Player`의 Turn In Place(TIP) 재생, 재선택, Actor yaw, Offset Root Bone

## 결론

Artistic의 TIP는 `30도 이상이면 재생, 30도 미만이면 즉시 Idle`이 아니어야 한다.  
Project_J와 동일하게 다음 두 단계를 분리한다.

1. **진입 요청(raw request)**: 정지 상태에서 Control/Actor yaw 차이가 30도 이상일 때 TIP 시작.
2. **파생 TIP 위상(phase)**: 이미 TIP 중이면 잔여 yaw가 5도보다 크고 최대 1.5초 동안 유지.

이 위상이 있어야 090 클립의 실제 root yaw가 대략 45도인 경우에도, 다음 재선택까지 Direct Blend Stack과 Steering/Offset Root가 중간에 끊기지 않는다.

## 왜 기존 방식이 문제였나

`ABasePlayer::UpdateCombatTurnInPlaceRequest()`가 매 프레임 30도 조건으로 `bShouldTurnInPlace`를 덮어썼다. 첫 090 root track이 적용된 뒤 잔여 각도가 30도 아래로 내려가면 다음 프레임에:

```text
TIP direct Blend Stack 해제
-> Motion Matching Idle 노출
-> Offset Root Bone Release
-> 시각적인 회전이 짧게 끝나거나 Idle 자세로 복귀
```

이는 Project_J의 `ApplyLocomotionPhaseStability()`가 하는 `TurnInPlace` 파생 위상 유지가 Artistic에 없어서 생긴 차이다.

## 현재 런타임 구조

```text
ABasePlayer::Tick
  ├─ UpdateCombatTurnInPlaceRequest(DeltaTime)
  │    └─ 30° 진입 / 5°·1.5초 유지 위상 계산
  ├─ ULocomotionAnimStateComponent::UpdateAnimationState
  ├─ ApplyCombatRotationMode
  └─ ApplyCombatTurnInPlaceRotation
       └─ 선택된 TurnInPlace 시퀀스의 이번 프레임 root yaw를 Actor에 적용

UMotionMatchingAnimInstance::EvaluateStateControllerPresentationState
  ├─ TIP phase가 true면 TurnInPlace Chooser 선택
  ├─ 0.75초 또는 L/R/90/180 bucket 변경 시 재선택
  └─ Direct Blend Stack이 Motion Matching을 일시 대체

ABP_Player
  ├─ Blend Stack: Direct one-shot (TIP 포함)
  ├─ Motion Matching: Idle/locomotion loop
  └─ Blend Poses by Bool: Should Override Motion Matching으로 둘 중 하나 선택
```

## C++ 계약

### `ABasePlayer`

파일:

- `Source/ClassFeature/Private/BasePlayer.cpp`
- `Source/ClassFeature/Public/BasePlayer.h`

`UpdateCombatTurnInPlaceRequest(float DeltaTime)`:

- 진입 조건: locally owned, 정지, 공격/회피/피격 아님, 공중/착지 아님, `abs(FacingDelta) >= 30`.
- 유지 조건: 기존 TIP phase, 위의 안전 조건 유지, `abs(FacingDelta) > 5`, 경과 시간 `< 1.5초`.
- 결과는 `ULocomotionAnimStateComponent::bTurnInPlacePhaseActive`와 `bShouldTurnInPlace`에 기록한다.

`ApplyCombatTurnInPlaceRotation(float DeltaTime)`:

- `PresentationState == TurnInPlace`일 때만 실행한다.
- raw 30도 요청을 추가 조건으로 사용하지 않는다. 즉, 위상 유지 중인 클립의 잔여 root yaw도 끝까지 적용한다.
- `UAnimSequence::ExtractRootMotionFromRange(PrevTime, CurrTime)`의 yaw만 Actor에 적용한다.
- 적용량은 현재 `DesiredFacingDeltaYaw`를 넘지 않도록 clamp한다.

### `ULocomotionAnimStateComponent`

파일:

- `Source/ClassFeature/Public/Animation/LocomotionAnimStateComponent.h`

새 상태:

- `bTurnInPlacePhaseActive`
- `TurnInPlacePhaseElapsed`

이들은 gameplay phase이며 AnimGraph 노드나 별도 `StateControllerComponent`가 아니다.

### `UMotionMatchingAnimInstance`

파일:

- `Source/ClassFeature/Private/Animation/MotionMatchingAnimInstance.cpp`
- `Source/ClassFeature/Public/Animation/MotionMatchingAnimInstance.h`

- TIP chooser index는 active phase에서 잔여 yaw가 5도보다 큰 동안 계속 계산한다.
- 재선택은 0.75초 경과 또는 semantic bucket 변경(L/R, 090/180)에서 발생한다.
- 같은 TIP asset도 재선택될 수 있으므로 `Force Blend On Next Update` pulse를 Blend Stack `On Update`에서 소비한다.
- `GetThreadSafeOffsetRootRotationMode()`은 선택된 `TurnInPlace` presentation 동안 `Interpolate`를 반환한다. 일반 Idle/Loop/공중은 `Release`다.

## ABP_Player 에디터 연결

### Main AnimGraph

Direct branch:

```text
Get Thread Safe State Controller Selected Animation
Get Thread Safe State Controller Selected Animation Start Time
Get Thread Safe State Controller Selected Animation Should Loop
Get Thread Safe State Controller Selected Animation Blend Time
    -> Blend Stack
```

`Blend Poses by Bool`은 `Get Thread Safe Should Override Motion Matching`으로 Direct Blend Stack과 Motion Matching을 선택한다.

Blend Stack의 `On Update` 함수:

```text
Node
 -> Convert to Blend Stack Node
 -> Branch(Get Thread Safe State Controller Force Blend Stack On Next Update)
 -> True: Force Blend On Next Update
```

이 bool은 한 프레임 pulse다. Blueprint pin watch에서 계속 false로 보이는 것은 정상이다.

### Blend Stack Input graph

```text
Blend Stack Input
 -> Local To Component
 -> Orientation Warping
 -> Steering
 -> Component To Local
```

- Orientation Warping은 `Enable_Warping` curve를 이용한다. 이 프로젝트에서는 대각 Jump Start/Land용이며 TIP의 회전량을 만들지 않는다.
- Steering alpha는 `enable_turninplacesteering` curve × `Get Thread Safe State Controller Turn In Place Steering Alpha`.
- Steering target orientation은 `Get Thread Safe State Controller Blend Stack Steering Target Orientation`.
- curve 이름은 대소문자를 구분한다. 현재 실제 애셋 이름인 소문자 `enable_turninplacesteering`을 사용한다.

### Offset Root / IK

`Offset Root Bone`은 Slot 이후, Foot Placement 이전에 둔다. 회전 mode는 `Get Thread Safe Offset Root Rotation Mode`에 연결한다.

- Foot Placement Alpha는 TIP 중 0으로 내려서 발 고정이 root 회전과 싸우지 않게 한다.
- `GetThreadSafeLegIKAlpha()`는 제거됐다. Leg IK Alpha에 이 getter가 남아 있다면 연결을 끊고 기본값 `1.0`으로 둔다.
- Leg IK를 아예 그래프에서 제거할지는 발 접지 품질을 별도로 보고 결정한다. TIP 개선만을 이유로 노드 자체를 지우지 않는다.

## 항상 Strafe 프로젝트에서 지켜야 할 것

- 이동 중 마우스 회전은 TIP가 아니라 strafe locomotion/warping이 담당한다.
- TIP는 무입력·저속·지상·비공중·비착지에서만 작동한다.
- Jump/Land와 대각 warping은 `Enable_Warping` 경로를 유지한다.
- `bUseControllerDesiredRotation`은 꺼둔다. stationary TIP의 Actor yaw를 CMC가 다음 프레임에 덮어쓰면 안 된다.
- `Root Motion from Everything`을 검증 없이 켜지 않는다. Direct root yaw 적용과 중복될 수 있다.

## 2026-08-13: Project_J 소유권 구조로 정리

Artistic의 기존 `ELocomotionState::TurnInPlace`는 이전 상태에서 다음 프레임에 Idle로
복귀하는 legacy 상태다. Project_J의 TIP는 별도 locomotion state가 아니라 `Idle/Stop`
위에서 유지되는 **derived presentation phase**다.

따라서 Artistic도 다음과 같이 책임을 분리했다.

```text
LocomotionAnimStateComponent
  stationary + facing delta(30°) -> TIP phase 시작
  residual yaw(>5°), 최대 1.5초 -> TIP phase 유지
  air / land / move input / action -> phase 취소

MotionMatchingAnimInstance
  TIP phase -> Chooser 090/180 선택
  0.75초 또는 방향 bucket 변경 -> 재선택 + Force Blend

BasePlayer
  현재 선택된 TIP Sequence의 root yaw만 capsule에 적용
```

이제 `BasePlayer`는 TIP 요청을 직접 생성하거나 유지하지 않는다. 이로써 캐릭터 Tick과
로코모션 상태 갱신이 서로 다른 프레임의 TIP 조건을 보는 구조를 제거했다.

## 디버깅

콘솔:

```text
a.StateControllerDebug 1
```

핵심 로그:

| 로그 | 확인할 내용 |
|---|---|
| `[SC_TIP_ROOT]` | Seq, Clock, RootDelta, Facing, Applied, Remaining. Actor yaw가 실제로 누적되는지 |
| `[SC_TIP_RESELECT]` | 0.75초 재선택 또는 bucket 변경 여부 |
| `[SC_TIP_FORCE]` | Blend Stack 강제 재블렌드 pulse 발행 여부 |
| `[SC_TIP]` | Chooser가 고른 090/180 L/R asset과 presentation state |

### 90도 회전 한 번이 짧게 끝날 때

`[SC_TIP_ROOT]`에는 다음 값을 우선 본다.

| 값 | 뜻 | 판정 |
|---|---|---|
| `Clock=현재/길이` | State Controller playback clock과 시퀀스 길이 | `현재`가 짧은 시간에 0으로 돌아가면 재선택/상태 전환 문제 |
| `AppliedTotal` | 이번 선택 revision이 Actor에 누적한 yaw | 090 clip 하나가 실제로 약 45도만 주는지 확인 |
| `Phase`, `PhaseTime`, `RawEntry` | 30도 raw 진입과 5도/1.5초 continuation phase | `RawEntry=0`이어도 `Phase=1`이면 Project_J식 정상 유지 중 |
| `Remaining` | Actor yaw 적용 뒤 남은 control-facing yaw | 45도 남았는데 다음 revision이 없으면 재선택 gate 문제 |
| `CMCDesired`, `OrientMove` | CMC가 Actor yaw를 덮어쓸 가능성 | TIP 중에는 둘 다 0이어야 정상 |
| `MeshVsActor`, `RootVsActor` | 캡슐과 메시/루트본의 현재 시각 yaw 차이 | Offset Root Bone과 Steering이 실제로 시각 보정을 유지하는지 확인 |
| `MeshTotal`, `RootTotal` | 같은 TIP 선택 안에서 누적된 메시/루트본 회전 | `AppliedTotal`과 합쳐 한 번의 090이 화면에서 만든 총 회전을 분리 확인 |

`[SC_TIP]`는 매 TIP 진입/선택 revision에서 `Index`, `ActiveIndex`, `Phase`, `PhaseTime`, `RawEntry`, `Retarget`, `ForceBlend`를 출력한다.

- `AppliedTotal`이 약 45도이고 `Remaining`도 약 45도인데 다음 `Rev`가 없다면: 0.75초 replay/chooser index 전달을 조사한다.
- 다음 `Rev`는 있으나 `AppliedTotal`이 0이면: Blend Stack/Force Blend 또는 State Controller clock이 reset되지 않았는지 조사한다.
- `ActorAfter`가 합산상 90도에 도달했는데 화면만 짧게 보이면: Actor 문제가 아니라 Offset Root Bone/Steering/mesh 보정 계층을 조사한다.
- `AppliedTotal`이 약 45도인데 `MeshVsActor` 또는 `RootVsActor`가 약 ±45도로 유지되면: 090 에셋의 루트 트랙(약 45도)과 시각 보정이 합쳐져 정상 90도를 만든다.
- `AppliedTotal`이 약 45도이고 `MeshVsActor`, `RootVsActor`도 0 부근이면: 재선택 문제가 아니라 ABP의 Steering/Offset Root Bone 시각 보정 경로가 사라진 것이다.
- `Phase=0`이 너무 이르게 나가면: 이동 입력, speed, air/landing, action 플래그 중 하나가 phase를 취소한 것이다.

### 제거한 이전 디버그

예전 `SC_TIP_GASP` 및 root-yaw scale 실험은 활성 경로와 다른 ownership model을 전제로 한다. 관련 상태 필드와 활성 로그는 제거했으며, 과거 비교용 구현은 컴파일되지 않는 `#if 0` 블록에만 남아 있다. 현재 분석에는 `[SC_TIP_ROOT]`, `[SC_TIP]`, `[SC_TIP_RESELECT]`, `[SC_TIP_FORCE]`만 사용한다.

검증 순서:

1. 정지 상태에서 정확히 90도만 돌린 후 마우스를 멈춘다.
2. `SC_TIP_ROOT`에서 `ActorAfter`가 누적되고 `Remaining`이 5도 부근까지 줄어드는지 확인한다.
3. 090 클립의 root yaw가 약 45도라면, phase가 남은 yaw에 대해 한 번 더 재선택하는지 확인한다.
4. 회전 중 반대 방향으로 돌려 L/R bucket이 바뀌는지 확인한다.
5. 이동 입력을 넣으면 즉시 TIP를 떠나 일반 Strafe Motion Matching으로 돌아가는지 확인한다.

## 검증 결과

2026-08-13 기준 다음 명령으로 C++ 빌드 성공:

```powershell
& 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe' `
  ArtisticSW2026Editor Win64 Development `
  '-Project=C:\Users\I\Documents\GitHub\ArtisticSW2026\ArtisticSW2026.uproject' -WaitMutex -NoHotReload
```

## Project_J-style presentation ownership

`ELocomotionState` remains in Artistic only for movement, timers, replication,
and compatibility. It no longer selects Blend Stack one-shots. The State
Controller derives the visible presentation exclusively from current
input/kinematics and transient requests:

```text
Landing / air request
  > derived stationary TIP phase
  > input-release Stop pulse
  > input-start / pivot pulse
  > locomotion or idle Motion Matching
```

This removes the old ownership conflict where `ELocomotionState::Stop` stayed
alive underneath TIP and replayed `TransitionToStop` after the turn finished.
The same request-pulse ownership is also packed into NativeUpdate and the MM
search-throttle path, so a hidden Motion Matching branch cannot keep treating a
completed legacy Start/Stop state as a live one-shot.

### Animation notify policy

`NotifyStartFinished`, `NotifyStopFinished`, and `NotifyLandingFinished` are
deliberately **empty compatibility hooks**. Existing asset notifies may remain
in place, but they do not change gameplay, locomotion, or presentation state.
One-shot completion is native-only:

- Start / Stop: State Controller direct-clip lifetime plus native fallback timer.
- Landing: native landing request lifetime plus native fallback timer.
- TIP: derived facing phase plus its State Controller replay policy.

The Stop release request is held natively until State Controller successfully
installs `TransitionToStop`; it is then consumed. If a stationary TIP wins on
that same input-release episode, TIP clears the pending Stop before it can be
selected.

### Ground Stop input-episode latch

`bPrevHasMoveInput` is still useful for start and pivot detection, but it is
not reliable as the sole Stop edge in this project: the player movement layer
can clear input before `ULocomotionAnimStateComponent::Update...` samples the
frame. `bGroundMoveEpisodeActive` therefore arms whenever grounded move input
is observed. Once input is absent, it produces one `bStopRequested` pulse even
if both the current and sampled-previous input values are already zero. The
State Controller consumes the latch only after `TransitionToStop` has been
selected; new movement, leaving the ground, or a winning stationary TIP clears
it. This guarantees exactly one Stop presentation per grounded movement
episode without animation notifies.

### Stop, restart, and Motion Matching hand-off

The direct-clip priority is now evaluated from **current input first**:

```text
Input resumes during Stop -> TransitionToStart
Start is stable                -> keep Start clip
Keyboard diagonal assembles on the next update -> reselect the matching diagonal Start
Start input direction / control yaw clearly changes -> LocomotionLoop (MM)
Input remains absent           -> keep TransitionToStop through its clip
Stationary after Stop          -> IdleLoop or TIP
```

This avoids the previous `Stop -> MotionMatching` skip caused by treating a
Stop hold as neither Idle nor Start. Input restart always receives an authored
Start, while an intentional WASD redirect or mouse-facing redirect during that
Start returns immediately to strafe Motion Matching. `a.StopDebug 1` is now
event-focused; use `a.StopDebug 2` only when the low-rate state samples are
needed.

`a.StartDebug 1` records the first Start input vector, its local yaw, the
eight-way sector, the evaluated Chooser path, and any Start-to-MM interruption.
It also marks `ReselectedInitialDiagonal`: this is the short native input
assembly window that fixes A/W or D/W arriving on adjacent updates without
turning ordinary later direction changes into another Start clip.
