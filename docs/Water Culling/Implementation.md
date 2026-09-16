# Ship Cabin Water Culling

## 목표

거스트너 수면이 `SM_Ship`의 닫힌 선실 내부를 통과하더라도 내부 픽셀을 그리지 않는다. 물의 BaseColor, Emissive, 산란, 흡수, WPO에는 손대지 않고 Single Layer Water의 `Opacity Mask`만 사용한다.

적용 대상은 동시에 한 척뿐이다. 태그나 월드 검색은 사용하지 않으며, 원하는 배 Blueprint에 `SWCabinWaterCullComponent`를 붙여 명시적으로 선택한다. 현재 `BP_PlayerShip_Kelvin`에 이 컴포넌트가 저장돼 있다.

## 베이크 결과

- 입력 표면: `SM_Ship` LOD0 실제 삼각형
- 밀폐 보조물: `SW_CabinBarrier` 태그를 가진 Cube 5개
- Flood Fill 시드: `SW_CabinSeed` 태그를 가진 PointLight
- 복셀 크기: 10 cm
- 표면 차단 두께: 12 cm
- 측면 팽창: 1복셀(10 cm)
- 아래 방향 팽창: 2복셀(20 cm)
- 위 방향 팽창: 없음
- 해상도: 359 x 141 x 298
- 내부 복셀: 302,683
- 외부 누수: 없음
- 디버그 표시: `SW_CabinVolume_Debug`, 4,713개 병합 인스턴스

런타임 자산:

- `/Game/Blueprints/Water/Culling/VT_SW_ShipCabinMask`
- `/Game/Blueprints/Water/Culling/DA_SW_ShipCabinWaterCull`

Volume Texture는 R8 점유 마스크다. 내부와 팽창 영역은 1, 외부는 0이다. Mip은 만들지 않고 주소 모드는 Clamp다.

## CPU → GPU 전달

`USWCabinWaterCullComponent`가 자신의 Owner Transform을 사용한다. 월드 검색이나 액터 태그가 없으며, Transform이 마지막 업로드 값과 달라졌을 때만 다음 값을 MPC에 갱신한다.

- `SW_CabinCullInvRow0`
- `SW_CabinCullInvRow1`
- `SW_CabinCullInvRow2`
- `SW_CabinCullEnabled`

정적인 `LocalMin/LocalMax`는 베이크 시 `MPC_Water_Custom` 기본값에 저장한다. 따라서 런타임에 Data Asset을 로드하거나 Bounds를 반복 전송하지 않는다.

비균일 스케일까지 UE의 `InverseTransformPosition()`과 일치하도록, 역행렬 행은 원점과 월드 단위축을 직접 역변환해 구성한다.

## GPU 머티리얼 경로

`M_Realistic_Water`는 기존의 `BLEND_Masked + MSM_SingleLayerWater`를 유지한다.

픽셀 처리 순서:

1. 변위가 반영된 Absolute World Position을 받는다.
2. MPC의 세 역변환 행으로 배 로컬 좌표를 계산한다.
3. Local Bounds를 0~1 UVW로 정규화한다.
4. Bounds 밖이면 즉시 `1`을 반환한다.
5. Bounds 안에서만 `VT_SW_ShipCabinMask`를 한 번 샘플한다.
6. 점유값이 `SW_CabinCullThreshold` 이상이면 Opacity Mask 0, 아니면 1을 반환한다.

Custom HLSL의 `[branch]` Bounds 반환이 `Texture3DSampleLevel`보다 앞에 있다. 바다 대부분의 픽셀은 3D 텍스처를 읽지 않는다.

## VS/CS를 사용하지 않은 이유

선실 경계를 가로지르는 물 삼각형은 정점 단계에서 일부만 제거할 수 없다. VS만 사용하면 삼각형 전체를 남기거나 제거해야 하므로 워터 메시 밀도에 따라 누수 또는 과도한 구멍이 생긴다.

