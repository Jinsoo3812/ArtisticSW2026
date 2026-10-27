# World audio / Lvl_CY

최종 갱신: 2026-10-28. UE 5.7.4. 기존 [Foley 시스템](Foley_Audio_System.md)을 확장한다.

## 현재 적용 상태

적용 맵은 `/Game/Level/Lvl_CY`다. World Audio 자산 295개(그중 SoundWave 162개), 프로필 사건 15개를 포함한다. `CannonSolidImpact`와 `ShipCreak`는 원음이 없어 비워 둔 별도 확장 슬롯이다. 새 게임플레이 음원은 프로젝트에 포함되므로 제공자의 UE 5.8 프로젝트나 로컬 WAV 경로가 실행에 필요하지 않다.

| 범위 | 적용 내용 |
| --- | --- |
| 대포·포탄 | 플레이어/적 함선 공통 발사·입수·물폭탄. 서버가 수락한 발사/최초 입수 경계에서 재생하며 캐릭터·포탄의 Heavy 입수 Cue를 분리한다. |
| 발 접촉 | 플레이어의 기존 Notify 유지. 적 이동 클립 16개에 접촉 Notify 86개를 작성하고 물/모래/흙/풀/갑판 목재/돌/금속 표면을 연결한다. |
| 캐릭터의 물 이동 | 얕은 물·전신 입수·출수·수면/수중 이동. 살아 있는 비수영 적의 낙하 입수도 감지하고 파도에 떠 있는 수직 운반은 stroke에서 제외한다. |
| 함선·환경 | 플레이어/적 함선의 항주 물살·선수 파도·누수, 바다/해안/수중 루프, 낮·잔잔한 날씨의 해안 갈매기를 연결한다. |
| 비·선실 | Rain을 Weather class로 분리하고 수중/선실에서는 전용 믹스로 감쇠한다. 강우가 전투·접촉 소리를 덮는 정도를 줄인다. |

최종 직접 UBT 빌드 및 SW.Audio/SW.Foley 자동화 **15/15 성공**. 2인 listen server/client PIE에서 플레이어·적 함선 대포/입수, 항주 및 양 플레이어·살아 있는 적의 입수 출력을 확인했다. 추가 누수·수영·선실·갈매기 검증은 아래 최신 기록을 따른다. 핵심 수치는 [버전 관리되는 검증 요약](validation/World_Audio_20261028.json)에 보존한다. 이전 날짜의 중단/실패 기록은 당시 상태이며 현재 미완료 상태로 읽지 않는다.

늦은 참가/패킷 손실, 모든 적 종류, 움직이는 선실 경계 전수, 시체/래그돌 입수와 패키지 실행은 남아 있다. 아래 ‘추가로 준비하면 좋은 에셋’은 필요한 원음과 후속 연결 경계를 정리한다. 대포 조종 중 캐릭터 표시/카메라 개선은 이번 오디오 변경에 포함하지 않는다. 에디터와 PIE는 정상 종료했고 원래 PIE 설정을 복구했다.

## 데이터와 재생 경계

- `/Game/Audio/World/DA_WorldAudio`: 발사, 포탄 입수, 물폭탄, 입수/출수, 수영, 바다/해안/수중 루프의 음원·볼륨·거리 감쇠·공유 concurrency. One-shot 큐는 복수 녹음을 비복원 랜덤 선택한다.
- `USWWorldAudioSubsystem`: 월드별 클라이언트 재생 서비스. BeginPlay에 프로필을 비동기로 준비한다. 준비 전 사건을 뒤늦게 재생하지 않는다. 전용 서버는 음원을 로드하거나 재생하지 않는다.
- `ACannon`: 투사체 생성 성공 뒤 발사 사건을 Unreliable Multicast로 한 번 보낸다. 플레이어/AI/물폭탄 발사가 같은 경계를 쓴다. 실패한 발사와 재장전 중 입력에는 소리가 없다.
- `ACannonball`: 서버의 최초 수면 접촉 뒤 입수 사건을 보낸다. 로컬 예측 파문에서는 소리를 재생하지 않는다. Niagara 누락 여부와 음향은 독립적이다. 포탄의 파괴 뒤에도 위치 기반 one-shot은 끝까지 재생한다.
- `USWFoleyComponent`/`USWFoleySubsystem`: 플레이어의 기존 발 접촉 Notify, 점프·착지 경계를 유지한다. 지형 선택·공유 로딩·원격 거리/예산/concurrency도 재사용한다. 얕은 물은 바닥 표면보다 우선하고 수영 중 지상 발소리는 차단한다.
- `ABaseEnemy`는 native Foley를 상속하며 현재 기본 발걸음은 `SWAnimNotify_EnemyFootContact`로 실제 이동 애니메이션에서 발생한다. 플레이어의 기존 Notify는 변경하지 않는다. 적의 점프·착지는 별도 이동 경계로 유지한다. `bUseDistanceDrivenSteps`는 접촉 Notify가 없는 별도 캐릭터에 명시적으로 사용하는 보조 경로이며 현재 적 기본값은 false다. 같은 캐릭터에서 두 발걸음 시계를 겹치지 않는다. 거리 보조 경로는 베이스의 스케일을 역적용하지 않아 cm를 유지한다.
- `ASWOceanAudioDirector`: Lvl_CY에 한 개. 복제하지 않으며 각 클라이언트의 listener를 사용한다. 0.1초마다 물/선실 판정을 확인하고 0.5초마다 베이크한 해안선과의 거리를 갱신한다. 바다·해안·수중 루프를 분리한다. 수중 진입/이탈에 15cm 히스테리시스, 믹스 페이드와 루프 페이드를 적용한다. 실제 수영 이동 상태와 오디오의 카메라 잠수 판정은 분리된다.
- 선실 water-cull 안의 카메라는 잠수로 취급하지 않는다. 선실의 바다/해안 음량을 낮춘다. 월드 종료 때 루프, 타이머, 수중 SoundMix를 정리한다. 동일 월드의 중복 디렉터는 루프를 겹치지 않는다. 현재 구성은 하나의 로컬 청취자를 대상으로 하며 분할 화면의 서로 다른 수중 상태 믹싱은 범위 밖이다.

## 음원과 믹스

