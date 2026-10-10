# Lvl_CY 선원·경호 상자·보스 보상 자동화 검증

## 결과

2026-10-10 실행. Unreal Engine 5.7.4 / Editor Development / NullRHI.

- 최종 빌드 성공.
- 최종 실행 10개: 성공 10개, 실패 0개, 미실행 0개.
- 엔진 보고서 분류: 경고 없는 성공 1개, 경고 포함 성공 9개.
- 보고서: `Saved/Automation/CrewChestVerified/index.json`.
- 로그: `Saved/CrewChestVerified.log`, 빌드 로그: `Saved/CrewChestIntegrationBuild.log`.

## 격리 방식

사용자가 이번 테스트에 대한 에셋 사용 및 테스트 실행 예외를 허용했다.

통합 테스트는 매 시나리오마다 별도 `UGameInstance`와 폐기 가능한 Game World를 만든다. 바닥, 고정 갑판 지점 3개, 적선 1척, 플레이어 배 1척, 루팅 매니저 1개, 상자 포인트 2개를 C++로 구성한다. 일반 풀 선원은 2명이다. 테스트 종료 시 월드 EndPlay, 월드 파괴, 게임 인스턴스 종료, 월드 컨텍스트 정리를 수행한다. 별도 `.umap`을 저장할 필요가 없으며 원본 레벨에 테스트 액터를 배치하지 않는다.

저장/복원, 서버 접속, 레벨 이동, 패키지 저장을 호출하지 않는다. 거리 휴면은 격리 픽스처에서 비활성화한다. AI 시스템 및 BeginPlay를 명시적으로 초기화하고, 순차 스폰은 latent 자동화 명령으로 엔진 프레임마다 진행한다. 타임아웃은 5초의 시뮬레이션 시간이며 실패 시 스포너 대기 이유, 상태, 활성 선원 수, 배의 배치 가능 상태를 기록한다.

시야 조건은 제어 가능한 Sight stimulus를 실제 AIPerception에 전달한다. 실제 NavalAIController의 인지 처리, EnemyShip 알림, 스포너의 요청·순차 활성화 경로가 실행된다. 경호 등록 및 사망 방송을 테스트에서 직접 대체하지 않는다. 사망은 실제 `UBaseHealthComponent::StartDeath` 경로를 사용한다.

## 검사한 시나리오

| 테스트 | 결과 | 검사 내용 |
|---|---|---|
| Crew.ChestBeforeActivation | 통과 | 비활성 풀은 상자를 잠그지 않음. 실제 등장 후 두 상자 경호 등록, 풀 복귀·재활성화, 순차 사망, 마지막 경호원 사망 후 개방 |
| Crew.ChestAfterActivation | 통과 | 선원 등장 후 늦게 생성한 두 상자가 경호원을 인지하고 같은 생명주기 처리 |
| Boss.ChestBeforeSpawn | 통과 | 실제 보스 생성 후 배/상자 등록, 잠금, 필수 아이템 주입, 중복 보상 적용 방지, 사망 후 보상 유지·개방 |
| Boss.ChestAfterSpawn | 통과 | 보스가 먼저 생성된 후 매니저가 생성한 상자에도 경호 등록과 필수 보상이 적용됨 |
| Boss.FailedSpawn | 통과 | 없는 지점에서 보스 생성 실패 시 부분 보스 등록 없음, 보스 상자 판정 없음, 경호원 없는 상자는 개방 유지 |
| Boss.AuthoredMid1 | 통과 | 실제 Lvl_CY 중간보스1 Rogue BP 소환 및 실제 DA 필수 아이템·수량 검증 |
| Boss.AuthoredMid3 | 통과 | 실제 Lvl_CY 중간보스3 Samurai BP 소환 및 실제 DA 필수 아이템·수량 검증 |
| Boss.WithLivingCrew | 통과 | 보스 사망 후 일반 경호원 생존 시 잠금 유지, 최종 경호원 사망 후 개방, 필수 보상 유지 |
| LvlCY.CrewChestAuthoring | 통과 | 저장된 원본 패키지 읽기 전용 검사: 적선 48척, 활성 보스 조우 2개, 매니저 1개, 선원 클래스·스탯 행·지점·상자 포인트·보스 정확 클래스 보상 매핑 |
| ArtisticSW.Chest.GuardedUnlockAndShipFailure | 통과 | 기존 상자 테스트: 사망/파괴, 늦은 경호 등록, 중복 등록, 보스 등록, 상자 상호작용 토글, 소속 배 사망 시 보상 실패 |

새 런타임 테스트 접두사: `ArtisticSW.Integration.ShipCrewChest`.
새 원본 설정 검사: `ArtisticSW.Integration.LvlCY.CrewChestAuthoring`.

