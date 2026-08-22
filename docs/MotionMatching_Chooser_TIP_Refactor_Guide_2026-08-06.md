# Motion Matching, Chooser Table & Turn In Place (TIP) 대개편 초정밀 구현 종합 마스터 가이드

- **작성일자**: 2026-08-06
- **대상 브랜치**: `MM_Refactor_And_TIP`
- **대상 프로젝트**: `ArtisticSW2026` (`ClassFeature` 모듈, `UMotionMatchingAnimInstance`, `ULocomotionAnimStateComponent`, `ABasePlayer`)
- **문서 목적**: 현재 `ArtisticSW2026`의 C++ 하드코딩 PoseSearchDatabase 분기 방식을 완벽히 해체하고, `Project_J` (GASP 기반 Chooser Table + State Controller + One-Shot Sequence Evaluator + Turn In Place + Offset Root Bone) 시스템을 소스 코드 단위부터 AnimGraph 노드 배치까지 100% 복사/구현할 수 있도록 수록한 완전 종합 마스터 가이드라인.

---

## 1. 개편 배경 및 기존 아키텍처 한계 정밀 비교

| 구분 | `ArtisticSW2026` (현재) | `Project_J` / GASP (개편 목표) |
|---|---|---|
| **자산 선택 방식** | C++ `switch-case`에서 수십 개의 `UPoseSearchDatabase*` 직접 대입 | Chooser Table 자산 (`CHT_Player_*`) 기반 정밀 분기 |
| **One-Shot 제어** | Start/Stop/Land를 모션매칭 PSD 쿼리로 단순 처리 | Chooser로 선별된 `AnimSequence` + `BlendStack` 원샷 전용 재생 |
| **Presentation State Machine** | 없음 (`ELocomotionState` 단순 이동 상태만 존재) | `State 1`~`State 8`로 구성된 GASP Presentation State Engine |
| **Turn In Place (TIP)** | Enum에만 존재하며, C++ 분기 생략으로 Idle로 처리됨 | 90°/180° L/R Chooser 원샷 + 0.5초 완주 홀드 + 캡슐 회전 |
| **AnimGraph 뼈대 보정** | 단일 Motion Matching 노드 위주 (Offset Root Bone 없음) | `Offset Root Bone` (Steering 1.0) + `bShouldOverrideMotionMatching` 블렌드 |
| **회전 초과(Overshoot) 방지** | 턴 회전 가산이 카메라 각도를 초과하여 지나침 | `ClampedRootYawDelta` 적용으로 카메라 정면 각도에서 0.0으로 칼정지 |
| **착지(Land) 예외 처리** | `sprint_land` (2.33초)를 끝까지 홀드하며 모션 매칭 지연 | `GaitLock` + Shift 해제/지면 도착 시 즉시 모션 매칭 PSD 탈출 |

---

## 2. 파일별 수정 사양 및 클래스 구조체 정의

### 2.1 `LocomotionAnimStateComponent.h` 수정 사양

```cpp
// 1. Presentation State Enum 추가
UENUM(BlueprintType)
enum class EStateControllerPresentationState : uint8
{
    None = 0,
    IdleLoop = 1,
    TransitionToStart = 2,
    LocomotionLoop = 3,
    TransitionToStop = 4,
    TransitionToPivot = 5,
    TransitionToJump = 6,
    TransitionToLand = 7,
    TurnInPlace = 8
};

// 2. State Controller Context Snapshot 구조체 추가
USTRUCT(BlueprintType)
struct FStateControllerContextSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    EStateControllerPresentationState CurrentPresentationState = EStateControllerPresentationState::IdleLoop;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    EStateControllerPresentationState DesiredPresentationState = EStateControllerPresentationState::IdleLoop;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    bool bShouldTurnInPlace = false;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    float DesiredFacingDeltaYaw = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    float TurnInPlaceRootYawDelta = 0.0f;
};
```

---

### 2.2 `MotionMatchingAnimInstance.h` 수정 사양

