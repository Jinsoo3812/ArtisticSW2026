# 남녀 플레이어 PhysicsAsset·시체 통합 — 2026-10-31

현재 운영 `BP_Player_Man`, `BP_Player_Woman`의 새 메시와 전용 PhysicsAsset에 맞춰 물리 시체를 연결했다. 기존 `Health → DeathStarted/Finished → MultiGameMode`의 생명주기와 PlayerState ASC·진행도·부활 소유권을 유지한다. 이전 캐릭터 리깅 보고서에서 “PhysicsAsset 자동 피팅, 래그돌 미검증”으로 남긴 항목은 이 보고서의 범위에서 갱신한다.

## 에셋과 관절

| 대상 | PhysicsAsset | 강체 / 관절 | 기준 질량 |
|---|---|---|---|
| 남성 | `/Game/jiwon/Characters/ChManUE5/PA_ChMan_UE5` | 17 / 16 | 72kg |
| 여성 | `/Game/jiwon/Characters/ChWomanUE5/PA_ChWoman_UE5` | 17 / 16 | 58kg |

두 메시의 실제 weighted body/clothes/shoes 정점과 관절을 Blender MCP로 읽어 캡슐·상자 크기와 위치를 따로 피팅했다. 머리카락·가방·장식은 충돌 크기에서 제외했다. 여성 BP의 기존 균일 배율 1.1을 보존한다. 질량은 명시적 override다. pelvis, 허리·가슴, 목·머리와 양쪽 상완·전완·손·허벅지·종아리·발을 사용한다. 손가락·twist·얼굴·IK marker마다 독립 강체를 만들지 않는다. 시뮬레이션되지 않는 가지는 가장 가까운 물리 부모를 따른다.

전체 관절의 선형 이동을 잠그고 인접 강체의 상호 충돌을 끈다. 비인접 부위는 충돌한다. 연결 파괴나 모터는 사용하지 않는다. 굽힘·비틀림 축을 구분하고 비대칭 범위는 parent frame 중심 편향으로 표현한다.

- 팔꿈치 굽힘 목표 0–145°, 회내·회외 65°, 측방 5°.
- 무릎 굽힘 목표 -2–125°, 축 비틀림 10°, 측방 5°.
- 어깨 75/85/55°, 손목 25/45/35°; 목과 머리는 작은 별도 제한을 적용한다.
- 고관절 swing 40/65°, twist 30°에 굽힘 중심 45°; 발목 swing 20/32.5°, twist 12°에 굽힘 중심 -12.5°.

공통 `/Game/jiwon/Characters/Physics/PM_PlayerCorpse`는 friction .7, static friction .8, restitution .02다. 강체 damping은 선형 .35/각 2.2, inertia scale 1.2, CCD를 사용한다. body solver iteration 64/16, projection 1 및 projection tolerance 3cm/8°로 설정했다. 세계 async 60Hz와 공용 Physics 설정은 변경하지 않았다. 이는 유한 반복 solver의 목표 제한이며 충격 순간 오차가 0이라는 뜻은 아니다.

## 런타임 책임과 수정 원인

`USWPlayerRagdollComponent`는 표현만 담당한다. `DeathStarted`에서 현재 평가된 자세를 .12초간 물리로 전환하고, locomotion capsule을 끄며 메시를 표시한다. 애니메이션은 최종 자세에 멈춰 물리 결과와 경쟁하지 않는다. 시체가 부족한 PA를 사용하면 기존 안전한 숨김 표현으로 빠진다.

사망 직전 캐릭터 속도와 접촉한 이동 기반의 선형·회전 운반 속도를 한 번 이어받는다. 유효하지 않은 값은 제거하고 최대 1800cm/s로 제한한다. 치명타 방향이 있을 때 실제 hit bone의 물리 부모에 질량 비례 충격을 준다. 손가락 피격은 손 강체를 사용하며, 영향점 lever arm 20cm, 속도 변화 기본 120cm/s(최대 500)로 제한한다. 방향 없는 사망에는 임의 정면·위쪽 발차기를 추가하지 않는다.

시체는 WorldStatic·WorldDynamic·PhysicsBody와 충돌한다. Pawn·Camera·Arrow·WeaponAim 등은 무시하므로 살아 있는 캐릭터를 밀거나 조준 대상을 막지 않는다. 부착 장비는 기존처럼 숨기고 충돌을 끈다. 같은 사망 통지가 반복되어도 속도와 충격을 재적용하지 않는다. tick은 짧은 전환 동안만 필요하며 이후 꺼진다. reset은 메시 attachment·상대 transform·1.1 배율·충돌·표시·애니메이션·gravity·overlap·CCD·서버 물리 플래그와 부착 장비 상태를 복원한다. reset이 체력·ASC·부활 정책을 소유하지 않는다.

