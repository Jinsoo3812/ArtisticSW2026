# 상체 전투 오버레이 구조와 AI 작업 가이드

## 목적

이 문서는 장비를 든 상태에서도 모션매칭 하체 이동을 유지하면서, 무기 자세·조준·공격 몽타주를 상체 뼈에만 덧씌우는 현재 구조를 설명한다. AI에게 코드/ABP 수정을 요청할 때도 이 문서의 용어와 제약을 함께 제공한다.

## 구조 원칙

- **이동 하체**: 모션 매칭 `Locomotion` 포즈가 소유한다.
- **전투 상체**: 무기 오버레이 BlendSpace, 상체 몽타주, Aim Offset이 소유한다.
- **합성 방식**: `Layered Blend per Bone`으로 지정된 상체 뼈부터만 덮는다.
- **데이터 전달**: 게임 스레드 C++가 상태를 `FMotionMatchingAnimInstanceProxy::ThreadSafeData`에 복사하고, AnimGraph는 `Get Thread Safe ...` 노드로 읽는다.

즉, AnimGraph에서 Pawn/장비 컴포넌트를 직접 접근해 상태를 새로 계산하지 않는다. 멀티스레드 애님 업데이트 안전성 때문에 C++ Thread Safe Getter를 사용한다.

## 데이터 흐름

```text
WeaponAnimationDataAsset
  (bUseUpperBodyOverlay, Tag, Index)
        ↓
UPlayerEquipmentComponent
  GetEquippedUpperBodyOverlayTag / Index
        ↓
UMotionMatchingAnimInstance::NativeUpdateAnimation
  FAnimWeaponUpperBodyData 생성
        ↓ proxy copy
Thread Safe Getters in ABP_Player
        ↓
BS_Bow + Blend Poses by int + Layered Blend per Bone
        ↓
WeaponPose -> UpperBody Slot -> Aim Offset -> final pose
```

## 장비 데이터 계약

`UWeaponAnimationDataAsset`의 아래 값이 장비별 상체 오버레이의 계약이다.

| 필드 | 역할 |
|---|---|
| `bUseUpperBodyOverlay` | 이 장비가 상체 오버레이를 사용하는지 |
| `UpperBodyOverlayTag` | 오버레이 종류 식별 태그. 비어 있으면 장비 태그를 사용 |
| `UpperBodyOverlayIndex` | ABP의 `Blend Poses by int`에서 선택할 포즈 번호 |

현재 `ABP_Player` 스크린샷에서는 `BS_Bow`가 활 오버레이 예시다. 새 무기를 추가할 때는 장비 데이터의 Index와 ABP `Blend Poses by int`의 포즈 슬롯을 반드시 같은 번호로 맞춘다.

## C++ 활성 조건

`UMotionMatchingAnimInstance`는 매 프레임 다음 조건이 모두 참일 때 상체 오버레이를 켠다.

```text
bEnableWeaponUpperBodyOverlay
&& OverlayTag is valid
&& LocomotionState is Idle, Start, Locomotion, or Stop
```

이때 `UpperBodyAlpha = 1.0`; 아니면 `0.0`이다.

따라서 기본 구현은 공중(`InAir`), 착지(`Landing`), 수영에서는 상체 무기 오버레이를 적용하지 않는다. 이 상태에서 전투 애님이 필요하면 기존 조건을 무작정 넓히지 말고, 해당 이동 모드용 오버레이/마스크/애셋을 먼저 정의해야 한다.

## ABP 구성 요소별 책임

| 요소 | 입력 | 책임 |
|---|---|---|
| `BS_Bow` | `WeaponUpperBodyDirection`, `WeaponUpperBodySpeed` | 이동 중 활 상체 포즈 샘플링 |
| `Blend Poses by int` | `WeaponUpperBodyOverlayIndex` | 장비/상태에 맞는 오버레이 포즈 선택 |
| 첫 `Layered Blend per Bone` | Base=`Locomotion`, Blend=무기 포즈, Alpha=`WeaponUpperBodyAlpha` | 이동 하체 위에 무기 상체를 합성 |
| `Slot 'UpperBody'` | 몽타주 | 공격/장전/장비 액션 같은 상체 전용 몽타주 적용 |
| `BS_Neutral_AO_Stand` | `AimYaw`, `AimPitch` | 컨트롤러 방향을 향하도록 상체 조준 보정 |
| `BowHoldAimOffsetAlpha` | 활 완전 시위 상태 | 홀드 중 추가 조준 AO 적용 |
| FABRIK | Bow String IK Target, Alpha | 완전 시위 중 손과 활 줄 목표 정렬 |

