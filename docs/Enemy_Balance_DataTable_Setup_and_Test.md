# 적 4단계 밸런스: DataTable 설정과 테스트

## 1. 적용 구조와 범위

CSV가 밸런스 원본이고, 게임은 임포트된 Unreal DataTable을 읽는다. 단계 번호를 조건문으로 분기하거나 행 이름을 조립하지 않는다. BP·스폰 항목에서 FDataTableRowHandle을 직접 선택한다. Tier와 EnemyType은 분류 정보다.

| CSV 원본 | Unreal 에셋 (/Game/GameplayAbilitySystem/Enemy/Data/) | 역할 |
|---|---|---|
| DataTable/EnemyBaseStats.csv | DT_EnemyBaseStats | 24개 적의 체력·기본 Strength·속도 배율 |
| DataTable/EnemyCombatBalance.csv | DT_EnemyCombatBalance | 24개 적의 기본 공격 간격·첫 공격 지연·근접 동시 공격 상한 |
| DataTable/EnemyEncounterBalance.csv | DT_EnemyEncounterBalance | 4단계 무리 편성 기준, 보스 소환·강공격·돌진 설정 |

자동 반영되는 값:

- 서버 ASC 초기화 후 MaxHealth → Strength/배율 → Health=MaxHealth 순서로 기본값을 설정한다.
- Stats의 CombatSettings가 공격 간격 DT 행을 참조한다.
- 지상 웨이브 Stats Row, 갑판 Spawn Plan의 Stats Row, 보스 조우의 Boss Stats Row가 BP 기본값보다 우선한다.
- 갑판 풀 반환 시 상태이상·활성 GE·장비 효과·공격 대기시간을 정리한다. 재활성화마다 선택 행으로 체력과 기본값을 복원하고 장비를 다시 적용한다.
- 같은 플레이어를 공격하는 설정된 근거리 적은 1단계 1명, 이후 2명까지 동시에 기본 공격한다. 원거리와 보스는 이 상한에 포함하지 않는다. 갑판 근거리에도 같은 상한을 초기안으로 적용했다.
- 보스 Encounter Balance Row를 지정하면 체력 임계점마다 소환 예산을 한 번 생성한다. BT의 Summon Deck Enemy가 예산을 소비한다. 반복 보충하지 않는다.
- StrongAttackDamage는 기존 Knockback 공격, MajorAttackDamage는 기존 DashSlash 공격의 목표 피해로 연결한다. 별도 무기 보너스와 버프가 없을 때 표의 피해가 된다.
- MajorAttackTelegraphSeconds는 DashSlash의 진입+홀드 예고 시간 목표다. 진입 애니메이션 길이가 목표보다 길면 진입을 잘라내지 않고 그만큼 길게 예고한다.

에디터에서 사용자가 구성할 부분:

- 레벨별 지상 웨이브의 경로와 수량, 갑판 Spawn Plan의 클래스·포인트·수량.
- 기본 갑판 무리를 먼저 처치한 후 보스 조우를 여는 흐름.
- 보스별 공격 몽타주·콤보·패턴 순서와 소환 BT 가지의 우선순위.
- 플레이어 단계별 장비 성장. 플레이어 BP나 레벨은 이 작업에서 자동 수정하지 않는다.

Encounter DT의 Ground/Deck Count는 편성 기준이다. 값을 바꿨다고 레벨의 Spawn Plan 배열이나 웨이브가 자동 생성되지는 않는다. 아래 절차로 직접 맞춘다.

## 2. 처음 열 때

1. C++ 변경이 포함되므로 에디터를 완전히 종료하고 ArtisticSW2026Editor / Win64 / Development를 빌드한 다음 다시 연다. Live Coding만 사용하지 않는다.
2. Content Browser에서 위 Data 폴더를 열어 DT 3개가 있는지 확인한다.
3. DT_EnemyBaseStats는 24행, DT_EnemyCombatBalance는 24행, DT_EnemyEncounterBalance는 4행이어야 한다.
4. Stats의 Combat Settings에서 Data Table과 Row Name이 모두 연결되어 있는지 확인한다.
5. 기존 BP Event BeginPlay·초기화 GE가 Health/MaxHealth/Strength를 다시 덮어쓰는 경우 해당 중복 초기화 경로를 제거한다. 기본값은 DT로 관리하고 장비·버프는 기존 GE를 사용한다.

에셋이 없거나 CSV 전체를 다시 임포트할 때 Unreal Output Log의 Python 모드에서 실행한다:

```python
exec(open(r"C:\Users\wonkii\Documents\GitHub\ArtisticSW2026\Scripts\import_enemy_balance.py", encoding="utf-8").read())
```

