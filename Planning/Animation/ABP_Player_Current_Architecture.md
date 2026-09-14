# ABP_Player 현재 구조

> 기준: 2026-09-14. 이 문서는 현재 `ABP_Player_Woman` / `ABP_Player_Man` AnimGraph와 `UMotionMatchingAnimInstance` C++ 구현을 함께 읽어 정리한 것이다.

## 한 줄 요약

`ABP_Player`는 **하체/전신 이동을 C++ State Controller 기반의 하이브리드 파이프라인(Blend Stack 단발성 에셋 + 연속 Motion Matching 루프)으로 생성**하고, `Inertialization(관성화)`으로 매끄럽게 결합한 뒤, 그 결과에 **장비별 상체 오버레이, 몽타주, 조준 오프셋, 활 줄 IK, 발 보정**을 순서대로 덧씌우는 파이프라인이다.

```text
[C++ State Controller & Chooser]
  ├─> Blend Stack (One-Shot: Land, TIP, Start, Stop)
  └─> Motion Matching (Continuous Loop: Walk/Run/Sprint/InAir)
        ↓
  Blend Poses by bool (Active: ShouldOverrideMotionMatching, Child Update Mode: Default)
        ↓
  Inertialization (Transition Type: Inertialization, 0.2~0.3s)
        ↓
  Locomotion cached pose
        ↓
  weapon upper-body overlay + weapon montage
        ↓
  upper-body action montage + aim offset
        ↓
  general aim offset
        ↓
  DefaultSlot montage
        ↓
  Bow string FABRIK -> Foot Placement -> Leg IK
        ↓
  Pose Search History Collector -> Final pose
```

## 1. 이동 기반 포즈: Locomotion (하이브리드 Blend Stack + Motion Matching)

과거의 단순 단일 Motion Matching 노드 방식에서 발전하여, **단발성 원샷 모션(착지, 제자리 회전, 출발/정지)은 `Blend Stack`**, **지속 이동 루프(보행/질주/체공)는 `Motion Matching`** 노드가 분담하는 GASP / Project_J 스타일 하이브리드 구조를 사용한다.

### 1.1 노드 구성 및 연결
1. **Blend Stack (Standalone)**:
   - `Get Thread Safe State Controller Selected Animation`
   - `Get Thread Safe State Controller Selected Animation Start Time`
   - `Get Thread Safe State Controller Selected Animation Should Loop`
   - `Get Thread Safe State Controller Selected Animation Blend Time`
   - 위 C++ State Controller의 선정 결과를 입력받아 단발성 에셋(Land, TIP, Start, Stop 등)을 즉시 블렌딩 재생한다.
2. **Motion Matching**:
   - `Get Current Active Pose Search Database Thread Safe`로부터 `LocomotionDatabase`, `SprintLocomotionDatabase`, `InAirDatabase`, `IdleDatabase` 등을 공급받아 연속 궤적 기반 포즈를 검색한다.
3. **Blend Poses by bool**:
   - **Active Value**: `Get Thread Safe Should Override Motion Matching` (C++에서 원샷 재생 중이고 Loop 상태가 아닐 때 True).
   - **True Pose**: `Blend Stack` 출력 포즈.
   - **False Pose**: `Motion Matching` 출력 포즈.
   - **Child Update Mode**: 반드시 **`Default`**로 설정해야 한다.  
     *(주의: `Always Tick Children`으로 설정하면 가중치가 0인 브랜치에서 `Evaluate_AnyThread`가 실행되지 않아 Blend Stack 내부 플레이어가 pop되지 않고 누수되어 `multiple BlendTo requests during the same frame` 경고가 무한 반복됨)*
   - **Transition Type**: **`Inertialization`** (블렌드 시간: 0.2s ~ 0.3s).
4. **Inertialization (관성화 노드)**:
   - `Blend Poses by bool` 출력과 `Locomotion` 포즈 캐시 노드 사이에 위치한다.
   - Blend Stack에서 Motion Matching으로 전환될 때(예: 착지/회전 중 WASD 이동 입력), 단순 크로스페이드로 인해 발생하는 **발 위상(Foot Phase) 불일치, 발 꼬임 및 1프레임 팝핑(Popping)**을 직전 모션의 본 속도/가속도 관성 감쇠를 통해 완전히 제거한다.

### 1.2 Pose Search History Collector의 위치
- `Pose Search History Collector` 노드는 메인 파이프라인의 후단(IK 직전 / 최종 포즈 출력 직전)에 배치된다.
- 이를 통해 캐릭터가 `Blend Stack`으로 착지나 제자리 회전을 하고 있을 때도 실제 화면에 렌더링된 본 트랜스폼이 히스토리에 정상 누적된다.
- 따라서 WASD 입력으로 모션 매칭이 활성화되는 첫 프레임에 현재 착지 발 위치와 완벽히 일치하는 루프 프레임을 찾아내 도킹(Docking)할 수 있다.

