# ChMan 남성 플레이어 연결 및 런타임 검증 — 2026-10-31

> 후속 작업: 전용 PA를 체형·관절에 맞게 다시 제작하고 물리 시체 시스템에 연결했다. 아래의 “래그돌 미검증/자동 피팅” 범위는 [시체 통합 보고서](Player_Ragdoll_Integration_2026-10-31.md)의 검사 항목에 한해 갱신한다.

## 적용한 에셋

실제 남성 플레이어 `/Game/Blueprints/Player/BP_Player_Man`의 Mesh를 `/Game/jiwon/Characters/ChManUE5/SKM_ChMan_UE5`로 변경하고, 운영 AnimBP `/Game/Anim_Logic/ABP_Player_Man`을 유지했다. ABP preview mesh도 새 메시다. 여성 BP/ABP, 공용 Manny Skeleton, 소스 모션과 Pose Search 데이터베이스는 변경하지 않았다. 기본 자세는 실제 프로젝트 Manny에 맞춘 A포즈다.

전용 Skeleton은 Manny와 호환되며 `Use Retarget Modes From Compatible Skeleton=True`를 사용한다. 전용 `PA_ChMan_UE5`는 새 체형에 자동 피팅한 26 body이며 래그돌 조율본은 아니다. 기존 메시에서 본이 유효한 소켓 13개를 메시 소켓으로 이전했다. 새 리그에 존재하지 않는 `weapon_r`의 `weapon_r_muzzle`은 이전하지 않았다. 무기 접촉 위치의 시각 검수는 별도다.

## ABP에서 제거한 보정

변경 전 그래프와 후보 그래프를 실제 PIE에서 비교한 뒤, 운영 ABP에서 아래 `Transform (Modify) Bone` 7개를 제거했다. 고정 transform을 우회해 입력 pose를 다음 노드로 직접 연결하고, 이 변경으로 사용처가 사라진 reroute 4개만 함께 제거했다. 전체 그래프 노드는 143개에서 132개로 줄었다.

| 본 | 기존 고정 회전 |
| --- | --- |
| neck_01 | Bone Space Yaw +24° |
| foot_r / foot_l | Yaw +6° / +10° |
| thigh_r / thigh_l | Pitch +10° |
| calf_r / calf_l | Pitch -10° |

제거한 이름은 `AnimGraphNode_ModifyBone_0`~`_6`이다. 다리 보정의 Alpha 연결 유무와 무관하게 회전값 자체는 고정이었다. 목이 지나치게 숙여지던 모습은 제거 후 개선됐다. Foot Placement, Leg IK, 무기 Fabrik IK, Orientation Warping, Motion Matching/Blend Stack, Cannon Operator와 게임플레이 이벤트는 보존했다. 리그 기준 축·자세를 바로잡아 기존 고정 보정이 필요 없어졌다는 범위의 수정이며, 다른 모든 IK 노드가 불필요하다는 뜻은 아니다.

## 손발 IK helper 후처리

초기 실제 PIE에서 스킨 본은 안정적이었으나 소스 모션의 `ik_hand_l`이 component space에서 한 프레임 약 149° 회전하고, 손발 helper와 사지가 최대 약 38cm/28cm 떨어지는 현상을 발견했다.

이 메시 전용 Post Process ABP `/Game/jiwon/Characters/ChManUE5/ABP_ChMan_FinalMarkers`를 추가했다. 최종 입력 pose → Component Space → Copy Bone 4개 → Local Space → 출력으로 구성한다. `hand_l/r`와 `foot_l/r`의 위치·회전을 대응하는 `ik_hand_l/r`, `ik_foot_l/r`에 복사하며 scale은 복사하지 않는다. 스킨 본, root와 무기 기준 `ik_hand_gun`은 수정하지 않는다.

이는 소스 애니메이션 helper 트랙과 최종 사지의 불일치를 처리하는 **메시 범위의 구조적 우회**다. 소스 모션 전체를 수정한 것은 아니다. 무기를 이용하는 기능에서 이 helper를 별도로 소비할 경우 파지 검수를 추가한다.

## 실제 실행 검증

`Scripts/Editor/ChManRigValidation`의 네이티브 에디터 도구와 Python runner를 사용했다. 단순 애니메이션 샘플 재생이 아니라 실제 production BP/ABP를 평가하고, 현재 선택된 `FAnimNode_MotionMatching` 검색 결과의 animation/database와 모든 본의 local/component transform을 수집했다. 이동 속도와 공중 프레임도 검사해 이동 없는 결과를 통과로 처리하지 않는다.