이 스크립트는 DT를 만들고 아래 BP 기본 선택도 다시 저장한다. 이후 직접 변경한 BP 행 선택을 유지하려면 스크립트를 재실행하지 말고 각 DT의 Reimport를 사용한다.

## 3. BP와 레벨 배치 적의 기본 수치 선택

1. 적 BP를 연다.
2. Class Defaults → Enemy → Balance → Default Stats Row를 찾는다.
3. Data Table = DT_EnemyBaseStats를 선택한다.
4. Row Name을 고른다. 예: T3_Ground_Melee.
5. Compile → Save.

레벨 배치 적 한 개만 다르게 만들려면 해당 액터를 선택하고 Details에서 같은 필드를 바꾼다. 단계별 BP 복제는 필요 없다.

기본 연결:

| BP | 기본 행 |
|---|---|
| BP_MeleeEnemy | T1_Ground_Melee |
| BP_RangedEnemy | T1_Ground_Ranged |
| BP_DeckMeleeEnemy | T1_Deck_Melee |
| BP_DeckRangedEnemy | T1_Deck_Ranged |
| BP_Ship_BossEnemy | T1_Boss, Encounter T1 |
| Bosses/BP_Boss_Mid_1 | T1_Boss, Encounter T1 |
| Bosses/BP_Boss_Mid_2 | T2_Boss, Encounter T2 |
| Bosses/BP_Boss_Mid_3 | T3_Boss, Encounter T3 |
| Bosses/BP_Boss_Final | T4_Boss, Encounter T4 |

Default Stats Row와 스폰 선택이 모두 비어 있으면 기존 기본값을 유지한다. 지정한 행이 삭제되었거나 값이 유효하지 않으면 초기화 실패 로그를 남기고 신규 액터를 제거한다. 잘못된 설정을 1단계로 조용히 대체하지 않는다.

## 4. 지상 무리 4단계 구성

웨이브 데이터에서 각 Wave의 Spawn Groups를 두 종류로 나눈다.

| 단계 | 근거리 그룹: 클래스 / 행 / Count | 원거리 그룹: 클래스 / 행 / Count |
|---|---|---|
| 1 | BP_MeleeEnemy / T1_Ground_Melee / 2 | BP_RangedEnemy / T1_Ground_Ranged / 1 |
| 2 | BP_MeleeEnemy / T2_Ground_Melee / 3 | BP_RangedEnemy / T2_Ground_Ranged / 1 |
| 3 | BP_MeleeEnemy / T3_Ground_Melee / 3 | BP_RangedEnemy / T3_Ground_Ranged / 2 |
| 4 | BP_MeleeEnemy / T4_Ground_Melee / 4 | BP_RangedEnemy / T4_Ground_Ranged / 2 |

각 그룹의 Stats Row에서 DT와 행을 지정하고, 실제 레벨에 존재하는 Route Id를 지정한다. Health Multiplier와 Speed Multiplier는 1.0부터 시작한다. Enemy Level 숫자를 바꾸는 것으로 DT 행이 자동 선택되지는 않는다.

체력 특수 배율이 필요하면 Health Multiplier를 바꾼다. 예를 들어 T1_Ground_Melee + 1.2는 HP 60이다. 이미 적용된 현재 체력에 다시 곱하지 않는다. 실행 중 InitializeFromWaveSpawn을 반복 호출해 강화하거나 회복시키는 방식은 지원하지 않는다.

## 5. 기본 갑판 무리 구성

1. 해당 적 함선 BP → Deck Enemy Spawner 컴포넌트를 선택한다.
2. Enable Spawning을 켠다.
3. Spawn Plan의 항목 하나가 적 한 명이다. Enemy Class / Spawn Point Id / Stats Row를 지정한다.
4. 단계에 따라 근거리+원거리 수량을 2+1 / 2+2 / 3+2 / 3+3으로 구성한다.
5. 근거리는 Tn_Deck_Melee, 원거리는 Tn_Deck_Ranged를 선택한다.
6. 포인트는 실제 함선의 Waypoint Id를 사용한다. 중복 ID를 쓰지 않고, Spawn·Combat 사용 및 연결 포인트가 유효해야 한다.

같은 클래스라도 슬롯마다 다른 Stats Row를 선택할 수 있다. 이 선택은 풀에서 어떤 액터가 재사용되는지와 무관하게 배치 시 전달된다.

보스 소환은 이 풀의 비활성 액터를 재사용한다. 1·2단계에는 최소 원거리 1개, 3·4단계에는 최소 원거리 2개가 필요하다. Summoned Enemy Class와 Spawn Plan의 클래스는 정확히 일치해야 한다. 기본 갑판 원거리 1/2/2/3개가 사망 후 풀에 반환되면 소환 용량을 충족한다.

