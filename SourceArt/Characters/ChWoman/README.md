# ChWoman UE5 작업본 — 2026-10-31

전용 PhysicsAsset 후속 제작과 시체 시스템의 현재 적용·검증 범위는 [시체 통합 보고서](../../../../docs/Player_Ragdoll_Integration_2026-10-31.md)를 따른다. 아래 초기 자동 피팅 설명을 갱신한다.

Desktop `SKM_Player_Woman.FBX`를 보존하고 Blender MCP로 만든 별도 리그다. 실제 프로젝트 Quinn Simple을 직접 export해 축·계층·기본 A포즈를 비교했다. 체형과 신발의 세계 방향은 유지했다. 원본의 신발 텍스처 이름에 `Pump_shoes`가 포함돼 있지만 실제 메시 형태를 기준으로 작업했다.

## 파일과 게임 연결

- `ChWoman_UE5_Rig.blend`: 원본, Quinn 기준, 게임용 골격, 제작 컨트롤, 위젯과 리뷰 카메라를 분리한 제작본. 사용 텍스처 14개를 패킹했다.
- `FBX/SKM_ChWoman_UE5.fbx`: 120본 게임용 메시. 컨트롤·위젯은 포함하지 않는다.
- `FBX/AS_ChWoman_Baked.fbx`: 루트·FK·IK·foot roll·손가락을 포함하는 **8프레임 export 검증용 예제**다. 게임용 완성 모션이 아니다.
- `/Game/jiwon/Characters/ChWomanUE5/SKM_ChWoman_UE5`: 전용 Skeleton, `PA_ChWoman_UE5`, `ABP_ChWoman_FinalMarkers`를 사용한다.
- 실제 `BP_Player_Woman`은 이 메시와 운영 `ABP_Player_Woman`을 사용한다. 기존 component scale 1.1과 상대 위치·회전은 유지한다.
- `Review/01_Quinn_Apose.png`, `02_Arms_Raised.png`: 기본 자세·팔 올림 확인. `PIE_*`는 실제 UE 평가 본을 Blender에서 렌더한 자료이며 게임 화면 캡처는 아니다.

전체 런타임 수치와 미검증 범위는 [통합 검증 보고서](../../../../docs/ChWoman_Runtime_Integration_2026-10-31.md)를 참조한다.

## 컨트롤 사용

1. blend를 열고 `Woman_CTRL`을 선택한 뒤 Pose Mode로 들어간다. `Woman_EXPORT`는 숨겨진 결과 골격이며 직접 포즈를 잡는 입구가 아니다.
2. Text Editor에서 내장 `ChWoman_Rig_UI.py`를 한 번 Run Script한다. 3D View의 N 패널 → **ChWoman**에 조작 패널이 나타난다. 세션마다 한 번 실행하며 전역 자동 스크립트 실행 허용을 요구하지 않는다.
3. `FK_root`는 전체 이동·회전, `FK_pelvis`는 몸체 이동, `FK_spine_*`, 목·머리·쇄골은 개별 FK다. 팔·다리 FK와 손가락 마디, weighted twist/추가 가지 컨트롤을 분리했다. 위치·배율을 잠근 컨트롤은 회전 용도다.
4. 사지마다 **0=FK, 1=IK**다. 숫자만 바꾸면 포즈가 달라질 수 있다. **Match IK / Match FK** 버튼으로 현재 결과를 맞춘 후 전환한다. IK 결과의 축 방향 회전은 숨겨진 RES adapter에 보존한다. 키 버튼은 이 회전 adapter와 모드 값도 함께 기록한다.
5. `IK_arm_l/r`, `IK_leg_l/r`는 손목·발목 목표다. `POLE_arm_*`, `POLE_leg_*`는 팔꿈치·무릎 방향이다. stretch는 사용하지 않는다. 목표는 root 공간이며 도달 거리 밖에서는 사지 길이를 늘리지 않는다.
6. 발 IK에서는 `FOOT_l/r`로 전체 발을 이동하고 `ROLL_heel_*`, `ROLL_toe_*`로 실제 신발의 낮은 지지 영역을 기준으로 회전한다. `IK_leg_*`는 발목 목표의 추가 조정이다. 이 컨트롤이 게임의 환경 접지 solver를 대신하지는 않는다.
7. `LOOK_eyes` 회전은 두 눈, `FK_cc_base_l_eye/r_eye`는 개별 눈 조작이다. 원래 머리에 고정돼 있던 분리된 눈 818정점만 각각 눈 본에 연결했다. 원본에는 입·턱 가중치와 표정 Shape Key가 없다. 나머지 얼굴 본은 숨긴 **Face Reference (unweighted)** 컬렉션에 보존했으며, 립싱크·눈 깜빡임·표정 리그 제작을 뜻하지 않는다.
8. **Snap independent weapon**은 현재 `ik_hand_gun` 결과를 보존한 독립 기준으로 전환한다. **Follow wrist**는 의도적으로 오른손 기준으로 돌아간다. 모드 값과 관련 컨트롤에 키를 기록한다. 애니메이션의 독립 무기 기준을 항상 손으로 강제하는 방식이 아니다.
9. 선택한 컨트롤을 키로 기록한다. 모드 전환에서 스위치 키의 보간은 의도에 맞게 Constant/Linear로 정한다. 일반 키 버튼은 선택한 컨트롤과 스위치/IK adapter를 기록하며, 선택하지 않은 목표나 pole의 애니메이션까지 자동으로 추측하지 않는다.

