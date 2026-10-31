# ShipAI 병합 및 남녀 시체 로직 복원

`JYU_merge`에 `KKH/ShipAI`의 `180f2036`을 병합하고, 작업 도중 추가된 배 돌진 수정 `2a4fe193`도 반영했다. 충돌은 ShipAI를 우선하고, 최신 BP/ABP에 리그·물리 시체 보정을 다시 적용했다. 병합 전 커밋은 `1c41c6e0`, 로컬 보존 브랜치는 `codex/backup-jyu-before-ship-ai-20261031`이다.

## 동작과 변경 범위

- ShipAI의 사망 모션과 움직이는 갑판의 CharacterMovement 이동을 유지한다. `DeathStarted` 중에는 물리 시체로 전환하지 않는다.
- `DeathFinished`에서 캐릭터를 숨기던 부분을 `SWPlayerRagdollComponent`로 전환한다. 물리 갱신 후 `SWCorpsePoseComponent`가 팔·다리 twist, 척추·목·쇄골의 시체 스킨을 보정한다. 원본 물리 본은 수정하지 않는다.
- 재진입은 시체 충돌이나 운동량을 초기화하지 않는다. reset에서 원래 메시·렌더·애니메이션·충돌·부착 장비 표시를 복원한다. 카메라는 골반 이동을 따라가되 골반 회전으로 화면이 기울지 않는다.
- 최신 CameraReveal, 체력/인벤토리/상호작용, 배 AI, 보스, 공통 GAS 사망 GA, 부활/시체 제거 **10초** 정책은 ShipAI 코드와 동일하게 보존했다. 과거 문서의 운영 5초 표기는 현재 코드에 해당하지 않는다.
- 최신 남녀 BP/ABP를 원본으로 사용했다. 기존에 검증한 고정 ModifyBone 설정과 정확히 일치한 남성 7개·여성 2개만 제거하고 새 메시를 연결했다. 남은 노드 GUID·설정과 BP 메시 위치·회전·배율(남 1.0, 여 1.1)을 대조했다. 새로 추가된 제어나 모션 매칭 그래프를 이전 에셋으로 덮지 않았다.
- 남녀 물리 에셋 및 재질 복원은 유지한다. 어깨 twist ±25°, 팔꿈치 굽힘 0–40°, 무릎 0–50°의 목표 제한이다. 충격 시 solver의 일시적 초과는 남을 수 있다.

## 검증

실행 결과는 [검증 보고서](validation/ShipAI_Corpse_Integration_20261031.json)에 저장한다. 원시 로그·프레임 자료·최신 BP/ABP 변경 전 사본은 `Saved/ShipAICorpseIntegration`에 있다.

- 직접 UnrealBuildTool 빌드 성공. 기존 경고는 있으나 컴파일 오류 없음.
- 사망 모션 중 갑판 이동, 시체 전환·표시·reset, 카메라, 부활 소유권, 적 사망, 발 배치·점프·정지·활·대포 회귀 23개 통과.
- 추가 ShipAI 커밋 반영 후 직접 UBT 재빌드 성공. 배 돌진 정지·배 네트워크 물리·개발 입력까지 확장한 62개 회귀는 **60개 통과 / 배 AI 2개 실패**다. 아래 실패도 보고서에 포함하며 전체 회귀 통과로 분류하지 않는다.
- 남녀 정지·달리기·질주·점프·이동 갑판 10개 / 2,369프레임. 비정상 수치 없음, reset 후 이동 재개. 물리 본과 visual 최대 오차 약 0.000224cm. 이 검사는 물리 진입점을 직접 실행하며, 체력 소진에 따른 생명주기는 별도 네트워크/맵 검사로 확인한다.
- listen server 및 client에서 남녀 총 4개 시체가 표시되고 보정 visual도 생성됨. 실제 체력 소진, 타격 방향, 새 캐릭터의 ASC 소유권과 이전 시체 유지 검사 통과.
- 최신 남녀 BP/ABP의 실제 모션 매칭을 각각 12개 이동·점프 단계, 총 3,119프레임 검사. 비정상 수치·변형 본 75° 초과 프레임 점프 후보 없음. 초기 idle 준비 프레임 남 1개·여 2개를 제외한 모든 단계에서 활성 MM 결과 유효. 여성의 기존 IK 마커 배율과 최대 약 0.68% 변형 본 스케일 변화는 기존 여성 리그 검증 범위와 일치한다.
- 실제 테스트 키 바인딩 경로에서 남 10.001초·여 10.000초 자동 부활, 성별 전환 및 새 생명의 표시·물리 종료·ASC 소유권 확인.
- 추적 Unreal 패키지 11,458개: 실제 파일 존재·포인터 잔류·0바이트·헤더·LFS 크기 이상 없음. 변경 패키지 84개는 SHA-256 대조 통과. 3줄/약 130바이트 Git diff는 LFS 포인터이며 실제 에셋 용량이 아니다.
- 프로젝트 레지스트리의 변경·필수 에셋 103개 로드 성공. 전체 BP 컴파일은 **프로젝트 463개 포함 총 627개**, 아래 기존 오류 때문에 전체 통과로 분류하지 않는다.