- 제공된 `cannon_sounds.zip`의 일반 Pirate Ship Cannon 녹음 6종. 이미 방향감이 들어간 Binaural 버전은 3D 위치 음원으로 사용하지 않는다. 준비/취급 소리를 잘라 주 폭음의 시작점을 발사에 맞췄다.
- 제공된 UE 5.8 프로젝트의 Footstep_Essentials, WaterSFX에서 SoundWave 147개를 WAV로 내보내고 UE 5.7에 새로 가져왔다. 5.8 uasset/Cue/BP를 역방향 복사하지 않았다.
- 원본은 보존한다. 위치 기반 one-shot은 mono, 환경 루프는 원래 채널 구성을 유지한다. 준비 스크립트는 시작/끝 무음, 짧은 경계 페이드, 루프 경계 크로스페이드와 제한된 선형 gain을 적용한다. 준비된 파일 peak는 0.89 이하다. 이는 여러 소리를 합친 최종 게임 믹스의 무클리핑 보장을 뜻하지 않는다.
- 프로젝트용 `SC_WorldSFX`, `SC_Foley`, `SC_Environment`를 분리한다. 수중 믹스는 프로젝트 SFX/Foley에만 1,400Hz low-pass와 볼륨 0.6을 적용한다. 기존 UI/BGM이나 모든 마스터 오디오에 필터를 걸지 않는다.
- 발사 concurrency=8, 입수/물폭탄=12, 물 이동=12, StopQuietest. 지상 Foley는 기존 로컬 4/원격 16 예산을 유지한다. 발사 최대 거리 약 453.5m, 입수 약 141.5m, 물 이동 약 22.8m. 청취 볼륨은 프로필과 큐에서 조절한다.
- 새 프로필은 AssetManager AlwaysCook root다. Foley 프로필의 soft 표면 큐는 기존 재귀 cook 규칙을 사용한다. 기존 GASP 음원/애니메이션 파일을 재작성하지 않는다.

## 지형 제작

Physical Surface 슬롯: 1 Sand, 2 Soil, 3 Grass, 4 Water, 5 Wood, 6 Rock, 7 Metal.

Lvl_CY 원래 지형은 `MI_Landscape_02`를 사용하며 Sand, Layer GrassBlend, No Trees 레이어에 물리 재질이 없었다. 단순히 레이어 이름만 지정하면 실제 자동 풀 생성과 표면 소리가 어긋날 수 있다.

오디오용 M/MI 복사본에 LandscapePhysicalMaterialOutput을 추가한다. 기존 Sand paint와 실제 LandscapeGrassOutput의 풀 생성 마스크를 재사용하고 나머지는 Soil로 둔다. 원본의 시각 출력과 원본 재질 자산은 보존한다. No Trees를 무조건 잔디나 흙 레이어로 재해석하지 않는다. 물리 표면 데이터는 렌더링이 가능한 에디터에서 생성해야 한다.

목재/돌/금속으로 명확히 식별되는 맵의 메시 재질은 물리 재질만 다른 MI 복사본을 컴포넌트에 연결한다. 기존 메시의 마찰/질량을 바꾸는 BodyInstance override는 추가하지 않는다. 선박의 simple hull trace가 기본 표면만 반환할 때는 Foley 프로필의 ShipDeckSurface를 사용한다.

해안선은 지형 높이가 기본 수면 높이를 가로지르는 위치를 베이크한다. 지형/해수면/해안 위치를 변경한 경우 다시 베이크한다. 매우 가파르거나 샘플 간격보다 작은 해안 형상은 보간 근사다.

## 제작 도구와 재현

1. UE 5.8 source project에서 `Scripts/Editor/export_audio_sources.py`, `-AudioExportDir=<workspace>/Saved/AudioIntegration/SourceWav`.
2. numpy가 있는 Python으로 `Scripts/prepare_audio_wavs.py --staging Saved/AudioIntegration --cannon-zip <zip>`.
3. UE 5.7 Python commandlet에서 `Scripts/Editor/author_world_audio.py`. `-AllowCommandletAudio`를 사용한다. 새 음원의 SHA-256 metadata로 재가져오기 여부를 판단한다.
4. `Scripts/Editor/apply_world_audio_map.py`: `-AllowCommandletRendering -AllowCommandletAudio`, `-nullrhi` 없이 실행한다. 원본 맵 백업은 `Saved/AudioIntegration/Backups`에 둔다. 원본 맵을 다시 복원하거나 다른 맵을 임의로 저장하지 않는다.
5. `SW.Audio`와 기존 `SW.Foley` 자동화, 저장된 Lvl_CY의 표면 질의, 실제 PIE 출력을 구분해서 확인한다.

원본 프로젝트 없이도 생성된 SoundWave가 게임 프로젝트에 포함된다. 재제작의 로컬 경로는 스크립트 인자로 받고 소스에 개인 경로를 고정하지 않는다. 로컬 상세 manifest, 해시, 로그와 녹음은 Saved에 둔다.

## 진단과 추가 사운드

- `SW.Audio.Status`: 프로필 준비 상태, one-shot 재생 제출, 준비 전 drop 수. 실제 하드웨어의 voice 수를 뜻하지 않는다.
- `SW.Audio.DebugEvents 1`: 이벤트·발생 actor·위치·음원 로그.
- `SW.Audio.Enabled 0/1`: 프로젝트 world one-shot과 환경음. 지상 Foley는 기존 `SW.Foley.Enabled`로 조절한다.
- `SW.Foley.Status`, `SW.Foley.DebugEvents 1`: 실제 지면 trace의 Surface/Sound와 승인/폐기 이유.
- 디렉터 Details의 ListenerUnderwater, NearestShoreDistance, CurrentOceanGain, CurrentShoreGain으로 환경 전환을 확인한다.

추가한 범위: 지형별 점프/착지, 갑판 목재와 돌/금속 접촉, 얕은 물, 입수·출수, 수면/수중 이동, 물폭탄의 젖은 파열음.

