# 남녀 플레이어 시체 물리 제작

현재는 실제 프로젝트의 자동 시체 스킨 보정과 새 관절 제한을 사용한다. [적용 및 테스트 맵 안내](../../../../docs/Player_Ragdoll_PoseFix_2026-10-31.md)를 따른다.

`ChMan_Physics.json`, `ChWoman_Physics.json`이 두 전용 PhysicsAsset의 재현 가능한 제작 입력이다. 단위는 cm, quaternion은 `[w,x,y,z]`다. `fit_humanoid_physics.py`는 Blender MCP에서 실제 제작본의 본과 body/clothes/shoes 정점 가중치를 읽어 피팅한다. 머리카락·가방·장식품은 충돌체 피팅에서 제외한다. 원본 Blender 리그와 FBX는 덮어쓰지 않는다.

각 에셋은 17개 강체와 16개 관절이다. pelvis, spine_02, spine_04, neck_01, head와 양측 upperarm/lowerarm/hand/thigh/calf/foot만 시뮬레이션한다. twist·손가락·얼굴·IK marker마다 작은 강체를 만들지 않는다. 팔꿈치와 무릎의 굽힘 평면과 긴 축을 분리하며, 현재 목표 굽힘은 각각 0–40° / 0–50°다. 어깨 twist는 ±25°, 전완은 ±45°, 무릎 twist는 ±5°다. 중심 편향을 포함하므로 swing 반각을 전체 굽힘 범위와 혼동하지 않는다. 각도는 목표 제한이며 충격 순간 solver 수치 오차 0을 보장하지 않는다.

제작 시 에디터를 종료하고 직접 UBT 빌드가 끝난 뒤 `Scripts/Editor/ChManRigValidation/author_player_ragdolls.py`를 Unreal Python으로 실행한다. 두 전용 메시 폴더에만 쓰도록 native authoring API의 경로를 제한했다. `UPhysicsConstraintTemplate::SetDefaultProfile`로 저장 프로파일을 갱신한다. 검증 스크립트는 다음 프로세스에서 재로드한 제한과 motion type을 입력 JSON과 비교한다.

최초 자동 피팅 PA·메시 사본은 `Saved/PlayerRagdollValidation/Before`에 있다. 경로를 유지한 채 복원할 수 있다. 재제작하면 **이 두 PA의 수작업 수정은 대체**되므로 편집 내용은 먼저 JSON에 반영한다. 엔진 공용 Physics 설정이나 기존 원본 캐릭터 PA를 수정하지 않는다.

`render_ragdoll_review.py`는 최종 실제 UE 포즈를 별도 Blender Scene/오브젝트 사본에 재구성한다. 기준 바닥은 실제 UE 높이이며 모델의 최저 정점에 맞춰 옮기지 않는다. `.blend`를 저장하지 않는다. 결과는 게임 프레임버퍼 캡처와 구별한다.

런타임 구성·검증 결과·제한은 [통합 보고서](../../../../docs/Player_Ragdoll_Integration_2026-10-31.md)를 따른다.
