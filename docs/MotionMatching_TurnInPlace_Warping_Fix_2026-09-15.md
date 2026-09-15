# Turn In Place (TIP) 잔류 Orientation Warping 간섭 및 0.25초 스냅백 이슈 해결 (2026-09-15)

## 1. 개요

본 문서는 `ArtisticSW2026` 프로젝트의 플레이어 캐릭터(`ABP_Player_Woman`, `ABP_Player_Man`)에서 이동 후 정지(`Stop`) 직후 제자리 회전(`Turn In Place`)을 수행할 때 발생했던 하체 지연 및 0.25초 스냅백(Snap-back) 현상의 원인 분석과 아키텍처 개선 사항을 정리한 기술 문서이다.

---

## 2. 문제 현상

- **발생 상황**: 캐릭터가 달리다가 정지(`Stop`)한 직후 마우스 카메라를 회전하여 제자리 턴(`Turn In Place`, 이하 TIP)을 시도할 때 발생.
- **시각적 증상**:
  1. 턴 초반 약 0.25초(250ms) 동안 캐릭터의 하체가 즉시 회전을 따라오지 못하고 비정상적으로 슬라이딩하며 지연됨.
  2. 정확히 0.25초가 되는 순간, 골반(Pelvis)과 루트(Root) 본이 원래 각도로 **순간 50도 가량 급격히 튕겨 돌아감(스냅백)**.
  3. 스냅백 직후 순수 TIP 회전 애니메이션이 다시 돌기 시작하여 "안 따라오다가 스윽 움직이다가 원래 자리로 팍 갔다가 다시 움직이는" 심한 덜컹거림이 발생.
- **성별 간 차이**:
  - 남캐(`ABP_Player_Man`)는 단순 전방 직진 정지(`Forward Stop`) 위주 테스트로 인해 정지 시 워핑 각도가 0도였기 때문에 증상이 표면화되지 않음.
  - 여캐(`ABP_Player_Woman`)는 대각선 감속/정지 모션(`M_Relaxed_Run_Stop_F_Lfoot` 등)으로 인해 정지 시 워핑 각도가 ~45도로 크게 남아 증상이 매우 두드러짐.

---

## 3. 원인 분석 (스모킹 건)

### 3.1 프레임 단위 로그 데이터 검증
인게임 프레임 추적 로그(`[TIP_DIAG]`)를 통해 8ms(1프레임) 사이의 본 트랜스폼 급변 포착:
- **`t = 0.250s` (Frm 30)**: `RootW_Yaw = 94.8°`, `PelvisW_Yaw = -44.4°`, `Hold = 0.24s`
- **`t = 0.258s` (Frm 31)**: `RootW_Yaw = 48.3°`, `PelvisW_Yaw = -96.0°`, `Hold = 0.25s`
- **변화량**: 단 1프레임 만에 Root가 `-46.5°`, Pelvis가 `-51.6°` 급격하게 역회전하여 튕김.

### 3.2 C++ 코드 로직 상의 근본 원인
1. **`StateControllerPostOneShotWarpingRemainingTime = 0.25f`의 잔류 누수**:
   - `EvaluateStateControllerPlaybackHold`에서 직전 원샷(Stop/Land/Jump/Pivot 등)이 종료될 때, 다음 상태로 부드럽게 넘어가도록 직전 워핑 각도를 보존하는 타이머(`PostOneShotWarpingRemainingTime = 0.25f`)가 동작함.
   - 이 타이머는 본래 원샷에서 `LocomotionLoop`(모션 매칭 연속 루프)로 전환될 때의 0도 튐을 방지하기 위한 것이었으나, **`TurnInPlace`로 전환될 때도 예외 처리 없이 잔류 워핑 각도(~45°)와 0.25초 타이머가 그대로 상속**됨.
2. **`bInPostOneShotBlendOut` 조건에 의한 Warping Node 강제 활성화**:
   - `TurnInPlace`는 단발성 방향 이동이 아니므로 `bDirectStrafeOneShot`에 포함되지 않음.
   - 따라서 TIP 상태임에도 불구하고 `!bDirectStrafeOneShot && PostOneShotWarpingRemainingTime > 0.0f` 조건이 성립하여 `bInPostOneShotBlendOut = true`가 됨.
   - 그 결과 `CombatStateOrientationWarpingAlpha = 1.0f`, `Angle = 45.0°`가 AnimGraph의 `Orientation Warping` 노드로 전달되어 회전 애니메이션의 하체를 억지로 비틀어놓음.