```cpp
#include "ChooserTable.h"

// ThreadSafe 복사 데이터 구조체 추가
USTRUCT(BlueprintType)
struct FAnimStateControllerThreadSafeData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    EStateControllerPresentationState PresentationState = EStateControllerPresentationState::IdleLoop;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    TObjectPtr<UAnimationAsset> SelectedAnimation = nullptr;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    float SelectedAnimationBlendTime = 0.2f;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    bool bShouldOverrideMotionMatching = false;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    float TurnInPlaceSteeringAlpha = 1.0f;

    UPROPERTY(BlueprintReadOnly, Category = "StateController")
    float TurnInPlaceRootYawDelta = 0.0f;
};

// UMotionMatchingAnimInstance 클래스 내 선언
UCLASS(Blueprintable, BlueprintType)
class CLASSFEATURE_API UMotionMatchingAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    // ThreadSafe Getter 함수군 (AnimGraph에서 직접 호출)
    UFUNCTION(BlueprintPure, Category = "StateController", meta = (BlueprintThreadSafe))
    EStateControllerPresentationState GetThreadSafeStateControllerPresentationState() const;

    UFUNCTION(BlueprintPure, Category = "StateController", meta = (BlueprintThreadSafe))
    UAnimationAsset* GetThreadSafeStateControllerSelectedAnimation() const;

    UFUNCTION(BlueprintPure, Category = "StateController", meta = (BlueprintThreadSafe))
    float GetThreadSafeStateControllerSelectedAnimationBlendTime() const;

    UFUNCTION(BlueprintPure, Category = "StateController", meta = (BlueprintThreadSafe))
    bool GetThreadSafeShouldOverrideMotionMatching() const;

    UFUNCTION(BlueprintPure, Category = "StateController", meta = (BlueprintThreadSafe))
    float GetThreadSafeStateControllerTurnInPlaceSteeringAlpha() const;

    UFUNCTION(BlueprintPure, Category = "StateController", meta = (BlueprintThreadSafe))
    FRotator GetThreadSafeOffsetRootRotation() const;

protected:
    // Chooser Tables
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
    TObjectPtr<UChooserTable> StartChooserTable;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
    TObjectPtr<UChooserTable> StopChooserTable;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
    TObjectPtr<UChooserTable> LandChooserTable;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
    TObjectPtr<UChooserTable> TurnInPlaceChooserTable;

    // State Controller 런타임 수명주기 데이터
    EStateControllerPresentationState StateControllerPlaybackHoldState = EStateControllerPresentationState::None;
    float StateControllerPlaybackHoldElapsed = 0.0f;
    float StateControllerPlaybackHoldDuration = 0.0f;
    TObjectPtr<UAnimationAsset> StateControllerSelectedAnimation = nullptr;
    float StateControllerSelectedAnimationBlendTime = 0.2f;

    // Land Gait Lock
    EGaitIntent StateControllerLandGaitLock = EGaitIntent::Walk;
    bool bHasStateControllerLandGaitLock = false;

    void EvaluateStateControllerPresentationState();
    void EvaluateStateControllerPlaybackHold(EStateControllerPresentationState DesiredState);
};
```

---

## 3. C++ 핵심 로직 완전 구현 코드 (Copy-Pasteable)

### 3.1 State Controller 엔진 구현 (`MotionMatchingAnimInstance.cpp`)