적절한 원음이 아직 없는 함선 고체 피격과 선체 삐걱임은 `CannonSolidImpact`, `ShipCreak` 확장 슬롯으로 남긴다. 무기 피격·피해/사망·함선 침몰·수리/상호작용/UI·특수 스킬은 각각의 기존 수명/게임플레이 사건에 맞는 음원이 필요하다. 누수와 갈매기는 아래 보완 단계에서 연결했다. 발사 녹음이나 물소리를 구조 파괴 등의 완성음처럼 재사용하지 않는다. 바람과 추가 날씨별 레이어는 후속 범위다.

## 검증 기록

direct UnrealBuildTool.exe 최종 빌드 성공. SW.Audio 2개와 기존 SW.Foley 10개, 총 12개 자동화가 성공했고 실패/테스트 경고는 0이다. 보고서는 Saved/AudioIntegration/Automation/index.json이다.

Lvl_CY는 저장 후 다시 로드했으며 지형 데이터 11,476개 샘플에서 Grass 10,240 / Soil 341 / Sand 895를 확인했다. 해안선 1,098개와 디렉터 한 개를 저장했다. 렌더가 없는 commandlet의 scene trace는 기본 표면 0을 반환했으므로 실제 지상 이동에서의 재생 성공으로 간주하지 않는다.

실제 PIE와 게임 믹스 녹음은 C: 여유 공간 0으로 중단됐다. GUI 시작 시 Turnkey dotnet의 System.IO.IOException(디스크 공간 부족)을 첫 로그와 Windows Application/.NET Runtime 이벤트에서 확인했다. 이어 ShaderCompileWorker 입력 version이 0으로 읽혀 실패했다. 엔진/빌드 경로를 변경하거나 공유 캐시를 삭제하지 않았다. 공간 확보 후 새 에디터에서 표면 trace, 양측 발사/입수 중복, 수중 페이드, NPC cadence, 실제 비무음 출력을 검증해야 한다. 2인 PIE와 전체 cook/패키징도 완료하지 않았다.

후속 디스크 조사에서 `.git/lfs/tmp`의 701개 임시 파일이 약 144 GiB를 차지함을 확인했다. Codex의 Git 비교가 반복 취소되면서 현재 맵과 같은 헤더의 부분 복사본을 남기는 흐름이었다. 원본 맵 LFS 해시와 인덱스 보존을 확인했다. 임시 junction을 공식 `lfs.storage` 설정으로 대체하고, 저장 위치 분리만으로는 취소 잔류가 해결되지 않아 저장소별 읽기 전용 어댑터를 추가했다. 비교와 경로 기반 해시는 대형 임시 복사본 없이 같은 LFS 포인터를 계산하며 실제 저장·복원은 원본 Git LFS로 전달한다. 원본 대비 통합 시험 15개와 실제 맵/미추적 맵/전체 비교 및 5회 소스 변경의 30초 관찰에서 새 LFS 임시 파일 0개를 확인했다. 앱 자체의 취소 로직을 수정한 것은 아니다. 현재 저장 위치·검증 범위·복구 방법은 `docs/Git_LFS_Disk_Recovery.md`, 상세 증거는 `Saved/DiskDiagnosis/`에 기록한다. 공간 복구가 실제 PIE 검증 완료를 뜻하지는 않는다.

이번 작업의 중복 WAV staging과 Python bytecode만 정리했다. 추가 맵 백업은 Git LFS 원본과 두 SHA-256이 모두 0369955d16afdd7c80d815c0ad77ad0d3aaaf5a3ab9cc17a5b51c39b9f1148ba로 일치함을 확인한 뒤 중복 복사본만 제거했다. 원본 객체는 `git lfs env`의 LocalMediaDir 아래 `03/69/`에 보존된다. 외부 LFS 저장소를 삭제 가능한 캐시로 취급하지 않는다. 원본 위치 기록과 제작 manifest는 Saved/AudioIntegration에 남긴다. 소스 프로젝트의 첫 WAV export는 147개 생성에 성공했지만 기존 GameFeatureData 설정 오류 때문에 전체 commandlet exit 1이었다. 게임 프로젝트의 오디오 작성은 `-AllowCommandletAudio` 환경에서 exit 0으로 재검증했다.

## PIE 재개 검증 — 2026-10-27 23:25 KST

- Lvl_CY, UE 5.7.4, Standalone(NetMode=0)에서 실제 캐릭터 이동과 포탄 발사를 실행하고 AudioMixer master 출력을 WAV로 저장했다. 로그 제출 수와 출력 신호를 구분했다. 주관적 청취·최종 믹싱 품질을 판정한 결과는 아니다.
- 발견·수정: 적이 7초간 140cm/s로 접지 이동해도 발소리가 없었다. Landscape_0 스케일은 (150,150,140)이었고, 이동 베이스의 `InverseTransformPosition`이 실제 이동 거리를 150분의 1로 줄였다. `InverseTransformPositionNoScale`로 바꿔 cm를 유지한다. 같은 실제 적·지형·이동 시나리오를 두 번 반복한 결과 각 8회, 총 16회의 좌우 교대 Grass Walk 사건과 비무음 출력을 확인했다. 이동 종료 뒤 추가 접촉 사건은 없었다.
- 플레이어의 실제 풀·모래 이동에서 Surface 3/1과 해당 Run Cue를 확인했다. 사전 에디터 지형 trace의 Surface 0만으로 런타임 실패를 단정하지 않는다.
- 플레이어 함선과 적 함선에서 각각 실제 포탄 1개, 발사음 1회, 해당 포탄의 바다 접촉음 1회를 확인했다. 즉시 재발사 시도는 false, 추가 발사음은 없었다. 수정 전후 두 세션에서 반복했다. 적 함선은 생산용 `BP_ES_Normal_MRR`를 PIE 월드에 임시로 생성한 개체이며, 미션의 함선 활성화·AI 목표 획득을 검증한 것은 아니다. 맵/에셋 저장은 하지 않았다.
- 캐릭터가 바다로 낙하할 때 WaterEnter와 수영 음원 사건, 수중 카메라에서 ListenerUnderwater=true 및 Ocean/Shore gain=0, 수면 위 복귀 뒤 false 및 Ocean gain=1을 확인했다. 출력 녹음도 저장했다.
- 최종 직접 UBT 빌드 성공. 회귀 검사의 120cm 이동은 0.1초 표본당 20cm씩 6회로 구성해 걷는 속도 조건을 지켰다. 스케일 바닥에서 접촉 1회, 바닥과 함께 이동할 때 추가 접촉 0회를 검사한다. 최종 SW.Audio+SW.Foley **12/12 성공, 경고 0, 실패 0**. `Saved/AudioIntegration/AutomationFinal/index.json`.
- 증거: `Saved/AudioIntegration/PIEValidation_20261027.json`, `Live/15952/{enemy_grass,enemy_grass_repeat,player_cannon,enemy_cannon,water_transitions}.wav`, `Saved/Logs/AudioPIEValidation_Fixed.log`. 이전 세션 기록은 `Live/24772`에 보존한다. WAV exporter의 길이를 전체 게임 시간과 동일하다고 가정하지 않는다.
- 남은 범위: **2인 네트워크 PIE**, 지연/패킷 손실/늦은 참가, 패키징, 전체 적·함선 종류, 흙·얕은 물·모든 갑판의 실제 이동을 전수 검증하지 않았다. 이 기록은 해당 범위까지 완료됐다는 뜻이 아니다.

