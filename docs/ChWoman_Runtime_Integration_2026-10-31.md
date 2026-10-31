# 여성 캐릭터 리그·컨트롤·실제 BP 연결 검증 — 2026-10-31

> 후속 작업: 전용 PA를 체형·관절에 맞게 다시 제작하고 물리 시체 시스템에 연결했다. 아래의 “래그돌 미검증/자동 피팅” 범위는 [시체 통합 보고서](Player_Ragdoll_Integration_2026-10-31.md)의 검사 항목에 한해 갱신한다.

## 적용 결과

`C:\Users\I\Desktop\SKM_Player_Woman.FBX`는 보존하고 [별도 제작본](../SourceArt/Characters/ChWoman/README.md)을 만들었다. 실제 프로젝트 `/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple`을 UE 5.7.4에서 export해 기준으로 사용했다. 단순 이름 변경이 아니라 본 계층·축·실제 기본 A포즈를 맞추고 LBS 표면을 bake/rebind했다. 체형·관절 길이·신발의 세계 방향은 보존했다.

| 역할 | 최종 에셋 |
| --- | --- |
| Blender 제작본 | `SourceArt/Characters/ChWoman/ChWoman_UE5_Rig.blend` |
| 게임용 메시 | `/Game/jiwon/Characters/ChWomanUE5/SKM_ChWoman_UE5` |
| 전용 Skeleton | 같은 폴더의 `SKM_ChWoman_UE5_Skeleton` |
| 실제 캐릭터 | `/Game/Blueprints/Player/BP_Player_Woman` |
| 운영 AnimBP | `/Game/Anim_Logic/ABP_Player_Woman` |
| 메시 전용 후처리 | `ABP_ChWoman_FinalMarkers` |
| 자동 피팅 물리 자산 | `PA_ChWoman_UE5` |

여성 BP의 component scale 1.1 및 상대 위치·회전은 유지했다. ABP preview mesh도 새 메시로 저장했다. 기존 메시와 공용 Manny Skeleton·공용 모션·Pose Search DB는 수정하지 않았다. 전용 Skeleton의 Compatible Skeleton은 Manny이며 `Use Retarget Modes From Compatible Skeleton=True`다.

새 물리 자산은 유효한 19 body를 가진 자동 피팅본이다. 기존 소켓 중 새 리그에 본이 존재하는 13개를 메시 소켓으로 이전했다. 없는 `weapon_r`의 `weapon_r_muzzle`은 이전하지 않았다. 재임포트가 유지한 소켓을 캐시 기반 조회가 놓쳐 이전 도구가 같은 소켓을 다시 추가하는 결함을 마지막 검사에서 발견했다. 도구가 직렬화된 목록을 직접 비교하고 map을 재구축하도록 수정했으며, 본·local transform이 정확히 같은 중복만 제거했다. 수정 후 이전 3회와 실제 FBX 재임포트 2회에서 13개와 모든 값이 유지됐고 남성 메시도 13개임을 확인했다. 이는 이전 도구의 중복 생성 원인 수정이며 임의 소켓 초기화가 아니다.

## 리그와 조작 기능

원본 117본에서 120본으로 정리했고 root 포함 실제 Quinn/Manny Simple 코어 89본을 포함한다. weighted CC twist 16개를 표준 이름/부모/위치로 이관했다. 정점 54,393개와 토폴로지·UV를 유지했으며, 눈을 제외한 기존 가중치는 보존했다. 원래 머리에 고정돼 있던 분리된 눈 818정점은 좌우 눈 본에 각각 409개씩 연결했다. 원본에는 턱/입 가중치와 표정 Shape Key가 없으므로 입·립싱크·깜빡임 리그를 제작했다고 보고하지 않는다. 기존 얼굴 기준 본은 숨긴 컬렉션에 보존했다.

별도 `Woman_CTRL`에는 159본의 제작 제어/메커니즘이 있다. root·pelvis·척추·목·머리·쇄골·사지 FK, 손가락·twist·추가 가지, 네 사지 non-stretch IK와 pole, 실제 신발 지지 영역의 heel/toe pivot, 공통/개별 눈 회전, 독립 무기 기준 snap을 제공한다. 게임용 `Woman_EXPORT`와 컨트롤·위젯을 분리했다.

