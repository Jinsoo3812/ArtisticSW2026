# 모션 매칭 & BlendStack 전환 및 안정화 작업 정리 (2026-09-14)

## 개요

본 문서는 `ArtisticSW2026` 프로젝트의 플레이어 애니메이션(`ABP_Player_Woman`, `ABP_Player_Man`) 및 `UMotionMatchingAnimInstance` C++ 코드에서 발생했던 주요 이슈 4가지의 원인 분석과 해결 내역, 아키텍처 개선 사항을 기록한 문서이다.

---

## 1. 이슈별 원인 분석 및 해결 내역

### [이슈 1] Stop / Start 임시 진단 로그 스팸 제거 (C++)
- **현상**: 인게임 실행 중 매 틱마다 `[STOP_DIAG]`, `[START_DIAG]` 등 다량의 콘솔 로그가 반복 출력되어 성능 저하 및 로그 오염 발생.
- **원인**: 과거 정지(Stop) 및 출발(Start) 시점의 오리엔테이션 워핑 각도와 본 회전을 분석하기 위해 임시 추가했던 프레임 단위 진단 코드(`bDebugStopDiagnosticActive`, `bDebugStartDiagnosticActive`)가 남아 있었음.
- **해결**:
  - `Source/ClassFeature/Public/Animation/MotionMatchingAnimInstance.h`: 진단용 플래그 및 카운터 멤버 변수 제거.
  - `Source/ClassFeature/Private/Animation/MotionMatchingAnimInstance.cpp`: `NativeUpdateAnimation` 내 프레임별 상세 로깅 블록 제거.

---

### [이슈 2] 점프 루프 에셋 Root Motion으로 인한 Handled Ensure 크래시 (에셋)
- **현상**:
  ```text
  LogOutputDevice: Error: === Handled ensure: ===
  LogOutputDevice: Error: Ensure condition failed: bPlayingBackwards ? (CurrentPosition <= PreviousPosition) : (CurrentPosition >= PreviousPosition) 
  [File:AnimSequence.cpp] [Line: 1561] in Animation M_Neutral_Jump_Loop_Fall : bPlayingBackwards(0), PreviousPosition(4.23), Current Position(3.33)
  ```
- **원인**:
  - 체공 중 반복 루핑되는 낙하 애니메이션 `M_Neutral_Jump_Loop_Fall`에 `EnableRootMotion = true`가 켜져 있었음.
  - 루프 애니메이션이 끝 지점에서 처음으로 되감겨 재생될 때 타임코드가 `4.23s -> 3.33s`로 역전되면서, 엔진 루트 모션 추출 루틴(`AnimSequence.cpp`)에서 `CurrentPosition >= PreviousPosition` 검증 실패로 Ensure 에러 발생.
- **해결**:
  - `Content/Characters/UEFN_Mannequin/Animations/Jump/M_Neutral_Jump_Loop_Fall.uasset` 에셋의 **`EnableRootMotion` 옵션을 체크 해제(False)**하여 제자리 루프로 동작하도록 수정.

---

### [이슈 3] BlendStack 다중 BlendTo 요청 경고 스팸 (ABP)
- **현상**:
  ```text
  LogBlendStack: Warning: FAnimNode_BlendStack_Standalone multiple BlendTo requests during the same frame: only the last request will be put on this BlendStack
  ```
- **원인**:
  - `ABP_Player_Woman`의 `Blend Poses by bool` 노드 디테일에서 **`Child Update Mode`**가 **`Always Tick Children`**으로 설정되어 있었음.
  - 가중치가 0인 비활성 상태(예: 지상 이동 또는 체공 루프 중)에서도 `Blend Stack (Standalone)` 노드가 계속 백그라운드 틱(`UpdateAssetPlayer`)을 실행함.
  - 언리얼 엔진의 `FAnimNode_BlendStack_Standalone` 구조상, 만료된 과거 플레이어는 `Evaluate_AnyThread`(`PopLastAnimPlayer`)에서 제거(pop)되어야 함. 그러나 가중치가 0인 브랜치는 `Evaluate`가 전혀 호출되지 않으므로, 플레이어가 제거되지 않고 지속적으로 누적(`MaxActiveBlends + 2` 초과)되어 매 프레임 경고를 뿜어냄.