## 적 접촉·강우·움직이는 갑판 보완 — 2026-10-28

플레이어의 기존 애니메이션/접촉 Notify는 변경하지 않았다. 적의 이동 거리 근사를 기본 경로에서 제거하고, 현재 적 31종이 참조하는 두 AnimBP의 Walk/Jog 16개 클립에 적 전용 좌우 접촉 86개를 작성했다. `SWAnimNotify_EnemyFootContact`는 BaseEnemy의 본체 메시에서만 받고 지상 이동 중에만 기존 Foley로 전달한다. 매 발걸음 RPC와 컴포넌트 Tick은 추가하지 않았다. 점프·착지의 기존 이동 경계는 `bPollMovementForJumpAndLand`로 분리했다.

발 높이를 180개 시점에서 평가하고 swing→plant 진입을 히스테리시스로 찾았다. 루프 경계를 포함한 두 주기를 검사해 좌우가 번갈아 나오는지 검증한다. 기존 포즈·커브·다른 Notify는 보존하며 전용 서버와 sync follower에서는 새 접촉을 실행하지 않는다. 각 blend space는 HighestWeightedAnimation을 유지한다. 전사 BS의 원래 idle=0/jog=500 구조는 140cm/s에서 idle이 우세해 접촉을 누락했으므로 실제 Walk=150 레이어를 추가했다. 매니퀸 BS의 기존 Walk=300도 적의 통상 이동에 맞춰 150으로 조정했다. 플레이어 GASP 선택 경로는 수정하지 않았다. 개별 전환과 IK 이후의 모든 픽셀 접촉까지 일치한다고 주장하지 않는다.

갑판의 DeckMesh_Simple/Complex가 Visibility를 무시하고 Pawn을 막는 실제 설정을 확인했다. Foley의 Visibility trace가 기본 표면/실패인 경우, 접지한 CharacterMovement의 movement base가 AShip이면 ShipDeckSurface를 사용한다. 선박 충돌 설정은 변경하지 않고 별도 trace도 추가하지 않는다. 다른 물리 표면을 이미 찾았다면 그 결과를 보존한다. 공중 발소리는 기존 승인 단계에서 차단한다.

빗소리 원음은 SC 없이 master로 재생되고 있었다. 기존 WeatherEvent(RAINY)가 생성한 실제 AudioComponent에서 원래 유효 음량·음소거·조정 음량을 비교했다. 통제 비교 동안 AI/날씨 시계는 PIE 안에서만 멈췄다. 원래/음소거/조정 구간의 적 접촉은 17/16/16회로, 비가 있어도 재생 제출이 발생했다. Master 녹음 RMS는 약 0.03777 / 0.00324 / 0.00739다. 무음 구간이 WAV에서 생략될 수 있어 이 값을 시간 정렬된 SNR로 해석하지 않는다. 조정 전후 혼합 출력 RMS 차이는 약 14.2dB이며 주관적 청취 평가는 아니다.

`SC_Weather`를 만들고 기존 Rain_Heavy_Ext01 wave/cue를 SM_Final로 라우팅했다. 기본 Weather class volume은 0.18, rain voice priority는 0.3이다. Foley 음량이나 플레이어 Notify를 일괄 증폭하지 않았다. Underwater mix에 Weather만 별도로 volume 0.2/LPF 800Hz를 적용해 수중 환경 루프 자체는 이 조정의 영향을 받지 않게 했다.

### 실제 PIE 결과

- 전사형 적: 저속 Walk 접촉과 강우 A/B, 고속 Run 접촉 확인. 매니퀸형 적: 전진 15회, 후진 6회, 횡이동 Run 16회. 후진은 전 구간의 일정한 보행을 보장하는 테스트가 아니므로 전진과 같은 횟수를 요구하지 않는다.
- 실제 BP_PlayerShip_Kelvin을 물리 속도로 약 50.06m 이동시켰다. 적은 정지/보행 구간 모두 선박을 movement base로 유지했다. 배가 움직이고 적은 정지한 5초 구간에서 발걸음 0회/상대 속도 0, 이후 보행에서 좌우 Wood 접촉 10회와 비무음 WAV를 확인했다.
- 플레이어는 기존 BP_Player_Woman/애니메이션/Enhanced Input으로 강우 중 이동했다. 기존 접촉 18회와 master 출력 WAV를 확인했다. 플레이어의 정지 전환 애니메이션 접촉은 임의로 제거하지 않았다.
- 최종 직접 UBT 성공. 독립 자동화 세션에서 **13/13 성공, 실패/경고 0**. `Saved/AudioIntegration/AutomationContactWeatherFinal/index.json`.
- 증거: `Saved/AudioIntegration/ContactWeatherValidation_20261028.json`, `Live/17840/rain_{before,off,after}.wav`, 같은 폴더의 manny_* 녹음/프레임 자료, `Live/5544/moving_deck{.wav,_summary.json}`, `player_rain_preserved.wav`. 로그는 `EnemyWeatherBoat_Final.log`, `EnemyWeatherDeck_Verified.log`, `AudioContactWeather_Regression.log`.

### 재제작과 복구