```cpp
void UMotionMatchingAnimInstance::EvaluateStateControllerPresentationState()
{
    if (!CachedLocomotionStateComponent) return;

    // 1. 컴포넌트 데이터 기반 목표 상태(DesiredState) 도출
    EStateControllerPresentationState DesiredState = EStateControllerPresentationState::IdleLoop;

    const bool bHasMoveInput = ThreadSafeData.InputData.bHasMoveInput;
    const float GroundSpeed = ThreadSafeData.MovementData.Velocity.Size2D();
    const bool bInAir = ThreadSafeData.AirData.bIsInAir;
    const bool bLanding = ThreadSafeData.LandingData.bIsLanding;
    const bool bShouldTurnInPlace = CachedLocomotionStateComponent->bShouldTurnInPlace;

    if (bInAir)
    {
        DesiredState = EStateControllerPresentationState::TransitionToJump;
    }
    else if (bLanding)
    {
        DesiredState = EStateControllerPresentationState::TransitionToLand;
    }
    else if (bHasMoveInput || GroundSpeed > 10.0f)
    {
        if (StateControllerPlaybackHoldState == EStateControllerPresentationState::IdleLoop ||
            StateControllerPlaybackHoldState == EStateControllerPresentationState::TurnInPlace)
        {
            DesiredState = EStateControllerPresentationState::TransitionToStart;
        }
        else
        {
            DesiredState = EStateControllerPresentationState::LocomotionLoop;
        }
    }
    else // 정지 상태
    {
        if (bShouldTurnInPlace)
        {
            DesiredState = EStateControllerPresentationState::TurnInPlace;
        }
        else if (GroundSpeed > 100.0f)
        {
            DesiredState = EStateControllerPresentationState::TransitionToStop;
        }
        else
        {
            DesiredState = EStateControllerPresentationState::IdleLoop;
        }
    }

    // 2. 착지(Land) 보행 형태 잠금 (popping 방지)
    if (DesiredState == EStateControllerPresentationState::TransitionToLand)
    {
        if (!bHasStateControllerLandGaitLock)
        {
            StateControllerLandGaitLock = ThreadSafeData.GroundData.bIsSprinting ? EGaitIntent::Sprint : EGaitIntent::Run;
            bHasStateControllerLandGaitLock = true;
        }
    }
    else
    {
        bHasStateControllerLandGaitLock = false;
    }

    // 3. Playback Hold 및 Chooser 애니메이션 갱신
    EvaluateStateControllerPlaybackHold(DesiredState);
}

void UMotionMatchingAnimInstance::EvaluateStateControllerPlaybackHold(EStateControllerPresentationState DesiredState)
{
    const float DeltaTime = GetWorld()->GetDeltaSeconds();

    // 신규 Hold 진입 필요성 검사
    const bool bStateChanged = (DesiredState != StateControllerPlaybackHoldState);

    // [특수 인터럽트 조건]: 착지 중 Shift 해제(!bWantsSprint) 또는 지면 부착 완료(!bIsLanding) 시 즉시 State 3 (모션매칭) 직행
    bool bInterruptLandForMotionMatching = false;
    if (StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToLand &&
        DesiredState == EStateControllerPresentationState::LocomotionLoop)
    {
        const bool bWantsSprint = ThreadSafeData.GroundData.bIsSprinting;
        const bool bLandWasSprinting = (StateControllerLandGaitLock == EGaitIntent::Sprint);

        if (!ThreadSafeData.LandingData.bIsLanding || (bLandWasSprinting && !bWantsSprint))
        {
            bInterruptLandForMotionMatching = true;
        }
    }

    if (bStateChanged || bInterruptLandForMotionMatching)
    {
        StateControllerPlaybackHoldState = DesiredState;
        StateControllerPlaybackHoldElapsed = 0.0f;

        // Chooser Table을 사용하여 원샷 애니메이션 선별
        UChooserTable* TargetChooser = nullptr;
        switch (DesiredState)
        {
        case EStateControllerPresentationState::TransitionToStart:
            TargetChooser = StartChooserTable;
            break;
        case EStateControllerPresentationState::TransitionToStop:
            TargetChooser = StopChooserTable;
            break;
        case EStateControllerPresentationState::TransitionToLand:
            TargetChooser = LandChooserTable;
            break;
        case EStateControllerPresentationState::TurnInPlace:
            TargetChooser = TurnInPlaceChooserTable;
            break;
        default:
            TargetChooser = nullptr;
            break;
        }

        if (TargetChooser)
        {
            // Chooser 평가 파라미터 전달 및 자산 획득
            FChooserEvaluationContext Context;
            // (필요 시 Context에 AnimInstance, Character, Struct 바인딩)
            UObject* EvaluatedObject = UChooserFunctionLibrary::EvaluateChooserForObject(TargetChooser, Context);
            StateControllerSelectedAnimation = Cast<UAnimationAsset>(EvaluatedObject);
            StateControllerPlaybackHoldDuration = StateControllerSelectedAnimation ? StateControllerSelectedAnimation->GetPlayLength() : 0.5f;
        }
        else
        {
            StateControllerSelectedAnimation = nullptr;
            StateControllerPlaybackHoldDuration = 0.0f;
        }
    }
    else
    {
        StateControllerPlaybackHoldElapsed += DeltaTime;
    }

    // ThreadSafe 전달 데이터 갱신
    ThreadSafeData.StateController.PresentationState = StateControllerPlaybackHoldState;
    ThreadSafeData.StateController.SelectedAnimation = StateControllerSelectedAnimation;
    ThreadSafeData.StateController.bShouldOverrideMotionMatching = (StateControllerPlaybackHoldState != EStateControllerPresentationState::LocomotionLoop && StateControllerPlaybackHoldState != EStateControllerPresentationState::IdleLoop);
}
```

---

### 3.2 Turn In Place (TIP) C++ 회전 적용 & Overshoot Clamping (`BasePlayer.cpp`)