이 리그는 프로젝트 용도의 커스텀 컨트롤이며 Rigify 전체 기능이나 UE Control Rig Backwards Solve를 구현한 것은 아니다. 다중 공간 전환·자동 환경 접촉·의상 물리·표정 제작은 포함하지 않는다.

## 익스포트

패널의 **Export Bind FBX**는 컨트롤을 제외한 reference mesh/skeleton을 내보낸다. **Bake Range FBX**는 현재 scene의 FPS와 시작·끝 범위를 사용해 결과 골격의 임시 사본을 매 프레임 visual bake한다. 원본 컨트롤/Action은 평탄화하지 않는다. 파일 선택창에서 경로를 지정한다.

내장 `ChWoman_FBX_Export.py`와 `Work/export_woman_fbx.py`가 같은 구현이다. root/object의 100배 scale로 보정하지 않고 실제 cm 데이터를 잠시 만들어 `FBX_SCALE_NONE`, `-Y` forward / `Z` up, leaf bone 없음, 모든 필요한 본을 내보낸다. `Only Deform Bones`를 사용하면 표준 IK marker가 빠질 수 있다.

UE에서는 legacy FBX importer, 기존 전용 Skeleton, 애니메이션/재질/텍스처/새 PA 생성 끔으로 메시를 재임포트한다. 공용 Manny Skeleton reference를 덮어쓰지 않는다. Blueprint PhysicsAsset은 자동 피팅이며 래그돌 조율은 별도다.

## 제작 검사와 복원

원본 117본에서 120본으로 정리했고, 실제 표준 코어는 root 포함 89본이다. 기존 weighted CC twist 16개를 표준 twist에 이관했다. 메시 정점 54,393개·토폴로지·UV는 유지했으며, 눈 818정점의 가중치만 의도적으로 추가 수정했다. 그 외 weight sum은 약 1, 누락 0, 최대 6 influence다. 기본 A포즈 방향 오차는 검사한 32구간에서 0도다.

`Work/control_validation.json`은 네 사지 굽힌 자세의 IK/FK 전환·목표 이동·길이 보존, root 이동·회전, 손발 marker, heel/toe·손가락·눈의 실제 스킨 변형과 독립 무기 기준 전환을 검사한다. `animation_bake_roundtrip_audit.json`은 8프레임 예제 중 3시점의 120본을 비교한다. 모든 극단 포즈와 임의 Action 전체를 승인하는 검사는 아니다.

`build_woman.py`와 `build_controls.py`는 초기 생성 단계다. 현재 제작본에 그대로 재실행해 수동 편집을 덮지 않는다. 이후 Quinn A포즈 bake/rebind, pole 각도 보정, foot/eye 컨트롤과 UI 추가 단계가 이어진다. 현재 blend를 편집할 때는 기존 리그와 export 버튼을 사용한다.

원본 Desktop FBX는 변경하지 않았다. 운영 BP/ABP 변경 전 파일은 `Saved/ChWomanRigValidation/Before`에 보존했다. 복원할 때 에디터를 종료하고 해당 콘텐츠 상대 경로의 BP/ABP를 되돌린다. 원래 메시·공용 Skeleton·공용 모션은 변경하지 않았다.
