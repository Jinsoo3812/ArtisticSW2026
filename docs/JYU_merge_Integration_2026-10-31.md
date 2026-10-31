# JYU_merge 통합 검증 (2026-10-31)

## 통합 대상

기준은 로컬 `fix-lss`이며 `master` → `KKH/ShipAI` → PR #124 (`wonkii/ArtisticSW2026:WonjunJang`) 순서로 병합했다. 최신 `Jiwon_UI`는 PR #124에 이미 포함되어 있다. 기존 브랜치의 ref는 변경하지 않았다.

| 입력 | 커밋 |
| --- | --- |
| `fix-lss` | `e08604dc5ead926255ee080f717cba07a5882f12` |
| `origin/master` | `1bab6c96b16ca97fc34e4cdc99eccfa303739806` |
| `origin/KKH/ShipAI` | `150dacd2222c56492e1dedabff5b9f6e175ba259` |
| `origin/pr/124` | `cf44a8350d1bb907d3006e376dc4f0e335f340e4` |
| `origin/Jiwon_UI` | `a590563d8d1ae9c0dc8e27ae696a3cdc3a846491` |

## 충돌 해결

- 제작 비용 합산·오버플로 검증, 스킬 태그 소유권, 대포 조작 코드를 유지하면서 master의 개발 옵션과 수면 스킬 오버레이 구조를 반영했다.
- PR의 `DA_ItemData.uasset` LFS 객체 `d8af1b5363a2bac7a90655c63025a7d8c82430399c87bc1cbe58802bcb2204dd`는 정상 패키지가 아니라 이전 병합 충돌 표시 254바이트였다. 정상 ShipAI 데이터 78개를 기준으로 UI 커밋 `6a57c064`와 그 부모를 UE에서 비교하고 `Icon2D` 변경 59개만 복원했다. 다른 필드와 신규 항목은 보존했으며 별도 프로세스에서 저장 후 전체 struct 일치를 확인했다. 이는 깨진 입력 에셋의 복구다.
- 철판 재질 2개는 `150dacd2`의 누락 텍스처 수정본을 유지했다.
- 전체 자동화 중 맵 전환의 이전 월드 정리에서 `UBossEncounterComponent::EndPlay`가 null World를 역참조했다. 타이머 정리를 유효한 World가 있을 때만 수행하도록 수정했다. 같은 전체 실행을 다시 수행해 종료 크래시가 재발하지 않았고 테스트 큐가 완료됐다. 이는 확인된 null 역참조 경로의 수정이다.
- UI 아이콘 테스트가 이미 복구된 퀘스트 데이터에서 예전 오류를 요구하지 않도록 낡은 expected-error 설정을 제거했다.

## 검증 결과

- direct UnrealBuildTool.exe로 UE 5.7.4 Editor 빌드 성공. 최종 추가 수정 후 재빌드도 성공했다.
- 추적 에셋 11,383개의 패키지 헤더 검사에서 추가 비정상 파일은 없었다. 이 검사는 전체 에셋의 의미·레퍼런스 유효성 전수 검사를 대신하지 않는다.
- 프로젝트 자동화 **277개: 253 통과 / 24 실패**. `ArtisticSW+SW` 문자열 선택으로 포함된 엔진 테스트까지 합하면 344개: 320 통과 / 24 실패다. 테스트 프로세스 종료 코드만으로 성공을 판정하지 않고 JSON 결과를 집계했다.
- 총잡이 주기 소환·조준/Focus, 아이콘 3개, 장비·공격·활 중단, 수영 외부 종료, 태그 소유권 검사는 통과했다.
- 원래 전체 실행의 크래시는 수정 후 재실행에서 재발하지 않았다. 반복적인 실사용 장기 플레이, 패키징, 모든 보스전의 수동 멀티플레이는 미검증이다.
- 아래 24개 실패는 남아 있다. 각 입력 브랜치를 별도로 빌드·재실행하지 않았으므로 기존 문제인지 통합 회귀인지 전부 확정하지 않았다. 테스트 기대값만 완화해 통과시키지 않았다.

## 남은 실패

