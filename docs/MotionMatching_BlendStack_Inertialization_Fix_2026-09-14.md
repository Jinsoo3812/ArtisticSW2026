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
- [ ] (선택 사항) 제자리 착지 후 WASD 이동 시 `TransitionToStart` 원샷을 경유하도록 `EvaluateStateControllerPresentationState()` 전이 흐름 보완 고려