## 실제 에셋 검증

- 중간보스1: `/Game/GameplayAbilitySystem/Enemy/Balancing/T2/T2_BP_ShipBoss_Rogue.T2_BP_ShipBoss_Rogue_C`.
- 중간보스3: `/Game/GameplayAbilitySystem/Enemy/Balancing/T4/T4_BP_ShipBoss_Samurai.T4_BP_ShipBoss_Samurai_C`.
- 보상 DA: `/Game/Blueprints/Item/Data/DA_BossChestGuaranteedLoot.DA_BossChestGuaranteedLoot`.
- 두 실제 보스 클래스 모두 DA 매핑이 있으며, 각각 필수 아이템 항목 1개를 실제 상자 슬롯의 수량과 비교했다.
- Lvl_CY는 `LoadPackage`로 읽고 에디터의 열린 맵을 교체하거나 플레이하지 않았다. 원본 `.umap` 및 `.uasset`의 Git 변경 없음.
- 최종 Lvl_CY SHA256: `0369955D16AFDD7C80D815C0AD77AD0D3AAAF5A3AB9CC17A5B51C39B9F1148BA`.

## 경고와 검증 한계

원본 레벨 검사에는 기존 콘텐츠의 누락된 애니메이션/AnimNotify 의존성 등 로드 경고 571개가 기록됐다. 나머지 경고는 실제 BP 의존성 및 테스트용 native 적의 미설정 시체 상자 클래스, 의도적인 잘못된 보스 지점 실패 경고 등을 포함한다. 경고를 숨기지 않았으며 원본 보고서에 보존했다. 선원 등록·잠금·보상에 대한 실패는 없지만, 이 결과는 콘텐츠 로드 경고가 해결되었다는 뜻은 아니다.

Native 보스 픽스처에는 전투 BT를 넣지 않아 발생하는 특정 오류를 기대 오류로 선언한다. 잘못된 보스 지점 시나리오의 `UnknownSpawnAnchor`도 정확히 1회 기대한다. 기존 아이템 데이터의 알려진 QuestItem 레시피 메시지 2종만 제한적으로 기대 처리한다. 전투 AI의 공격/이동/애니메이션 품질은 검사 대상이 아니다.

일반 선원 런타임 경로는 native `ADeckEnemy`로 검사하며, Lvl_CY의 선원 BP는 클래스·스탯·스폰 지점의 저작 설정을 검사했다. 실제 보스 BP 2개는 격리 월드에서 런타임 검사했다. Lvl_CY 전체 PIE, 광학적인 실제 거리·차폐 기반 시야 획득, 네트워크 복제 및 클라이언트 UI, 보상 재분배 전체 계산, 캠페인 저장/재접속은 이번 결과에 포함하지 않는다.

Final 보스 조우는 Lvl_CY에서 비활성이고 최종 보스 구현 대기라는 기존 지침에 따라 소환/보상을 통과한 것으로 계산하지 않는다. 중간보스2는 지상 조우라 배 위 보스 런타임 범위에서 제외한다.

## 재실행

에디터 자동화 창에서 두 새 접두사와 기존 상자 테스트를 선택하거나 다음 명령줄을 사용한다.

```powershell
& 'C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/Unreal Projects/ArtisticSW2026/ArtisticSW2026.uproject' /Engine/Maps/Entry '-ExecCmds=Automation RunTests ArtisticSW.Integration.ShipCrewChest+ArtisticSW.Integration.LvlCY.CrewChestAuthoring+ArtisticSW.Chest.GuardedUnlockAndShipFailure' '-TestExit=Automation Test Queue Empty' '-ReportExportPath=C:/Unreal Projects/ArtisticSW2026/Saved/Automation/CrewChestVerified' -unattended -nop4 -nullrhi -nosound -stdout '-abslog=C:/Unreal Projects/ArtisticSW2026/Saved/CrewChestVerified.log'
```

## Implementation Log & Checklist

- [x] 사용자 승인 예외에 따라 테스트 전용 C++ 추가.
- [x] 독립 게임 인스턴스, 월드 및 갑판/스폰/상자 환경 구축.
- [x] 초기 픽스처의 BeginPlay·AI 시스템·프레임/타이머 진행 문제 수정.
- [x] 실제 루팅 매니저의 상자 생성 후 필수 보상 적용 경로 사용.
- [x] 실제 선원 생성/활성화·복귀·재활성화·사망 알림 검사.
- [x] 보스 소환/실패/사망/보상 및 일반 경호원과의 조합 검사.
- [x] Lvl_CY 저장된 설정 읽기 전용 검사.
- [x] Editor Development 빌드 및 최종 테스트 10개 통과.
- [x] 제품 코드 및 원본 레벨/에셋 수정 없음.
