# 캐릭터 Foley / 지형별 발소리

2026-10-16, Unreal Engine 5.7.4 기준. Project_J의 Foley 기반을 이식하고 ArtisticSW2026의 캐릭터·이동 상태 수명에 맞춰 연결했다.

## 2026-10-28 표면·적 접촉 확장

현재 `/Game/Level/Lvl_CY`는 Sand/Soil/Grass/Water/Wood/Rock/Metal을 SurfaceType1~7로 정의하고 전용 큐를 연결한다. 기존 GASP 기본 음원은 미지정 표면의 fallback으로 남긴다. 플레이어 애니메이션 Notify는 유지한다. 아래 2026-10-16 기록의 ‘표면 슬롯/맵을 변경하지 않았다’는 당시 초기 이식 범위다.

`ABaseEnemy`에도 native FoleyComponent를 상속시킨다. 실제 걷기/뛰기 클립 16개에 `SWAnimNotify_EnemyFootContact` 86개를 작성했으며, 원본 포즈의 발 높이·속도로 접촉을 찾아 작성할 때만 분석한다. 런타임 매 프레임 발 IK를 분석하는 방식은 아니다. 적의 기본 `bUseDistanceDrivenSteps=false`와 authored contact 경로를 함께 사용해 거리 주기와 중복하지 않는다. 같은 발 marker의 blend 중복은 접촉 컴포넌트에서 거른다. 해당 클립/BlendSpace를 사용하는 다른 캐릭터를 변경할 때도 접촉 경로를 확인한다.

스케일된 Landscape의 표면 조회는 월드 cm를 유지하며, 선박의 단순 충돌이 기본 표면을 반환하면 현재 이동 베이스의 갑판 목재 표면을 보조 사용한다. 전용 서버 제외·거리/동시 재생 예산과 공유 비동기 로딩은 유지한다. 가까운 살아 있는 비수영 적의 공중 입수 감지도 동일 컴포넌트에 추가했으며 사망 래그돌은 대상이 아니다.

강우 믹스·발 접촉 검증, 적 클립 재작성/백업 절차, 수영/입수·함선·선실 및 2인 출력 결과는 [World audio 통합 문서](World_Audio_Integration.md)를 따른다. 최종 SW.Audio/SW.Foley 자동화 15/15와 실제 출력 수치는 [검증 요약](validation/World_Audio_20261028.json)에 보존한다. 이전의 10개/13개 성공 기록은 각 실행 시점의 범위다.

## 런타임 연결

`GASP Animation → SWAnimNotify_FoleyEvent → BasePlayer.FoleyComponent → SWFoleySubsystem → 지면 Physical Material → SurfaceSounds / DefaultSound → 공간화 재생`

- `ABasePlayer`의 기본 서브오브젝트이므로 남녀 플레이어 BP에 자동 상속된다. BP를 다시 만들거나 GASP AC_FoleyEvents를 추가할 필요가 없다.
- 기본 프로필은 `/Game/Audio/Foley/DA_Mannequin_Foley`. 캐릭터의 `Foley.ProfileOverride`로 별도 프로필을 지정할 수 있다. 다른 Character BP는 SWFoleyComponent를 명시적으로 추가하고 프로필을 할당한다.
- 기존 GASP MetaSound preset 12개를 사용한다. Walk, Run, Jump, Land, Scuff, Handplant, RunBackwds, ScuffPivot, ScuffWall, RunStrafe, Tumble, WalkBackwds를 정확한 태그로 연결한다. 런타임에서 태그 이름의 부분 문자열로 음원을 추정하지 않는다.
- 사망·래그돌·대포/탈것 조종·수영 중에는 차단한다. Footstep/Scuff는 CharacterMovement가 지상일 때만 허용한다. 소스 메시만 노티파이를 받으므로 표시 메시나 의복에서 중복 접촉을 만들지 않는다.
- 로컬 점프는 실제 `OnJumped`, 착지는 기존 중복/가짜 착지 방지 경계 뒤 `StartLanding`에서 재생한다. 원격 점프·착지는 기존 이동 이벤트를 사용한다. Jump/Land 애니메이션 노티파이는 기본 정책에서 제외해 두 경로의 중복을 막는다.
- 기존 이동 복제 스냅샷에 이벤트 서버 시각을 보존한다. 원격 확인 이벤트가 기본 0.15초보다 오래되면 음향 요청을 버린다. 매 발걸음 RPC나 별도 컴포넌트 Tick은 추가하지 않았다.
- 가까운 타 캐릭터는 기본 볼륨 0.6. listener 거리, 가족/좌우 cooldown, 공유 concurrency, 프레임·초당 원격 예산을 적용한다. 로딩 중 이벤트를 나중에 몰아서 재생하지 않는다. 전용 서버는 서비스·음원 로딩·트레이스를 수행하지 않는다.
- 프로필과 음원은 공유 비동기 로딩으로 준비한다. 소유권 획득 시 로컬 준비를 요청한다. 기본 프로필은 AssetManager의 AlwaysCook 자산이며 음원 의존성을 재귀적으로 포함한다.

