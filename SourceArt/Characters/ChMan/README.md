# ChMan UE5 제작본 — 2026-10-31

전용 PhysicsAsset 후속 제작과 시체 시스템의 현재 적용·검증 범위는 [시체 통합 보고서](../../../../docs/Player_Ragdoll_Integration_2026-10-31.md)를 따른다. 아래 초기 자동 피팅 설명을 갱신한다.

Blender MCP로 원본을 조사하고 실제 프로젝트 마네킹과 비교해 만든 작업본이다. Desktop 원본 FBX와 기존 `/Game/jiwon/Characters/ChManNewRig`, 공용 Manny Skeleton은 보존했다. 이후 남성 플레이어 BP와 ABP에도 적용했다. 런타임 변경 및 검증은 [통합 보고서](../../../../docs/ChMan_Runtime_Integration_2026-10-31.md)를 참조한다.

## 사용할 파일

- `ChMan_UE5_Rig.blend`: 원본과 Manny 비교본을 숨겨 보관한 수정 리그. 원본 텍스처를 패킹했다. export 골격은 `Armature`, 스킨은 `SKM_ChMan_UE5`다. FK 본 편집과 손발 helper 추종을 제공하며, 완전한 FK/IK 전환 컨트롤 리그를 제작한 것은 아니다.
- `FBX/SKM_ChMan_UE5.fbx`: 애니메이션 없는 skeletal mesh.
- UE 메시: `/Game/jiwon/Characters/ChManUE5/SKM_ChMan_UE5`.
- UE 전용 Skeleton: `/Game/jiwon/Characters/ChManUE5/SKM_ChMan_UE5_Skeleton`. Manny `SK_Mannequin`을 단방향 Compatible Skeleton으로 지정하고 `Use Retarget Modes From Compatible Skeleton=True`를 설정했다. 원본 재질 7개를 다시 연결했다.
- `Review/08_textured_arms_raised.png`: 소매 내부 수정 확인. `UE_Idle.png`, `UE_Walk.png`, `UE_Jump.png`는 UE에서 평가한 실제 본 포즈를 Blender에 옮겨 렌더한 것이다. 게임 화면 캡처는 아니다.
- `Review/09_Manny_Apose_Comparison.png`: 왼쪽 실제 프로젝트 Manny, 오른쪽 수정본의 **저장된 기본 A포즈**. 팔을 수평으로 든 이미지는 변형 검사 자세이며 기본 자세가 아니다.

남성 `BP_Player_Man`은 위 메시와 기존 `ABP_Player_Man`을 사용하도록 저장했다. **Manny Skeleton 기반** AnimBP/애니메이션을 사용한다. 기존 ChGirl Skeleton 전용 AnimBP까지 자동 호환된다는 뜻은 아니다. 전용 `PA_ChMan_UE5`를 생성하고 유효한 기존 소켓 13개를 메시로 이전했다. PhysicsAsset은 자동 피팅이며 래그돌 조율은 별도다.

## 관찰과 수정

원본은 UE5 이름의 코어에 CC 보조 본이 섞인 117본 골격이었다. 실제 프로젝트 Manny와 일부 기준 회전이 약 6~18도 달랐고, CC twist 가지는 표준 twist 이름/계층과 달랐다. IK 손발 목표도 실제 관절과 어긋나 있었다. 프로젝트에 수입된 기존 ChMan은 `ChGirlRig_Skeleton`을 공유한다.

수정본은 명시적인 단일 root와 UE5 Simple 코어 89본을 포함한 총 120본이다. 기존 weighted CC twist 16개를 표준 twist에 이관하고 기준 축과 부모 관계를 맞췄다. 얼굴·발가락·기타 추가 가지를 유지했다. 모든 export object transform과 root 위치·배율을 항등값으로 정리했다. 손발 helper 네 개와 wrist-follow `ik_hand_gun`은 Blender 제작 시 실제 사지를 따른다. 런타임 애니메이션의 helper 트랙을 자동 수정하는 기능은 아니다.