실제 코드 경로에서 확인한 원인과 수정은 다음과 같다.

1. `PrepareDeathMovement`가 사망 GA에서 재호출되며 actor 전체 충돌을 껐다. capsule만 끄도록 바꿔 이미 시작한 물리 메시의 바닥 충돌을 보존했다.
2. Unreal은 skeletal body 초기화 때 PA iteration을 component 기본값으로 덮어쓴다. 사망 전환 시 명시적으로 override된 PA 값을 각 실제 body에 한 번 다시 적용한다. AnimGraph RigidBody용 PA solver setting만 수정하는 방식으로 세계 래그돌이 개선됐다고 간주하지 않는다.
3. `UPhysicsConstraintTemplate`은 editor 표시 instance와 저장 `DefaultProfile`이 다르다. 지원 API `SetDefaultProfile`을 사용하고 다음 프로세스에서 저장된 제한·Limited/Locked 상태를 입력과 비교한다.
4. 물리 전환 뒤 멈춘 애니메이션의 사망 몽타주를 기다리면 completion timeout이 난다. `ShouldPlayDeathMontage` 정책으로 물리 전환에 성공한 플레이어는 기존 GA에서 즉시 `FinishDeath`하고, BP가 직접 호출한 montage도 같은 정책을 따른다. 다른 캐릭터의 기본 montage 정책은 유지한다.
5. 카메라는 pelvis의 이동만 world offset으로 추적하고 horizon은 사망 당시 회전을 유지한다. 시체 회전을 받아 카메라가 굴러가지 않는다. 시체가 만료되면 마지막 위치를 유지하고 새 생명 소유 시 기존 카메라 관리로 돌아간다.

이는 물리 표현과 실제 원인 수정이다. 새로운 부활 관리자를 만들지 않았다. 현재 `MultiGameMode::IndividualRespawnDelay`는 5초이며 기존 시체 수명·부활 재시도·침몰 흐름을 그대로 따른다.

## 검증 방식과 결과

최종 수치와 통과 목록은 [검증 JSON](validation/Player_Ragdoll_20261031.json)을 따른다. 실제 운영 BP/ABP의 PIE로 남녀 각각 idle 30/60/120 FPS, 달리기·질주·점프·250cm/s 평행 이동 `ShipDeck` 발판을 실행한다. 각 시체를 8초 관찰한 다음 중복 사망 진입과 표시 상태 reset, 이동 재개를 검사한다. 지연된 수치 오류·anchor separation·메시/강체 불일치·정착 속도를 기록한다.

최종 **14개 시나리오 / 7,200프레임**이 통과했다. 실제 idle FPS는 약 30/60/120이었다. NaN/무한 transform은 0, .25초 이후 render bone/body 위치 차이는 최대 약 0.000233cm였다. 중복 진입의 속도 재적용은 0이며 모든 reset 뒤 애니메이션이 다시 평가되고 250cm/s 이동을 재개했다. 두 이동 발판 case의 최초 상속 속도는 모두 `[250,0,0]`cm/s다.

충격 순간 최대 제한 초과는 **11.482°**, anchor separation은 **3.312cm**였다. 약 7.9초의 최종 측정에서는 각각 **0.655° / 0.156cm** 이하였다. 따라서 충격을 포함한 모든 프레임에서 관절 제한이나 연결 오차가 0이었다고 주장하지 않는다. 정지 바닥의 마지막 1초 최대 body 속도는 0–0.84cm/s, 이동 발판에 대한 상대 속도는 남성 약 5.60cm/s·여성 약 0.08cm/s였다. 강체가 계속 가속하거나 발산하지 않았고, 확인한 여섯 최종 포즈에서 지속적인 역관절은 관찰하지 않았다.

호스트·클라이언트는 15회 시점에서 매번 남녀 **네 시체 replica**의 유효성을 검사했고, 두 새 생명의 server/client 네 replica가 표시·비물리·정상 ASC avatar 상태로 돌아왔다. 이전 네 시체는 새 소유 전환 뒤에도 표시와 물리를 유지했다. 운영 MultiGameMode에서 정한 5초 만료 자체는 native lifecycle test로 검증하며, 8초 관찰용 network fixture는 GameModeBase이므로 실제 room에서의 시체 만료/부활을 실행한 결과와 구분한다.