기존 메시+보정, 새 메시+보정, 새 메시+보정 제거의 비교와 저장된 BP 재로드를 별도로 수행했다. 최종 후처리 적용 후 30/60/120 FPS 검사에서는 총 5,543 프레임에서 helper 위치 오차가 1.5e-13cm 이하, 회전 오차가 0.000003° 이하로 줄었다. NaN/무한값, 비정상 scale, 팔다리 길이 파손은 발견되지 않았고 길이 편차는 최대 약 0.203cm였다.

이 첫 fixture의 기본 컨트롤러는 스프린트 권한을 주지 않았다. 따라서 이 실행의 sprint 이름 단계는 실제 run 500cm/s였으며 **스프린트 통과로 집계하지 않는다**. 이후 production controller를 저장한 별도 검증 GameMode로 다시 실행했다. 네트워크 dedicated context로 잘못 평가된 초기 실행도 성능·포즈 통과 집계에서 제외했다.

최종 검사에서는 저장된 남성 BP/ABP에 런타임 메시 override를 넣지 않고 production `BP_BasePlayerController`로 조종했다. 실제 스프린트 상태, 700cm/s 및 `PSD_Sprint_Locomotion`의 유효한 검색 결과를 확인했다. 걷기·달리기·정지·좌우 이동·후진·회전·스프린트·전진/측면 점프·착지를 포함한다.

| 최종 검사 | 30 FPS | 60 FPS | 120 FPS |
| --- | ---: | ---: | ---: |
| 측정 FPS | 29.79 | 60.00 | 119.46 |
| 평가 프레임 | 779 | 1,590 | 3,168 |
| deform 본 최대 local 회전/프레임 | 72.44° | 39.07° | 24.68° |
| 팔다리 bind 길이 최대 편차 | 0.197cm | 0.201cm | 0.203cm |
| 이동 중 유효하지 않은 MM 검색 | 0 | 0 | 0 |
| local 회전 75° 초과 후보 | 0 | 0 | 0 |

합계 5,537 프레임에서 비정상 수치·scale은 없었다. 손발 helper 위치 오차 최대 1.35e-13cm, 회전 오차 최대 약 0.000003°였다. 30 FPS 실행 시작의 초기화 idle 프레임은 유효한 검색 이전의 상태를 포함하므로 이동 중 검색 실패와 구분한다.

관찰 기준은 deform bone의 프레임 간 local 회전 75° 초과를 이상 후보로 기록하는 것이다. 30 FPS 빠른 점프 전환에서 손가락 component 회전은 약 85°까지 관측됐지만 local 회전은 약 71° 이하였고, 60/120 FPS의 최대 local 회전은 약 38°/18°로 감소했다. 모든 빠른 움직임이 결함이라는 의미도, 모든 모션에서 절대 튐이 없다는 보장도 아니다.

기존 네이티브 자동화 14개(Foot Placement 2, Jump Air 7, Stop 1, Cannon graph 1, Player Bow 3)를 실행했다. 최종 요약은 [검증 JSON](validation/ChMan_Runtime_20261031.json)을 참조한다. raw transform과 로그는 `Saved/ChManRigValidation`에 있다. `Review/PIE_*` 이미지는 실제 평가된 본 포즈를 Blender에서 렌더한 비교 자료이며 게임 화면 캡처는 아니다.

## 남은 검증과 복원

네트워크 전체 게임, 무기 파지·공격 몽타주 전체, 래그돌, LOD 전환, 패키징 및 모든 콘텐츠 조합은 이번 검사 범위가 아니다. 일부 소스 UEFN 모션에는 기존의 누락된 experimental Pose Search DB/notify 참조 경고가 있어, 경고 없는 전체 콘텐츠 검증으로 해석하지 않는다. 원본의 겨드랑이 보강은 소매 안 피부 연장이며 모든 의상/극단 포즈의 관통까지 해결한 것은 아니다.

변경 전 에셋은 `Saved/ChManRigValidation/Before`의 콘텐츠 상대 경로에 보존했다. 복원 시 에디터를 종료한 뒤 해당 남성 BP/ABP와 변경 전 메시/Skeleton만 되돌린다. 새 source art와 새 에셋은 별도로 보관할 수 있다. Desktop 원본 FBX는 변경하지 않았다. Git/LFS 설정과 저장소는 수정하지 않았다.