최초 버전은 원래 A포즈의 표면을 보존하고 축만 맞췄다. 이후 기준 자세도 맞춰 달라는 사용자 피드백을 반영해 **쇄골·상완·하완·손·손가락·허벅지·종아리의 실제 관절 방향을 Manny의 A포즈에 맞췄다.** 기존 버전은 `Archive/Before_Manny_APose`에 보존했다. 본을 회전시킨 상태에서 실제 LBS 피부 변형을 bake한 뒤, 그 결과 관절 위치에서 표준 축으로 다시 바인딩했다. 관절 길이·체형 비율·가중치·UV는 유지한다. 발의 세계 방향은 보존해 마네킹의 발 각도로 신발을 강제로 기울이지 않았고, 바닥을 맞추기 위해 root 이외의 스킨/골격을 약 1.3804cm 위로 이동했다. 의도적으로 바뀐 rest 표면의 최대 이동은 손끝 등에서 약 20.368cm이며 이전 버전의 표면 불변 수치는 최종본에 적용하지 않는다.

팔 벌림에서 소매 안쪽 피부가 잘린 경계가 드러나는 현상을 재현했다. 각 상완의 19정점 경계를 소매 안으로 연장하는 skin inset을 추가해 노출을 막았다. 추가분은 총 152정점이며 기존 표면·UV·가중치를 유지했다. 이는 **소매 아래의 구조적 피부 보강**이며 잘린 몸통 전체를 복원한 단일 manifold body가 아니다. 옷을 벗기거나 소매를 교체하는 용도는 별도 제작이 필요하다. 새 피부 UV는 경계에서 연장했다.

리깅의 기준 축/계층/translation 정책을 수정하고 노출된 겨드랑이는 메시를 보강했다. 기존 ABP의 고정 회전 보정 7개를 제거했다. 소스 애니메이션의 손발 IK helper 트랙과 실제 사지의 불일치는 별도 메시 전용 `ABP_ChMan_FinalMarkers`에서 최종 손발 위치·회전을 복사하는 구조적 우회로 처리했다. 스킨 본의 고정 회전 offset은 추가하지 않았다.

## 검증 결과와 범위

| 검사 | 결과 |
| --- | --- |
| 실제 기준 | UE 5.7.4 `/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple` 직접 FBX export |
| 코어 | 누락 0, 부모 불일치 0, Blender 기준 축 차이 0도 |
| 기본 관절 방향 | Manny 대비 검사한 32개 구간의 방향 오차 0도 |
| A포즈 bake 후 재바인딩 표면 오차 | 최대 약 0.000018cm |
| 기준 자세 조정 중 관절 길이 변화 | 최대 약 0.000134cm |
| 가중치 | 빠진 정점 0, 합 약 1, 최대 7 influence |
| 팔 수평/추가 올림/머리 위 검사 | 원래 A-pose에서 60/90/120도 올림, 앞·뒤 이미지 확인 |
| Blender 손발 marker | 네 검사 자세에서 최대 위치 오차 0cm |
| UE mesh 높이 | 약 178.611cm |
| UE root | 원점, scale `(1,1,1)` |
| UE→FBX→Blender 표면 | 최대 최근접 차이 약 0.000009cm |
| UE 왕복 본 위치/축 | 최대 약 0.000146cm / 0.0396도 |
| 애니메이션 | Idle/Walk/Jump 각 5시점, 총 ChMan 15 + Manny 비교 15 포즈 평가 |
| 체형 길이 | 상완·하완·허벅지·종아리의 bind 대비 최대 변화 약 0.000027cm |

숫자 원본은 `Work/manny_apose_audit.json`, `blender_pose_audit.json`, `ue_roundtrip_audit.json`, `native_pose_validation.json`, `ue_pose_samples.json`에 있다. `build_audit.json`은 A포즈 조정 이전 초기 생성 단계의 검사다. UE FBX의 Blender 재수입은 root를 Armature Object로 표현해 119본으로 표시되지만 UE의 root를 잃은 것은 아니다.