## 확인된 기존 에셋 문제

이 항목의 소스 에셋은 ShipAI와 동일하며 시체 이식에서 새로 생긴 차이가 아니다. 범위 밖 에셋을 일괄 저장하거나 참조를 삭제하지 않았다.

| 항목 | 확인 결과 |
|---|---|
| `/Game/Chae_Asset/YiSunSinContent/Animations/ABP_YiSunSin` | 타깃 스켈레톤 누락으로 BP 컴파일 오류 1건 |
| 같은 폴더의 `BS_YiSunSin_Locomotion` | `AS_YiSunSin_Walk`, `AS_YiSunSin_Run` 샘플 범위 오류 |
| `/Game/jiwon/Test_BP_EnemyShip` | 삭제된 `DeckNavigationComponent` 클래스 참조 경고 |
| `/Game/Blueprints/AnimNotifies/E_FoleyEventSide` | 일부 기존 에셋에서 누락 enum 로드 경고 |
| `StylizedWeather` 예제 캐릭터, `Shooter_VFXPack` 예제 BP | 전체 BP 컴파일 경고 목록에 포함 |
| `ArtisticSW.Enemy.Ship.Anchor.Integration` | 앵커를 올린 뒤 입력 기대값 1.0/0.5 대신 0.0/0.0. 해당 배 AI·테스트 코드는 ShipAI 원본과 동일하며 수정하지 않음 |
| `ArtisticSW.Enemy.Ship.Navigation.DeterministicAvoidance` | 양보 선박의 후진 입력과 복귀 시 회피 종료 단언 실패. 해당 배 AI·테스트 코드는 ShipAI 원본과 동일하며 수정하지 않음 |
| 누락 하드 패키지 참조 후보 389건 | 외부 액터 290건, 외부 오브젝트 7건, 예제/캐릭터 콘텐츠 등. 파일 손상 389개라는 의미가 아니다. 이번 ShipAI 대비 변경 에셋에서 발생한 후보는 0건. 전체 목록은 보고서에 포함 |

배 AI의 위 두 실패는 플레이어 시체 검사를 실행하지 않는 새 프로세스에서 두 테스트만 실행해도 동일하게 재현됐다. 테스트는 배 액터만 생성한다. 단순 실행 순서 문제로 처리하거나 배 AI 구현을 변경하지 않았다. 이 검증은 별도 순수 ShipAI 프로젝트를 빌드해 비교한 결과까지 의미하지 않는다.

## 직접 테스트

Development Editor 빌드 후 `/Game/Developers/PlayerRagdoll/L_PlayerRagdollTest`에서 1인 Selected Viewport PIE로 실행한다. 바탕화면 **남녀 시체 테스트 (프로젝트)** 또는 저장소의 `Scripts/Editor/ChManRigValidation/Open_PlayerRagdollTest.cmd`도 같은 맵을 연다.

- **Ctrl+Alt+Shift+F10**: 사망 모션 → 시체 → 테스트 키 입력 후 10초 자동 부활
- **Ctrl+Alt+Shift+F11**: 남녀 전환

이 테스트 GameMode는 Editor 전용이다. 실제 운영 부활 정책은 MultiGameMode가 소유하며 테스트 모드로 대체하지 않는다. 최신 개발 입력의 Ctrl+Alt+F7과 겹치지 않도록 성별 전환을 F11 조합으로 바꿨다.

ShipAI 병합 프로젝트의 실제 UE 캡처:

![남성 시체](images/PlayerRagdollPoseFix/ShipAI_Man_idle_settled_angled.png)

![여성 시체](images/PlayerRagdollPoseFix/ShipAI_Woman_idle_settled_angled.png)

충격 순간의 관절 제한 초과, 옷·피부·머리카락·가방 관통은 완전 제거 결과가 아니다. 다수 시체 성능, dedicated server, 물속/침몰, 패키징/cook, 모든 LOD·모프·커스텀 재질 데이터의 전 조합은 이번 검증 범위 밖이다.
