# 보스 조우 밸런스 테이블 복구 기록

> 2026-10-03 에셋과 CSV를 복구한 뒤 저장값을 다시 확인했다. 갑판 보행면과 스폰 앵커의 현재 구조는 [구현 설명서](DeckWalk_Implementation.md), 에디터 플레이 순서는 [설정 안내](DeckWalk_Manual_Anchor_Editor_Setup.md)를 참고한다.

## 발생 원인

기존 T1 보스의 `EncounterBalanceRow`는 `DT_EnemyEncounterBalance1 / T1`을 가리켰지만, 실제 테이블의 행 구조가 `EnemyCombatBalanceRow`였다. 보스 코드가 요구하는 `EnemyEncounterBalanceRow`로 행을 읽을 수 없어 `AShipBossEnemy::BeginPlay`에서 초기화 실패 후 보스를 제거했다. 이전 실행 로그에는 잘못된 DataTable 형식과 `[EnemyBalance] Invalid boss encounter row or missing summon class`가 기록됐다. 당시 스폰 앵커 ID와 조우 트리거 자체는 통과했다.

CSV의 T1 `SummonHealthFractions`도 `-0.5`였으므로, 행 구조만 고쳐도 보스의 값 검증에 실패하는 상태였다.

## 적용한 복구

- 기존 `DT_EnemyEncounterBalance1`을 Unreal CSV 임포트 경로로 다시 구성해 행 구조를 **`EnemyEncounterBalanceRow`**로 저장했다. `DT_EnemyEncounterBalance`는 실제 데이터 테이블이 아니라 이 에셋으로 향하는 Redirector다.
- `DataTable/EnemyEncounterBalance.csv`의 T1 소환 체력 비율을 **`(0.5)`**로 정정했다.
- T1–T4의 `SummonStats`를 실제 `DT_EnemyBaseStat`의 각 `Tn_Deck_Melee` 행으로 연결했다. 공격 피해·예고 시간·소환 횟수는 기존 CSV 값을 유지했다.
- 보스 BP들의 `SummonedEnemyClass`를 **`BP_DeckMeleeEnemy`**로 지정했다. 기존 일반 적 풀의 비활성 기본 근접 적을 재사용한다.

T1 행은 `SummonCount=1`, `SummonAliveLimit=1`, 체력 비율 `0.5`, `StrongAttackDamage=18`, `MajorAttackDamage=24`, `MajorAttackTelegraphSeconds=1.2`로 저장되어 있다.

## 보스 BP의 행 선택

| BP | 현재 `EncounterBalanceRow` |
| --- | --- |
| `BP_Ship_BossEnemy`, `T1_BP_ShipBoss` | `DT_EnemyEncounterBalance1 / T1` |
| `T2_BP_ShipBoss_Rogue` | 지정되지 않음 (`Row Name=None`) |
| `T3_BP_ShipBoss`, `T4_BP_ShipBoss_Samurai` | 기존 선택인 `DT_EnemyEncounterBalance1 / T1` |

각 BP의 행 선택은 기존 설정을 보존했다. T2의 조우 행이 비어 있으므로 T2를 시험할 때 T1의 체력 50% 소환 조건을 적용한다고 가정하면 안 된다. T3/T4도 현재는 각각의 T3/T4 행 대신 T1 행을 선택한다. 티어별 보스 전투 밸런스를 적용하려면 해당 BP의 행 선택을 따로 정해야 한다.

## 확인 범위

저장된 DataTable 행 구조, T1–T4의 소환 Stats 참조, 보스 BP의 소환 클래스와 행 선택을 다시 읽어 확인했다. 보스가 설정된 Test_Level 함선의 수동 앵커 검증은 오류 0개였고 보행면은 `Ready=1`이었다. 실제 PIE에서 보스의 최초 등장과 체력 조건에 따른 소환은 아직 확인하지 않았다.