## 지형별 음원 작성

1. 프로젝트에서 사용할 Physical Surface 이름을 Project Settings → Physics에 정의한다. 현재 작업에서는 전역 표면 슬롯 이름이나 기존 맵/재질을 임의로 바꾸지 않았다.
2. 각 바닥/메시/랜드스케이프의 Physical Material에 Surface Type을 지정한다. 실제 충돌 질의가 해당 Physical Material을 반환해야 한다.
3. `DA_Mannequin_Foley`의 `Events`에서 Walk/Run 등 이벤트를 펼친다.
4. `SurfaceSounds`에 해당 Surface Type → Sound Wave/Cue/MetaSound를 연결한다. `DefaultSound`는 기존 GASP 음원을 유지한다.
5. 프로필을 저장한다. 해당 표면 접촉 시 override를 사용하고, 미정의 표면·빈 override·trace miss는 기본음을 사용한다. 표면 음원이 아직 준비되지 않았으면 준비된 기본음으로 돌아간다.

기존 GASP 음원은 Sneaker/Concrete 등을 포함한 샘플 콘텐츠다. 지형 판별 기능과 별개로 목재·흙·잔디 등 실제로 다른 소리를 내려면 해당 음원을 SurfaceSounds에 작성해야 한다. 이름만 다른 동일 음원을 만들어 지형 차이로 가장하지 않는다.

프로필의 Contact는 Foot/Hand/Capsule/Socket을 지원한다. 기본 본은 foot_l/r와 hand_l/r다. 최근 갱신되지 않은 메시 포즈는 캡슐 하단 위치를 사용하며 발소리를 위해 군중 전체의 본 갱신을 강제하지 않는다. 표면 판정은 기본 Visibility의 짧은 simple line trace다. 실제 landscape physical material 연결, 복잡 충돌 전용 메시, 물 깊이/젖음은 제작 환경에서 별도 확인한다. ScuffWall은 벽 방향을 추적하지 않는다.

`Slide.Loop` 태그는 기존 뱅크의 데이터 해석을 위해 등록하되 one-shot 프로필에서는 제외한다. 시작/종료 수명이 필요한 루프 재생을 발걸음 API로 만들지 않는다.

## 에셋 보존과 변환

- 보유 GASP 애니메이션 635개 검사, 499개 파일의 Foley 4,438개를 native로 저장했다. 그중 객체가 제거되고 이름만 남은 768개는 로컬 원본 GASP 5.7의 GUID와 이벤트 메타데이터를 대조해 복구했다. 이 프로젝트에는 GASP Walk 애니메이션 폴더가 없으며 없는 시퀀스를 복사하거나 생성하지 않았다.
- 포즈·커브·애니메이션 선택·트랙·접촉 시간·GUID·Trigger Weight·확률·필터와 다른 노티파이는 보존했다. 의도적인 이벤트 메타데이터 변경은 Foley의 `bTriggerOnDedicatedServer=false`다. VolumeMultiplier=0인 무음 접촉과 원본의 실제 named-only 마커도 유지한다.
- 오디오 기존 파일 266개의 SHA-256은 변경 전후 동일하다. 기존 GASP 오디오 뱅크·AC_FoleyEvents는 샘플/제작 자산으로 유지하며 기본 플레이어의 native 경로와 중복 연결하지 않는다.
- `USWFoleyMigrationLibrary`와 `Scripts/Editor/author_foley.py`는 Editor 전용 변환·검증 도구다. 원본의 AnimNotifies 폴더만 읽기용으로 임시 mount하며 대상 Player BP/Audio 폴더를 가리지 않는다. 원본 GASP 파일은 저장하지 않는다.
- 변환 스크립트는 원본이 필요한 모드에 `-FoleySourceContent="<원본 GASP Content 절대 경로>"`를 명시한다. 보고/백업의 기본 위치는 `Saved/Validation/FoleyMigration/`이며 `-FoleyReportDir="<기존 보고 폴더>"`로 검증/재개할 기록을 선택한다. 개인 PC 경로를 소스에 고정하지 않는다. `verify`는 선택한 보고의 apply snapshot과 오디오 해시를 사용하며 새 PC에 그 로컬 기록이 자동 포함되는 것은 아니다.
- 적용 전 해시와 백업, 배치 결과는 `Saved/Validation/Foley_20261016/`에 있다. 기존 백업이 있는 상태의 재적용은 거부한다. `-FoleyMode=verify`는 원본 없이 검사하고 `verify-source`는 원본 문맥에서 전체 메타데이터와 재교체 0개를 확인한다.