- `ArtisticSW.AreaSlow.InputLifecycle`: Expected 'Holding the dedicated skill input enters Area Slow aiming' to be true.
- `ArtisticSW.Bombardment.GameplayAbilityShipIntegration`: Expected 'Helming preserves the living player's skill permission' to be true.
- `ArtisticSW.Chest.FixedDrops.AuthoredChances`: Expected 'Five rare item entries' to be 5, but it was 8.
- `ArtisticSW.Chest.MapPlacedLogic`: LogTemp: Random chest point must use OceanRandom or IslandRandom. Point=ChestSpawnPoint_0
- `ArtisticSW.Enemy.Balance.HitCounts`: Expected 'T1_BossSummon_Ranged actual GAS damage count' to be 4, but it was 2.
- `ArtisticSW.Enemy.Balance.Initialization`: LogTemp: [EnemyBalance] Invalid stats: Enemy=BaseEnemy_0 Row=Bad
- `ArtisticSW.Enemy.DeckMVP.FixedAnchorLifecycle`: Expected 'Server activates pooled enemy at a committed deck point' to be true.
- `ArtisticSW.Enemy.DeckMVP.RangedCombatTreeHasAttackAndGraphMovement`: Expected 'Combat subtree retains the ranged attack task' to be true.
- `ArtisticSW.Enemy.DeckMVP.SpawnerCompositionAndShipLocalTransform`: Expected 'An external boss-style requester uses the shared spawn reservation path' to be true.
- `ArtisticSW.Enemy.Ship.Anchor.Integration`: Expected 'Move input applied after anchor raised' to be 1.000000, but it was 0.000000 and outside tolerance 0.000100.
- `ArtisticSW.Enemy.Ship.Navigation.DeterministicAvoidance`: Expected 'Yielding ship applies reverse thrust' to be true.
- `ArtisticSW.GAS.HitReaction.AuthorityRootMotion.Man`: Expected 'Configured montage enables root motion' to be true.
- `ArtisticSW.GAS.HitReaction.AuthorityRootMotion.Woman`: Expected 'Configured montage enables root motion' to be true.
- `ArtisticSW.GAS.HitReaction.PlayerConfiguration`: Expected 'Montages without authored root motion receive a fallback push' to be true.
- `ArtisticSW.GravityVortex.SelectionInputLifecycle`: Expected 'Pressing the skill key enters Gravity Vortex aiming mode' to be true.
- `ArtisticSW.Integration.LvlCY.CrewChestAuthoring`: Expected 'BP_ES_Mid_1 actual boss class has exact-class guaranteed loot mapping' to be true.
- `ArtisticSW.Integration.ShipCrewChest.Boss.IdleReturnCleanup`: Expected 'Real spawner activates a regular guard before boss spawn' to be true.
- `ArtisticSW.Integration.ShipCrewChest.Boss.WithLivingCrew`: Expected 'Real spawner activates a regular guard before boss spawn' to be true.
- `ArtisticSW.Integration.ShipCrewChest.Crew.ChestBeforeActivation`: Crew activation timed out after five simulated seconds. WaitReason=CapsuleBlocked State=4 Active=1/0 HostDeployable=1
- `ArtisticSW.Network.SwimmingPredictionQuantitative`: LogTemp: Random chest point must use OceanRandom or IslandRandom. Point=ChestSpawnPoint_0
- `ArtisticSW.Status.PlayerPreview`: Sword B data uses the sword preview idle: The two values are not equal.
- `ArtisticSW.Tools.BuildChestVisualTestMap`: Expected 'Previous generated test map is removed' to be true.
- `ArtisticSW.UI.ItemQuickSlot.FUsePriority`: Expected 'F consumes exactly one selected item' to be 1, but it was 2.
- `ArtisticSW.WaterBomb.GameplayAbilityCannonIntegration`: Expected 'Water Bomb GA activates by ability tag' to be true.

## 로그와 작업 파일

- 상세 실패 메시지: `docs/validation/JYU_merge_20261031.json`.
- 로컬 로그·전체 보고서: `Saved/MergeValidation/JYU_merge/BuildFinal.log`, `AutomationFinal/index.json`, `catalog_comparison.json`, `catalog_reload_verified.json`.
- 자동화가 저장한 테스트 전용 데이터 2개는 실행 전 인덱스 버전으로 복원했으며 생성한 chest redirector는 Saved에 보관했다. 병행 작업으로 생긴 미추적 캐릭터/SourceArt 파일은 이번 커밋에서 제외했다.
- 기존 미커밋 `BP_Player_Man.uasset` 수정은 사용자 요청에 따라 적용하지 않았다.