- `Scripts/Editor/author_enemy_foot_contacts.py`: PIE 밖에서 실행. 클립과 blend space 원본은 `Saved/AudioIntegration/EnemyContacts/Backups`, 접촉/기존 메타데이터/해시는 `EnemyContacts/authored.json`에 보존한다. 이 백업은 Git에 포함되지 않으므로 별도 보관한다. 원본 복원 시 에디터를 정상 종료하고 이 작업의 대상 파일만 대응되는 백업으로 복원한다.
- `Scripts/Editor/configure_weather_audio.py`: 기존 Rain wave/cue와 수중 mix의 백업은 `WeatherMix/Backups`. `author_world_audio.py`의 재제작 마지막에도 이 설정을 다시 적용한다. 배포 전 새 클립/음향 의존성을 포함해 cook해야 한다.
- 맵과 플레이어 BP/GASP 애니메이션은 이번 단계에서 저장하지 않았다. 임시 AI 정지, 강우 강제, 카메라/캐릭터 이동, 배 속도 설정은 PIE 종료로 폐기했다.

### 점검에서 확인한 남은 항목

| 항목 | 현재 상태 / 다음 연결 경계 |
| --- | --- |
| 선박 항주·선수 물살 | 후속 구현은 아래 ‘함선 이동·입수음 보강’ 참고. 실제 수류 벡터를 빼는 모델은 아니며, 수평 이동과 선수 수면 변화에 반응한다. |
| 선체 삐걱임·돛·밧줄 | ShipCreak는 확장 슬롯만 있다. 자세 변화/파도 하중·돛 상태와 적절한 원음을 연결해야 한다. 발소리의 creaky-wood 녹음을 선체음처럼 사용하지 않았다. |
| 선박 피격·침몰·누수·수리 | 고체 피격 슬롯은 무음이고 피격·침몰·수리 원음은 추가 필요하다. 누수는 아래 ‘보유 에셋으로 보완’ 단계에서 연결·2인 검증했다. |
| 수면 정지 중 동작음 | 아래 보완 단계에서 파도의 수직 운반과 실제 수영 이동을 구분하고 정지/전진을 2인 검증했다. |
| 날씨/선실 경계 | 수중 Weather class 처리는 작성했으나 강우 세기별 최종 믹스, 선실/실내 지붕 차폐, 반복 날씨 전환에서의 컴포넌트 재사용은 전수 검증하지 않았다. |
| 멀티플레이·패키징 | 이번 실제 출력은 Standalone이다. 2인 네트워크 PIE, 늦은 참가/패킷 손실/풀 재활성화, 전 종류의 시각적 접촉과 패키지 실행은 미검증이다. |

## 함선 이동·입수음 보강 — 2026-10-28

`USWShipAudioSubsystem`은 플레이어/적 `AShip`을 공통 처리하는 월드 서비스다. 0.25초 타이머에서 수평 위치 변화로 속도를 구하므로 클라이언트의 복제 이동에도 반응한다. 숨긴 풀 개체, 침몰 중인 배, 런타임 물리가 꺼진 배, 80m 밖 배는 제외한다. 수직 흔들림만으로 항주하지 않으며 순간이동/오래된 표본은 버린다. 주변 관찰은 64척, 수면 조회는 갱신당 8회, 동시 루프는 4개로 제한한다. 컴포넌트는 재사용하고 월드 종료 때 해제한다. 발걸음/항주음용 새 RPC나 함선별 오디오 Tick은 추가하지 않는다.

`Cue_Ship_WaveMovement`는 소유한 Boat1 녹음 25%와 Ocean_Wave7 75%를 합친 공간 루프다. 속도에 따라 크기·피치가 변하고 시작/종료는 페이드한다. 파도가 선수 쪽으로 상승하는 상대 속도와 수평 이동 조건을 만족하면 `ShipWaveImpact`를 재생한다. Wave_Crash 1/3/4를 무작위로 쓰고 척당 최소 4~6초 간격을 둔다. 충돌음은 루프와 별도 4보이스 concurrency여서 서로 루프를 빼앗지 않는다. 이는 물리 충격량을 계산하는 모델이나 네트워크로 동일 시각을 보장하는 사건은 아니다.

캐릭터/포탄은 서로 다른 Heavy Cue를 사용한다. 캐릭터는 Heavy swimming/Splash의 철썩이는 어택에 실제 Diving_Underwater의 물소리를 55ms 뒤 겹친다. 포탄은 Splash 4/5 어택과 더 낮은 피치의 Splash 6/수중 진입 몸통을 35ms 뒤 겹친다. 합성 저음/폭발음을 넣지 않는다. 기존 프로필 배율 WaterEnter 0.55, CannonWaterImpact 0.9와 전신 입수 호출 배율 0.8은 유지한다. 얕은 물 진입은 별도 WaterWadeEnter로 나눠 0.6초 공통 쿨다운이 곧이어 오는 전신 입수음을 막지 않게 한다. 큰 입수음은 재생 후 해제하고 물 밖에서 0.5초 머물거나 얕은 곳에 접지해야 다시 준비한다. 파도로 수영/얕은 상태가 오갈 때 반복하는 현상을 단순 긴 쿨다운 대신 이 재진입 경계로 막는다. 플레이어의 애니메이션 Notify는 그대로다.

제작 데이터는 161개 Wave/13개 World event다. UE 5.8 소스에서 후보 14개와 추가 후보 9개만 WAV로 내보냈고 매번 원본 uasset 해시를 전후 대조했다. 소스 commandlet은 기존 GameFeatureData 오류로 exit 1이었으므로 전체 실행 성공으로 표기하지 않는다. 출력 manifest의 성공과 원본 보존을 별도 확인했다. 게임에는 필요한 8개 새 Wave만 가져왔다. `prepare_ship_water_audio.py`가 정리·모노화·레이어를 만들고 `author_ship_water_audio.py`가 강제 저장한다. 프로필 원본은 `Saved/AudioIntegration/ShipWater/Backups`에 있다. 이 백업은 Git에 자동 보관되지 않는다. 에디터를 종료하고 대상 백업만 복원할 수 있다.

### 재현 가능한 PIE 검사