내장 UI Text를 세션마다 한 번 실행해 ChWoman 패널을 연다. IK/FK는 0/1이며 Match 버튼이 현재 포즈와 axial twist를 보존한다. 선택한 컨트롤 키와 스위치·RES adapter 키를 기록한다. Export Bind와 Bake Range 버튼은 임시 결과 골격만 내보내며 컨트롤/Action을 평탄화하지 않는다. 현재 scene FPS와 범위를 따른다. 실제 검사 장면은 **25 FPS**다.

이는 필요한 기능을 구현한 커스텀 리그이며 Rigify 전체 기능이나 UE Control Rig Backwards Solve 구현으로 부르지 않는다. 사용 방법과 제작 단계는 제작본 README에 있다.

## 기존 여성 ABP 보정 정리

기존 메시+기존 ABP, 새 메시+기존 ABP, 새 메시+보정 제거 후보를 실제 standalone PIE에서 비교했다. 후보는 60/30/120 FPS를 검사했으며 총 비교 프레임은 8,688이다.

`foot_l`, `foot_r`에 Bone Space Yaw +6°를 추가하던 `AnimGraphNode_ModifyBone_0`, `_1`만 제거했다. 다른 노드의 클래스별 개수와 연결 기능은 보존했다. 그래프 전체 노드는 124→122개이며 이번 여성 그래프에는 추가로 지울 orphan reroute가 없었다. Foot Placement, Leg IK, 무기 Fabrik, Orientation Warping, Motion Matching/Blend Stack, Slot, Cannon Operator와 이벤트는 유지했다.

새 리그의 기준 축/포즈 수정으로 기존 고정 발 보정이 불필요해진 범위의 원인 수정이다. 다른 모든 IK가 불필요하다는 의미가 아니다.

## 손발 helper와 scale 관찰

후처리 이전에는 손발 IK helper가 실제 사지와 최대 약 32.5cm 떨어지는 결과를 관측했다. 여성 메시 전용 `ABP_ChWoman_FinalMarkers`는 Input Pose → Component Space → Copy Bone 4개 → Local Space → 출력으로 구성한다. 최종 hand/foot의 위치·회전을 대응하는 `ik_hand_l/r`, `ik_foot_l/r`에 복사한다. 스킨 본, root와 소스 모션의 독립 무기 기준 `ik_hand_gun`은 변경하지 않는다. 이것은 소스 helper 트랙과 최종 사지의 불일치에 대한 **메시 범위의 구조적 우회**이며 공용 소스 애니메이션을 고친 것은 아니다.

scale은 복사하지 않았다. 기존 여성 그래프에서도 발 helper의 local scale은 약 0.9318이며, 무조건 1로 만들면 authored 목표의 의미가 달라질 수 있다. 최종 스킨 본 scale 변화는 최대 약 0.679%였다. 별도 raw 압축 모션 `M_Relaxed_Run_Stop_B_Lfoot`의 twist scale 변화와 새 메시 compatible 평가를 5시점에서 비교해 같은 값임을 확인했다(예: 중간 0.997756, 75% 시점 1.002410). 모든 bone scale이 항상 1이라는 주장은 하지 않는다. 작은 소스 변형은 보존하며 폭주·비정상 수치와 구분했다.

## 최종 저장 연결 검사

production `BP_BasePlayerController`로 저장된 여성 BP를 스폰했다. 런타임 메시/ABP override 없이 실제 디스크 연결을 읽고 스프린트 상태·700cm/s·`PSD_Sprint_Locomotion`의 유효한 검색 결과를 확인했다. 걷기, 달리기, 정지, strafe, 후진, 회전, 스프린트, 전진/측면 점프와 착지를 포함한다.

| 최종 PIE | 30 FPS | 60 FPS | 120 FPS |
| --- | ---: | ---: | ---: |
| 측정 FPS | 29.77 | 60.00 | 119.53 |
| 평가 프레임 | 779 | 1,590 | 3,171 |
| deform 최대 local 회전/프레임 | 56.41° | 45.00° | 45.00° |
| 팔다리 bind 길이 최대 편차 | 0.294cm | 0.306cm | 0.295cm |
| local 회전 75° 초과 후보 | 0 | 0 | 0 |
| 이동 중 유효하지 않은 활성 MM 검색 | 0 | 0 | 0 |

