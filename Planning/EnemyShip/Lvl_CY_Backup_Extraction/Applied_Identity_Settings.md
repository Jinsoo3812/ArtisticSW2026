# Lvl_CY EnemyShip 설정 적용 결과

- 대상 브랜치: WonjunJang
- 적용: World Outliner 이름으로 찾은 EnemyShip 51척
- 적용한 속성: Squad ID, Enemy Ship Archetype, Actor Tags
- BP 종류가 백업과 다른 2척도 사용자 요청대로 이름 기준 적용
- 이름 수정: BP_ES_Normal_4MMR_C_21 — BP_ES_Normal_4_8 → BP_ES_Normal_4_9
- 저장 후 다시 로드하여 51척의 설정과 이름을 검증함
- 전체 액터의 위치·회전·크기와 나머지 검사 대상 설정 유지 확인
- SWRoomStableId 태그는 현재 액터의 값을 유지함
- 플레이 테스트 및 Commit/Push는 수행하지 않음

적용 전 레벨 백업: `C:\Users\wonkii\Documents\GitHub\ArtisticSW2026\Saved\LvlCYExtraction\Application\20261010_131108\Lvl_CY_before.umap`

| 액터 이름 | Squad ID | Archetype | Actor Tags (저장 식별값 제외) |
|---|---|---|---|
| `BP_ES_Final` | `Final` | `DA_ES_TimeStop` | Enemy, DebugShip07 |
| `BP_ES_Mid_1` | `Normal_1_4` | `DA_ES_Charge` | Enemy, DebugShip_M_1 |
| `BP_ES_Mid_3` | `Normal_3_3` | `DA_ES_Torpedo_Obstacle` | Enemy, DebugShip_M_3 |
| `BP_ES_Normal_1_1` | `Normal_1_1` | `DA_ES_Normal_1` | Enemy, DebugShip01_1 |
| `BP_ES_Normal_1_2` | `Normal_1_1` | `DA_ES_Normal_1` | Enemy, DebugShip01_2 |
| `BP_ES_Normal_1_3` | `Normal_1_1` | `DA_ES_Normal_1` | Enemy, DebugShip01_3 |
| `BP_ES_Normal_1_4` | `Normal_1_2` | `DA_ES_Normal_1` | Enemy, DebugShip01_4 |
| `BP_ES_Normal_1_5` | `Normal_1_2` | `DA_ES_Normal_1` | Enemy, DebugShip01_5 |
| `BP_ES_Normal_1_6` | `Normal_1_2` | `DA_ES_Normal_1` | Enemy, DebugShip01_6 |
| `BP_ES_Normal_1_7` | `Normal_1_4` | `DA_ES_Normal_1` | Enemy, DebugShip01_7 |
| `BP_ES_Normal_1_8` | `Normal_1_4` | `DA_ES_Normal_1` | Enemy, DebugShip01_8 |
| `BP_ES_Normal_1_9` | `Normal_1_4` | `DA_ES_Normal_1` | Enemy, DebugShip01_9 |
| `BP_ES_Normal_1_10` | `Normal_1_3` | `DA_ES_Normal_1` | Enemy, DebugShip01_10 |
| `BP_ES_Normal_1_11` | `Normal_1_3` | `DA_ES_Normal_1` | Enemy, DebugShip01_11 |
| `BP_ES_Normal_1_12` | `Normal_1_3` | `DA_ES_Normal_1` | Enemy, DebugShip01_12 |
| `BP_ES_Normal_2_1` | `Normal_2_1` | `DA_ES_Normal_2` | Enemy, DebugShip02_1 |
| `BP_ES_Normal_2_2` | `Normal_2_1` | `DA_ES_Normal_2` | Enemy, DebugShip02_2 |
| `BP_ES_Normal_2_3` | `Normal_2_1` | `DA_ES_Normal_2` | Enemy, DebugShip02_3 |
| `BP_ES_Normal_2_4` | `Normal_2_2` | `DA_ES_Normal_2` | Enemy, DebugShip02_4 |
| `BP_ES_Normal_2_5` | `Normal_2_2` | `DA_ES_Normal_2` | Enemy, DebugShip02_5 |
| `BP_ES_Normal_2_6` | `Normal_2_2` | `DA_ES_Normal_2` | Enemy, DebugShip02_6 |
| `BP_ES_Normal_2_7` | `Normal_2_3` | `DA_ES_Normal_2` | Enemy, DebugShip02_7 |
| `BP_ES_Normal_2_8` | `Normal_2_3` | `DA_ES_Normal_2` | Enemy, DebugShip02_8 |
| `BP_ES_Normal_2_9` | `Normal_2_3` | `DA_ES_Normal_2` | Enemy, DebugShip02_9 |
| `BP_ES_Normal_3_1` | `Normal_3_1` | `DA_ES_Normal_3` | Enemy, DebugShip03_1 |
| `BP_ES_Normal_3_2` | `Normal_3_1` | `DA_ES_Normal_3` | Enemy, DebugShip03_2 |
| `BP_ES_Normal_3_3` | `Normal_3_1` | `DA_ES_Normal_3` | Enemy, DebugShip03_3 |
| `BP_ES_Normal_3_4` | `Normal_3_2` | `DA_ES_Normal_3` | Enemy, DebugShip03_4 |
| `BP_ES_Normal_3_5` | `Normal_3_2` | `DA_ES_Normal_3` | Enemy, DebugShip03_5 |
| `BP_ES_Normal_3_6` | `Normal_3_2` | `DA_ES_Normal_3` | Enemy, DebugShip03_6 |
| `BP_ES_Normal_3_7` | `Normal_3_3` | `DA_ES_Normal_3` | Enemy, DebugShip03_7 |
| `BP_ES_Normal_3_8` | `Normal_3_3` | `DA_ES_Normal_3` | Enemy, DebugShip03_8 |
| `BP_ES_Normal_3_9` | `Normal_3_3` | `DA_ES_Normal_3` | Enemy, DebugShip03_9 |
| `BP_ES_Normal_4_1` | `Normal_4_1` | `DA_ES_Normal_4` | Enemy, DebugShip04_1 |
| `BP_ES_Normal_4_2` | `Normal_4_1` | `DA_ES_Normal_4` | Enemy, DebugShip04_2 |
| `BP_ES_Normal_4_3` | `Normal_4_1` | `DA_ES_Normal_4` | Enemy, DebugShip04_3 |
| `BP_ES_Normal_4_4` | `Normal_4_2` | `DA_ES_Normal_4` | Enemy, DebugShip04_4 |
| `BP_ES_Normal_4_5` | `Normal_4_2` | `DA_ES_Normal_4` | Enemy, DebugShip04_5 |
| `BP_ES_Normal_4_6` | `Normal_4_2` | `DA_ES_Normal_4` | Enemy, DebugShip04_6 |
| `BP_ES_Normal_4_7` | `Normal_4_5` | `DA_ES_Normal_4` | Enemy, DebugShip04_7 |
| `BP_ES_Normal_4_8` | `Normal_4_5` | `DA_ES_Normal_4` | Enemy, DebugShip04_8 |
| `BP_ES_Normal_4_9` | `Normal_4_5` | `DA_ES_Normal_4` | Enemy, DebugShip04_9 |
| `BP_ES_Normal_4_10` | `Normal_4_3` | `DA_ES_Normal_4` | Enemy, DebugShip04_10 |
| `BP_ES_Normal_4_11` | `Normal_4_3` | `DA_ES_Normal_4` | Enemy, DebugShip04_11 |
| `BP_ES_Normal_4_12` | `Normal_4_3` | `DA_ES_Normal_4` | Enemy, DebugShip04_12 |
| `BP_ES_Normal_4_13` | `Normal_4_4` | `DA_ES_Normal_4` | Enemy, DebugShip04_13 |
| `BP_ES_Normal_4_14` | `Normal_4_4` | `DA_ES_Normal_4` | Enemy, DebugShip04_14 |
| `BP_ES_Normal_4_15` | `Normal_4_4` | `DA_ES_Normal_4` | Enemy, DebugShip04_15 |
| `BP_ES_Normal_4_16` | `Final` | `DA_ES_Normal_4` | Enemy, DebugShip04_16 |
| `BP_ES_Normal_4_17` | `Final` | `DA_ES_Normal_4` | Enemy, DebugShip04_17 |
| `BP_ES_Normal_4_18` | `Final` | `DA_ES_Normal_4` | Enemy, DebugShip04_18 |
