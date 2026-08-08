# 프로젝트 완전 통합 인수인계 문서 (Master Handover Document)

**프로젝트 명**: `ArtisticSW2026` (Unreal Engine 5.7)  
**문서 목적**: 모션 매칭(Motion Matching) 및 추저 테이블(Chooser Table) 기반의 하이브리드 로코모션 애니메이션 시스템 구현 내역, C++ 아키텍처, ABP(AnimBlueprint) 노드 구조, 버그 이력, 그리고 미해결 버그 원인 분석을 다른 AI 에이전트 및 개발자에게 100% 완전하게 전달하기 위함.

---

## 📑 목차 (Table of Contents)

1. [시스템 전체 아키텍처 및 설계 사상](#1-시스템-전체-아키텍처-및-설계-사상)
2. [GASP 및 Project_J 구조와의 비교 및 차이점](#2-gasp-및-project_j-구조와의-비교-및-차이점)
3. [C++ 클래스 및 함수 레벨 상세 명세](#3-c-클래스-및-함수-레벨-상세-명세)
   - 3.1 `UMotionMatchingAnimInstance`
   - 3.2 `ULocomotionAnimStateComponent` (스테이트 컴포넌트 구조)
   - 3.3 `ABasePlayer` (캐릭터 컴포넌트 부착)
4. [Chooser Table 계층 구조 및 전체 매핑 에셋](#4-chooser-table-계층-구조-및-전체-매핑-에셋)
   - 4.1 마스터 추저 테이블 (`CHT_Player_Main.uasset`)
   - 4.2 6개 서브 추저 테이블 상세 (`Start`, `Stop`, `Pivot`, `InAir`, `Land`, `TurnInPlace`)
5. [ABP_Player (AnimBlueprint) 애니메이션 그래프 구조](#5-abp_player-animblueprint-애니메이션-그래프-구조)
6. [트러블슈팅 및 진행된 버그 수정 이력](#6-트러블슈팅-및-진행된-버그-수정-이력)
   - 6.1 `ContextData entry 1` 누락 크래시 및 파라미터 보완
   - 6.2 Chooser `NULL` 반환 (`[MM_CHOOSER_EMPTY]`) 원인 및 C++ 평가 순서 교정
   - 6.3 AnimGraph `Blend Poses by bool` 핀 반대로 연결된 문제
7. [현재 미해결 버그 현상 및 차기 AI 에이전트 분석 가이드](#7-현재-미해결-버그-현상-및-차기-ai-에이전트-분석-가이드)

---

## 1. 시스템 전체 아키텍처 및 설계 사상

본 시스템은 **상태 기반 원샷 재생(Chooser Table)**과 **실시간 포즈 탐색(Motion Matching)**을 결합한 하이브리드 애니메이션 아키텍처입니다.

```
                               ┌────────────────────────────────────────────────────────┐
                               │           ABasePlayer (Character Actor)                │
                               │  - ULocomotionAnimStateComponent (이동 상태 계산)      │
                               │  - USWTrajectoryComponent (궤적 생성)                  │
                               └───────────────────────────┬────────────────────────────┘
                                                           │
                                                           ▼
                               ┌────────────────────────────────────────────────────────┐
                               │      UMotionMatchingAnimInstance (AnimInstance C++)     │
                               │  - EvaluateStateControllerPresentationState()           │
                               │  - EvaluateStateControllerPlaybackHold()               │
                               └───────────────────────────┬────────────────────────────┘
                                                           │
                                ┌──────────────────────────┴──────────────────────────┐
                                ▼                                                     ▼
    ┌───────────────────────────────────────────────────────┐   ┌───────────────────────────────────────────────────────┐
    │          Chooser Table (원샷 애니메이션 영역)         │   │       Motion Matching (지속 포즈 검색 영역)           │
    │  - CHT_Player_Main (최상위 마스터 추저)               │   │  - PSD_Idle (대기 상태 포즈 검색)                     │
    │  - 6개 서브 추저 (Start, Stop, Pivot, Jump, Land 등)  │   │  - PSD_Run_Locomotion (지속 이동 포즈 검색)           │
    └───────────────────────────┬───────────────────────────┘   └───────────────────────────┬───────────────────────────┘
                                │                                                           │
                                ▼                                                           ▼
                   AnimGraph: [Blend Stack]                                    AnimGraph: [Motion Matching]
                                │                                                           │
                                └───────────────────────────┬───────────────────────────────┘
                                                            │
                                                            ▼
                                               [Blend Poses by bool]
                                         (ActiveValue: bShouldOverrideMM)
                                                            │
                                                            ▼
                                                   Final Skeletal Pose
```

### 핵심 역할 분담
1. **Chooser Table (원샷 애니메이션)**:
   - **담당 범위**: `TransitionToStart` (출발), `TransitionToStop` (멈춤), `TransitionToPivot` (피벗/180도 전환), `TransitionToJump` (점프), `TransitionToLand` (착지), `TurnInPlace` (제자리 회전).
   - **동작 방식**: 8방향 스트레이프(`MovementDirection`), 게이트(`Gait`: Walk/Run/Sprint), 디딤발(`Foot`: Left/Right)을 조합하여 authored Sequence 에셋을 선택한 뒤 `Blend Stack`으로 푸시.
2. **Motion Matching (지속 포즈 검색)**:
   - **담당 범위**: `LocomotionLoop` (지속 이동), `IdleLoop` (대기).
   - **동작 방식**: 캐릭터 궤적(Trajectory) 및 포즈 조인트 속도를 실시간 계산하여 `PSD_Run_Locomotion` 및 `PSD_Idle` 포즈 데이터베이스에서 최적의 프레임을 검색.

---

## 2. GASP 및 Project_J 구조와의 비교 및 차이점

| 항목 | UE5 GASP (Standard) | Project_J Reference | ArtisticSW2026 (현재 프로젝트) |
| :--- | :--- | :--- | :--- |
| **State Controller 구현** | Blueprint 중심 컴포넌트 | C++ AnimInstance 내장 파이프라인 | C++ AnimInstance 내장 파이프라인 |
| **별도 StateController 컴포넌트 여부** | 존재 (`BP_StateController`) | **없음** (`LocomotionAnimStateComponent`가 전담) | **없음** (`LocomotionAnimStateComponent`가 전담) |
| **Chooser 파라미터 전달** | BP 인스턴스 파라미터 | C++ `FChooserEvaluationContext` 직접 구동 | C++ `FChooserEvaluationContext` 직접 구동 |
| **AnimGraph Blend 방식** | Blend Stack + Inertialization | Blend Stack + Blend Poses by bool | Blend Stack + Blend Poses by bool |

> 📌 **주의 (중요)**: 본 프로젝트에는 `UStateControllerComponent`라는 별도의 액터 컴포넌트 클래스가 존재하지 않으며, **`ULocomotionAnimStateComponent`가 캐릭터 이동 상태를 관리하고, `UMotionMatchingAnimInstance`가 C++ 내부에서 GASP 스타일 State Controller를 직접 평가**합니다.

---

## 3. C++ 클래스 및 함수 레벨 상세 명세

### 3.1 `UMotionMatchingAnimInstance`
- **헤더**: [`Source/ClassFeature/Public/Animation/MotionMatchingAnimInstance.h`](file:///C:/Users/I/Documents/GitHub/ArtisticSW2026/Source/ClassFeature/Public/Animation/MotionMatchingAnimInstance.h)
- **소스**: [`Source/ClassFeature/Private/Animation/MotionMatchingAnimInstance.cpp`](file:///C:/Users/I/Documents/GitHub/ArtisticSW2026/Source/ClassFeature/Private/Animation/MotionMatchingAnimInstance.cpp)

#### 주요 UPROPERTY 변수:
```cpp
// Master Chooser Table (에디터 디테일 패널에서 CHT_Player_Main 할당)
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
TObjectPtr<UChooserTable> MainChooserTable;

// 서브 추저 테이블 (MainChooserTable 미할당 시 펄백용)
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
TObjectPtr<UChooserTable> StartChooserTable;
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
TObjectPtr<UChooserTable> StopChooserTable;
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
TObjectPtr<UChooserTable> LandChooserTable;
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
TObjectPtr<UChooserTable> InAirChooserTable;
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
TObjectPtr<UChooserTable> PivotChooserTable;
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
TObjectPtr<UChooserTable> TurnInPlaceChooserTable;

// Chooser 평가용 동기화 멤버 변수들
UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
EStateControllerPresentationState StateControllerPresentationState;
UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
EMovementDirection StateControllerMovementDirection;
UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
EMovementDirection StateControllerPreviousMovementDirection;
UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
EGaitIntent StateControllerGait;
UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StateController|Chooser")
bool bStateControllerIsPivoting;
```

#### 주요 C++ 함수 및 동작 설명:
1. `NativeInitializeAnimation()`:
   `TryGetPawnOwner()`로부터 `ABasePlayer` 및 `ULocomotionAnimStateComponent`, `USWTrajectoryComponent` 캐싱.
2. `NativeUpdateAnimation(float DeltaSeconds)`:
   - 매 프레임 `UpdateMovementDirection()`, `CalculateAOValueAndEnableAO()`, `EvaluateStateControllerPresentationState()` 실행.
   - `ShouldEvaluateMotionMatchingThisFrame()` 체크 후 모션 매칭 PSD 데이터베이스 할당.
3. `EvaluateStateControllerPresentationState()`:
   `CachedLocomotionStateComponent` 상태(공중, 착지, 이동입력, 피벗 요청 등)를 분석하여 `DesiredState` (`EStateControllerPresentationState`)를 도출하고 `EvaluateStateControllerPlaybackHold(DesiredState)` 호출.
4. `EvaluateStateControllerPlaybackHold(EStateControllerPresentationState DesiredState)`:
   - **[치명적 핵심]** Chooser 평가 전 `this` (`UMotionMatchingAnimInstance`)의 UPROPERTY 변수들(`StateControllerPresentationState`, `StateControllerMovementDirection`, `StateControllerGait` 등)을 **먼저 업데이트**함.
   - `FChooserEvaluationContext` 구축:
     ```cpp
     FChooserEvaluationContext ChooserContext;
     ChooserContext.AddObjectParam(this);
     FChooserPlayerSettings PlayerSettings;
     ChooserContext.AddStructParam(PlayerSettings);
     FS_ChooserOutputs ChooserOutputs;
     ChooserContext.AddStructParam(ChooserOutputs);
     ```
   - `UChooserFunctionLibrary::EvaluateObjectChooserBase()` 호출 및 재귀 Sub-Chooser 처리.
   - `StateControllerSelectedAnimation`, `StateControllerSelectedAnimationBlendTime`, `StateControllerPlaybackHoldDuration` 갱신.
   - `ThreadSafeData.StateController.bShouldOverrideMotionMatching` 설정 (`LocomotionLoop` 및 `IdleLoop`가 아니면 `true`).

5. **Thread-Safe Getters (AnimGraph 노드 노출 함수들)**:
   - `GetThreadSafeStateControllerSelectedAnimation()`: `SelectedAnimation` 에셋 반환
   - `GetThreadSafeStateControllerSelectedAnimationBlendTime()`: `BlendTime` 반환
   - `GetThreadSafeStateControllerShouldOverrideMotionMatching()`: `bShouldOverrideMotionMatching` 반환

---

### 3.2 `ULocomotionAnimStateComponent`
- **헤더**: [`Source/ClassFeature/Public/Animation/LocomotionAnimStateComponent.h`](file:///C:/Users/I/Documents/GitHub/ArtisticSW2026/Source/ClassFeature/Public/Animation/LocomotionAnimStateComponent.h)
- **소스**: [`Source/ClassFeature/Private/Animation/LocomotionAnimStateComponent.cpp`](file:///C:/Users/I/Documents/GitHub/ArtisticSW2026/Source/ClassFeature/Private/Animation/LocomotionAnimStateComponent.cpp)

#### 주요 역할:
- 캐릭터의 물리 상태(속도, 가속도, 입력 크기, 점프/착지 플래그)를 프레임 단위로 수집하고 물리적 이동 상태(`ELocomotionState`)를 제어.
- `ELocomotionState` 열거형:
  - `Idle` (0), `Start` (1), `Locomotion` (2), `Stop` (3), `InAir` (4), `Landing` (5), `TurnInPlace` (6).
- `EStateControllerPresentationState` 열거형:
  - `None` (0), `IdleLoop` (1), `TransitionToStart` (2), `LocomotionLoop` (3), `TransitionToStop` (4), `TransitionToPivot` (5), `TransitionToJump` (6), `TransitionToLand` (7), `TurnInPlace` (8).

---

### 3.3 `ABasePlayer`
- `BasePlayer.cpp` 108번째 라인:
  ```cpp
  AnimStateComponent = CreateDefaultSubobject<ULocomotionAnimStateComponent>(TEXT("AnimStateComponent"));
  ```
- `ABasePlayer` 생성자에서 `ULocomotionAnimStateComponent`를 자동 추가하므로 Blueprint에서 별도로 추가하지 않아도 기본 탑재됨.

---

## 4. Chooser Table 계층 구조 및 전체 매핑 에셋

### 4.1 마스터 추저 테이블 (`CHT_Player_Main.uasset`)
- **경로**: `/Game/Anim_Logic/Choosers/CHT_Player_Main`
- **구조**: 최상위 추저 테이블로서 `PresentationState` 파라미터 값에 따라 아래 6개의 서브 추저 테이블을 반환함.

```
                                  [CHT_Player_Main]
                                         │
       ┌──────────────┬──────────────┬───┴──────────┬──────────────┬──────────────┐
       ▼              ▼              ▼              ▼              ▼              ▼
[CHT_Start]     [CHT_Stop]     [CHT_Pivot]    [CHT_InAir]     [CHT_Land]   [CHT_TurnInPlace]
```

---

### 4.2 서브 추저 테이블별 포함 애니메이션 시퀀스 에셋 목록 (전수 조사)

#### 1) `CHT_Player_Start.uasset` (출발 원샷 22종)
- **Forward Run**: `M_Neutral_Run_Start_F_Lfoot`, `M_Neutral_Run_Start_F_Rfoot`
- **Forward-Left**: `M_Neutral_Run_Start_FL_Lfoot`, `M_Neutral_Run_Start_FL_Rfoot`
- **Forward-Right**: `M_Neutral_Run_Start_FR_Lfoot`, `M_Neutral_Run_Start_FR_Rfoot`
- **Backward**: `M_Neutral_Run_Start_B_Lfoot`, `M_Neutral_Run_Start_B_Rfoot`
- **Backward-Left**: `M_Neutral_Run_Start_BL_Lfoot`, `M_Neutral_Run_Start_BL_Rfoot`
- **Backward-Right**: `M_Neutral_Run_Start_BR_Lfoot`, `M_Neutral_Run_Start_BR_Rfoot`
- **Left-Right / Right-Left**: `M_Neutral_Run_Start_LR_Lfoot`, `M_Neutral_Run_Start_LR_Rfoot`, `M_Neutral_Run_Start_RL_Lfoot`, `M_Neutral_Run_Start_RL_Rfoot`
- **Sprint**: `M_Neutral_Sprint_Start_F_Lfoot`, `M_Neutral_Sprint_Start_F_Rfoot`, `M_Neutral_Sprint_Start_FL_Lfoot`, `M_Neutral_Sprint_Start_FL_Rfoot`, `M_Neutral_Sprint_Start_FR_Lfoot`, `M_Neutral_Sprint_Start_FR_Rfoot`

#### 2) `CHT_Player_Stop.uasset` (멈춤 원샷 22종)
- **Run Stops**: `M_Neutral_Run_Stop_F_Lfoot`, `M_Neutral_Run_Stop_F_Rfoot`, `M_Neutral_Run_Stop_FL_Lfoot`, `M_Neutral_Run_Stop_FL_Rfoot`, `M_Neutral_Run_Stop_FR_Lfoot`, `M_Neutral_Run_Stop_FR_Rfoot`, `M_Neutral_Run_Stop_B_Lfoot`, `M_Neutral_Run_Stop_B_Rfoot`, `M_Neutral_Run_Stop_BL_Lfoot`, `M_Neutral_Run_Stop_BL_Rfoot`, `M_Neutral_Run_Stop_BR_Lfoot`, `M_Neutral_Run_Stop_BR_Rfoot`, `M_Neutral_Run_Stop_LR_Lfoot`, `M_Neutral_Run_Stop_LR_Rfoot`, `M_Neutral_Run_Stop_RL_Lfoot`, `M_Neutral_Run_Stop_RL_Rfoot`
- **Sprint Stops**: `M_Neutral_Sprint_Stop_F_Lfoot`, `M_Neutral_Sprint_Stop_F_Rfoot`, `M_Neutral_Sprint_Stop_FL_Lfoot`, `M_Neutral_Sprint_Stop_FL_Rfoot`, `M_Neutral_Sprint_Stop_FR_Lfoot`, `M_Neutral_Sprint_Stop_FR_Rfoot`

#### 3) `CHT_Player_Pivot.uasset` (방향 전환 원샷 16종)
- `M_Neutral_Run_Pivot_F_B_Lfoot`, `M_Neutral_Run_Pivot_F_B_Rfoot` (전방 ➔ 후방 180도 피벗)
- `M_Neutral_Run_Pivot_B_F_Lfoot`, `M_Neutral_Run_Pivot_B_F_Rfoot` (후방 ➔ 전방 180도 피벗)
- `M_Neutral_Run_Pivot_FL_BR_Lfoot`, `M_Neutral_Run_Pivot_FL_BR_Rfoot`
- `M_Neutral_Run_Pivot_FR_BL_Lfoot`, `M_Neutral_Run_Pivot_FR_BL_Rfoot`
- `M_Neutral_Run_Pivot_BL_FR_Lfoot`, `M_Neutral_Run_Pivot_BL_FR_Rfoot`
- `M_Neutral_Run_Pivot_BR_FL_Lfoot`, `M_Neutral_Run_Pivot_BR_FL_Rfoot`
- `M_Neutral_Run_Pivot_LR_RR_Lfoot`, `M_Neutral_Run_Pivot_LR_RR_Rfoot`
- `M_Neutral_Run_Pivot_RL_LL_Lfoot`, `M_Neutral_Run_Pivot_RL_LL_Rfoot`

#### 4) `CHT_Player_InAir.uasset` (점프 원샷 20종)
- `M_Neutral_Jump_F_Start_Stand_Lfoot`, `M_Neutral_Jump_F_Start_Stand_Rfoot`
- `M_Neutral_Jump_F_Start_Sprint_Lfoot`, `M_Neutral_Jump_F_Start_Sprint_Rfoot`
- `M_Neutral_Jump_F_Off_Run_Lfoot`, `M_Neutral_Jump_F_Off_Run_Rfoot`
- `M_Neutral_Jump_B_Off_Run_Lfoot`, `M_Neutral_Jump_B_Off_Run_Rfoot`
- `M_Neutral_Jump_LL_Off_Run_Lfoot`, `M_Neutral_Jump_LL_Off_Run_Rfoot`
- `M_Neutral_Jump_RL_Off_Run_Lfoot`, `M_Neutral_Jump_RL_Off_Run_Rfoot`
- `M_Neutral_Jump_F_Start_Cliff_Lfoot`, `M_Neutral_Jump_F_Start_Cliff_Rfoot`
- `M_Neutral_Jump_B_Start_Lfoot`, `M_Neutral_Jump_B_Start_Rfoot`
- `M_Neutral_Jump_LL_Start_Lfoot`, `M_Neutral_Jump_LL_Start_Rfoot`, `M_Neutral_Jump_RL_Start_Lfoot`, `M_Neutral_Jump_RL_Start_Rfoot`

#### 5) `CHT_Player_Land.uasset` (착지 원샷 28종)
- `M_Neutral_Jump_F_Land_Run_Heavy_Lfoot`, `M_Neutral_Jump_F_Land_Run_Heavy_Rfoot`
- `M_Neutral_Jump_F_Land_Run_Light_Lfoot`, `M_Neutral_Jump_F_Land_Run_Light_Rfoot`
- `M_Neutral_Jump_F_Land_Sprint_Heavy_Lfoot`, `M_Neutral_Jump_F_Land_Sprint_Heavy_Rfoot`
- `M_Neutral_Jump_F_Land_Sprint_Light_Lfoot`, `M_Neutral_Jump_F_Land_Sprint_Light_Rfoot`
- `M_Neutral_Jump_F_Land_Stand_Heavy_Lfoot`, `M_Neutral_Jump_F_Land_Stand_Heavy_Rfoot`
- `M_Neutral_Jump_F_Land_Stand_Light_Lfoot`, `M_Neutral_Jump_F_Land_Stand_Light_Rfoot`
- `M_Neutral_Jump_B_Land_Stand_Heavy_Lfoot`, `M_Neutral_Jump_B_Land_Stand_Heavy_Rfoot`, `M_Neutral_Jump_B_Land_Stand_Light_Lfoot`, `M_Neutral_Jump_B_Land_Stand_Light_Rfoot`, `M_Neutral_Jump_B_Land_Run_Heavy`, `M_Neutral_Jump_B_Land_Run_Light`
- `M_Neutral_Jump_LL_Land_Stand_Heavy_Lfoot`, `M_Neutral_Jump_LL_Land_Stand_Heavy_Rfoot`, `M_Neutral_Jump_LL_Land_Stand_Light_Lfoot`, `M_Neutral_Jump_LL_Land_Stand_Light_Rfoot`, `M_Neutral_Jump_LL_Land_Run_Heavy`, `M_Neutral_Jump_LL_Land_Run_Light`
- `M_Neutral_Jump_RL_Land_Stand_Heavy_Lfoot`, `M_Neutral_Jump_RL_Land_Stand_Heavy_Rfoot`, `M_Neutral_Jump_RL_Land_Stand_Light_Lfoot`, `M_Neutral_Jump_RL_Land_Stand_Light_Rfoot`, `M_Neutral_Jump_RL_Land_Run_Heavy`, `M_Neutral_Jump_RL_Land_Run_Light`

#### 6) `CHT_Player_TurnInPlace.uasset` (제자리 회전 4종)
- `M_Neutral_Stand_Turn_090_L`, `M_Neutral_Stand_Turn_090_R`
- `M_Neutral_Stand_Turn_180_L`, `M_Neutral_Stand_Turn_180_R`

---

## 5. ABP_Player (AnimBlueprint) 애니메이션 그래프 구조

`ABP_Player` AnimGraph 내부의 노드 배선도 및 세부 설정입니다.

```
 [Get Current Active Pose Search Database Thread Safe]
                         │
                         ▼
                 [Motion Matching] ─────────► (False Pose) ──┐
                                                             ├──► [Blend Poses by bool] ──► [Locomotion Pose]
 [Get Thread Safe Selected Animation]                        │             ▲
                         │                                   │             │
                         ▼                                   │   (Active Value)
                   [Blend Stack]    ─────────► (True Pose)  ──┘             │
                                                        [Get Thread Safe Should Override Motion Matching]
```

### 상세 핀 연결 사양:
1. **`Blend Stack` 노드**:
   - `Animation Asset` 핀 ◄ `GetThreadSafeStateControllerSelectedAnimation()`
   - `Blend Time` 핀 ◄ `GetThreadSafeStateControllerSelectedAnimationBlendTime()`
2. **`Blend Poses by bool` 노드**:
   - `Active Value` 핀 ◄ `GetThreadSafeStateControllerShouldOverrideMotionMatching()`
   - **`True Pose` 핀 ◄ `Blend Stack` 출력 포즈** (원샷 애니메이션 선택 시 덮어쓰기)
   - **`False Pose` 핀 ◄ `Motion Matching` 출력 포즈** (지속 이동/대기 포즈 출력)

---

## 6. 트러블슈팅 및 진행된 버그 수정 이력

### 6.1 `ContextData entry 1` 누락 크래시 및 파라미터 보완
- **증상**: Chooser 평가 시 `ContextData entry 1` 경고 및 에셋 리턴 불가.
- **수정**: `FChooserEvaluationContext`에 `FChooserPlayerSettings`뿐만 아니라 출력용 구조체인 **`FS_ChooserOutputs`**를 `AddStructParam()`으로 추가하여 Chooser 행들의 조건 평가 충족.

### 6.2 Chooser `NULL` 반환 (`[MM_CHOOSER_EMPTY]`) 및 평가 순서 교정
- **증상**: `State=1 (Start)` 및 `State=2 (Stop)`일 때 `[MM_CHOOSER_EMPTY]` 에러 로그가 찍히며 Chooser Table이 `NULL` 반환.
- **원인**: C++ `EvaluateStateControllerPlaybackHold()`에서 Chooser Table을 평가하는 `EvaluateObjectChooserBase()` 호출 이후(라인 3039)에 `StateControllerPresentationState` 속성을 업데이트하고 있었음.
- **수정**: Chooser Table 평가 **직전에** `StateControllerPresentationState`, `StateControllerMovementDirection`, `StateControllerGait` 등의 `UPROPERTY` 변수들을 먼저 동기화하여 해결.

### 6.3 AnimGraph `Blend Poses by bool` 핀 연결 반대 교정
- **증상**: 원샷 재생 시 계속 Motion Matching만 재생되고 멈춤 시 `SelectedAnimation`이 `None`이면서 T-pose로 굳음.
- **원인**: `Blend Poses by bool`의 `True Pose`에 Motion Matching이, `False Pose`에 Blend Stack이 연결되어 있어 `bShouldOverrideMotionMatching`이 true일 때 Motion Matching을 출력하고 있었음.
- **수정**: `True Pose` ➔ `Blend Stack`, `False Pose` ➔ `Motion Matching`으로 핀 교정.

---

## 7. 현재 미해결 버그 현상 및 차기 AI 에이전트 분석 가이드

### 🚨 현재 남아있는 결함 현상
1. C++ 출력 로그상 `[MM_CHOOSER_EVAL]`에서는 Chooser Table이 `M_Neutral_Sprint_Start_F_Lfoot` (2.50s), `M_Neutral_Jump_F_Off_Run_Lfoot` (1.60s) 에셋을 **100% 정상 추출하고 있음**.
2. 그러나 **PIE 실행 시 캐릭터 상에서 원샷 애니메이션이 시각적으로 렌더링되지 않고 씹히거나 T-pose/Idle 상태로 유지되는 현상이 계속됨.**

---

### 🔍 차기 담당 AI가 집중 검토해야 할 5가지 정밀 분석 가이드

#### 1) Chooser Table 에디터 파라미터 순서 및 열거형 타입 매핑 (가장 유력)
- 에디터에서 `CHT_Player_Main.uasset`을 열어, **파라미터 (Parameters)** 항목에 다음 3개가 순서대로(Index 0, 1, 2) 등록되어 있는지 점검:
  - `Index [0]`: Class Provider (`MotionMatchingAnimInstance`)
  - `Index [1]`: Struct Provider (`ChooserPlayerSettings`)
  - `Index [2]`: Struct Provider (`FS_ChooserOutputs` / `S_ChooserOutputs`)
- `CHT_Player_Main`의 `PresentationState` 열거형 컬럼이 C++ `EStateControllerPresentationState` (1=IdleLoop, 2=TransitionToStart, 3=LocomotionLoop, 4=TransitionToStop, 5=TransitionToPivot, 6=TransitionToJump, 7=TransitionToLand)와 **정수 값이 정확히 1:1 매칭되는지** 확인. (만약 Chooser 테이블 내부에서 `EProject_JStateControllerPresentationState` 등을 참조하고 있다면 정수 인덱스가 어긋나 잘못된 행을 선택하거나 `NULL`을 리턴함).

#### 2) UE 5.7 `Blend Stack` 노드의 포즈 샘플링 트리거 방식
- 언리얼 엔진 5.7의 `Blend Stack` 애니메이션 노드는 `Animation Asset` 핀에 전달되는 포즈 에셋 레퍼런스 포인터가 변경되지 않으면 애니메이션을 새로 믹스에 푸시하지 않을 수 있음.
- C++에서 `StateControllerSelectedAnimation`을 전달할 때, 동일 에셋이 지정되더라도 Blend Stack이 샘플 재생을 시작하도록 하는 **트리거 시그널(또는 에셋 포인터 인벤토리 지우기)**이 필요한지 검토.

#### 3) AnimGraph 상류의 `PoseHistory` (또는 `PoseSearchHistoryCollector`) 노드 부재 여부
- UE5 PoseSearch 및 Blend Stack 노드는 포즈 이행 시 이전 뼈대의 관성 및 궤적 데이터를 필요로 함.
- `ABP_Player` AnimGraph 내에 `PoseHistory` (또는 `PoseSearchHistoryCollector`) 노드가 생략되어 `Blend Stack`이 주입받을 관성 포즈 데이터를 얻지 못하고 렌더링을 캔슬하는지 확인.

#### 4) Game Thread ➔ Worker Thread간 쓰레드 동기화 타임랩스
- `EvaluateStateControllerPlaybackHold()`는 Game Thread 틱에서 동작하여 `ThreadSafeData.StateController.SelectedAnimation`에 에셋을 작성함.
- Worker Thread에서 AnimGraph를 평가할 때 `GetThreadSafeStateControllerSelectedAnimation()` 값이 읽히기 전에 `StateControllerPlaybackHoldElapsed` 카운터 소실에 의해 `nullptr`로 클리어되는 1프레임 레이스 조건(Race Condition)이 존재하는지 분석.

#### 5) `Two Way Blend` vs `Blend Poses by bool`의 노드 평가(Evaluation) 특성
- `Blend Poses by bool` 노드가 `Active Value` 변경 시 파이프라인에서 inactive 브랜치를 완전히 틱하지 않아 `Blend Stack`이 애니메이션 진행 시간을 틱받지 못하고 프레임 0에서 멈추는지 점검.