`Scripts/Editor/install_audio_pie_checks.py`로 `AudioPIE/*.py`를 LiveScripts에 설치한다. `probe_world_audio.py`의 `run_script` 명령을 한 번에 하나씩 사용한다. 순서는 configure_network → begin → network_prepare → enable_both_devices → network_quiet → network_motion → network_cannons → network_entries → end → restore_network_settings이다. 완료 JSON/로그를 확인한 뒤 다음 단계로 넘어간다. 임시 생산용 적 함선 생성, AI/환경음 정지, 물리 속도 고정, 카메라/캐릭터 이동은 PIE에만 적용하며 맵을 저장하지 않는다.

- 호스트는 NetMode 2, 클라이언트는 3이다. `DescribeAudioWorld`에서 서로 다른 장치 ID를 확인한다. 단일 프로세스여도 실제 listen server/client 복제를 이용한다.
- 실제 게임 창을 활성화한다. `au.Debug.PlayAllPIEAudio`는 장치 선택 무음만 해제하며 앱 전체의 백그라운드 무음까지 해제하지 않는다. 원래 토글 상태로 돌린다.
- WAV export 내부 writer가 공유되므로 두 장치의 StopRecordingOutput은 0.6초 간격으로 수행한다. 따라서 두 WAV 길이를 동일한 게임 타임라인으로 가정하지 않는다. 무음 submix가 정지되면 WAV에 무음 시간이 생략되거나 No audio data가 반환될 수 있다.
- 발사 검사는 Slate 편집기 callback에서 직접 RPC를 발생시키지 않고 `QueueAudioCannonShot`으로 실제 게임 월드 타이머에 예약한다. 결과 로그에 실제 수락/즉시 재발사 거부를 남긴다.
- 대포 시험은 서로 반대쪽 바다를 향하는 대포를 고른다. 가까운 다른 함선을 맞힌 결과를 바다 입수 시험으로 세지 않는다. Slate 직접 호출에서 양쪽 발사음 누락이 나타났지만 게임 월드 타이머의 동일한 기존 대포 RPC 경로에서는 양쪽 발사/입수 사건을 확인했다. 추정했던 함선 채널 중계 변경은 적용하지 않았다.
- Begin PIE 자체가 재생 설정을 저장하므로 RestoreAudioPIE는 CDO와 프로젝트 사용자 ini를 함께 복구한다. 비정상 종료 뒤에는 `restore_audio_pie_settings.py -AudioSettingsSnapshot=<원래 JSON>`로 복구할 수 있다. 원래 JSON을 다음 검사로 덮어쓰지 않는다.

자동화 후 PIE 진입에서 테스트용 WoodDeckContact가 임시 월드를 잡는 Fatal World Leaks를 확인했다. 테스트 사운드의 Outer를 임시 월드에서 분리해 수명을 수정했다. 임시 에셋 저장 누락은 `only_if_is_dirty=False`로 수정하고 재시작 후 실제 프로필을 확인했다. 정리/감시로 해결했다고 표현하지 않는다.

최종 실제 출력/사건별 결과는 후속 검증 기록에 남긴다. 늦은 참가, 인위적 지연/손실, 전체 적/함선 종류, 실제 수류, 실내 차폐와 패키지 실행은 별도 범위다.

### 2인 PIE 실제 출력 결과

`ShipWaterNativeVerified.log`, `Live/17924`에서 listen server/client와 별도 AudioDevice 2/3을 확인했다. 플레이어 함선 바깥쪽 대포와 적 함선 반대쪽 대포를 실제 게임 타이머에서 발사했다. 각각 첫 발 수락/즉시 재발사 거부, 양쪽 발사 사건 각 1회, 동일 포탄의 Heavy 바다 입수 사건 각 1회를 확인했다. 네 개의 master WAV는 비무음이고 클리핑 0이다. 이 과정은 미션의 적 함선 활성화/AI 조준 성공을 검증하지 않는다.

함선 물리 이동은 정지 → 플레이어만 → 적만 → 둘 다 → 정지 → 200m 이탈 → 재접근 순서로 실행했다. 두 장치의 마지막 활성 루프 수는 각각 0/1/1/2/0/0/2였다. 양쪽 함선의 선수 파도 사건도 호스트/클라이언트 모두에서 발생했고, 이동/재접근 WAV에 비무음 출력 및 클리핑 0을 확인했다. 정지 직후는 페이드/기존 파도 여운을 허용한다. 수평 속도를 주입하면서 수직/회전 속도를 제어한 임시 물리 시험이며, 조타 입력/수류 모델 전수 시험은 아니다.

두 플레이어 각각의 실제 낙하에서 양쪽 World의 Heavy WaterEnter와 네 개의 비무음 WAV를 확인했다. 첫 검사에서 수영/얕은 상태 변동 중 두 번째 입수가 나타나 위의 재진입 준비 조건을 보완했다. 해당 보완의 재검사 결과는 최종 로그와 검증 JSON에 따로 연결한다.

재진입 보완 뒤 `WaterEntryGuardFinal.log`/`Live/11180/EntriesVerified`에서 Woman/Man 각각 NetMode 2/3에 전신 입수 1회씩, 총 4회를 확인했다. 네 개의 WAV 모두 비무음/클리핑 0이고 기록한 부유 구간의 추가 전신 입수는 0회다. 이 결과는 플레이어 두 명의 해당 낙하/부유 시험이며 모든 재입수·파도 조건이나 SwimmingComponent가 없는 적 NPC 전신 입수까지 검증했다는 뜻은 아니다. 보완 뒤 직접 UBT 성공 및 자동화 14/14 성공을 다시 확인했다. PIE 종료 후 원래 Standalone/1인/640x480/개별 오디오 장치 비활성 설정을 프로젝트 사용자 ini까지 복구했다.

집계는 `Scripts/summarize_ship_water_validation.py`, 증거는 `Saved/AudioIntegration/ShipWaterValidation_20261028.json`, 녹음은 `Live/17924/{MotionVerified,CannonsVerified,EntriesVerified}`이다. 최종 자동화는 **14/14 성공**이며 해당 테스트 실패/경고는 0이다. 빌드의 기존 RoomProgress shadowing/Chaos 경고, PIE의 기존 적 내비게이션/물리 프레임 경고와 구분한다. 로그와 WAV는 제출/출력 확인이며 주관적 음색 청취 합격 판정은 아니다.

