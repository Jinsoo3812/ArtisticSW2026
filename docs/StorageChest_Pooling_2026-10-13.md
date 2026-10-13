# Random Storage Chest Pooling

사용자 요청: 바다/지상 Random 상자만 Actor Pool + 원거리 ISM으로 전환한다. 배 위 상자, 섬 경호 상자, 잠긴 상자와 보스 상자는 제외한다. 활동 + 유휴 기본 Actor 목표는 5개이며 EnemyShip처럼 근거리 수요가 목표를 넘으면 필요한 Actor는 유지한다.

## 구현 경로

`AChestSpawnPoint`의 기존 SpawnMode/Environment/guard/owner 설정으로 자동 판별한다. 별도 BP 편집이나 기존 물리 거리 최적화 체크는 필요 없다. 공용 보관함/시체/침몰 후 드롭 상자는 Random SpawnPoint의 소유 상자가 아니므로 등록하지 않는다. 같은 BP를 쓰더라도 Guarded, ShipDeck 또는 guard/owner 설정이 있으면 제외한다. 직접 배치한 AStorageChest에는 Random 여부가 없으므로 자동 등록하지 않는다.

순서는 기존 상자 생성 및 loot 설정 → 실제 인스턴스의 BP 클래스/설정/mesh/슬롯 상태 캡처 → 거리 판정 및 바다 안정화 확인 → SpawnPoint의 Actor 참조 해제 → Actor의 저장 등록 해제/물리·틱·충돌 중지 → 풀 반환 및 ISM 표시 → 같은 클래스/같은 레벨 Actor 확보 → 배치 설정 import → 슬롯/개봉 상태 복원 → 위치/소유자/원래 stable ID/creator 복원 → 활성화 → 클라이언트 할당 ID 도착 후 ISM 제거다. Actor 전환은 프레임당 한 건, 거리 평가와 reserve 보충은 0.5초 간격이다.

작은 BP와 큰 BP는 실제 ClassPath가 다르므로 서로 재사용하지 않는다. 동일 BP의 배치별 editable 값은 모두 기록하고 되돌린다. 각 BP의 Construction Script/BeginPlay는 최초 Actor 생성 시 실행되고 재사용 시에는 다시 실행하지 않는다. 형상은 각 native/BP static mesh와 재질 및 상대 transform으로 ISM을 구성한다. ISM은 충돌이 없으며 원거리 바다 부력 이동은 멈춘다. 활성 위치를 캡처하므로 수면 위에서 잠든 위치를 기준으로 다시 활성화한다.

기본 거리 100000cm(1km), 활성 Actor 반납에는 추가 10000cm hysteresis를 적용한다. 플레이어 Pawn 또는 Player 태그의 아군 배 중 하나라도 근처면 활성화한다. 물 위 상자는 pontoon이 물에 있고 선속도 100cm/s, 각속도 30deg/s 이하로 2초간 유지된 뒤에만 반납한다. 탐색 UI가 열려 있거나 empty destroy timer가 남은 상자는 유지한다.

저장 정의는 별도 전역 manager record 대신 원래 SpawnPoint의 Loot domain에 보관한다. 활성 상자는 기존 Actor record도 유지하고, 휴면 상자는 SpawnedActorId를 비워 존재하지 않는 Actor를 참조하지 않는다. 풀 대기 Actor는 Transient로 제외한다. 미등록/이전 저장의 빈 PooledChest는 기존 경로로 복원한 뒤 자동 발견한다. 스트리밍 레벨이 사라지면 Actor 참조도 정리되고 SpawnPoint domain은 기존 partition 저장 경로를 따른다. 명시적인 loot 재계산 전에는 휴면 상자를 일시 활성화하여 기존 census에서 빠지지 않게 한다.

## 수동 런타임 확인

1. 최신 DLL로 Editor를 다시 시작하고 서버 + 클라이언트 2개로 Random 바다/지상 상자와 Guarded 섬/갑판 상자를 확인한다.
2. Random 상자에서 멀어졌다 돌아오며 Actor ↔ ISM 전환, 충돌/상호작용, 바다 부력 재시작을 확인한다. falling chest가 공중 ISM으로 고정되지 않아야 한다.
3. 서로 다른 설정을 가진 같은 BP 상자를 번갈아 접근하고 큰 BP도 확인한다. mesh, 크기, 이름, 슬롯/열 수, 일부 가져간 내용물, 개봉 상태가 해당 논리 상자에 남아야 한다.
4. 상자 탐색 UI가 열린 동안 반납되지 않는지, 다른 논리 상자의 탐색 진행도가 같은 Actor 재사용 때문에 섞이지 않는지 확인한다.
5. 근거리 Random 상자가 6개 이상이면 모두 Actor로 남는지, 멀어지면 최종 활동+대기 Actor 합계가 5로 정리되는지 확인한다. 잠긴/경호 상자는 이 합계에 포함하지 않는다.
6. 활동/휴면 혼합 저장과 재접속 및 voyage level unload/reload를 확인한다. 슬롯과 stable ID 보존, 휴면 상자 중복 spawn/재추첨 없음, client에서 잠깐의 이중 표시/오래된 위치 이동이 없는지 확인한다.