## 검증과 남은 범위

- 설치 엔진 direct UnrealBuildTool.exe: ArtisticSW2026Editor Win64 Development 성공. 빌드가 끝나기 전에 다른 빌드를 시작하거나 기존 프로세스를 종료하지 않았다.
- 새 UE 5.7 프로세스, 원본 mount 없이: 499개/4,438개 native Foley payload·저장 메타데이터, 남녀 플레이어 기본 컴포넌트/프로필, 12개 이벤트와 기존 오디오 266개 해시 검증. 프로세스 exit 0.
- 원본 문맥: 기존 non-Foley 참조를 포함한 전체 이벤트 메타데이터 비교 및 재교체 0개 통과. 원본 EarlyTransition BP의 별도 의존성 누락으로 프로세스 exit 1이므로 무오류 실행으로 기록하지 않는다.
- 최종 `SW.Foley` 결과: **10개 성공, 실패 0, 테스트 오류/경고 0**. `Saved/Validation/Foley_20261016/Automation/index.json`에 기록했다.
- `SW.Foley` 자동화는 실제 GASP 달리기 클립의 native notify→서비스 요청, 물리 재질을 가진 실제 바닥 trace→표면 override/기본음, 접촉 본/소켓, 중복/군중 예산, 종료/취소, 전용 서버 차단과 프로필 검증을 포함한다. 임시 물리 재질은 물리 월드 초기화 전에 준비해 Chaos의 query-material snapshot에 포함시킨다. 게임의 재질 판정 로직을 테스트 편의용 fallback으로 바꾸지 않았다.
- 원본과 같은 경로를 참조하지만 프로젝트에 클래스가 없는 **EarlyTransition 이벤트 21개**는 기존 문제다. 해당 이벤트/참조는 그대로 보존했다. Foley 변환으로 전환 정책을 교체하거나 복원했다고 주장하지 않는다.
- 자동화는 NullRHI/nosound 조건이다. 아래 실제 PIE 출력 검증과 구분한다. 사람의 청취·볼륨 평가, 제작 맵 Lvl_CY의 전체 동선, 2인 PIE 지연, 대규모 군중 성능, 전체 Cook/패키징 실행은 수행하지 않았다. 패키징 규칙의 등록은 별도 자동화에서 확인한다.

### 실제 PIE 이동·점프 출력 확인 (2026-10-16)