```cpp
void ABasePlayer::ApplyCombatTurnInPlaceRotation(float DeltaTime)
{
    if (!LocomotionStateComponent || !LocomotionStateComponent->bShouldTurnInPlace)
    {
        return;
    }

    const float FacingDeltaYaw = GetDesiredFacingDeltaYaw(); // Controller Yaw - Actor Yaw (-180 ~ +180)
    const float AnimRootYawDelta = GetMesh()->GetAnimInstance() ? GetMesh()->GetAnimInstance()->GetRootMotionData().RootTransform.GetRotation().Rotator().Yaw : 0.0f;

    // [핵심 기술]: RootYawDelta Overshoot Clamping
    // 애니메이션 턴 회전 가산량이 카메라 목표 각도를 넘어서 오버슈트하지 못하도록 정확히 클램핑
    float ClampedRootYawDelta = 0.0f;
    if (FacingDeltaYaw >= 0.0f)
    {
        ClampedRootYawDelta = FMath::Min(AnimRootYawDelta, FMath::Max(FacingDeltaYaw, 0.0f));
    }
    else
    {
        ClampedRootYawDelta = FMath::Max(AnimRootYawDelta, FMath::Min(FacingDeltaYaw, 0.0f));
    }

    // 캐릭터 Actor Yaw 회전 적용
    FRotator NewRotation = GetActorRotation();
    NewRotation.Yaw += ClampedRootYawDelta;
    SetActorRotation(NewRotation);

    // 회전 오차가 45도 미만으로 감소 시 TurnInPlace 조기 해제 (360도 무한 스핀 방지)
    if (FMath::Abs(FacingDeltaYaw - ClampedRootYawDelta) < 45.0f)
    {
        LocomotionStateComponent->bShouldTurnInPlace = false;
    }
}
```

---

## 4. Master ABP AnimGraph 상세 노드 설계 (Node Wiring Layout)

```text
[Node 1: Motion Matching PSD] ────► (Pose: MotionMatchingPose)
 (Database: PSD_Run/Sprint/Idle)         │
                                         ├──► [Node 3: Blend Poses by bool]
[Node 2: Sequence Evaluator]  ────► (Pose: OneShotPose)             │ (Active Child: bShouldOverrideMotionMatching)
 (Asset: SelectedAnimation)              │                            │
 (Chooser 자산 원샷 평가)                ┘                            ▼
                                                       [Node 4: Offset Root Bone]
                                                        - Target: root
                                                        - Steering Alpha: 1.0f
                                                        - Mode: Interpolate
                                                                      │
                                                                      ▼
                                                       [Node 5: Foot Placement IK]
                                                                      │
                                                                      ▼
                                                       [Output Pose]
```

### 4.1 상세 노드 파라미터 바인딩 테이블

1. **`Blend Poses by bool` (Node 3)**:
   - `Bool Pipeline Pin`: `GetThreadSafeShouldOverrideMotionMatching()`
   - `True Blend Time`: `0.2s` (Sequence Evaluator 원샷으로 즉시 스위칭)
   - `False Blend Time`: `0.3s` (원샷 완료 후 Motion Matching PSD로 매끄럽게 복귀)

2. **`Offset Root Bone` (Node 4)**:
   - `Target Bone`: `root`
   - `Steering Alpha`: `GetThreadSafeStateControllerTurnInPlaceSteeringAlpha()` (값 = `1.0f` 고정)
   - `Rotation Mode`: `Interpolate` (스냅 없이 뼈대 회전을 부드럽게 보완)

---

## 5. Chooser Table 자산 상세 저작 규격 (`Content/Animation/Chooser/`)

### 5.1 `CHT_Player_TurnInPlace`
- **입력 컬럼 (Context Evaluation)**:
  1. `FacingDeltaYaw` (`Float` 범위):
     - `[-180.0, -135.0]` ➔ `TurnInPlace_180_L`
     - `[-135.0, -45.0]` ➔ `TurnInPlace_90_L`
     - `[45.0, 135.0]` ➔ `TurnInPlace_90_R`
     - `[135.0, 180.0]` ➔ `TurnInPlace_180_R`
- **출력 결과 (Output Asset)**:
  - 해당 방향의 authored root motion 90°/180° `AnimSequence` 자산.

