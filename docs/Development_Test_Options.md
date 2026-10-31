# 개발 테스트 옵션 사용법

## 기본 사용

Development 또는 Editor의 정상적으로 열린 방에서 호스트 콘솔로 실행한다. 서버/클라이언트 준비가 끝난 후 사용하며 Shipping/Test 빌드에서는 비활성이다.

방 생성·이어하기·접속은 기존 방 UI를 사용한다. `SW.DevTest.Local.Host/Join`은 주석 처리되어 등록되지 않는다. Editor/패키징 서버 선택은 기존 `sw.Room.ServerBackend` 설정을 사용한다.

```text
SW.DevTest.Session 1
SW.DevTest.Status
```

세션을 처음 켜면 강화 무료, 제작 무료, 모든 스킬 임시 사용, 스킬 쿨타임 무시 옵션이 켜진다. C 카메라 전환도 허용된다. 기존 사망/침몰 테스트 단축키는 별도의 `SW.DevTest.Input 1`로 켠다.

개별 옵션은 호스트가 조절한다. 마지막에 0을 붙이면 해당 옵션만 끈다.

```text
SW.DevTest.ShipUpgradeFree 1
SW.DevTest.CraftingFree 1
SW.DevTest.SkillFree 1
SW.DevTest.SkillNoCooldown 1
SW.DevTest.Session 0
```

- 강화: 재료 검사와 소비를 생략한다. 선행 강화 노드 조건은 유지한다.
- 제작: 레시피 아이템과 재료 검사/소비를 생략한다. 제작대 거리, 유효한 레시피, 결과 수량/수용 조건은 유지한다. 영구 스킬 제작의 캠페인 해금 조건은 유지한다.
- 스킬: 아직 해금하지 않은 등록된 플레이어 스킬도 임시 사용하며 재료를 소비하지 않는다. 정상 스킬 해금 기록을 변경하지 않으므로 테스트를 끄면 원래 잠금 상태로 돌아온다.
- 쿨타임: 플레이어 스킬의 GAS 쿨타임 검사/적용과 물폭탄 발사 대기를 우회한다. 일반 대포와 적 스킬에는 적용하지 않는다. 시전 중 재진입 제한과 조준/승선 등 사용 조건은 유지한다.
- 카메라: 조종 중 C 전환은 테스트 세션에서만 가능하다. 세션 종료 시 기본 시점으로 복귀한다.

## 배 추진력 배수

```text
SW.DevTest.ShipSpeedMultiplier 2
SW.DevTest.ShipSpeedMultiplier 1
```

플레이어 배의 전진·후진 추진력에 배수를 곱한다. 기본값 `1`은 원래 추진력, `2`는 추진력 2배, `0`은 추진력 없음이며 음수는 0으로 처리한다. 실제 속력은 저항 등에 따라 달라진다. 적 배와 회전력에는 적용하지 않는다.

이 명령은 `Session`과 독립적이다. 개발 중에는 호스트·게스트·서버 중 한 곳에서 입력하면 서버가 현재 월드의 배수를 바꾸고 모든 클라이언트에 복제한다. 서버 물리와 클라이언트 예측은 이 공유 값을 기존 추진력 계산에 곱한다. 인자 없이 입력하면 현재 복제된 값을 확인한다. 나중에 접속한 클라이언트도 같은 값을 받으며 새 월드에서는 기본값 `1`로 돌아간다. Shipping/Test 빌드에서는 등록·적용하지 않는다.

## E 스킬·C 구르기 입력 진단

Development/Editor 빌드에서는 기본값이 `0`으로 꺼져 있다. 조사할 때 `SW.Debug.CombatInput 1`로 켜고 `SW.Debug.CombatInput 0`으로 끈다. 서버와 클라이언트 각각의 프로세스에 적용된다. 진단용이며 입력이나 스킬 판정은 변경하지 않는다.

문제가 생기면 E와 C를 각각 눌렀다 떼고 클라이언트·서버 로그에서 `[SWCombatInput]`을 검색한다. 정상 동작 때의 기록과 비교한다.

- `RawKey`: 키 수신, 현재 조종 Pawn, 생명주기·UI 차단 상태. 키가 UI/콘솔/포커스 단계에서 소비되면 컨트롤러의 `RawKey`도 남지 않을 수 있다.
- `RawKeySnapshot`, `ActionPressed`, `VortexPressed`: 생명주기·전투 허용 여부, 수영·구르기·피격·장비 전환 상태, 기본·퀵슬롯 IMC 적용 여부, ASC의 소유 태그, 차단 어빌리티 태그와 활성 어빌리티. `VortexCancelExisting`은 기존 스킬 태그 때문에 활성화 대신 취소를 요청한 경우다.
- `RoutePressed`, `MatchedSpec`, `NoMatchingSpec`, `ActivationRequest`: 입력 태그와 실제 어빌리티 연결. `Accepted=1`은 요청 수락이며 서버 발동 성공을 보장하지 않는다.
- `ShipSkillPressed/Rpc/Toggle`, `CannonSkillPressed/Rpc/Toggle`: 배·대포의 로컬 입력과 서버 RPC 수신·ASC 진입.
- `Activated`, `ActivationFailed`, `Ended`: 클라이언트·서버 GAS 실행과 실패 태그, 취소 여부. `SkillUseRejected`는 스킬 사용 권한·재료 판정 실패다. `Authority`와 `NetMode`로 실행 측을 구분한다.

