# Lvl_CY EnemyShip 배치 안내

백업 `lv-cy-backup` (`c57708e1`), 작업 전 `350364a3`, 현재 `WonjunJang` (`91c34087`) 레벨을 읽어서 비교한 결과입니다.

EnemyShip은 작업 전 51척, 백업 51척, 현재 51척입니다.
현재 기준: BP 교체 50척, 새 배치 1척, 기존 BP 사용 0척.

## 에디터에서 할 작업

1. 현재 WonjunJang의 원본 프로젝트에서 `/Game/Level/Lvl_CY`를 엽니다. Saved 아래 검사 프로젝트를 열지 않습니다.
2. 표의 Blueprint를 콘텐츠 브라우저에서 찾아 배치합니다. BP 교체 항목은 기존 BP_EnemyShip을 그대로 두는 방식으로 완료되지 않습니다.
3. World Outliner 이름을 표의 액터 이름과 정확히 맞춥니다. 같은 이름의 기존 액터는 먼저 별도 이름으로 바꾸거나 교체하여 중복을 피합니다.
4. 위치·회전·크기는 원하는 상태로 배치합니다. 백업 좌표는 참고값이며 자동 적용 시 현재 배치를 유지합니다.
5. 기존 액터를 교체할 때 다른 액터/레벨 블루프린트의 참조가 있으면 새 액터로 다시 연결합니다. 새 액터 확인이 끝나면 교체한 구형 액터를 제거하여 중복 배치를 남기지 않습니다.
6. 아래 별도 확인 항목까지 처리한 뒤 저장하고 에디터를 닫습니다. 이후 설정 적용 전 이름·BP 종류를 다시 검사합니다.

Squad ID, Archetype, 태그, 상자 설정 및 검사 대상 컴포넌트 설정은 JSON에 보관했습니다. 이번 단계에서 전부 수동 입력할 필요는 없습니다.
`SWRoomStableId=...` 태그는 저장 시스템의 액터 식별값이므로 복사하지 않습니다. 액터 이름 대신 쓰는 값도 아닙니다.
비교에는 해당 BP에서 상속받은 기본 설정의 차이도 포함됩니다. 같은 BP 종류를 먼저 맞춘 뒤 인스턴스 설정을 적용해야 합니다.

## 배치 목록

Blueprint 열은 `/Game/GameplayAbilitySystem/Enemy/Balancing/Final/` 아래의 상대 경로입니다. N1과 N2에 같은 파일 이름의 BP가 있으므로 폴더까지 확인합니다.