직접 UBT Development Editor 빌드가 성공했다. native 자동화 **23개가 모두 통과**했고 이 중 6개는 기존 콘텐츠 경고를 포함했다. 범위는 Player.Death 3, PlayerCorpseLifecycle 1, Enemy.DeathRagdoll 5, FootPlacement 2, JumpAir 7, Stop 1, Cannon graph 1, PlayerBow 3이다. 사망 재진입·장비 표시 복원·1.1 배율·중력/overlap 복원·ASC/인벤토리 생명 소유권·시체 만료 후 카메라·오래된 부활 콜백·로그아웃/침몰 정책을 포함한다.

실제 UE 최종 포즈를 Blender 사본에서 렌더한 예: [남성 점프 후](../SourceArt/Characters/Physics/Review/Man_jump.png), [여성 점프 후](../SourceArt/Characters/Physics/Review/Woman_jump.png). 게임 프레임버퍼 캡처가 아니다.

각도 검사는 whole quaternion을 각 축에 투영하는 `GetCurrentSwing1/2` 값을 그대로 제한과 비교하지 않는다. 회내·회외와 굽힘이 섞이면 실제보다 크게 계산되기 때문이다. 실제 constraint frame 상대 quaternion을 swing/twist로 분해하고 Chaos `FPBDJointUtilities::GetConeAngleLimit`과 같은 타원 cone 반경으로 초과량을 계산한다. 프레임간 solver 보정으로 생긴 일시 초과와 지속적인 역관절을 구별한다.

네트워크 fixture는 리슨 호스트+클라이언트, 운영 캐릭터 BP·컨트롤러·native PlayerState를 사용한다. 남성에게 손가락 치명타 context, 여성에게 방향 없는 체력 사망을 적용해 총 네 시체 replica의 health state·물리·표시·유한 transform 및 남성 impact payload를 확인한다. 새 생명 소유는 운영 `BeginLifeInitialization → RestoreProgressForNewLife → EndLifeInitialization` 절차를 따른다. 이 barrier 없이 dead ASC를 새 pawn에 곧바로 연결한 첫 fixture는 재진입 사망으로 충돌했으며 유효한 부활 검사에서 제외했다. 초기 충돌 로그는 `Saved/PlayerRagdollValidation/network.log`의 첫 시도 사본에 보존한다.

## 자료·복원·검증 경계

재현 입력과 Blender 렌더 스크립트는 `SourceArt/Characters/Physics`, 실행 도구는 `Scripts/Editor/ChManRigValidation`, 원시 결과는 `Saved/PlayerRagdollValidation`에 있다. 처음 PA·메시 사본은 `Saved/PlayerRagdollValidation/Before`에 있으며 에디터를 닫고 해당 Content 상대 경로로 복원할 수 있다. 원본 Desktop FBX·공용 Skeleton·모션·기존 원본 캐릭터 PA와 Git/LFS 설정은 변경하지 않았다.

시체 자세는 각 머신의 로컬 물리 표현이다. 관절 전체의 네트워크 동기화나 시체를 게임 판정용 권위 객체로 만드는 구현은 추가하지 않았다. 물리 충돌 proxy가 실제 의상 모든 표면을 덮지 않으므로 일부 옷·머리카락의 바닥/신체 관통은 가능하다. 이 작업은 천·머리카락 시뮬레이션 제작을 포함하지 않는다.

렌더 검사에서도 표면 관통이 실제로 남았다. 여섯 최종 포즈의 최저 정점 기준 남성 피부/신발은 최대 약 5.33cm, 여성 피부/신발은 최대 약 4.49cm 바닥 아래였다. 여성의 머리카락은 최대 18.37cm, 가방은 9.99cm다. 머리카락 최저점은 head에 100% 바인딩된 긴 머리 정점이었다. 이를 완전한 표면 충돌이나 의상·머리카락 물리 완성으로 해석하면 안 된다. 재구성한 전체 표면·재질별 수치는 JSON의 `render_review.bounds`에 보존했다.

평행 이동 평면 검사는 실제 배의 회전·파도·침몰·물속 buoyancy 검증을 대신하지 않는다. 전용 서버·패킷 지연/유실·전체 room 여행/재접속·모든 전투/장비/몽타주·계단/복잡지형/높은 추락·다수 시체 성능·cook/packaging은 이번 검증 범위 밖이다. 관련 기존 누락 experimental animation DB 경고가 있어 프로젝트 전체의 무경고 상태를 주장하지 않는다.