## 이순신 대화 직후로 이동

캠페인 진행은 `StoryFacadeSubsystem`의 노드와 Facts/Counters를 사용한다. `StoryDefinition`의 로딩·규칙 실행·Cook 루트는 폐기되었으며 체크포인트에 Definition 에셋이나 ID가 필요하지 않다. 기존 저장의 Definition ID는 무시하고 진행 데이터를 복원한다. 기존 `sw.Dialogue.YiSunSin.SkipRequirements` 및 대화 조건/거래 우회 경로는 제거되었다. 일반 NPC 대화의 조건·아이템 거래는 항상 정상 규칙을 따른다.

```text
SW.DevTest.Campaign.List
SW.DevTest.Campaign.Skip SupplyPatrol
```

| 명령 대상 | 대화 이후 상태 | 누적 보상과 분기 |
| --- | --- | --- |
| Recon | 정찰 퀘스트 수락 | 암호 해독서 분기 완료 및 해독서 지급; 중간보스 1 미처치 |
| SupplyPatrol | 보급로 순찰 퀘스트 수락 | 위 보상 + 명량 침공 지도, 해류 발생기 해금; 중간보스 2 미처치 |
| Decipher | 암호 해독 퀘스트 수락 | 위 보상 + 일본군 암호; 중간보스 2 처치 완료 |
| Suppress | 해독된 암호 제작 및 원군 저지 퀘스트 수락 | 위 보상 + 해독된 암호, 물폭탄 해금; 중간보스 3 미처치 |
| Uldolmok | 울돌목 전투 퀘스트 수락 | 위 보상 + 일본군 공습 정보, 포탄세례 해금; 최종보스 미처치 |
| Ending | 최종보스 처치 후 마지막 대화 완료 | 모든 선행 필수 보상/스킬, 캠페인 완료 |

접속한 모든 플레이어에게 필수 퀘스트 아이템별 최소 1개를 보장한다. 이미 보유한 아이템은 추가하지 않는다. 선행 보상은 누적으로 남기며 무작위 일반 전리품은 지급하지 않는다. 이미 도달한 대화나 이전 단계는 거절한다.

모두 생존·복원 완료 상태여야 한다. 인벤토리가 부족하거나 안전 진입 지점/배/아이템 정의가 누락되면 거절한다. 필요한 승선과 상자 조작 정리는 자동으로 수행한다.

처리 순서는 복원 데이터 준비 → 전원 로딩 준비 → 최신 상태 재확인 → 서버 재시작 → 진행/보상/스킬 복원이다. 일반 단계는 기존 귀환 지점, 울돌목은 기존 최종전 진입 지점으로 배치한다. 준비 시간 초과, 참가자 이탈, 재시작 요청 거절 시 보상이나 스토리를 일부 적용하지 않는다.

성공한 재시작은 기존 귀환/최종 출항 저장 절차를 통해 현재 방 저장에도 반영된다. 디스크 저장 실패 시 기존 경고가 표시되며 수동 저장이 필요하다. 테스트 옵션은 세이브에 영구 저장하지 않지만 이 명령으로 승인된 재시작에는 이어진다. 정상 재접속/다른 방에서는 다시 세션을 켜야 한다.

## BP_Player 설정 이전

기존 `sw.Campaign.MiddleBossAndReturn`도 폐기되었다. 진행을 초기화해 앞뒤로 이동하던 임시 방식 대신 `SW.DevTest.Campaign.Skip`의 전진 전용 체크포인트를 사용한다.

BP_Player의 기존 테스트 변수는 기존 에셋 참조 호환용으로 남기고 기본값 편집 노출과 동작 경로를 제거했다.

시작 아이템 목록은 `Project Settings → Game → SW Development Tests`의 `Editor Starting Inventory`로 옮겼다. 새 인벤토리의 에디터 시작 시만 지급한다. 기존 BP에 저장된 목록을 자동으로 가져오지는 않으므로 사용하던 목록이 있다면 이 설정에 한 번 입력해야 한다.

## 검증 상태

2026-10-28 통합 검증: UE 5.7 `ArtisticSW2026Editor Win64 Development` 빌드 성공. 헤드리스 Editor에서 Story, NPCDialogue, ShipUpgrade.MaterialAndMultiParentPipeline, Player.Death.LifeOwnership 자동화 테스트 15개가 모두 통과했다. Definition 없는 여섯 체크포인트 준비·복원, 중복/역방향 거절, 옛 Definition ID 저장 복원, 폐기된 콘솔 명령 미등록, 일반 대화 및 보상 스냅샷 회귀 검사를 포함한다.

로그: `Saved/Logs/DevTestIntegration_Build.log`, `Saved/Logs/DevTestIntegration_Automation.log`. 실제 2인 방에서의 이동/보상/접속 유지 및 패키징 실행은 별도 확인이 필요하다. 무료 옵션의 실제 클라이언트 UI·쿨타임 동기화 전체를 이 자동화 결과만으로 검증했다고 간주하지 않는다.