### 5.2 `CHT_Player_Start`
- **입력 컬럼**:
  1. `GaitIntent` (`Enum`: Walk / Run / Sprint)
  2. `MoveDirection` (`Float`: Angle -180 ~ +180)
- **출력 결과**:
  - `Start_Forward`, `Start_Backward`, `Sprint_Start_F` 등 조건에 들어맞는 `AnimSequence`.

---

## 6. 검증 및 디버깅 가이드라인 (QA & Verification)

1. **빌드 검증**:
   - `ArtisticSW2026Editor Win64 Development` UBT 빌드 성공 확인.
2. **콘솔 디버그 CVar 탑재**:
   - `p.ProjectJ.MMTransitionDebug 1` 실행 시 프레임별 `StateControllerLandDiag` 및 `TurnInPlace` 상태 추적 출력이 정상 가동하는지 확인.
3. **최종 핵심 검증 씬**:
   - **Scene A (TIP 오버슈트)**: 제자리에서 마우스 180도 급회전 시 캐릭터가 오버슈트 없이 카메라 정면에 칼정지하는가?
   - **Scene B (TIP 360도 스핀)**: 회전 후 턴 상태에서 빠르게 빠져나와 연속 회전 현상이 사라졌는가?
   - **Scene C (스프린트 착지)**: 착지 도중 Shift를 뗬을 때 착지 모션을 끝까지 억지로 재생하지 않고, 모션 매칭 러닝/스프린트로 즉시 인터럽트 넘어가는가?
   - **Scene D (착지 제자리 회전)**: 점프 착지 중 마우스를 30° 이상 돌렸을 때 Land 락을 풀고 즉시 Turn In Place로 자연스럽게 돌아가는가?
   - **Scene E (공중 손 뗌 ➡️ Stand Land)**: 공중에서 이동 중 착지 직전 키를 뗐을 때 공중 잔여 관성과 무관하게 Stand Land가 출력되는가?
   - **Scene F (착지 이동 ➡️ 즉시 Stop)**: 착지 후 짧게 WASD를 쳤다가 손을 뗐을 때 착지 잔여 딜레이 없이 즉시 `MM_Stop` 모션이 발동되는가?

---

## 7. 최신 패치 내역: Stop & Land 상태 제자리 회전(TIP) 및 즉시 정지(Stop) 개선

### 7.1 개요 및 변경 배경
* **기존 문제**:
  1. `!bIsLanding` 및 `GroundSpeed <= IdleSpeedThreshold` 하드코딩 조건으로 인해, 착지(Land) 중이거나 감속 정지(Stop) 중일 때 마우스를 돌려도 제자리 회전(TIP)이 차단됨.
  2. 공중 이동 중 착지 직전 손을 뗐을 때 공중 잔여 속도로 인해 `Stand Land` 대신 `Run Land`가 오선택됨.
  3. 착지 중 이동 후 손을 뗐을 때 `MinimumLandingDuration` 대기 및 Stop 요청 누락으로 `Stop` 대신 `Idle`로 직행하거나 딜레이 발생.

### 7.2 소스 코드 핵심 변경 사항
1. **`LocomotionAnimStateComponent.cpp`**:
   - `UpdateTurnInPlacePhase`: `bCanTurnInPlace`에서 `!bIsLanding` 제한을 해제하고, `!bIsInAir` 및 `!bHasMoveInput` 상태에서 시선 각도 30° 이상 시 즉시 TIP 진입 허용.
   - `StartLanding`: `bLandWasMoving = bHasMoveInput && ...` 로 변경하여 착지 순간 손을 뗐다면 공중 관성이 있어도 무조건 `Stand Land` 선택.
   - `InterruptLandingForStop`: `bStopRequested = true;` 및 `bGroundMoveEpisodeActive = true;`를 명시적으로 세팅하고, 이동 착지 중 손을 떼는 순간 딜레이 없이 즉시 호출되도록 단축.
2. **`MotionMatchingAnimInstance.cpp`**:
   - `UpdateStateControllerTransitions`: `bLanding` 및 `bStopHoldActive` 상태에서 `bCanStartTurnInPlace`가 참이면 Land/Stop 홀드를 즉시 해제하고 `TurnInPlace`로 분기.
   - `bLocomotionStateStop` (`CurrentState == Stop`) 시 속도와 무관하게 `TransitionToStop`으로 분기하여 Stop Chooser가 100% 실행되도록 보장.