## 보유 에셋으로 보완하는 범위와 추가 원음 목록 — 2026-10-28

Asset Registry에서 프로젝트 SoundWave 447개, SoundCue 64개, MetaSoundSource 15개와 WeaponFeedbackDataAsset 3개를 조사했다. 대부분 기존 Foley/Water와 세 가지 Whoosh 및 Rain이다. 무기 피드백 DA 세 개의 Default/Combo1/Combo2에는 이미 Whoosh 01/08/11이 연결돼 있다. 따라서 무기 휘두르기 전체가 무음이라고 분류하지 않는다. 조사 목록은 `Saved/AudioIntegration/CoverageAudit/{sound_inventory,weapon_feedback}.json`이다. Blueprint 전체의 모든 분기를 전수 실행한 결과는 아니며 아래 목록은 확인한 전용 원음/연결 경계에 근거한다.

### 이번 보완 구조

- 살아 있는 적 NPC가 SwimmingComponent 없이 바다에 떨어지는 경우도 기존 Heavy WaterEnter를 사용한다. 기존 Foley 타이머에서 가까운 공중 NPC만 최대 0.2초마다 조회하고, 전체 월드의 프레임당 수면 조회 예산은 8회다. 플레이어 Notify와 이동/사망 로직은 바꾸지 않는다. 사망 래그돌/시체의 물 충돌은 별도 경계다.
- 수영 stroke는 수평 이동 또는 실제 수직 잠수/부상 상태의 이동 거리로 계산한다. 수면에서 파도가 수직으로 운반하는 이동만으로 stroke를 만들지 않으며, 비수영 NPC에게 stroke를 만들지 않는다.
- 기존 Running_Water2를 모노 루프로 준비해 복제된 ShipRepairPoint의 IsLeakActive에 연결한다. 플레이어/적 함선 공통, 25m 이내 가까운 세 곳, 재사용 AudioComponent 세 개가 최대다. 정지한 배에서도 누수는 들리고, 수리/비활성/풀 숨김/거리 이탈 후 페이드한다. 누수 자체의 새 사운드 RPC나 누수 지점별 Tick은 없다.
- 기존 Seagull 1/2는 한 번에 한 목소리만 쓰며 첫 12~22초, 이후 35~65초의 무작위 간격이다. 해안 50m 안, 수면 위/선실 밖, 현재 표시 중인 날씨의 낮(6~19시) Sunny/Cloudy에서만 재생한다. 날씨 시계/복제/머티리얼을 수정하는 기능이 아니다.
- 선실 판정에 기존 Cabin Water Cull의 실제 점유 데이터를 재사용한다. Cabin mix는 Weather class만 volume 0.2/LPF 1800Hz로 페이드하며 발소리·전투 소리는 이 mix로 줄이지 않는다. 바다/해안 루프의 기존 선실 감쇠와 수중 mix를 유지한다.

### 추가로 준비하면 좋은 에셋

권장 수량은 변주용 원음 수다. 같은 녹음의 단순 피치 변경을 서로 다른 원음으로 세지 않는다. 현재 없는 적절한 원음을 발소리나 발사음으로 대체하지 않는다.

| 우선 | 필요한 원음 | 권장 구성 / 연결할 게임 상황 |
| --- | --- | --- |
| P0 | 포탄의 목재 선체 피격 | 가벼운/강한 충격 각 3~5개, 목재 파편/쪼개짐 여운 3개. 기존 CannonSolidImpact 슬롯과 실제 고체 충돌 결과에 연결. 돌/금속 피격도 각 3개 권장. |
| P0 | 함선 충돌·충각·좌초 | 나무 선체가 눌리고 긁히는 소리 3개, 큰 충돌/파열 3~5개. 실제 서버 충돌 확정 사건과 강도별 연결. 파도 충돌음과 구분. |
| P0 | 침몰·파손 단계 | 큰 목재 파단 3개, 선체 붕괴 2~3개, 거품/잔해/마지막 수몰 꼬리 2~3개. 기존 물 녹음 일부는 물 레이어로 재사용 가능하지만 구조 파괴 원음은 필요. |
| P0 | 근접·투사체의 피격/방어 | 살·천·갑옷 피격 각각 3~5개, 칼 막기/튕김 3개, 화살 박힘/석궁 또는 활 발사·비행 원음. 실제 피해/방어/재질 사건에서 선택. Whoosh와 별개다. |
| P1 | 선체·돛·밧줄의 움직임 | 긴 선체 굽힘/삐걱임 4~6개, 돛 펄럭임 약/강 루프 각 1개, 밧줄 당김·도르래·장력 풀림 각 3개. ShipCreak 슬롯/부하·자세·돛 상태별 연결. CREAKY_WOOD 발소리는 발 디딤 원음이라 긴 선체 소리의 완성 대체가 아니다. |
| P1 | 수리 동작과 완료 | 목재 망치질 5개, 판자/못/도구 취급 각 3개, 짧은 완료/취소 피드백 각 1~2개. 실제 수리 시작/유지/소모 성공/중단에 연결. 이번 누수 루프 종료가 수리 동작음 전체를 대신하지 않는다. |
| P1 | 대포 조작·재장전 준비 | 포탄/장약 취급, 밀어 넣기, 기구 조작 각 3개와 준비 완료 1~2개. 실제 구현된 조작/쿨다운 경계에 맞추며 없는 장전 애니메이션 단계를 임의로 재생하지 않는다. |
| P1 | 피해·사망·위험 인지 | 인물별 짧은 통증/힘씀 4~6개, 사망 2~3개, 물 밖 호흡/깊은 잠수 기포/익수 위험 피드백. 필요한 캐릭터 성별·종류별 확보. 시체 충돌/물 충돌도 별도 게임플레이 연결 필요. |
| P1 | 상자·아이템·상호작용/UI | 상자 열기/닫기 각 3개, 아이템 줍기/드롭/장착, 금전·거래 성공/실패, 클릭/포커스/닫기/사용 불가·스킬 준비 완료 각 1~3개. 물리 상호작용은 3D, 본인 UI는 로컬 2D로 구분. |
| P2 | 바람·폭풍의 독립 레이어 | 약/강 해풍 루프, 가까운/먼 천둥 각 3~5개, 실내 빗방울/젖은 돛 세부 루프. 바다+천둥 합본 루프만으로 번개와 정확히 맞는 단발을 만들기는 어렵다. |
| P2 | 음악과 상황 전환 | 평온 항해/섬 탐험/일반 전투/보스 전투의 루프, 승리·실패·위험 전환 짧은 스팅어. 전체 게임 음악 흐름과 UI/대사 우선순위를 별도로 설계. |