기본 갑판 무리 처치 여부는 Deck Enemy Spawner의 Are All Deployed Enemies Defeated로 확인할 수 있다. 보스 조우 트리거를 이 조건 이후에 열도록 레벨 로직에서 연결한다. 시체가 풀에 돌아오는 지연도 고려한다.

## 6. 보스와 소환 설정

1. 보스 BP의 Default Stats Row를 선택한다.
2. Boss → Balance → Encounter Balance Row에 DT_EnemyEncounterBalance와 T1~T4를 선택한다.
3. Summoned Enemy Class는 BP_DeckRangedEnemy로 지정한다.
4. 함선의 Boss Encounter 컴포넌트에서 Boss Class, Boss Spawn Point Id를 지정한다.
5. 이 조우만 보스 체력을 바꾸려면 Boss Stats Row를 지정한다. 비워두면 보스 BP 기본값을 사용한다. 단계 변경 시 보스 BP의 Encounter Balance Row도 함께 맞춘다.
6. 보스 BT에서 Can Summon Deck Enemy가 참일 때 Summon Deck Enemy 태스크를 실행하는 가지가 있어야 한다. 태스크 한 번으로 해당 이벤트의 1~2명을 시도한다.

| 단계 | 보스 HP / Strength | 소환 HP 비율 | 1회 / 생존 상한 | Knockback / DashSlash | 예고 목표 |
|---|---|---|---|---|---|
| 1 | 120 / 12 | 0.5 | 1 / 1 | 18 / 24 | 1.2초 |
| 2 | 225 / 17 | 0.6, 0.3 | 1 / 1 | 26 / 34 | 1.0초 |
| 3 | 360 / 23 | 0.65, 0.3 | 2 / 2 | 35 / 46 | 0.9초 |
| 4 | 550 / 30 | 0.7, 0.4 | 2 / 2 | 45 / 60 | 0.8초 |

한 임계점은 회복 후 다시 내려가도 재발동하지 않는다. 임계점에 도달했을 때 생존 상한이 꽉 차면 추가 예산을 만들지 않는다. 한 번에 여러 임계점을 통과하면 남은 생존 공간까지 합친다. 실제 BT 소환 시 풀이나 포인트가 없으면 그 시도는 보충하지 않는다. 보스가 사망한 타격에서는 소환하지 않는다.

보스 패턴 구성은 기존 Basic Attack Set과 각 Ability의 몽타주를 사용한다. 1단계 단타, 2단계 2연타, 3단계 범위·연속 공격, 4단계 패턴 조합을 에디터에서 구성한다. 2연타를 강공격으로 만들 경우 목표 총합 26이면 13+13처럼 나눈다. CSV 값만으로 몽타주나 새로운 패턴이 생성되지는 않는다.

소환 원거리의 첫 공격은 활성화 후 1.5초 이후다. 원거리 사격과 보스 대형 공격의 중첩을 피하는 전투 흐름은 BT 가지 우선순위·Wait로 조정하고 PIE에서 확인한다.

## 7. 공격력과 공격 간격 주의사항

일반 직접 피해 = max(1, 최종 Strength × 공격 계수 × 차지 배율).

- 기본 공격을 표의 Strength와 같게 만들려면 적 Weapon Registry의 해당 Weapon Definition에서 Strength Bonus=0, Combat Data의 Attack Coefficient=1로 맞춘다.
- 적 화살 BP의 Damage Data → Attack Coefficient도 1인지 확인한다.
- 보스 Basic Attack Set의 일반 단타 Attack Coefficient도 1로 맞춘다. 패턴 계수는 무기 계수와 곱해진다.
- 공유 Weapon Registry를 변경할 때에는 실제 적 Weapon Tag의 항목을 수정한다. 플레이어 무기 설정은 별도다.
- Saved/EnemyBalanceImport.json에는 적이 참조하는 무기 레지스트리와 계수를 기록한다.
- 공격 간격은 공격 시작 간 최소 시간이다. 몽타주 길이, 피격 경직, 이동, BT Wait 때문에 실제 간격은 더 길 수 있다. DT 간격만 줄였는데 빨라지지 않으면 BT와 몽타주를 확인한다.
- AttackSpeedMultiplier는 애니메이션 재생 배율이며 AttackInterval과 별개다. 둘 다 처음에는 정해진 값/1.0을 사용한다.
- 플레이어 성장 기준은 단계별 기본 피해 10/15/20/25, 최대 체력 100/120/150/180이다. 이 기준은 플레이어에 자동 적용하지 않는다.

## 8. CSV 수정과 재임포트