### 7.3 실시간 콘솔 및 화면 HUD 디버깅
* **콘솔 명령어**: `a.StateControllerDebug 1`
* **화면 출력**: 좌측 상단 청록색 실시간 HUD 표시
  ```text
  [AnimState] Presentation: %s | Requested: %s | State: %s | Asset: %s | LandMoving: %d | StopReq: %d | TIP: %d | Speed: %.1f
  ```

---

### 7.4 최신 패치 내역: BlendStack 동일 프레임 중복 BlendTo 경고 수정 및 연속 회전(TIP Replay) 보존

* **수정일자**: 2026-08-22
* **문제 현상**:
  - `ABP_Player` 애님그래프에서 `Blend Stack` 노드의 `Animation Asset`, `Blend Time` 핀에 C++ Getter 함수가 직접 연결되어 있는 상태에서, 노드의 `업데이트 시(On Update)` 콜백인 `OnUpdate_StateMachineBlendStack`이 `Force Blend On Next Update`를 호출함.
  - 새 애니메이션 에셋 전환(예: `Idle` ➔ `TIP`, `Start` ➔ `Stop`) 시 핀 자체 평가에 의한 `BlendTo`와 `Force Blend`에 의한 `BlendTo`가 동일 프레임에 2회 발생.
  - 엔진에서 `LogBlendStack: Warning: FAnimNode_BlendStack_Standalone multiple BlendTo requests during the same frame: only the last request will be put on this BlendStack` 경고가 대량 출력됨.
* **해결 방안 (`MotionMatchingAnimInstance.cpp`)**:
  - `EvaluateStateControllerPlaybackHold`에서 직전 선택 에셋 `PreviousSelectedAnimation`을 캐싱.
  - 에셋이 실제로 변경되는 일반 원샷 전환(`Previous != Current`)은 핀 자체 평가가 단독으로 `BlendTo`를 처리하도록 `ForceBlend`를 끔 (`0 Warnings`).
  - **동일 에셋을 연속 재생하는 상황(`Previous == Current`, 예: 동일 방향 연속 제자리 회전 TIP Replay)**에서만 `bStateControllerForceBlendStackOnNextUpdate = true`를 펄스로 인가하여, 연속 턴 반응성을 100% 보존하면서 중복 호출 경고를 완전히 제거함.

---

### 7.5 최신 패치 내역: 공중 체공(Air Loop) 시간 역전 ensure 에러 수정 및 안정화

* **수정일자**: 2026-08-22
* **문제 현상**:
  - `Player As Client` 환경 또는 공중 스폰 시 체공 시간이 3.33초를 초과할 때, 엔진에서 `Ensure condition failed: (CurrentPosition >= PreviousPosition) in Animation M_Neutral_Jump_Loop_Fall` 에러 발생.
  - C++의 `StabilizeAirLoopBlendStackBeforeUpdate`가 누적 시간을 1주기 내로 강제 정규화(`RestoreBlendStackTopPlayer`)하면서, 이전 프레임 위치(9.47s)보다 작은 시간(3.33s)을 주입하여 엔진 무결성 검사를 위반함.
* **해결 방안 (`MotionMatchingAnimInstance.cpp`)**:
  - `StabilizeAirLoopBlendStackBeforeUpdate`에서 다중 스택 겹침 방지(`CollapseBlendStackToDominantPlayer`)는 그대로 유지.
  - 루핑 시간 진행은 언리얼 엔진 자체 `FBlendStackAnimPlayer`의 네이티브 Looping Modulo 파이프라인에 일임하여 인위적인 시간 되감기 제거 ➔ `ensure` 에러 100% 제거 및 긴 체공/클라이언트 스폰 안정성 확보.

---

### 7.6 최신 패치 내역: 퀵슬롯/인벤토리 무기 장착 시 상체 오버레이(Weapon UpperBody Overlay) 태그 호환 및 디버깅 보강

* **수정일자**: 2026-08-22
* **문제 현상**:
  - 인벤토리(Tab)에서 퀵슬롯을 통해 활(`Item.Id.Weapon.Bow.LongBow1`)을 장착했을 때, 손에는 활이 부착되지만 캐릭터 상체 애니메이션이 활 조준/대기 포즈(`BS_Bow`)로 오버라이딩되지 않고 일반 `Locomotion` 포즈로 유지됨.
  - **원인**: 인벤토리는 신규 태그(`Item.Id.Weapon.*`)를 사용하고, 무기 데이터 에셋 및 C++는 구형 태그(`Item.Weapon.*`)를 조회하여 `MatchesTag` 검사 실패. `GetEquippedUpperBodyOverlayIndex()`가 `0`으로 반환되어 애님그래프의 `Blend Poses by int`에서 `Blend Pose 0 (Locomotion)`이 선택됨.