| 처리 | World Outliner 액터 이름 | 배치할 Blueprint (Final 기준 경로) | Squad ID | Archetype | Actor Tags (식별값 제외) |
|---|---|---|---|---|---|
| BP 교체 | `BP_ES_Final` | `BP_ES_Final` | `Final` | `DA_ES_TimeStop` | Enemy, DebugShip07 |
| BP 교체 | `BP_ES_Mid_1` | `BP_ES_Mid_1` | `Normal_1_4` | `DA_ES_Charge` | Enemy, DebugShip_M_1 |
| 새 배치 | `BP_ES_Mid_3` | `BP_ES_Mid_3` | `Normal_3_3` | `DA_ES_Torpedo_Obstacle` | Enemy, DebugShip_M_3 |
| BP 교체 | `BP_ES_Normal_1_1` | `N1/BP_ES_Normal_MMR` | `Normal_1_1` | `DA_ES_Normal_1` | Enemy, DebugShip01_1 |
| BP 교체 | `BP_ES_Normal_1_2` | `N1/BP_ES_Normal_MMR` | `Normal_1_1` | `DA_ES_Normal_1` | Enemy, DebugShip01_2 |
| BP 교체 | `BP_ES_Normal_1_3` | `N1/BP_ES_Normal_MRR` | `Normal_1_1` | `DA_ES_Normal_1` | Enemy, DebugShip01_3 |
| BP 교체 | `BP_ES_Normal_1_4` | `N1/BP_ES_Normal_MMR` | `Normal_1_2` | `DA_ES_Normal_1` | Enemy, DebugShip01_4 |
| BP 교체 | `BP_ES_Normal_1_5` | `N1/BP_ES_Normal_MMR` | `Normal_1_2` | `DA_ES_Normal_1` | Enemy, DebugShip01_5 |
| BP 교체 | `BP_ES_Normal_1_6` | `N1/BP_ES_Normal_RRR` | `Normal_1_2` | `DA_ES_Normal_1` | Enemy, DebugShip01_6 |
| BP 교체 | `BP_ES_Normal_1_7` | `N1/BP_ES_Normal_MMR` | `Normal_1_4` | `DA_ES_Normal_1` | Enemy, DebugShip01_7 |
| BP 교체 | `BP_ES_Normal_1_8` | `N1/BP_ES_Normal_RRR` | `Normal_1_4` | `DA_ES_Normal_1` | Enemy, DebugShip01_8 |
| BP 교체 | `BP_ES_Normal_1_9` | `N1/BP_ES_Normal_RRR` | `Normal_1_4` | `DA_ES_Normal_1` | Enemy, DebugShip01_9 |
| BP 교체 | `BP_ES_Normal_1_10` | `N1/BP_ES_Normal_MMR` | `Normal_1_3` | `DA_ES_Normal_1` | Enemy, DebugShip01_10 |
| BP 교체 | `BP_ES_Normal_1_11` | `N1/BP_ES_Normal_MMR` | `Normal_1_3` | `DA_ES_Normal_1` | Enemy, DebugShip01_11 |
| BP 교체 | `BP_ES_Normal_1_12` | `N1/BP_ES_Normal_MMR` | `Normal_1_3` | `DA_ES_Normal_1` | Enemy, DebugShip01_12 |
| BP 교체 | `BP_ES_Normal_2_1` | `N2/BP_ES_Normal_MMR` | `Normal_2_1` | `DA_ES_Normal_2` | Enemy, DebugShip02_1 |
| BP 교체 | `BP_ES_Normal_2_2` | `N2/BP_ES_Normal_MMR` | `Normal_2_1` | `DA_ES_Normal_2` | Enemy, DebugShip02_2 |
| BP 교체 | `BP_ES_Normal_2_3` | `N2/BP_ES_Normal_MMR` | `Normal_2_1` | `DA_ES_Normal_2` | Enemy, DebugShip02_3 |
| BP 교체 | `BP_ES_Normal_2_4` | `N2/BP_ES_Normal_MMR` | `Normal_2_2` | `DA_ES_Normal_2` | Enemy, DebugShip02_4 |
| BP 교체 | `BP_ES_Normal_2_5` | `N2/BP_ES_Normal_MRR` | `Normal_2_2` | `DA_ES_Normal_2` | Enemy, DebugShip02_5 |
| BP 교체 | `BP_ES_Normal_2_6` | `N2/BP_ES_Normal_MRR` | `Normal_2_2` | `DA_ES_Normal_2` | Enemy, DebugShip02_6 |
| BP 교체 | `BP_ES_Normal_2_7` | `N2/BP_ES_Normal_MMR` | `Normal_2_3` | `DA_ES_Normal_2` | Enemy, DebugShip02_7 |
| BP 교체 | `BP_ES_Normal_2_8` | `N2/BP_ES_Normal_MMR` | `Normal_2_3` | `DA_ES_Normal_2` | Enemy, DebugShip02_8 |
| BP 교체 | `BP_ES_Normal_2_9` | `N2/BP_ES_Normal_RRR` | `Normal_2_3` | `DA_ES_Normal_2` | Enemy, DebugShip02_9 |
| BP 교체 | `BP_ES_Normal_3_1` | `N3/BP_ES_Normal_3MMR` | `Normal_3_1` | `DA_ES_Normal_3` | Enemy, DebugShip03_1 |
| BP 교체 | `BP_ES_Normal_3_2` | `N3/BP_ES_Normal_3RRR` | `Normal_3_1` | `DA_ES_Normal_3` | Enemy, DebugShip03_2 |
| BP 교체 | `BP_ES_Normal_3_3` | `N3/BP_ES_Normal_3RRR` | `Normal_3_1` | `DA_ES_Normal_3` | Enemy, DebugShip03_3 |
| BP 교체 | `BP_ES_Normal_3_4` | `N3/BP_ES_Normal_3MMR` | `Normal_3_2` | `DA_ES_Normal_3` | Enemy, DebugShip03_4 |
| BP 교체 | `BP_ES_Normal_3_5` | `N3/BP_ES_Normal_3MRR` | `Normal_3_2` | `DA_ES_Normal_3` | Enemy, DebugShip03_5 |
| BP 교체 | `BP_ES_Normal_3_6` | `N3/BP_ES_Normal_3MRR` | `Normal_3_2` | `DA_ES_Normal_3` | Enemy, DebugShip03_6 |
| BP 교체 | `BP_ES_Normal_3_7` | `N3/BP_ES_Normal_3MMR` | `Normal_3_3` | `DA_ES_Normal_3` | Enemy, DebugShip03_7 |
| BP 교체 | `BP_ES_Normal_3_8` | `N3/BP_ES_Normal_3MMR` | `Normal_3_3` | `DA_ES_Normal_3` | Enemy, DebugShip03_8 |
| BP 교체 | `BP_ES_Normal_3_9` | `N3/BP_ES_Normal_3RRR` | `Normal_3_3` | `DA_ES_Normal_3` | Enemy, DebugShip03_9 |
| BP 교체 | `BP_ES_Normal_4_1` | `N4/BP_ES_Normal_4MMR` | `Normal_4_1` | `DA_ES_Normal_4` | Enemy, DebugShip04_1 |
| BP 교체 | `BP_ES_Normal_4_2` | `N4/BP_ES_Normal_4MMR` | `Normal_4_1` | `DA_ES_Normal_4` | Enemy, DebugShip04_2 |
| BP 교체 | `BP_ES_Normal_4_3` | `N4/BP_ES_Normal_4MRR` | `Normal_4_1` | `DA_ES_Normal_4` | Enemy, DebugShip04_3 |
| BP 교체 | `BP_ES_Normal_4_4` | `N4/BP_ES_Normal_4MRR` | `Normal_4_2` | `DA_ES_Normal_4` | Enemy, DebugShip04_4 |
| BP 교체 | `BP_ES_Normal_4_5` | `N4/BP_ES_Normal_4RRR` | `Normal_4_2` | `DA_ES_Normal_4` | Enemy, DebugShip04_5 |
| BP 교체 | `BP_ES_Normal_4_6` | `N4/BP_ES_Normal_4RRR` | `Normal_4_2` | `DA_ES_Normal_4` | Enemy, DebugShip04_6 |
| BP 교체 | `BP_ES_Normal_4_7` | `N4/BP_ES_Normal_4MMR` | `Normal_4_5` | `DA_ES_Normal_4` | Enemy, DebugShip04_7 |
| BP 교체 | `BP_ES_Normal_4_8` | `N4/BP_ES_Normal_4MMR` | `Normal_4_5` | `DA_ES_Normal_4` | Enemy, DebugShip04_8 |
| BP 교체 | `BP_ES_Normal_4_9` | `N4/BP_ES_Normal_4MMR` | `Normal_4_5` | `DA_ES_Normal_4` | Enemy, DebugShip04_9 |
| BP 교체 | `BP_ES_Normal_4_10` | `N4/BP_ES_Normal_4MMR` | `Normal_4_3` | `DA_ES_Normal_4` | Enemy, DebugShip04_10 |
| BP 교체 | `BP_ES_Normal_4_11` | `N4/BP_ES_Normal_4MRR` | `Normal_4_3` | `DA_ES_Normal_4` | Enemy, DebugShip04_11 |
| BP 교체 | `BP_ES_Normal_4_12` | `N4/BP_ES_Normal_4MRR` | `Normal_4_3` | `DA_ES_Normal_4` | Enemy, DebugShip04_12 |
| BP 교체 | `BP_ES_Normal_4_13` | `N4/BP_ES_Normal_4MMR` | `Normal_4_4` | `DA_ES_Normal_4` | Enemy, DebugShip04_13 |
| BP 교체 | `BP_ES_Normal_4_14` | `N4/BP_ES_Normal_4MMR` | `Normal_4_4` | `DA_ES_Normal_4` | Enemy, DebugShip04_14 |
| BP 교체 | `BP_ES_Normal_4_15` | `N4/BP_ES_Normal_4MMR` | `Normal_4_4` | `DA_ES_Normal_4` | Enemy, DebugShip04_15 |
| BP 교체 | `BP_ES_Normal_4_16` | `N4/BP_ES_Normal_4MMR` | `Final` | `DA_ES_Normal_4` | Enemy, DebugShip04_16 |
| BP 교체 | `BP_ES_Normal_4_17` | `N4/BP_ES_Normal_4MRR` | `Final` | `DA_ES_Normal_4` | Enemy, DebugShip04_17 |
| BP 교체 | `BP_ES_Normal_4_18` | `N4/BP_ES_Normal_4MRR` | `Final` | `DA_ES_Normal_4` | Enemy, DebugShip04_18 |

