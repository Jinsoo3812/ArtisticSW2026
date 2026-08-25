# Water Culling Verification

검증일: 2026-08-25

## 통과 항목

### 에디터 C++ 빌드

`ArtisticSW2026Editor Win64 Development` 빌드 성공.

### 베이크 파이프라인

- Grid: 359 x 141 x 298
- Interior voxels: 302,683
- Debug instances: 4,713
- Exterior leak: false
- Volume Texture 및 Data Asset 저장 성공
- `SM_Ship` 태그: `SW_CabinShip`, `SW_CabinCullTarget`

### 대상 배 컴포넌트

- 태그 기반 World Subsystem 제거
- `BP_PlayerShip_Kelvin`에 `SWCabinWaterCullComponent` 1개 저장
- `Test_Level` 실제 액터 인스턴스에서 상속 컴포넌트 1개 확인
- `ArtisticSW2026Editor Win64 Development` 재빌드 성공

### 머티리얼/셰이더 파이프라인

`validate_cabin_water_culling.py` 독립 프로세스 검증 결과 PASS.

- Blend Mode: Masked 유지
- Shading Model: Single Layer Water 유지
- Data Asset과 Volume Texture 참조 일치
- MPC 필수 파라미터 존재
- MPC 정적 Bounds와 베이크 Data Asset Bounds 일치
- Custom 입력 9개 모두 연결
- Bounds early-out 코드가 Volume Texture sample보다 앞에 존재
- 최종 SetMaterialAttributes의 Opacity Mask에 Custom 결과 연결
- 머티리얼 재컴파일 오류 없음

### CPU → GPU 좌표 수식

자동화 테스트 `ArtisticSW.Water.CabinCull.InverseRows` 성공.

회전, 이동, 비균일 스케일이 포함된 Transform에서 MPC 세 행으로 복원한 로컬 좌표가 `FTransform::InverseTransformPosition()`과 허용 오차 안에서 일치한다.

## 게임 타깃 빌드 상태

`ArtisticSW2026 Win64 Development` 전체 빌드는 실패했다. Water Culling 구현 파일 자체는 컴파일됐지만, 기존 코드의 다음 에디터 전용 Data Validation 선언이 게임 타깃에서 실패했다.

- `NPCDialogueData::IsDataValid`
- `EnemyShipAbilitySet::IsDataValid`
- `EnemyShipArchetypeData::IsDataValid`
- `EnemyShipPatternData::IsDataValid`
- `EnemyShipSkillModuleData::IsDataValid`

이는 이번 Water Culling 변경에서 발생한 오류가 아니다. 현재 프로젝트 전체의 기존 게임 타깃 빌드 차단 요소로 기록한다.

## 사용자가 확인할 항목

실제 컬링 모양은 PIE에서 육안 확인한다.

1. 에디터에 외부 변경된 `Test_Level`과 머티리얼을 다시 로드한다.
2. `SW_CabinVolume_Debug`는 숨긴다.
3. PIE를 실행한다.
4. 높은 파도가 선실 바닥보다 위로 통과하는 순간 내부를 관찰한다.
5. 바닥, 벽, 창문 가장자리에서 물 조각이 남는지 확인한다.
6. 갑판 위의 정상적인 물까지 과도하게 잘리는지 확인한다.
7. 배를 회전/이동시킬 경우 컬링 부피가 배와 함께 움직이는지 확인한다.

문제가 생기면 먼저 다음 값을 확인한다.

- `BP_PlayerShip_Kelvin` Components의 `CabinWaterCull`
- MPC `SW_CabinCullEnabled`
- MPC `SW_CabinCullThreshold` 기본값 0.35
- Volume Texture 파라미터 `SW Cabin Cull Mask`