## 로딩 화면 분석 (코드 변경 없음)

`USWConnectionSubsystem::BuildReadinessMask`는 client world, local controller, PlayerState, Pawn, possession, Pawn BeginPlay, camera, RoomWorldReady의 8비트를 검사한다. 전부 충족되는 Tick이 3회 연속 이어지면 `CompleteReadiness`에서 화면을 치운다. 카메라는 PlayerCameraManager와 ViewTarget의 존재만 검사한다. hosted room이면 ASWRoomReadyState의 RestoreGeneration > 0, bWorldReady, 유효 RoomRunId를 검사한다. PendingDisplayName이 비어 있으면 RoomWorldReady 비트는 생략 충족한다.

서버 `bWorldReady`는 서버 복원/방 초기 저장 흐름의 완료 신호다. 해당 replicated Actor 하나를 받았다는 사실은 다른 Actor channel/ISM 정의의 client 수신 완료를 보장하지 않는다. streaming level visible, weather first frame, texture/shader/첫 render frame 완료를 검사하는 조건은 현재 없다.

날씨 `ASWNetworkWeatherActor::TryStartPlayback`는 초기 server clock sample이 2초 이상 안정적인지(variation <= 0.05초) 확인한 후 ready segment 및 관련 assets를 사용한다. 따라서 Pawn 준비 3틱과 하늘 준비는 시간적으로 어긋날 수 있다. `Tick`에서 `EvaluatePresentedWeather` 후 `CaptureVisualFrame`에 성공할 때 `bHasVisualFrame`을 설정하지만 이 값은 로딩 준비 조건과 연결되지 않는다.

권장: 서버 복원 신호 + 필요한 streaming level loaded/visible + client weather 초기 epoch의 첫 유효 visual frame + 플레이어 주변 필수 Actor 및 dormant proxy 정의의 적용 완료를 하나의 client readiness barrier로 묶고, 이후 2~3 render/game frames 안정화 및 짧은 fade를 둔다. weather가 없는 map에서는 해당 조건을 명시적으로 생략하며, 누락된 readiness 항목을 timeout 진단에 남긴다. 이동/복원 generation을 식별해서 이전 여행의 Ready 신호를 재사용하지 않게 해야 한다.

“레벨 내 모든 Actor 완료”는 계속 spawn/despawn하는 AI·pool·replication relevancy 때문에 완료 집합이 불명확하다. 서버의 초기 생성 작업은 완료 신호를 보낼 수 있지만 client는 현재 필요한 초기 집합을 generation/manifest 기준으로 별도로 확인해야 한다. 원거리 상자를 모두 Actor로 깨울 필요는 없다. 비동기 asset loading은 frame hitch와 동기 TryLoad 비용을 줄이는 별도 개선이며 readiness 검사를 대신하지 않는다. 현재 weather에도 비동기 asset preparation이 존재한다. 첫 조치는 asset async 전면 변경보다 날씨와 초기 주변 표시 완료를 loading gate에 연결하는 것이다.

## Implementation Log & Checklist

- [x] Random eligibility, 5 Actor baseline, same-class/same-level reuse, dormant ISMs.
- [x] Instance profiles, chest contents and identity, point-domain persistence, UI search isolation.
- [x] Core room audit excludes idle Actor and recognizes derived assignment state.
- [x] Final build/automation results (2026-10-14 KST): UE 5.7 Editor Development build 성공. 최종 `ArtisticSW.Chest.Pool.LifecycleAndPersistence`, `Progression.DefinitionRecipeTargetManager`, `DataDrivenSpawnGroups`, `GuardedUnlockAndShipFailure` 4개 전부 Success. Pool 테스트는 클래스 불일치 reserve 제외, 배치 간 Actor 교환과 슬롯/물성/ID 보존, 활동 설정 변경의 저장/복원, 휴면 domain 왕복, 실제 초과 Actor 삭제를 확인한다. 보고서 `Saved/Automation/ChestPoolFinal/index.json`, 로그 `Saved/Logs/StorageChestPool_Final_20261013.log`.
- 전체 `ArtisticSW.Chest` 첫 실행은 12개 중 8개 Success였다. 새 Pool 테스트는 disposable world에서 BeginPlay dispatch가 누락된 fixture를 수정한 뒤 위 최종 실행에서 통과했다. 남은 3개 실패는 `FixedDrops.AuthoredChances`의 실제 DA 확률과 기대값 불일치, `EmptyChestAutoDespawn`의 기존 recipe fixture 오류 로그, `MapPlacedLogic`의 테스트맵 Random Kind 설정/기대 소환 수 불일치다. 전체 테스트 통과로 보고하지 않는다. 전체 로그 `Saved/Logs/StorageChestPool_Automation_20261013.log`.
- 넓은 필터에 포함된 authoring 테스트가 저장한 `DA_ChestTest_High`, `DA_ChestTest_Low` 변경과 새 `BP_StorageChest`는 초기 상태로 되돌렸다. 최종 집중 테스트는 authoring 테스트를 실행하지 않았으며 이 작업의 Content 에셋 변경은 없다.
- [ ] 실레벨 2-client 표시/스트리밍 확인: 위 수동 시나리오 필요.