CS 사전 마스크는 여러 배를 처리할 때 유리하지만, 한 척에서는 현재 거스트너 수면 높이를 CS에서 다시 계산하거나 별도 마스크를 갱신해야 한다. 한 척의 작은 Bounds만 3D 샘플하는 현재 경로보다 고정 비용과 파이프라인 복잡도가 커진다.

## 주요 구현 파일

- `Source/WaterAndShip/Public/SWCabinWaterCullData.h`
- `Source/WaterAndShip/Public/SWCabinWaterCullComponent.h`
- `Source/WaterAndShip/Private/SWCabinWaterCullComponent.cpp`
- `Source/ClassFeatureEditor/Public/ShipCabinVolumeBakerLibrary.h`
- `Source/ClassFeatureEditor/Private/ShipCabinVolumeBakerLibrary.cpp`
- `Scripts/bake_test_level_ship_cabin_debug.py`
- `Scripts/integrate_cabin_water_culling.py`
- `Scripts/validate_cabin_water_culling.py`

## 대상 배 변경

대상 배 Blueprint의 Components 패널에서 `SWCabinWaterCullComponent`를 추가한다. 다른 배로 옮길 때는 기존 배에서 제거하고 새 배에 추가한다. 선실 메시와 로컬 원점이 동일하면 Volume Texture는 다시 구울 필요가 없다. 현재 자동 부착 스크립트는 `Scripts/attach_cabin_water_cull_component.py`다.

## 런타임 진단

`SWCabinWaterCullComponent`는 Output Log의 `LogSWCabinWaterCull` 카테고리에 파이프라인 상태를 기록한다.

1. `[1/5 BeginPlay]`: 컴포넌트, Owner, World, NetMode
2. `[2/5 Asset]`: `MPC_Water_Custom` 로드 성공 여부
3. `[3/5 MPC]`: 현재 World의 MPC Instance 획득
4. `[4/5 Upload]`: Owner Transform과 역변환 행 업로드
5. `[5/5 Heartbeat]`: MPC의 Enabled, DebugView, Threshold readback

`Debug View`는 셰이더 로그를 대신하는 시각 진단 기능이다.

- `0 - Normal Culling`: 실제 Volume Texture 점유 마스크를 이용한 정상 컬링
- `1 - Cull Entire Local Bounds`: 복셀 샘플 전에 로컬 Bounds 전체를 컬링한다. 컴포넌트, Transform, MPC, 머티리얼 및 Opacity Mask 경로를 한 번에 검증한다.
- `2 - Show Only Occupied Voxels`: Bounds 밖의 물을 숨기고 점유 복셀과 겹치는 물만 표시한다. Volume Texture 바인딩, UVW 및 Threshold를 검증한다.

## 발견 및 해결한 G8 점유값 오류

최초 베이크는 CPU의 논리 점유 배열을 `0`과 `1`로 만들고 그대로 `TSF_G8` Volume Texture에 복사했다. CPU 디버그 메시는 `0이 아닌가`만 검사하므로 정상적인 선실 부피를 표시했지만, GPU는 G8을 UNorm으로 샘플하여 점유값 `1`을 다음처럼 읽었다.

```text
1 / 255 = 0.0039215686
```

기본 `SW_CabinCullThreshold = 0.35`보다 작기 때문에 모든 점유 복셀이 빈 공간으로 판정됐다. 이로 인해 Bounds 전체 컬링은 성공했지만 Normal Culling과 Occupied Voxels 표시는 실패했다.

해결책은 Volume Texture 생성 직전에 모든 점유 셀을 `255`로 변환하는 것이다.

```cpp
Voxel = Voxel != 0 ? 255 : 0;
```

이제 GPU 샘플 결과는 점유 영역에서 `1.0`이며 Threshold를 정상 통과한다. 수정 후 동일한 승인 형상으로 다시 베이크했고 실제 PIE에서 선실 내부 물 컬링 성공을 확인했다.