- **해결**:
  - `ABP_Player_Woman` / `ABP_Player_Man` 내 `Blend Poses by bool` 노드의 **`Child Update Mode`를 `Default`로 수정**. 가중치가 0인 비활성 브랜치는 불필요하게 틱되지 않도록 방지.

---

### [이슈 4] Blend Stack -> Motion Matching 전환 시 움직임 튐 / 발 꼬임 (ABP)
- **현상**:
  - 제자리 착지(Land)나 제자리 회전(Turn In Place) 등 `Blend Stack` 단발성 에셋 재생 후, WASD 이동 또는 마우스 회전으로 `Motion Matching` 노드로 넘어갈 때 캐릭터 하체가 1프레임 튀거나 발이 땅 속으로 파고들고 미끄러지는 현상 발생.
- **원인**:
  1. **Standard Blend(단순 크로스페이드)의 한계**: `Blend Poses by bool`이 단순 선형 스켈레탈 보간을 수행하므로, 착지 시점의 디딘 발(예: 오른발)과 모션 매칭 러닝 루프의 첫 프레임(예: 왼발 전진)이 0.2초 동안 서로 반대 방향으로 엇갈리며 보간되어 발 꼬임/슬라이딩 발생.
  2. **운동량 단절**: 직전 모션의 본 속도와 가속도(물리적 관성)가 완전히 끊긴 채 정적인 포즈 대 포즈로 합성됨.
  3. **Inertialization 부재**: `Project_J`와 달리 `Blend Poses by bool` 후단에 관성화 노드가 누락되어 있었음.
- **해결**:
  1. `Blend Poses by bool` 출력 핀과 `[Locomotion]` 포즈 캐시 노드 사이에 **`Inertialization` 노드 추가** (Blend Time: 0.2s).
  2. `Blend Poses by bool`의 **`Transition Type`을 `Standard Blend` → `Inertialization`**으로 변경 (False Blend Time: 0.2s).
  3. 관성화 블렌드가 직전 Blend Stack 포즈의 본 속도/가속도 벡터를 캡처하여 Motion Matching 새 루프 포즈로 부드럽게 감쇠/흡수시킴으로써 발 꼬임과 1프레임 팝핑(Popping) 완벽 제거.

### [이슈 5 / 신규 기능] 질주(Sprint) 카메라/화면 연출 및 물(수영/얕은 물) 이벤트 기반 질주 차단 (C++)
- **배경 및 요구사항**:
  - 스태미나 게이지가 없는 상시 질주 환경에 맞추어, 어지럽지 않고 눈이 편안한 은은한 속도감 연출(카메라 거리 후퇴 + 다이내믹 FOV + 외곽 비네팅) 적용.
  - 수영 상태 및 다리만 잠기는 얕은 물(`bIsInShallowWater`)에서는 빠른 수영/질주가 불가능하므로 질주를 철저히 차단하고, 카메라와 실제 이동 속도(300 감속)가 일치하도록 연결.
  - `Tick()`에서 매 프레임 질주 상태를 폴링(Polling)하던 비효율적인 구조를 탈피하여, 언리얼 엔진 이벤트 및 상태 전이 콜백 기반의 고성능 아키텍처로 개편.