- UE 5.7.4 `/Game/ThirdPerson/Lvl_ThirdPerson`에서 실제 `BP_Player_Woman`/`ABP_Player_Woman`, `BP_Player_Man`/`ABP_Player_Man`을 사용했다. Enhanced Input의 기존 IA_Move/IA_Jump에 입력을 주고 CharacterMovement와 Motion Matching을 그대로 실행했다. 테스트를 위해 다른 Character는 PIE 안에서만 멀리 이동시켰으며 맵/캐릭터 에셋을 저장하지 않았다.
- 각 캐릭터에서 정지 → 전진 → 정지 → 후진 → 제자리 점프 3회 → 이동 점프 1회 → 횡이동 → 정지를 약 24초, 각각 1,440개 프레임 샘플로 확인했다. 중앙값 60fps. 실제 이륙/착지 상태 변화와 native 재생 제출 기록을 대조했다.
- 여성 `woman_focus60`: Run 20, Walk 8, Jump 4, Land 4. 남성 `man_60fps`: Run 11, RunStrafe 9, RunBackwds 7, WalkBackwds 7, Jump 4, Land 4. 각 점프 구간마다 Jump/Land가 정확히 1개씩이며 총 8회에서 중복 Jump/Land 및 누락 0. 이동 점프 후 지상 발소리도 재개된다. 남성의 일부 제자리 점프 후 별도 WalkBackwds 접촉은 Land로부터 0.6초 이상 뒤의 애니메이션 접촉이며 추가 Land는 아니다.
- 전면 에디터의 실제 게임 믹스를 48kHz stereo WAV로 녹음했다. 여성 peak 0.055908, 남성 peak 0.081909로 비무음 출력을 확인했다. 재생 제출 수를 곧바로 실제 하드웨어 청취 수로 간주하지 않는다. WAV 길이는 게임 시간보다 짧으므로 구간별 오디오 샘플과 월드 시각의 정밀 정렬을 주장하지 않는다.
- 여성 최종 PIE 전체 상태는 Submitted=37, Played=37, NotReady/Duplicate/Expired/Budget/Unmapped=0이다. 여기에는 시나리오 시작 전 위치 조정 착지 1회가 포함되므로 위 시나리오 36개와 구분한다. 남성 시나리오에서도 준비/중복/예산 drop 증가는 없었다.
- 별도 콜드 시작 시행에서는 시작 약 3.5초에 스크립트가 캐릭터를 150cm 높이로 옮겨 발생한 착지 1회가 음원 비동기 준비 중 NotReadySound로 폐기됐다. 그 시행의 실제 입력 점프 4회는 모두 정상이다. 준비 전 이벤트를 나중에 몰아서 재생하지 않는 기존 정책은 유지했고, 시작 즉시 접촉까지 무조건 보장한다고 주장하지 않는다. 백그라운드 에디터의 무음 녹음은 출력 성공 근거에서 제외했다.
- 이번 소스 추가는 기본값 0인 `SW.Foley.DebugEvents` 진단뿐이다. 접촉 승인/재생 제출·준비 실패를 기록하며 재생 정책, GASP 음원과 애니메이션은 추가 변경하지 않았다. 진단 추가 후 direct UBT 성공, 최종 자동화도 **10개 성공, 실패/테스트 경고 0, exit 0**이다.
- 근거: `Saved/Validation/FoleyLive_20261016/Summary.json`, `woman_focus60_samples.json`, `man_60fps_samples.json`, 동일 이름의 WAV/analysis JSON, `Automation/index.json`, `Saved/Logs/FoleyLive60Final.log`, `Saved/Logs/FoleyLiveRegression.log`. 재현용 일회성 스크립트도 검증 폴더에 보관했다. 검증용 PIE와 에디터는 정상 종료했다.

## 진단

콘솔 `SW.Foley.Status`는 제출·재생 제출·거리/준비/중복/예산 drop·표면 trace·미매핑·fallback·loop 거부 수와 마지막 Surface/Sound를 출력한다. Played는 하드웨어에서 실제 들린 voice 수를 뜻하지 않는다.

- `SW.Foley.DebugEvents 0/1`: 기본 0. 접촉 승인/재생 제출·준비 실패 진단
- `SW.Foley.Enabled 0/1`: 전체 Foley
- `SW.Foley.OtherCharacters 0/1`: 타 캐릭터
- `SW.Foley.RemotePerFrame`, `RemotePerSecond`, `WorkMilliseconds`: 원격/CPU 예산
- `SW.Foley.MaxEventAge`: 오래된 확인/대기 이벤트 폐기

커밋 전 정리에서는 검증 때만 필요했던 컴포넌트 사전 준비 상세 로그와 전용 로그 카테고리, 미사용 include/import를 제거했다. 이벤트 진단은 기본 비활성으로 유지하며 준비/재생 정책은 바꾸지 않았다. 기존 GASP 뱅크와 제작용 BP, 에디터 변환 도구, 영구 회귀 테스트는 각자의 제작·재검증 역할을 보존한다. 로컬 로그·백업·녹음은 Saved에 두고 커밋하지 않는다.
정리 후 재검증: UE 5.7 direct UBT 성공, `FoleyCommitVerify.log`의 499개/4,438개 및 기존 오디오 266개 해시 검증 exit 0, `Saved/Validation/FoleyCommit_20261016/Automation/index.json`의 10개 성공·실패/경고 0을 확인했다. 앞선 실제 PIE 검증 이후 재생 동작과 에셋 데이터는 바꾸지 않았으므로 그 출력 근거를 재사용한다.