### 1.3 루프 애니메이션 루트 모션 규칙
- 체공/낙하 루프 애니메이션(`M_Neutral_Jump_Loop_Fall`) 등 제자리에서 반복 루핑되는 시퀀스는 **`EnableRootMotion = false`**로 설정해야 한다.
- 루프 애니메이션에 루트 모션이 켜져 있을 경우 루프 지점에서 타임코드 역전이 감지되어 `AnimSequence.cpp Handled ensure: CurrentPosition >= PreviousPosition` 크래시성 에러가 발생한다.

### 1.4 가속도 기반 가산 기울기 (Additive Lean)

달리기 및 질주 시 이동 방향 전환이나 가감속에 따라 몸체가 자연스럽게 기울어지는 기능이다.

- **원리 및 계산**:
  - `NativeUpdateAnimation`에서 액터 로컬 좌표계 기준 수평 속도 변화량(가속도)을 무브먼트 컴포넌트의 최대 가속/제동력으로 정규화하여 `RelativeAccelerationAmount`를 산출한다.
  - 이를 기반으로 `LeanAmount` (X: 좌우 횡 기울기, Y: 전후 기울기)를 계산하며, `FMath::Vector2DInterpTo`(기본 감쇠 속도 `6.0f`)로 부드럽게 보간한다.
- **Run vs Sprint 차등 배율**:
  - **일반 달리기(Run)**: `RunLeanMultiplier = 0.1f` — 과도한 흔들림 없이 아주 은은한 기울기만 연출.
  - **전력 질주(Sprint)**: `SprintLeanMultiplier = 1.0f` — 전속력 질주 및 커브 시 다이내믹하고 스포티한 기울기 연출.
  - 정지 상태이거나 체공(InAir) 중일 때는 자동으로 기울기가 0으로 부드럽게 복귀한다.
- **AnimGraph Thread-Safe 노드 제공**:
  - `Get ThreadSafe Lean LR`: BlendSpace 1D의 좌우 기울기 파라미터로 직결.
  - `Get ThreadSafe Lean Amount`: BlendSpace 2D용 (X: 좌우, Y: 전후).
  - `Get ThreadSafe Relative Acceleration Amount`: 로컬 가속도 벡터.
- **적용 위치**:
  - `Locomotion` 캐시 포즈를 만들기 직전(Inertialization 후단) 또는 `Locomotion` 캐시 포즈 출력 후단에 `BS_Lean`을 **`Apply Additive`** 노드로 결합한다.

---

## 2. 장비 상체 오버레이: WeaponPose

`Locomotion` 캐시 포즈는 무기 상체 블렌드의 Base Pose가 된다.

1. `BS_Bow`는 Thread Safe `WeaponUpperBodyDirection`, `WeaponUpperBodySpeed`를 입력으로 사용한다.
2. `Blend Poses by int`는 `WeaponUpperBodyOverlayIndex`로 무기별 오버레이 포즈를 선택한다. 스크린샷에서는 Blend Pose 0이 원본 `Locomotion`, Blend Pose 1이 활 BlendSpace이다.
3. 선택 결과를 `Layered Blend per Bone`으로 `Locomotion` 위에 적용한다. 블렌드 가중치는 `WeaponUpperBodyAlpha`이다.
4. 결과를 `WeaponPose` 캐시 포즈로 저장한다.
5. `Slot 'UpperBody'`의 몽타주를 같은 상체 마스크로 `WeaponPose`에 합친 뒤 다시 `WeaponPose`로 캐싱한다.
6. 그 뒤 `AimYaw`, `AimPitch`를 사용한 조준 오프셋이 상체에 적용된다.

`WeaponUpperBodyAlpha`는 현재 C++에서 0 또는 1이다. 장비 오버레이가 활성이고 지상 허용 상태일 때 1, 그 외 0이다.

## 3. 상체 액션과 일반 조준: GeneralAimPose

스크린샷 세 번째 부분은 무기 오버레이 이후의 상체 전투 후처리다.

- `UpperBodyActionPose` 캐시 포즈를 Base로 하고, `BS_Neutral_AO_Stand`를 `Apply Mesh Space Additive`로 적용한다.
- 이 첫 AO의 알파는 `AimOffsetAlpha`이다.
- 결과 위에 `UpperBodyAction` 포즈/슬롯을 `Layered Blend per Bone`으로 적용해 `GeneralAimPose`로 캐싱한다.
- `GeneralAimPose` 위에 다시 같은 Neutral AO를 적용한다.
- 두 번째 AO의 알파는 `BowHoldAimOffsetAlpha`이며, 활이 완전 시위 상태이고 발사 중이 아닐 때만 활성화된다.

