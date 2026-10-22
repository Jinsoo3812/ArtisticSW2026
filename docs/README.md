# 구현 문서 안내

최신 정리: 2026-10-08. 이번 변경과 관련된 갑판 적 생명주기·저장/복제·근접 전투·PCG·함선 편성 문서를 현재 C++ 및 저장 레벨과 대조했다. 문서의 작성일·검증 범위를 함께 확인한다. 이번 점검과 관계없는 시스템 문서 전체의 런타임 재검증을 의미하지 않는다.

## 현재 구현과 설정

| 내용 | 문서 |
| --- | --- |
| 갑판 적 공통 책임·스폰·풀·복제 | [DeckEnemy 공통 구조](DeckEnemy_Core.md) |
| 휴면/Sight/슬롯/Room 복원 수동 확인 | [스폰 생명주기 검증](Deck_Enemy_Spawn_Refactoring_Editor_Test_Guide.md) |
| 원인 수정과 구현된 계약·남은 검증 | [생명주기 구현 상태](../Planning/Enemy%20On%20Ship/Deck_Enemy_Spawn_Lifecycle_Refactoring_Plan_2026-10-07.md) |
| 보행면 이동·스폰 구조 | [DeckWalk 구현](DeckWalk_Implementation.md) |
| 앵커·편성·보스 에디터 설정 | [앵커 설정 가이드](DeckWalk_Manual_Anchor_Editor_Setup.md) |
| 전투 BT와 쿨타임 재배치 | [Deck 전투 BT](DeckEnemy_Combat_BT_Design_2026-10-04.md) |
| Enemy 공통 기능·스탯 저장 | [BaseEnemy 공통 구조](BaseEnemy_Core.md) |
| Strength 피해 경로와 검증 기록 | [Strength 전투 가이드](Strength_Combat_Implementation_and_Editor_Guide.md) |
| 검 피격 후보·거절 사유 진단 | [검 피격 진단](Deck_Melee_Hit_Diagnosis.md) |
| 피격 연출·Capsule/Physics Asset 정책 | [Damage feedback and hurtboxes](Damage_Feedback_And_Animated_Hurtbox.md) |
| PCG Profile 적용 상태·PIE 절차 | [PCG 적용 가이드](PCG_Combat_Fix_Editor_Check.md) |
| PCG 생성·충돌 보존 근거 | [PCG 영향 검토](PCG_Biome_Collision_Impact_Review_2026-10-07.md) |
| 최신 LV_ET/Lvl_CY 편성·앵커 비교 | [레벨 비교](LV_ET_vs_Lvl_CY_EnemySpawn_Audit.md) |
| Lvl_CY 상세 스탯·배치와 조사 시점 | [Lvl_CY 설정 기록](Lvl_CY_EnemyShip_Editor_Setup_Audit.md) |

## 과거 진단과 설계 문서

[스폰 실패 진단](LV_ET_EnemySpawn_Pipeline_Diagnosis.md)은 리팩터링 전 휴면/풀 스탯 문제의 재현 기록이다. 그 실패 흐름을 현재 구현 동작으로 읽지 않는다. Planning의 설계 문서는 계획·구현 상태를 확인하고 해당 현재 구현 가이드를 우선한다. 과거 Test_Level 노드 수·BP 설정과 테스트 성공 기록은 그 실행 시점에 한정된다.

## 이번 점검에서 확인한 상태

- `ArtisticSW2026Editor / Win64 / Development` 빌드 성공.
- 두 저장 레벨 각각 EnemyShip 51척, BossSpawnPoint=20, Boss Encounter=false. LV_ET 티어 편성은 이름에 맞게 정리되어 있다.
- PCG_Biome Profile은 LV_ET=`PCGVolumeBounds`, Lvl_CY=`Custom`. Lvl_CY 추가 적용과 실제 피해/벽 차단 PIE가 남아 있다.
- 2026-10-07 실제 검 BP Capsule 테스트는 성공 기록이 있다. 기존 SharedHitResolver/MeleePayload 실패는 피격 진단 문서에 보존했다. 이번 정리에서는 전투 자동화를 재실행하지 않았다.
- 읽기 전용 레벨 조회 중 기존 `GCNS_Rogue_Arrival` BP 컴파일 오류가 기록되었다. C++ 빌드 성공을 에셋 전체 검증 성공으로 해석하지 않는다.
- 생명주기 변경의 실제 Sight·휴면·귀환·보스·멀티플레이·Room 복원 및 성능 측정은 남은 검증이다.

로컬 Saved 진단·백업은 Git 제외 경로다. 저장 결과의 핵심은 각 문서에 기록했고 임시 검사 파일은 푸시에 포함하지 않는다.