- **해결 및 구현 내역**:
  1. **물(완전 입수 & 얕은 물)에서의 질주 차단**:
     - `CanSprintFromInput()` 및 `CanSprintFromServerState()`에 `IsCustomSwimming()`(수영)뿐만 아니라 `IsInShallowWater()`(얕은 물) 검사를 추가하여 물속에서의 질주 시도를 완전 차단.
     - 얕은 물 진입 시 `LocomotionAnimStateComponent`에서 `MaxWalkSpeed = 300.0f` 감속 적용과 동시에 `CachedBasePlayer->StopSprint()`를 호출하여 질주 상태를 즉시 해제.
  2. **이벤트 기반 단발성 질주 해제 (`OnMovementModeChanged` & `ApplySwimmingGameplayState`)**:
     - `ABasePlayer::OnMovementModeChanged()`를 오버라이드하여 무브먼트 모드가 수영으로 전환되는 순간 단 1회 즉각 `StopSprint()` 호출.
     - `SwimmingComponent::ApplySwimmingGameplayState(true)`에서도 입수 시 `Player->StopSprint()`를 1회 호출하여 어빌리티 캔슬과 함께 즉각 연동.
     - `BasePlayer::Tick()`에서 매 프레임 불필요하게 돌던 `RefreshSprintFromInput()` 호출을 완전 제거하여 CPU 낭비 방지 (입력 이벤트 `DoMove`, `StopMoveInput`, `StartSprint`, `StopSprint`에서만 호출).
  3. **질주 카메라 및 화면 연출 (`BasePlayer.h/cpp`)**:
     - `SprintTargetArmLength = 450.f` (기본 400에서 +50 은은하게 후퇴)
     - `SprintFOV = 96.f` (기본 90에서 +6 부드러운 시야 확장)
     - `SprintCameraInterpSpeed = 4.5f` (0.3~0.4초에 걸쳐 부드럽게 보간되어 덜컹거림 방지)
     - `SprintVignetteIntensity = 0.25f` (질주 시 화면 외곽에 은은한 비네팅을 주어 중앙 몰입감 형성)
     - 조준(Aiming) 및 스나이핑(Sniping) 상태가 질주 카메라보다 항상 높은 우선순위를 갖도록 처리.
     - 질주 종료 시 줌아웃에서 기본 상태로 돌아올 때도 `SprintCameraInterpSpeed`로 부드럽게 감쇠 복귀.

---

### [이슈 6] 이동/방향 전환 시 Foot Placement 과도 접지 완화 및 Run/Sprint Additive Lean 구현 (C++)
- **배경 및 현상**:
  1. **Foot Placement 과도 접지**: 방향 전환 및 이동 시 발이 지면에 과하게 붙어있어(Lock), 회전 시 발목이 뒤틀리거나 뻣뻣하게 끌리는 어색함 발생.
  2. **Additive Lean 차등 적용**: 달리기(Run)와 전력 질주(Sprint) 시 일반 달리기에서는 살짝만 기울어지고 전력 질주에서는 역동적으로 기울어지도록 개선 요구.
- **원인 분석**:
  1. 엔진 기본 `FFootPlacementPlantSettings`의 `UnplantAngle`(45도)과 `UnplantRadius`(35cm)가 너무 커서 캐릭터가 45도 이상 회전하기 전까지 발을 지면에 강제로 고정시킴.
  2. C++ `NativeUpdateAnimation`에서 이동 중 `FootPlacementAlpha`가 무조건 `1.0f`(100% 강제 고정)로 하드코딩되어 있었음.
  3. 로컬 가속도 기반 Lean 기능 부재.
- **해결 및 구현 내역**:
  1. **Foot Placement 파라미터 최적화**:
     - `UnplantAngle = 18.0f` (회전 시 즉각 발 잠금 해제)
     - `UnplantRadius = 15.0f` (15cm 이탈 시 즉각 해제)
     - `SpeedThreshold = 25.0f` (달리는 도중 지면 강제 락 방지)
     - `AnkleTwistReduction = 0.9f` (발목 과도 비틀림 방지)
     - `FloorLinearStiffness = 600.0f`, `FloorAngularStiffness = 350.0f` (지면 스냅 완충)
     - 신규 `LocomotionFootPlacementAlpha = 0.75f` 프로퍼티 도입: 이동 시 원본 달리기 모션 25% + IK 75%의 유연한 블렌딩 지원.
  2. **가속도 기반 Additive Lean 구현**:
     - 수평 속도 변화량과 무브먼트 컴포넌트의 가속/제동력을 이용해 정규화된 로컬 가속도 `RelativeAccelerationAmount` 계산.
     - **Run**: `RunLeanMultiplier = 0.1f` (은은하고 자연스러운 최소 기울기)
     - **Sprint**: `SprintLeanMultiplier = 1.0f` (역동적인 질주 기울기)
     - `LeanInterpSpeed = 6.0f`로 부드럽게 감쇠 보간 및 정지/체공 시 중립 복귀.
     - Thread-Safe Getter 노드 제공: `Get ThreadSafe Lean Amount`, `Get ThreadSafe Lean LR`, `Get ThreadSafe Relative Acceleration Amount`.

---

## 2. 권장 AnimGraph 구조 다이어그램