1. CSV에서 값을 수정하고 UTF-8 CSV로 저장한다. 컬럼 이름, Name, 구조체 문자열의 큰따옴표를 유지한다.
2. Unreal의 해당 DT를 우클릭 → Reimport → Save.
3. 새 적을 스폰하거나 갑판 적을 다시 활성화한다. 이미 전투 중인 적을 자동 회복·변경시키지 않는다.
4. DT에서 먼저 값을 시험했다면 Export as CSV로 원본에 반영한다. CSV와 uasset의 값이 달라진 채로 두지 않는다.
5. CSV와 변경된 DT/BP 에셋을 함께 버전 관리한다.

다른 폴더로 DT를 이동했다면 CSV 안 CombatSettings/SummonStats 경로도 수정한다. 에셋 리다이렉터만으로 CSV 원문이 변경되지는 않는다.

## 9. 자동 테스트

Editor → Tools/Session Frontend → Automation에서 ArtisticSW.Enemy.Balance를 검색하고 실행한다.

- Csv: 24행 임포트, 기본 값 유효성, CombatSettings 참조, CSV와 저장 DT 일치.
- Initialization: 웨이브 배율 한 번 적용, 중복 초기화로 회복되지 않음, 사용 중 행 변경 거부, 재사용 시 수치 교체, 잘못된 값 거부.
- HitCounts: 24개 행에 단계별 기준 피해를 GAS로 반복 적용해 실제 Health가 0이 되는 타수 확인. 무기 트레이스·플레이어 장비를 포함하는 테스트는 아니다.

## 10. PIE 수동 테스트

### 단일 적 수치

1. 빈 테스트 구역에 적 1명과 플레이어를 배치한다.
2. T1_Ground_Melee를 선택하고 플레이어 기본 공격 피해를 10으로 맞춘다.
3. 콘솔 `showdebug abilitysystem`과 `sw.Combat.Strength.Debug 1`을 켜고, Output Log에서 `[EnemyBalance]`를 검색한다.
4. HP 50, Strength Base 8인지 확인한다. 최종 Strength가 더 크면 무기·버프 보너스를 확인한다.
5. 기본 공격 5회에 사망하는지 확인한다. 원거리와 소환 원거리는 4회, 보스는 12회다.

| 단계 | 기준 플레이어 피해 | 근거리 처치 | 기본 원거리 처치 | 소환 원거리 처치 | 보스 처치 |
|---|---|---|---|---|---|
| 1 | 10 | 5회 | 4회 | 4회 | 12회 |
| 2 | 15 | 5회 | 4회 | 4회 | 15회 |
| 3 | 20 | 6회 | 5회 | 4회 | 18회 |
| 4 | 25 | 6회 | 5회 | 4회 | 22회 |

### 무리와 보스

1. 단계별 무리를 소환해 수량, 선택 행, 공격 간격, 같은 대상을 공격하는 근거리 공격 상한을 확인한다.
2. 1단계 보스 체력을 120→60으로 낮춘다. BT 소환 가지가 원거리 1명을 활성화해야 한다. 해당 적은 HP 40, Strength 4, 첫 공격 지연 1.5초다.
3. 소환 적을 처치한 후 같은 임계점에서 계속 보스 체력을 내려도 반복 소환되지 않아야 한다.
4. 2단계 이후에는 두 번째 임계점에서 생존 상한이 꽉 찬 경우와 비어 있는 경우를 각각 확인한다.
5. 3단계는 234 HP와 108 HP, 4단계는 385 HP와 220 HP를 통과할 때 최대 2명이다.
6. 갑판 원거리를 처치하고 풀 복귀 후 소환 원거리로 재사용한다. 이전 체력·Strength·독·둔화·공격 쿨다운이 남지 않고 새로운 행으로 회복되어야 한다.
7. 보스 Knockback/DashSlash가 표의 피해를 주는지, 예고 표시가 실제 돌진 전에 충분히 보이는지 확인한다.

### 멀티플레이

1. PIE 플레이어 수 2, Listen Server로 실행한다.
2. 서버와 클라이언트에서 같은 적의 체력·사망·소환 수를 비교한다.
3. 클라이언트 공격으로 보스 임계점을 넘겨도 소환 이벤트가 한 번만 발생해야 한다.
4. 가능하면 Dedicated Server PIE에서도 반복한다. 지연 환경에서 초기 체력바와 풀 활성화 상태를 확인한다.
5. 테스트 후 `sw.Combat.Strength.Debug 0`으로 끈다.

빌드 및 자동 테스트 실행 결과는 작업 완료 시 아래 검증 기록에 남긴다. 실제 레벨 편성·몽타주 체감·멀티플레이 PIE는 이 절차로 별도 확인한다.

## 검증 기록

작업 중. 최종 빌드·임포트·자동 테스트 결과로 갱신 예정.