3. **0.25초 만료 순간의 스냅백**:
   - 정확히 0.25초(`Hold = 0.25s`)가 경과하자 타이머가 만료(`0.0f`)되어, `CombatStateOrientationWarpingAlpha`가 1프레임 만에 `1.0f -> 0.0f`로 뚝 떨어짐.
   - Orientation Warping 노드가 단숨에 꺼지면서 억지로 비틀려 있던 골반과 루트가 51.6도 뒤로 팍 튕기며 원래 애니메이션 포즈로 복귀함.

---

## 4. 해결 및 아키텍처 개선

### 4.1 TIP 전용 Orientation Warping 완전 격리 (Isolation)
`TurnInPlace`는 자체적인 제자리 회전 시퀀스와 Root Yaw Steering 커브(`enable_turninplacesteering`)를 통하여 액터 회전을 제어하므로, 이전 이동/정지의 워핑 각도를 일절 상속받지 않아야 한다.

`MotionMatchingAnimInstance.cpp`:
1. **TIP 진입 시 잔류 워핑 즉시 소멸**:
   ```cpp
   if (bStateChanged && bPreviousWasOneShot)
   {
       if (bHasStateControllerOneShotOrientationWarpingAngle &&
           DesiredState != EStateControllerPresentationState::TurnInPlace)
       {
           StateControllerPostOneShotWarpingRemainingTime = 0.25f;
           StateControllerPostOneShotWarpingAngle = StateControllerOneShotOrientationWarpingAngle;
       }
       else
       {
           StateControllerPostOneShotWarpingRemainingTime = 0.0f;
           StateControllerPostOneShotWarpingAngle = 0.0f;
       }
   }
   if (DesiredState == EStateControllerPresentationState::TurnInPlace)
   {
       StateControllerPostOneShotWarpingRemainingTime = 0.0f;
       StateControllerPostOneShotWarpingAngle = 0.0f;
   }
   ```
2. **TIP 재생 중 Warping Alpha 완전 차단**:
   ```cpp
   const bool bInPostOneShotBlendOut = !bDirectStrafeOneShot &&
       (StateControllerPlaybackHoldState != EStateControllerPresentationState::TurnInPlace) &&
       (StateControllerPostOneShotWarpingRemainingTime > 0.0f);

   float OrientationWarpingAngle = 0.0f;
   bool bHasOrientationWarpingDirection = false;
   if (StateControllerPlaybackHoldState == EStateControllerPresentationState::TransitionToStart ||
       StateControllerPlaybackHoldState == EStateControllerPresentationState::TurnInPlace)
   {
       // Start와 TurnInPlace는 전용 에셋 및 Root Yaw Steering을 사용하므로
       // 직전 이동/정지의 Orientation Warping을 일절 적용하지 않습니다.
       OrientationWarpingAngle = 0.0f;
       bHasOrientationWarpingDirection = false;
   }
   ```
   `bHasOrientationWarpingDirection = false`이므로 `CombatStateOrientationWarpingAlpha`가 항상 `0.0f`로 고정됨.

### 4.2 TIP 진입 블렌드 시간 최적화
- `StateControllerTurnInPlaceDefaultBlendTime = 0.2f` (기존 0.06s에서 0.2s로 조정):
  Chooser row에서 BlendTime을 명시하지 않았을 때도 정지/아이들 포즈에서 회전 모션으로 넘어갈 때 덜컹거림 없이 부드러운 스켈레탈 크로스페이드 제공.

### 4.3 임시 디버그 로직 정리
- 문제 분석을 위해 임시 추가했던 프레임 단위 골반/루트 트랜스폼 추적, 노드 프로퍼티 리플렉션 덤프, 온스크린 디버그 메시지(`LogTipDetailedDiagnostics`)를 프로덕션 수준으로 완전 정리하여 불필요한 연산 및 로그 오염 방지.

---

## 5. 결론 및 결과

- `Stop -> TIP` 전환 시 더 이상 이전 감속/대각 워핑 각도가 덮어씌워지지 않음.
- 회전 시작 시점부터 하체가 지연 없이 매끄럽게 따라오며, 0.25초 시점의 50도 스냅백 현상이 100% 제거됨.
- 남캐 및 여캐 모두 동일한 Unreal Skeleton 환경 하에서 일관되고 안정적인 TIP 동작을 보장.