## 현재에만 있는 EnemyShip

- `BP_ES_Normal_3_10` — 현재 BP: `BP_EnemyShip`. 백업 목록에는 없으므로 중복/불필요한 액터인지 확인하고 처리합니다.

백업 구성에는 `BP_ES_Normal_3_10`이 없고 `BP_ES_Mid_3`가 있습니다. 백업과 같은 구성을 만들려면 기존 Normal_3_10을 남긴 채 Mid_3를 추가하여 총 52척이 되지 않도록 처리합니다.

## Blueprint 위치

| Blueprint | 콘텐츠 브라우저 경로 | 수량 |
|---|---|---|
| `BP_ES_Final` | `/Game/GameplayAbilitySystem/Enemy/Balancing/Final/BP_ES_Final` | 1 |
| `BP_ES_Mid_1` | `/Game/GameplayAbilitySystem/Enemy/Balancing/Final/BP_ES_Mid_1` | 1 |
| `BP_ES_Mid_3` | `/Game/GameplayAbilitySystem/Enemy/Balancing/Final/BP_ES_Mid_3` | 1 |
| `BP_ES_Normal_MMR` | `/Game/GameplayAbilitySystem/Enemy/Balancing/Final/N1/BP_ES_Normal_MMR` | 8 |
| `BP_ES_Normal_MRR` | `/Game/GameplayAbilitySystem/Enemy/Balancing/Final/N1/BP_ES_Normal_MRR` | 1 |
| `BP_ES_Normal_RRR` | `/Game/GameplayAbilitySystem/Enemy/Balancing/Final/N1/BP_ES_Normal_RRR` | 3 |
| `BP_ES_Normal_MMR` | `/Game/GameplayAbilitySystem/Enemy/Balancing/Final/N2/BP_ES_Normal_MMR` | 6 |
| `BP_ES_Normal_MRR` | `/Game/GameplayAbilitySystem/Enemy/Balancing/Final/N2/BP_ES_Normal_MRR` | 2 |
| `BP_ES_Normal_RRR` | `/Game/GameplayAbilitySystem/Enemy/Balancing/Final/N2/BP_ES_Normal_RRR` | 1 |
| `BP_ES_Normal_3MMR` | `/Game/GameplayAbilitySystem/Enemy/Balancing/Final/N3/BP_ES_Normal_3MMR` | 4 |
| `BP_ES_Normal_3MRR` | `/Game/GameplayAbilitySystem/Enemy/Balancing/Final/N3/BP_ES_Normal_3MRR` | 2 |
| `BP_ES_Normal_3RRR` | `/Game/GameplayAbilitySystem/Enemy/Balancing/Final/N3/BP_ES_Normal_3RRR` | 3 |
| `BP_ES_Normal_4MMR` | `/Game/GameplayAbilitySystem/Enemy/Balancing/Final/N4/BP_ES_Normal_4MMR` | 10 |
| `BP_ES_Normal_4MRR` | `/Game/GameplayAbilitySystem/Enemy/Balancing/Final/N4/BP_ES_Normal_4MRR` | 6 |
| `BP_ES_Normal_4RRR` | `/Game/GameplayAbilitySystem/Enemy/Balancing/Final/N4/BP_ES_Normal_4RRR` | 2 |

## 저장된 자료

- `EnemyShip_Transfer.json`: 액터별 대상 BP, 전체 추출 설정, 변경 전/후 값, 현재 액터 정보. 자동 적용의 입력 자료.
- `EnemyShip_Settings_Detail.md`: 액터별 핵심 설정, 변경 항목, 백업 좌표.
- `backup_snapshot.json`, `base_snapshot.json`, `current_snapshot.json`: 읽기 전용 추출 원본.

추출은 분리된 검사 프로젝트에서 수행했으며 원본 레벨을 저장하지 않았습니다. 레벨 로드와 추출 성공을 확인했으며 플레이 테스트는 수행하지 않았습니다.
BP 에셋 내부, 다른 액터, 머티리얼/물리 컴포넌트 전체를 복제하는 자료는 아닙니다. 범위는 EnemyShip/Ship 편집 속성과 갑판·보스·항법 컴포넌트의 편집 설정입니다.