총 5,540프레임에서 NaN/무한값은 없었다. 손발 helper 위치 오차 최대 약 1.01e-13cm, 회전 오차 최대 0.000003°였다. MM 초기 idle의 초기화 상태와 실제 움직임의 검색 실패를 구분한다. 75° 기준은 이상 후보를 찾기 위한 검사이며 모든 동작에서 튐이 절대 없다는 보장은 아니다. 실제 UE 평가 Idle/Run/Jump의 Blender 렌더도 확인했다. 게임 프레임버퍼 캡처는 아니다.

기존 네이티브 자동화 14개(Foot Placement 2, Jump Air 7, Stop 1, Cannon graph 1, Player Bow 3)가 모두 통과했다. 에디터 도구 변경은 직접 UnrealBuildTool 빌드에 성공했다. 일부 기존 experimental DB/notify 의존성 경고는 남아 있으므로 전체 콘텐츠가 경고 없이 정상이라는 결과는 아니다.

## 제작·왕복 검사

- 기본 자세의 32개 실제 관절 구간은 Quinn 방향 오차 0도, 표준 core 축/부모 불일치 0이다. A포즈 재바인딩 표면 오차는 약 0.000018cm, 관절 길이 변화는 약 0.000081cm다.
- 게임 import 결과를 UE→FBX→Blender로 비교해 표면 최대 약 0.000013cm, 본 위치 약 0.000277cm, 기준 축 약 0.056° 차이를 확인했다. UE FBX는 root를 Armature Object로 표현해 Blender에서 119본으로 보이지만 실제 UE 골격은 120본이다.
- 재임포트 후 실제 UE 메시를 다시 export해 좌우 눈에 각 409정점의 가중치가 남아 있는지도 확인했다.
- 굽힌 자세에서 네 사지 FK→IK 전환 위치 차이 최대 약 0.0029cm, 목표 오차 약 0.000604cm, IK→FK 복귀 약 0.000036cm였다. root 이동/회전, marker 추종, 실제 heel/toe·손가락·눈 스킨 변형과 독립 무기 기준 snap을 검사했다.
- 8프레임 컨트롤 예제를 visual bake해 FBX 왕복의 3시점·120본을 비교했다. Blender 본 위치 최대 약 0.000227cm, 회전 약 0.056°였다. 같은 FBX를 실제 Unreal AnimSequence로 임포트해 평가한 본 위치 차이는 최대 약 0.001783cm였다. 이 검사는 25 FPS이며 24 FPS로 가정한 초기 시간 비교는 제외했다.

원본에는 면적 0인 면이나 zero normal vertex가 없었지만 UE MikkTSpace가 zero-length normal 로그를 남겼다. Legacy importer의 bind pose 재구성 경고도 포함해 무경고 import라고 주장하지 않는다. 실제 왕복 geometry/transform, 렌더와 플레이를 별도로 검증했다.

원시 자료는 `Saved/ChWomanRigValidation`, 제작 검사는 `SourceArt/Characters/ChWoman/Work`, [최종 요약 JSON](validation/ChWoman_Runtime_20261031.json)에 있다.

## 미검증 범위와 복원

무기 파지의 모든 시각 접촉, 공격 몽타주 전체, full multiplayer 게임, 래그돌 조율, LOD 전환, cook/packaging은 미검증이다. PhysicsAsset은 새 체형에 자동 피팅했으며 일반 무기 소켓의 bone 유효성 이전을 파지 품질 승인으로 해석하지 않는다. 표정·립싱크와 자동 의상/헤어 물리는 추가 제작 범위다. 초기 남성 작업의 미검증 항목까지 이번 여성 검사로 통과한 것은 아니다.

여성 BP/ABP 변경 전 사본은 `Saved/ChWomanRigValidation/Before`에 있다. 에디터 종료 후 해당 콘텐츠 상대 경로로 복원한다. 기존 여성 메시·공용 Skeleton·모션은 보존했다. Git/LFS 설정이나 저장소는 수정하지 않았다.

원본 SHA256: `7AF6A898E8F1BB213DE81E0FB7753BFD7CED86B8616B3C62AD306149AAF3AF81`.

최종 메시 FBX SHA256: `37A67B3F0FA490DBE5BB1247F4E4C6E8FDB304A40DC27B3BC5AE46F85BA3FDE6`.
