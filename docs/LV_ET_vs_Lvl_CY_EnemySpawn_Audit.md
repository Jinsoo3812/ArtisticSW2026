# LV_ET와 Lvl_CY의 Enemy 생성 설정 비교

최신 확인: 2026-10-08. UE 5.7.4에서 디스크의 두 레벨을 읽기 전용으로 불러와 함선 클래스·Spawn Plan·앵커·Boss Encounter·PCG Profile을 확인했다. 저장되지 않은 에디터 변경과 실제 플레이 결과는 포함하지 않는다.

## 현재 저장 구성

| 항목 | Lvl_CY | LV_ET |
| --- | --- | --- |
| EnemyShip | BP_EnemyShip 51척 | Balancing/Final 아래 15종, 총 51척 |
| 일반 Spawn Plan | T1 근접 2명 @0/10 | 일반함 3명 @10/11/12, 특수함 2명 @11/12 |
| Boss Encounter Enabled | 51척 모두 false | 51척 모두 false |
| Boss Spawn Point ID | 51척 모두 20 | 51척 모두 20 |
| 앵커 | 아래층 0/1, 위층 10/11/12, 보스 20 | 동일 |
| PCG_Biome Brush Profile | Custom | PCGVolumeBounds |
| PCG 생성 식생 인스턴스 | 3,886 | 3,886 |

BossSpawnPoint는 Can Spawn=false인 보스 전용 앵커다. 일반 0/1/10/11/12는 Can Spawn=true다. 이전 ID 12 중복과 LV_ET의 Boss Encounter 일괄 활성 설정은 현재 저장값에서 정리되어 있다. 이번 점검에서 Boss Class 전체나 스탯·좌표를 다시 측정하지 않았으므로 클래스 누락이 모두 해결되었다고 판단하지 않는다. 비활성 보스 조우는 최초 보스를 생성하지 않는다.

## LV_ET 티어별 실제 편성

MMR=근접 2+원거리 1, MRR=근접 1+원거리 2, RRR=원거리 3이다. 클래스와 Stats Row는 해당 T1~T4를 사용한다.

| BP (Balancing/Final 기준) | 척수 | 실제 순서와 포인트 |
| --- | ---: | --- |
| N1/BP_ES_Normal_MMR | 2 | T1 근접@10, 근접@11, 원거리@12 |
| N1/BP_ES_Normal_MRR | 3 | T1 근접@10, 원거리@11, 원거리@12 |
| N1/BP_ES_Normal_RRR | 3 | T1 원거리@10/11/12 |
| N2/BP_ES_Normal_MMR | 8 | T2 근접@10, 근접@11, 원거리@12 |
| N2/BP_ES_Normal_MRR | 2 | T2 근접@10, 원거리@11, 원거리@12 |
| N2/BP_ES_Normal_RRR | 1 | T2 원거리@10/11/12 |
| N3/BP_ES_Normal_3MMR | 5 | T3 근접@11, 근접@10, 원거리@12 |
| N3/BP_ES_Normal_3MRR | 2 | T3 근접@11, 원거리@10, 원거리@12 |
| N3/BP_ES_Normal_3RRR | 3 | T3 원거리@11/10/12 |
| N4/BP_ES_Normal_4MMR | 11 | T4 근접@10, 근접@11, 원거리@12 |
| N4/BP_ES_Normal_4MRR | 5 | T4 근접@10, 원거리@11, 원거리@12 |
| N4/BP_ES_Normal_4RRR | 3 | T4 원거리@10/11/12 |
| BP_ES_Mid_1 | 1 | T1 근접@11/12 |
| BP_ES_Mid_3 | 1 | T3 원거리@11/12 |
| BP_ES_Final | 1 | T4 근접@11/12 |

합계는 51척이다. 이전 N1/RRR와 N4/MRR·RRR의 이름/편성 불일치는 현재 Spawn Plan에서 수정되었다. N3은 첫 두 슬롯의 ID 순서가 11→10이므로 표의 실제 순서를 따른다. Final 폴더에는 위 레벨 사용 클래스 외의 기본/변형 BP도 포함된다.

## 최신 C++ 소환 정책

일반함은 앞의 함선·편대를 처치해야 소환하는 조건이 없다. 배별 실제 Player 배 Sight로 요청하고 Active·보행면·시작/반응 지연을 함께 확인한다. 항해 TargetShip/Approach/Orbit는 Sight를 대신하지 않는다. Final 편대의 Story Gate는 별도로 유지한다.

거리 휴면에서 비활성 풀 스탯을 미리 적용하던 문제는 생명주기 리팩터링으로 수정했다. 거리 휴면은 조우를 초기화하지 않고, 슬롯 구성·준비·점유/결과 확정 후 표시·AI를 공개한다. [현재 검증 가이드](Deck_Enemy_Spawn_Refactoring_Editor_Test_Guide.md)와 [수정 전 원인 재현](LV_ET_EnemySpawn_Pipeline_Diagnosis.md)을 구분한다.

## 2026-10-07 조사 기록과 남은 검증

수정 전 별도 서버 PIE에서 같은 N2/MMR 두 척을 비교했을 때 거리 휴면을 거치지 않은 배는 3명 활성화, 휴면을 거친 배는 슬롯 10/11/12 모두 재시도 소진이었다. 공개 직접 활성화 성공은 자동 Sight 큐 검증을 대신하지 않는다.

높이 통제 실험에서는 Spawn Height Offset 30/90cm 모두 직접 활성화와 UpperDeck 보행이 성공했다. 스폰 높이만으로 당시 자동 큐 실패를 설명하지 않는다. 초기 원거리 강제 활성화에서는 휴면 함선 충돌이 꺼져 적이 낙하한 기록도 있으나 정상 접근 재현과 구분한다.

당시 Final 편대·아키타입·수치·좌표의 상세 조사는 [Lvl_CY 기록](Lvl_CY_EnemyShip_Editor_Setup_Audit.md)과 아래 로컬 진단 자료에 보존한다. 이번 대조는 편대/아키타입 전체를 다시 조사하지 않았다.

2026-10-08 현재 C++ Editor 빌드는 성공했다. 저장 설정 읽기는 성공했지만 로딩에서 기존 GCNS_Rogue_Arrival BP 컴파일 오류가 기록되어 에셋 전체 검증 성공을 의미하지 않는다. 수정 후 자동 소환, 실제 멀티플레이, 보스 및 Room 복원은 수동 검증이 남는다. Lvl_CY PCG Profile 적용도 [PCG 가이드](PCG_Combat_Fix_Editor_Check.md)의 남은 작업이다.

로컬 자료: `Saved/CommitPreparation/assets.json`, `assets-verified.log`; 이전 `Saved/CodexDiagnostics/LV_ET_SpawnAudit.json`, `LV_ET_FloorProbe.json`, `LV_ET_HeightControlled.json`, `LV_ET_DormancyDeployment.json`과 해당 로그. Saved는 Git 제외 경로이므로 문서의 표·결론을 검토 기준으로 사용한다.