## 상태 값 의미

| Getter | 의미 |
|---|---|
| `GetThreadSafeWeaponUpperBodyOverlayIndex()` | 장비 데이터에서 온 포즈 선택 인덱스 |
| `GetThreadSafeWeaponUpperBodyAlpha()` | 현재 상체 오버레이 활성 가중치(현재 0/1) |
| `GetThreadSafeWeaponUpperBodySpeed()` | 지상 속도 |
| `GetThreadSafeWeaponUpperBodyDirection()` | 이동 방향. 질주 중에는 옵션에 따라 0(전방)으로 고정 가능 |
| `GetThreadSafeAimYaw/Pitch()` | 캐릭터와 컨트롤러의 조준 회전 차이 |
| `GetThreadSafeBowHoldAimOffsetAlpha()` | 활 완전 시위·비발사 중일 때만 AO 가중치 |
| `GetThreadSafeBowStringIKAlpha()` | 활 줄 IK 활성 가중치 |

## AI에게 요청할 때 지켜야 할 규칙

1. `Locomotion` 캐시 포즈를 없애거나 전신 무기 애님으로 교체하지 않는다.
2. 장비별 상체 포즈 선택은 하드코딩한 장비 이름보다 `WeaponAnimationDataAsset`의 Tag/Index를 우선 사용한다.
3. AnimGraph에서 UObject 접근이 필요한 새 계산을 넣기보다, C++에서 `FAnimThreadSafeData`에 값을 넣고 `BlueprintThreadSafe` Getter를 추가한다.
4. 새 공격 몽타주는 전신 `DefaultSlot`이 아니라, 전신 동작이 의도된 경우를 제외하고 `UpperBody` Slot과 같은 상체 마스크 경로를 사용한다.
5. `Layered Blend per Bone`의 Branch Filter/Blend Profile을 확인한다. 마스크 시작 뼈가 바뀌면 활·무기 손·척추가 함께 깨질 수 있다.
6. `BowStringIKAlpha`가 0인 상황에서는 FABRIK가 영향을 주지 않아야 한다. 활 발사(`State.Bow.Releasing`) 중에는 C++가 0으로 만든다.
7. 로컬 PIE뿐 아니라 원격 클라이언트에서도 장비 장착/해제, 이동, 조준, 몽타주가 동일하게 보이는지 확인한다.

## 새 무기 추가 체크리스트

- [ ] `WeaponAnimationDataAsset`에 Overlay 사용 여부, 태그, Index를 정의했다.
- [ ] `ABP_Player`의 `Blend Poses by int`에 해당 Index의 포즈/BlendSpace를 연결했다.
- [ ] 상체 블렌드 마스크가 해당 무기의 양손/척추 요구에 맞는다.
- [ ] 공격 몽타주가 `UpperBody` Slot에서 정상 재생된다.
- [ ] 지상 Idle/걷기/달리기/질주와 공중·수영 전환 시 기대한 활성 조건인지 검증했다.
- [ ] 서버 + 클라이언트 PIE에서 원격 외형을 확인했다.

## 관련 코드

- `Source/ClassFeature/Public/Animation/MotionMatchingAnimInstance.h`
- `Source/ClassFeature/Private/Animation/MotionMatchingAnimInstance.cpp`
- `Source/ClassFeature/Public/Equipment/WeaponAnimationDataAsset.h`
- `Source/ClassFeature/Public/Equipment/PlayerEquipmentComponent.h`
- `Source/ClassFeature/Private/Equipment/PlayerEquipmentComponent.cpp`