* **해결 방안**:
  1. **`WeaponAnimationDataAsset.cpp` & `PlayerEquipmentComponent.cpp`**:
     - `Item.Id.Weapon.*`과 `Item.Weapon.*` 간의 접두어 차이를 자동 정규화하는 `DoesWeaponGameplayTagMatch` 유연 매칭 로직 적용.
     - `GetEquippedUpperBodyOverlayIndex`: 활(Bow) 무기이고 `bUseUpperBodyOverlay`가 참일 때 에셋 인덱스가 0으로 미설정되어 있어도 자동으로 `Index 1 (BS_Bow)`로 안전하게 폴백되도록 보강.
  2. **`MotionMatchingAnimInstance.cpp`**:
     - `GetThreadSafeHasBowEquipped()`, `GetThreadSafeWeaponUpperBodyMode()`에서 `Item.Id.Weapon.Bow`와 `Item.Weapon.Bow`를 모두 인식하도록 매칭 확장.
     - `a.StateControllerDebug 1` 콘솔 실행 시 실시간 상체 오버레이 HUD(`[WeaponOverlay]`)를 에메랄드 색상으로 함께 출력하여 실시간 상태 모니터링 지원.

---

### 7.7 최신 패치 내역: 활 발사(Release) 시 활시위(String IK / DrawAlpha) 즉시 원복 처리

* **수정일자**: 2026-08-22
* **문제 현상**:
  - 활을 당긴(Full Draw) 상태에서 좌클릭을 떼어 발사했을 때, 화살 발사 및 릴리즈 몽타주(`Shooting_Arrow_Anim_Montage`)가 재생되는 동안에도 활시위(String)가 오른손에 계속 붙어 늘어나 있는 버그 발생.
  - **원인**: `GA_BowAimFire::BeginRelease`에서 `SetDrawAlpha(1.0f)`를 유지한 채 릴리즈 몽타주가 완전히 끝날 때(`FinishShot`)까지 `DrawAlpha`를 0으로 리셋하지 않아 발생.
* **해결 방안**:
  1. **`GA_BowAimFire.cpp`**:
     - `BeginRelease` 및 `FireArrow`, `OnReleaseFireEvent` 발생 시 즉시 `CachedBowComponent->SetDrawAlpha(0.0f)` 호출.
  2. **`MotionMatchingAnimInstance.cpp`**:
     - `State_Bow_Releasing` 태그 활성화 시 또는 `DrawAlpha <= 0`일 때 `bHasStringIKTarget = false`로 즉시 전환하여 활시위가 손에서 분리되고 본래 원위치로 복귀하도록 동기화.

---

### 7.8 최신 패치 내역: 조준(우클릭) 유지 중 연속 발사(좌클릭 반복) 지원

* **수정일자**: 2026-08-22
* **문제 현상**:
  - 우클릭으로 조준(Aim) 상태를 유지한 채 첫 번째 화살을 발사한 후, 우클릭을 떼지 않고 좌클릭을 다시 눌렀을 때 다음 드로우/발사가 반응하지 않는 현상.
  - **원인**: `GA_BowAimFire::ActivateAbility` 내의 좌클릭 이벤트 수신 태스크(`WaitLeftPressedTask`, `WaitLeftReleasedTask`, `WaitFireArrowTask`)의 `OnlyTriggerOnce` 인자가 `true`로 설정되어 첫 번째 발사 후 태스크가 소멸됨.
* **해결 방안 (`GA_BowAimFire.cpp`)**:
  1. `WaitGameplayEvent`의 `OnlyTriggerOnce`를 `false`로 변경하여 어빌리티가 활성화되어 있는 동안(우클릭 유지 중) 좌클릭 프레스/릴리즈/발사 이벤트를 연속으로 수신할 수 있도록 수정.
  2. `OnLeftClickPressed`에서 이전 발사의 반동 릴리즈(`bIsReleaseInProgress`)가 진행 중이더라도 화살이 이미 발사된 상태(`bHasFiredCurrentShot`)라면 즉시 반동을 캔슬하고 다음 화살 드로우로 매끄럽게 연계되도록 응답성 개선.