```text
                                  +-----------------------+
                                  |  Blend Stack          |
                                  +-----------------------+
                                              |
                                              v (True Pose)
+------------------------------+    +------------------------------------+    +-------------------+    +--------------+
| ShouldOverrideMotionMatching | -> | Blend Poses by bool                | -> | Inertialization   | -> | [Locomotion] |
+------------------------------+    | - Child Update Mode: Default       |    | (Blend Time: 0.2s)|    | (Pose Cache) |
                                    | - Transition Type: Inertialization |    +-------------------+    +--------------+
                                    | - False Blend Time: 0.2s           |
                                    +------------------------------------+
                                              ^ (False Pose)
                                              |
                                  +-----------------------+
                                  | Motion Matching 노드  |
                                  +-----------------------+
```

---

## 3. 체크리스트 및 향후 작업 가이드

- [x] `MotionMatchingAnimInstance` C++ 임시 진단 로그 제거
- [x] `M_Neutral_Jump_Loop_Fall` 에셋 `EnableRootMotion = false` 확인
- [x] `ABP_Player_Woman` `Blend Poses by bool` Child Update Mode = Default 확인
- [x] `ABP_Player_Woman` `Inertialization` 노드 배치 및 Transition Type = Inertialization 확인
- [x] `ABP_Player_Man` 동기화 확인
- [x] 수영 중 질주(Sprint) 차단 및 입수 시 즉시 질주 해제 구현
- [x] 질주 시 다이내믹 FOV(90->96), 카메라 거리(400->450), 외곽 비네팅(0->0.25) 부드러운 보간 구현
- [x] 이동/방향 전환 시 Foot Placement 과도 접지 완충 (`UnplantAngle = 18도`, `LocomotionFootPlacementAlpha = 0.75f`)
- [x] Additive Lean 구현 및 Run(`0.1`) / Sprint(`1.0`) 가속도 기반 기울기 분리
- [ ] (선택 사항) 제자리 착지 후 WASD 이동 시 `TransitionToStart` 원샷을 경유하도록 `EvaluateStateControllerPresentationState()` 전이 흐름 보완 고려

## 4. 배 위 Foot Placement 잠금 분리 (2026-10-04)

배가 파도로 움직일 때 월드 접지점을 유지하려는 발 잠금이 다리를 끌어당겼다. Foot Placement와 Leg IK는 계속 사용하고, 배 위에서만 Plant Settings의 `Lock Type`을 `Unlocked`로 선택한다. 육지에서는 기존 일반/정지 프리셋을 복원해 언덕의 발 높이·경사 정렬과 골반 보정을 유지한다.

### 4.1. 공통 C++ 처리와 AnimBP 설정

남녀 ABP는 모두 `UMotionMatchingAnimInstance`를 부모로 사용한다. 성별 분기 없이 동일한 Getter로 설정을 받는다.

- 일반 보행은 Walking/NavWalking 상태의 Movement Base 소유자와 부착 부모를 통해 `AShip`을 판정한다. 베이스가 `BuoyancyRoot`여도 동작하며, 낙하 중 오래된 베이스는 배 위 보행으로 취급하지 않는다.
- 조종자는 이동이 꺼지고 컨트롤러 소유 관계가 바뀌므로 플레이어의 부착 부모 계층으로 판정한다. 배에 부착된 대포 등 중간 액터도 지원한다.
- 설정은 게임 스레드에서 모션 매칭의 거리별 평가 생략 전에 갱신하고, 애니메이션 Getter는 프록시 스냅샷만 읽는다. 전체 `FAnimThreadSafeData` 재생성 때도 배 컨텍스트와 Plant/Interpolation Settings를 보존해 기본 잠금 값으로 덮어쓰지 않는다.
- 추가 월드 액터 순회, 프레임별 충돌 쿼리, RPC, 복제 프로퍼티를 만들지 않는다. 클라이언트는 기존 이동 베이스와 복제된 부착 관계로 시각 설정을 선택하며 전용 서버의 프레임별 애니메이션 평가 생략은 유지한다.

`ABP_Player_Woman`과 `ABP_Player_Man`의 **Foot Placement → Pelvis Settings → Actor Movement Compensation Mode**는 모두 `Component Space`로 설정한다. UE 5.7 엔진 설명상 `Sudden Motion Only`는 움직이는 발판을 지원하지 않는다. 두 ABP의 단순·복잡 트레이스 채널은 `FootPlacement`를 사용한다.

