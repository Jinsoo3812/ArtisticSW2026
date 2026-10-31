# 총잡이 기본 사격 및 20초 주기 소환

대상: `/Game/GameplayAbilitySystem/Enemy/Balancing/T1/BP_T1Boss_Enemy`.

## 적용 동작

- 총잡이 전투 Task는 `GA_BossGunAttack` 기본 사격만 선택한다. BP의 Starting Abilities에서도 `GA_BossGunBurst`를 제거했다. 연사 데이터와 애니메이션은 보관한다.
- `BT_T1Boss_GunnerCombat`의 루트를 Simple Parallel로 구성했다. 주 Task는 기존 `Gunner Boss Combat`, 병행 Sequence는 `Wait(20초, 편차 0) → Summon One Deck Enemy`다.
- 전투 트리 진입 후 처음 20초에 소환을 시도하며, 이후 매 회차 20초를 기다린다. 소환 시점에 사격 준비·발사·후딜 또는 스턴/넉백 중이면 종료될 때까지 기다린다. 소환은 진행 중인 사격을 중단하지 않는다.
- 소환 Task는 기존 `BTT_SummonDeckEnemy`를 사용한다. `Wait For Boss Idle`은 이 트리에서만 켰고, 다른 보스의 기존 Task는 기본값을 유지한다.
- 새 Wait와 소환 Task에도 기존 `Maintain Deck Combat Focus` 서비스를 연결했다.
- 회차당 원거리 적 1명, 동시에 살아 있는 소환 적은 최대 2명이다. 상한에 도달하거나 유효한 갑판 소환 위치가 없으면 회차를 건너뛰고 다음 20초 대기로 돌아간다. 기존 거리·충돌·갑판 예약 검사를 유지한다.

## 실제 연결된 에셋

모두 `/Game/GameplayAbilitySystem/Enemy/Balancing/T1/` 아래에 저장되어 있으며, 직접 Blueprint를 추가로 연결할 작업은 없다.

| 에셋 | 변경 |
|---|---|
| `BP_T1Boss_Enemy` | 기본 사격만 등록, 소환 클래스/최대 인원 연결 |
| `BT_T1Boss_GunnerCombat` | 사격과 20초 소환 Sequence 병행 |
| `BP_T1Boss_SummonedRanged` | 기존 `T1_BP_DeckRangedEnemy` 복제. 원거리 역할과 기존 무기/AI 유지 |

전용 소환 적의 Default Stats Row는 `/Game/GameplayAbilitySystem/Enemy/Data/DT_EnemyBaseStat`의 `T1_BossSummon_Ranged`다. 현재 행은 체력 20, Strength 13이다.

보스의 이전 체력 구간 소환과 중복되지 않도록 Encounter Balance Row를 비웠다. 소환 주기는 BT가 소유하므로 별도 Summon Cooldown은 0이다. 기본 사격 수치·조준/발사 애니메이션·원통 표시 시간·Niagara 크기는 유지했다.

## 소환 풀 및 전투 종료

함선의 기존 풀에서 정확히 같은 클래스의 비활성 적을 먼저 찾는다. 없으면 보스의 `Additional Summon Pool Capacity` 범위에서 예비 풀을 생성한다. 이 보스는 2이며, 다른 보스의 기본값은 0이다. 새 예비 적도 기존 서버 활성화·갑판 예약·죽음·함선 정리·방 저장 경로를 사용한다.

죽은 적이 비활성 풀로 돌아오면 다음 회차에서 재사용한다. 같은 적을 재소환해도 살아 있는 인원에 중복 집계하지 않는다. 비활성 예비 적은 전투 인원으로 세지 않으며 배치 목록에서도 제거한다. 원래 경호원이 먼저 전멸해도 살아 있는 이 보스가 함선 전투를 유지한다. 보스가 죽으면 기존 소환 정리와 선원 사망 알림을 통해 종료를 평가한다.

## 조정 위치

| 조정할 값 | 위치 |
|---|---|
| 소환 간격 | `BT_T1Boss_GunnerCombat` → 배경 Sequence → Wait → **Wait Time** (현재 20) |
| 동시 생존 상한 | `BP_T1Boss_Enemy` → Class Defaults → Boss / Summon → **Max Summoned Deck Enemies** |
| 추가 풀 크기 | 같은 위치의 **Additional Summon Pool Capacity**. 인원 상한을 늘리면 함께 조절 |
| 소환 적 클래스 | `BP_T1Boss_Enemy` → **Summoned Enemy Class** |
| 소환 적 스탯 | `BP_T1Boss_SummonedRanged` → **Default Stats Row**, 또는 연결된 데이터 테이블 행 |

## 재현 및 검증

- 적용: Editor 빌드 후 `Scripts/author_gunner_basic_and_periodic_summon.py`.
- 변경 전 BP/BT 복사본: `Saved/GunnerPeriodicSummonBackup`.
- 저장 재로드 검사: `Scripts/audit_gunner_periodic_summon.py`. 기본 사격·BT 구조/간격·원거리 클래스/스탯을 확인하고, 기존 애니메이션/원통/Niagara/Focus 연결도 검사한다.
- 자동 검사: `ArtisticSW.Enemy.GunnerPeriodicSummon` 및 `ArtisticSW.Enemy.ShotFocus`.
- 실제 PIE 화면과 멀티플레이 플레이 검증은 별도 확인 대상이다.

2026-10-31 검증 결과: `Saved/GunnerPeriodicFinalBuild.log` Editor Win64 Development 빌드 성공. `Saved/GunnerPeriodicAudit.log`에서 저장된 20초 BT·기본 사격·원거리 소환 BP 및 기존 연출 연결을 재로드 확인했다. 9개 전투 트리의 총 120개 Task Focus 설정도 유효하다. `Saved/GunnerPeriodicTestsReport/index.json`의 자동 검사 3개가 모두 성공했다. 소환 검사는 사격 중 대기, 기존 원거리 풀 없이 생성, 동시 2명 상한, 비활성 적 재사용과 중복 집계 방지, 예비 풀 저장 ID, 배치 기록 정리, 보스 생존 중 조우 유지와 사망 후 전투 종료를 포함한다.