새 원음은 가능하면 무가공 WAV 48kHz/24-bit로 보관한다. 위치 효과음은 mono, 넓은 환경·음악은 stereo가 적합하며 루프는 끊김 없는 경계를 제공한다. 효과음은 과한 리버브·배경 음악·불필요한 무음을 포함하지 않는 분리 녹음이 유용하다. 프로젝트의 기존 16-bit 준비 파이프라인과 원본 보관을 구분하며 기존 에셋을 일괄 재인코딩하지 않는다.

종료 준비에서 오디오 수정과 무관한 미저장 머티리얼 4개를 발견했다. 원본 파일을 덮어쓰지 않고 엔진의 AUTOSAVING/KEEPDIRTY 저장 경로로 `CoverageAudit/UnsavedMaterialCheckpoint`에 패키지와 manifest를 남겼다. 확인 가능한 /Game 원본 세 파일의 전후 SHA-256도 일치한다. 필요할 때 에디터를 종료하고 manifest의 해당 패키지만 복구한다. 이 보관본은 Git에 자동 포함되지 않는다.

### 보완 단계의 실제 검증과 종료

직접 UnrealBuildTool 빌드를 완료했고 `AudioCoverageFinal.log`의 최종 SW.Audio/SW.Foley 자동화는 **15/15 성공**이다. 작성 전 검사 오류가 같은 로그 앞부분에 남아 있으므로 전체 로그의 모든 오류가 0이라고 해석하지 않는다. 에디터를 다시 시작한 `AudioCoverageRecheck.log`에서 디스크의 World Wave 162개, 프로필 15개 사건 및 Cabin mix를 재검증했다. 이번에 새로 가져온 것은 기존 수출 WAV의 Running_Water2 하나이며, 기존 무기 피드백/플레이어 Notify는 유지한다.

`Live/21548/CoverageVerified`의 실제 listen server/client 및 별도 AudioDevice에서 다음을 확인했다.

- 수면 정지 구간의 stroke는 양쪽 0회, 실제 전진 수영은 양쪽 11회다. 실제 수직 잠수 입력은 이번 구간에서 실행하지 않았다.
- 플레이어 함선/적 함선 누수 각 1곳이 양쪽에 출력된다. 여섯 누수 지점을 동시에 켜도 보이스는 양쪽 최대 3개다. 비활성→0, 멀어짐→0, 재접근→1, 풀 숨김→0, 다시 표시→1, 최종 비활성→0을 확인했다. 활성 구간 WAV는 비무음/클리핑 0이다. 실제 수리 입력 완료 흐름 전체를 실행한 검사는 아니다.
- 낮의 해안 갈매기 사건은 양쪽 1회씩이다. 밤은 허용 판정이 양쪽 false였고 관찰한 12초 구간에 재생 0회였다. 35~65초 주기 전체나 모든 날씨 전환의 전수 검사는 아니다.

초기 NPC 검사에서는 서버와 클라이언트의 actor 이름이 달랐고, 공중 정지 대기 중 클라이언트의 예측 낙하가 먼저 발생했다. 시험이 서버 actor 이름만 허용했던 설정을 고치고, 네이티브 PIE 월드 타이머로 생성/낙하를 예약했다. 복제 준비 중 예측 낙하가 수면에 닿지 않는 높은 시작점을 사용한 최종 검사 `Live/7708/CoverageRecheckFinal`에서는 살아 있는 적 NPC의 Heavy WaterEnter가 **NetMode 2/3에 각 1회**였다. 최초 격리 녹음은 `CoverageRecheck`에 보존한다. 최종 NPC master에는 비도 함께 포함되므로 master 에너지만으로 NPC 단독 출력이라고 분류하지 않는다.

선실의 초기 넓은 격자 표본은 얇은 실제 점유 마스크를 놓쳤고, 경계 voxel에 붙인 카메라는 배의 물리 이동과 표본 시차로 판정이 흔들렸다. 최종 mix 검사는 실제 점유 voxel 좌표와 고정된 함선 transform에서 수행했다. 밖→안→밖의 안정 구간 판정은 양쪽 false→true→false였고, 선실의 강우 master RMS는 바깥 대비 **0.266/0.253**, 밖으로 나오면 원래 수준으로 복구됐다. 최종 8개 WAV 모두 비무음/클리핑 0이다. 이 격리 검사는 움직이는 배의 모든 선실 경계에서의 안정성 보증은 아니다.

집계 재현은 `Scripts/summarize_audio_coverage.py --session Saved/AudioIntegration/Live/21548 --log Saved/Logs/AudioCoverageFinal.log --recheck-session Saved/AudioIntegration/Live/7708 --recheck-log Saved/Logs/AudioCoverageRecheck.log --output Saved/AudioIntegration/AudioCoverageValidation_20261028.json`이다. WAV 지표는 먼저 `Scripts/analyze_ship_audio.py`로 각 결과 폴더에 작성한다. Python 분석기는 numpy가 있는 런타임을 사용한다. `network_coverage_recheck.py`는 전체 흐름 중 적 입수/선실만 따로 재현하는 보조 검사이며, main probe와 함께 쓰고 각각의 완료 JSON을 확인한다.

PIE를 종료하고 원래 Standalone/1인/640×480/개별 오디오 장치 비활성 및 성능 설정을 복구했다. 최종 세션의 미저장 머티리얼 4개도 `CoverageAudit/UnsavedMaterialCheckpoint_7708`에 별도로 보존했고 미저장 맵은 없었다. 사용자 요청에 따라 에디터를 정상 종료했으며 로그의 `LogExit: Exiting` 및 프로세스 부재를 확인했다. 늦은 참가/패킷 손실, 시체 물 충돌, 전체 적 종류와 패키지 실행은 별도 검증이 남는다. 출력/사건 검증과 주관적인 음색 청취 합격을 구분한다.