`AimYaw`는 캐릭터 Yaw와 컨트롤러 Yaw의 차이, `AimPitch`는 컨트롤러 Pitch를 각각 제한 범위로 잘라 계산한다. 값은 C++ Game Thread에서 프록시로 복사된 뒤 AnimGraph에서 Thread Safe로 읽힌다.

## 4. 몽타주와 IK/발 보정: 최종 출력

`GeneralAimPose` 이후의 최종 후처리 순서는 다음과 같다.

1. `Slot 'DefaultSlot'`: 전신 또는 별도 그룹에 배치한 일반 몽타주.
2. `Local To Component`.
3. `FABRIK`: 활 줄 손 목표 Transform 및 `BowStringIKAlpha`를 사용한다. 활 완전 시위이고 발사 중이 아닐 때 활성화된다.
4. `Foot Placement`: 발 접지/경사면 적응 및 방향 전환 완충.
   - **핀 연결 (AnimGraph 직결)**:
     - `Alpha`: `Get ThreadSafe Foot Placement Alpha`
     - `Plant Settings`: `Get Foot Placement Plant Settings`
     - `Interpolation Settings`: `Get Foot Placement Interpolation Settings`
   - **방향 전환 및 이동 시 완충 개선 (C++ 최적화)**:
     - 언리얼 엔진 기본값(`UnplantAngle = 45도`, `UnplantRadius = 35cm`)은 회전 시 발목 뒤틀림과 발 끌림을 유발하므로, `UnplantAngle = 18.0f`, `UnplantRadius = 15.0f`, `SpeedThreshold = 25.0f`로 낮춰 방향 전환 시 즉시 발 잠금이 해제되도록 개선.
     - `AnkleTwistReduction = 0.9f`로 발목의 과도한 회전 비틀림을 방지하고, `FloorLinearStiffness = 600.0f`, `FloorAngularStiffness = 350.0f`로 바닥 스냅 충격을 완화.
   - **접지 강도 제어 (`LocomotionFootPlacementAlpha`)**:
     - 이동 중 Alpha를 1.0(100% 고정) 대신 `LocomotionFootPlacementAlpha`(기본값 `0.75f`)로 설정하여, IK 75% + 원본 달리기 애니메이션 25%가 자연스럽게 블렌딩되도록 구성.
     - ABP Class Defaults 또는 Details 패널에서 `Locomotion Foot Placement Alpha` 프로퍼티(0.0~1.0)를 통해 실시간으로 접지 감도를 미세조정 가능.
5. `Leg IK`.
6. `Component To Local`.
7. `Pose History`: 모션 매칭에서 다음 프레임의 과거 포즈/궤적 참조에 사용.
8. `Output Pose`.

## 5. 포즈 캐시 의미

| 캐시 포즈 | 의미 | 변경 시 주의점 |
|---|---|---|
| `Locomotion` | Blend Stack과 Motion Matching이 결합되어 관성화(Inertialization)된 전신 이동 포즈 | 지상 이동의 기준 포즈다. 직접 수정 대신 후단에서 블렌드한다. |
| `WeaponPose` | 장비 상체 오버레이와 `UpperBody` 몽타주가 반영된 포즈 | 무기/전투 자세의 기준이다. |
| `UpperBodyActionPose` | 상체 액션 단계에 넘기는 포즈 | 액션 몽타주와 AO의 기준 순서를 유지한다. |
| `GeneralAimPose` | 일반 조준과 활 홀드 AO까지 적용된 포즈 | 최종 전신 몽타주·IK·발 보정 직전 포즈다. |

## 6. 수영을 붙일 위치

수영은 지상 모션매칭 Database에 섞지 않는다. `ABP_Player_Swim`의 `Swim` 레이어 결과와 현재 **최종 지상 파이프라인 결과**를 `bIsSwimming`으로 전환한다.

- 지상: 현재 `ABP_Player` 체인을 그대로 사용.
- 수영: `SwimmingComponent.IsCustomSwimming()`이 true일 때 `ABP_Player_Swim`의 `Swim` 레이어 출력 사용.
- 수영 중에는 Foot Placement, Leg IK, 지상용 상체 활 오버레이를 기본적으로 적용하지 않는다. 수영용 상체 공격이 필요해질 때 별도의 swim combat 레이어를 만든다.

이 전환은 모션 매칭 노드 **앞**보다, 현재 최종 지상 체인이 완료된 **뒤**에 두는 편이 지상 캐시와 기존 전투 파이프라인을 보존하기 쉽다.

## 관련 코드

- `Source/ClassFeature/Public/Animation/MotionMatchingAnimInstance.h`
- `Source/ClassFeature/Private/Animation/MotionMatchingAnimInstance.cpp`
- `Source/ClassFeature/Public/Animation/LocomotionAnimStateComponent.h`
- `Source/ClassFeature/Private/Animation/LocomotionAnimStateComponent.cpp`
- `Source/ClassFeature/Private/Equipment/PlayerEquipmentComponent.cpp`