Plant Settings, Interpolation Settings, Alpha의 기존 C++ Getter 연결을 유지한다. ABP나 클래스 기본 프리셋의 Lock Type을 항상 Unlocked로 고정하지 않는다. 잠금 선택은 배 컨텍스트에만 적용하고 Alpha·보간·Leg IK는 기존 튜닝을 사용한다.

### 4.2. 충돌 응답과 기존 BP 호환

기존 `FootPlacement` 채널을 그대로 사용한다. 기본 Block 응답으로 인해 피격용 선체와 상호작용 볼륨까지 발 트레이스에 잡히지 않도록 프로파일 응답을 명시한다.

| 프로파일/컴포넌트 | FootPlacement 응답 | 역할 |
| --- | --- | --- |
| ShipDeck / DeckMesh_Simple·Complex | Block | 발 접지 및 경사 보정 대상 |
| PlayerShipDamage / EnemyShipDamage | Ignore | 피격 판정용 선체 |
| Interactable | Ignore | 조타·앵커 등 상호작용 쿼리 볼륨 |
| ShipHullPhysics / BuoyancyRoot | Ignore, PhysicsOnly | 기존 배 물리 루트 |

`DefaultEngine.ini`의 프로파일과 Ship 생성자에서 기본값을 설정하고, BeginPlay에서 갑판·피격 메시 및 Interactable 컴포넌트 응답을 재적용한다. 기존 BP에 저장된 컴포넌트 템플릿에도 적용하기 위한 일회성 처리다. Pawn·Arrow·대포 응답과 배 물리/복제 정책은 유지한다.

### 4.3. 검증 결과와 재현 방법

- 엔진의 직접 `UnrealBuildTool.exe`로 `ArtisticSW2026Editor Win64 Development` 빌드 성공.
- `ArtisticSW.Animation.FootPlacement.ShipContext` 성공(오류/경고 0): 육지 프리셋, 갑판/물리 루트, 정지, 이동이 꺼진 조종자, 중간 장비 부착, 하선, 낙하 중 오래된 베이스, 전체 NativeUpdateAnimation 이후 스냅샷 보존 및 충돌 프로파일 검사.
- `ArtisticSW.Animation.FootPlacement.PlayerGraphContract` 성공(오류 0): 저장된 남녀 ABP의 공통 부모, Foot Placement 노드 존재, Component Space 보정 및 단순·복잡 FootPlacement 채널 검사. 로드 시 기존 Foley Notify/실험용 Pose Search 참조 누락 등의 경고 183건은 남아 있다. 이 변경에서는 관련 없는 애니메이션 에셋을 수정하지 않았다.
- Play_Test의 실제 멀티플레이 PIE 클라이언트에서 여캐 AutonomousProxy/Walking, Kelvin Movement Base, `Unlocked`, Alpha 0.75를 확인했다. 전역 잠금은 true였고 양쪽 발의 단순·복잡 구 트레이스(반경 10cm, 시작 +40cm/끝 -100cm)가 모두 `DeckMesh_Simple`을 적중했다. 피격 메시와 조타/앵커 볼륨의 Ignore 응답도 확인했다. 사용자가 수정 후 정상 동작을 확인했다.

자동화 실행은 `Automation RunTests ArtisticSW.Animation.FootPlacement`를 사용한다. 보고서는 `Saved/Automation/ShipFootPlacement/index.json`, 로그는 `Saved/Logs/ShipFootPlacementAutomation.log`에 생성되며 커밋에는 포함하지 않는다. 조사용 임시 Python 스크립트는 정리했고 제품 코드에 진단 로그·콘솔 명령·디버그 드로잉을 추가하지 않았다.

PIE 비교 시 `a.AnimNode.FootPlacement.Enable.Lock 1`을 사용한다. `0`은 육지까지 전역 잠금을 해제하므로 조건부 동작을 검증할 수 없다. 모든 위치·조종 전환·원격 플레이어의 시각 품질 검증을 완료한 것은 아니며, 추가 화면 검증은 언덕, 배 정지/보행, 조종/해제, 점프/착지와 소유/원격 캐릭터를 비교한다.
