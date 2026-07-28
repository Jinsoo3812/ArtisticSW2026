# 스킬 및 배 Enhanced Input 설정

스킬 입력은 C++에서 숫자 키를 직접 바인딩하지 않습니다. 아래 Input Action을
각 상태에서 활성화되는 Input Mapping Context에 등록하고, 해당 Pawn Blueprint의
C++ 프로퍼티에 연결합니다.

| 조종 상태 | Input Action | 값 형식 | 키 매핑 | C++ 프로퍼티 |
| --- | --- | --- | --- | --- |
| 플레이어/도보 | `IA_GravityVortex` | Digital | `3` | `GravityVortexSkillAction` |
| 대포 | 기존 `IA_CannonWaterBomb` | Digital | `4` | `CannonWaterBombToggleAction` |
| 배 | `IA_ShipBombardment` | Digital | `5` | `ShipBombardmentToggleAction` |
| 배 | `IA_ShipBombardmentConfirm` | Digital | 마우스 왼쪽 | `ShipBombardmentConfirmAction` |
| 배 | `IA_ShipBombardmentCancel` | Digital | 마우스 오른쪽, Escape | `ShipBombardmentCancelAction` |
| 배 | `IA_ShipZoom` | Axis1D | Mouse Wheel Axis | `ShipZoomAction` |

권장 Mapping Context 구성은 다음과 같습니다.

- `IA_GravityVortex`는 `ABasePlayer`가 사용하는 도보 상태의 `DefaultIMC`에 넣습니다.
- `Content/New/Cannon/CannonIMC`에는 `IA_CannonWaterBomb`가 이미 들어 있습니다.
  `BP_Cannon`의 `CannonWaterBombToggleAction`에도 같은 IA를 할당해야 합니다.
- 배 입력 네 개는 `Content/New/Ship/Input/IMC_Ship`에 넣은 뒤 배 Blueprint의
  각 프로퍼티에 할당합니다.

`IA_ShipZoom`은 휠 위쪽이 `+1`, 아래쪽이 `-1`이 되게 설정합니다. 코드에서
양수 입력을 Spring Arm 길이에서 빼므로 휠을 올리면 줌인됩니다. 줌 간격과
최소/최대 거리는 `Ship | Camera | Zoom`에서 조절할 수 있습니다.

## 중력 소용돌이 조작

중력 소용돌이는 다음 순서로 사용합니다.

1. `3`을 누르고 있는 동안 조준 모드를 유지하고 조준선을 표시합니다.
2. `3`을 누른 상태에서 마우스 왼쪽 버튼을 누르면 발사합니다.
3. 마우스 오른쪽 버튼을 누르거나 `3`을 떼면 조준 모드를 취소합니다.

## Default Input Config Data Asset의 용도

`DefaultInputConfig` 또는 `Default_Input_Config`는 Input Action과 GAS 입력용
Gameplay Tag를 연결하는 데이터 테이블 역할을 합니다.

예를 들어 다음처럼 등록되어 있으면:

| Input Action | Gameplay Tag |
| --- | --- |
| `IA_MouseLeftClick` | `Key.Default.Mouse.LeftClick` |
| `IA_Interact` | `Key.Default.F` |

`ABasePlayer::SetupPlayerInputComponent`가 Data Asset을 순회하면서 각 IA를
공통 GAS 입력 함수에 자동으로 바인딩합니다. 여러 GA가 동일한 입력 처리 규칙을
공유할 때 유용하고, IA와 Gameplay Tag의 연결을 C++에 하드코딩하지 않아도 됩니다.

현재 `IA_GravityVortex`는 `GravityVortexSkillAction`이라는 전용 프로퍼티를 통해
직접 바인딩됩니다. 따라서 `Default_Input_Config`에 추가하지 않아도 작동합니다.
같은 IA를 전용 프로퍼티와 Data Asset 양쪽에 동시에 넣으면 입력이 중복 전달될 수
있으므로 한쪽 방식만 사용해야 합니다.

## 포탄 세례 데칼 프리뷰

`ABombardmentPreview`에는 기존 Static Mesh와 평면 Decal 컴포넌트가 모두
들어 있습니다. 기존 Blueprint가 깨지지 않도록 기본값은 Mesh 방식이며,
Blueprint 자식에서 `bUseDecalPreview`를 활성화하면 Decal 방식으로 바뀝니다.

1. 머티리얼을 만들고 `Material Domain`을 `Deferred Decal`로 설정합니다.
2. 원형 텍스처 또는 마스크를 Opacity에 연결합니다.
3. Bombardment Preview Blueprint에서 `bUseDecalPreview`를 활성화합니다.
4. `ValidPreviewDecalMaterial`에 만든 머티리얼을 할당합니다.
5. 필요하면 `InvalidPreviewDecalMaterial`과 `DecalProjectionDepth`도 조절합니다.

Decal 컴포넌트의 Y/Z 크기는 스킬 반경을 사용하며 지면 방향으로 투영됩니다.
화면에 보이는 결과는 완전한 2D 원이고, `DecalProjectionDepth`는 굴곡진 지형을
덮기 위한 보이지 않는 투영 공간의 깊이입니다.