초기 FBX 검사는 `ClassFeatureAnimGraph` 모듈 누락으로 별도 content-only host를 사용했다. 이후 실제 프로젝트 모듈 빌드에 성공했고 production BP/ABP를 사용하는 standalone PIE에서 이동·점프·모션매칭·손발 helper를 검사했다. 결과와 실제 시험 범위는 통합 보고서를 참조한다. 무기 파지의 시각 검수, 공격 몽타주 전체, 래그돌, LOD, 패키징과 네트워크 전체 게임은 미검증이다.

Legacy FBX importer는 메시 bind pose의 relative matrix 경고 후 `Recreating bind pose succeeded`를 기록한다. 이것을 경고 없는 import라고 보고하지 않는다. 지원되는 importer의 재구성 결과를 실제 UE export·root scale·표면·본 위치·애니메이션 길이로 검증했다. 다른 importer나 다른 FBX 옵션의 동일 결과를 보장하지 않는다.

## 재생성·익스포트

사용자 제공 Project_J의 `Docs/Animation/Authoring/Character_Rigging_And_UE5_Postprocess_Playbook.md`, `HumanoidSkeletonRetargetingStandards.md`, GreatSword `ExportMeshes.py`를 참고했다. 체형을 Manny 크기로 강제로 늘이지 않는다.

`Work/build_chman_ue5.py`는 조사한 source/Manny 객체가 있는 Blender 장면에서 **새 작업 사본을 만드는** 재현 스크립트다. 같은 이름의 출력이 이미 있는 장면에서 그대로 반복 실행하지 않는다. 제작본의 최종 편집을 덮을 수 있으므로 수동 편집 후에는 기존 객체를 사용해 별도 export한다. 이 스크립트가 만들어 내는 초기 리그에는 이후 패킹한 텍스처나 리뷰 카메라 정리가 모두 포함되는 것은 아니다.

최종 기준 자세는 초기 생성 다음 `Work/align_manny_reference_pose.py`에서 실제 pose bake·rebind로 만든다. 현재 리그를 내보낼 때는 초기 생성 스크립트 대신 `Work/export_chman_fbx.py`를 사용한다. `Work/verify_blender.py`는 저장된 기본 자세를 변경하지 않고 검사 자세를 렌더한 후 rest로 복귀한다.

FBX는 export 리그와 스킨만 선택한다. 컨트롤/참조/원본은 제외한다. 실제 cm 데이터와 scene unit `.01`을 사용해 `FBX_SCALE_NONE`, Forward `-Y`, Up `Z`, leaf bone 없음, 모든 본, 애니메이션 없음, FACE smoothing, tangent를 사용했다. 작업 blend는 m 단위이며 스크립트의 export 구간이 cm로 잠시 변환 후 복원한다. root에 100배 scale만 넣는 방식은 사용하지 않는다. UE에서는 legacy FBX, 재질/텍스처/애니메이션/새 PA 생성 끔, 이 전용 Skeleton을 사용한다. 공용 Manny Skeleton reference를 덮어쓰지 않는다.

`Work/UEValidation/ChManValidation.uproject`는 게임 모듈을 로드하지 않는 임시 검증 host다. 실행 시에만 그 옆 `Content` junction을 이 저장소의 실제 `Content`로 연결했다. 검사 후 junction 자체를 제거했으며 원본 콘텐츠를 삭제하지 않았다. 재생성할 때도 정확한 target을 확인하고 junction을 따라 재귀 삭제하지 않는다.

런타임 롤백을 위한 변경 전 BP/ABP/메시/Skeleton 사본은 `Saved/ChManRigValidation/Before`에 있다. 에디터를 닫은 상태에서 해당 콘텐츠 상대 경로로 복원한다. FBX 기본 A포즈 변경 전 제작본은 `Archive/Before_Manny_APose`에 있다.

원본 SHA256: `01DCCF8795DA26D80CF397E62B10B8E535985E055EC2F8D5E4F76D6D1795D29A`.

수정 FBX SHA256: `006CEF8D28A99C386CEEE449C0478943A3553040D18B9CB6DA4184BD8838B1F6`.
