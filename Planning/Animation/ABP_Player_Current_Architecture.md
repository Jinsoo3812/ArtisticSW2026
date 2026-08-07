# ABP_Player 현재 구조

> 기준: 2026-07-22. 이 문서는 현재 `ABP_Player` AnimGraph 스크린샷과 `UMotionMatchingAnimInstance` C++ 구현을 함께 읽어 정리한 것이다.

## 한 줄 요약

`ABP_Player`는 **하체/전신 이동을 C++ 선택형 모션 매칭으로 만들고**, 그 결과에 **장비별 상체 오버레이, 몽타주, 조준 오프셋, 활 줄 IK, 발 보정**을 순서대로 덧씌우는 파이프라인이다.

```text
Pose Search Database
  -> Motion Matching
  -> Locomotion cached pose
  -> weapon upper-body overlay + weapon montage
  -> upper-body action montage + aim offset
  -> general aim offset
  -> DefaultSlot montage
  -> Bow string FABRIK -> Foot Placement -> Leg IK
  -> Pose History -> Final pose
```

## 1. 이동 기반 포즈: Locomotion

스크린샷 첫 부분은 `Get Current Active Pose Search Database Thread Safe`의 반환값을 `Motion Matching` 노드 Database 핀에 넣고, 결과를 `Locomotion` 캐시 포즈로 저장한다.

- AnimGraph는 직접 이동 상태 머신으로 보행 애니메이션을 고르지 않는다.
- `UMotionMatchingAnimInstance`가 C++에서 현재 `ELocomotionState`에 맞는 `UPoseSearchDatabase`를 매 업데이트 선택한다.
- 대표 상태: `Idle`, `Start`, `Locomotion`, `Stop`, `InAir`, `Landing`, `Combat`.
- 로컬/원격 캐릭터 및 달리기 여부에 따라 Start/Locomotion 데이터베이스가 별도로 선택될 수 있다.
- 결정된 Database는 애님 프록시에 복사되며, AnimGraph의 Thread Safe Getter가 그것을 읽는다.

따라서 **지상 이동 포즈의 주 소유자는 C++ 모션매칭 로직**이다. ABP에서는 `Motion Matching` 노드의 파라미터와 후처리 레이어를 다룬다.

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
4. `Foot Placement`: 발 접지/보정. Stop 요청 여부에 따라 C++가 다른 Plant/Interpolation 설정을 반환한다.
5. `Leg IK`.
6. `Component To Local`.
7. `Pose History`: 모션 매칭에서 다음 프레임의 과거 포즈/궤적 참조에 사용.
8. `Output Pose`.

## 5. 포즈 캐시 의미

| 캐시 포즈 | 의미 | 변경 시 주의점 |
|---|---|---|
| `Locomotion` | 모션 매칭이 선택한 이동 전신 포즈 | 지상 이동의 기준 포즈다. 직접 수정 대신 후단에서 블렌드한다. |
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

