# BP_T1Boss_Enemy 총잡이 연결 및 수동 작업

대상: `/Game/GameplayAbilitySystem/Enemy/Balancing/T1/BP_T1Boss_Enemy`

현재 공격 선택은 기본 사격만 사용한다. 전투 BT에서 기존 원거리 적 소환 Task를 20초 주기로 병행한다. 최신 구조·연결 에셋·조절 위치는 [기본 사격 및 주기 소환](Gunner_Basic_Attack_and_Periodic_Summon.md)을 우선한다.

최신 발사 표현: 발사 0.5초 전부터 공격 완료까지 주황 원통이 현재 총구와 대상/함선의 로컬 끝점을 연결한다. 공격 완료 시 원통도 즉시 제거한다. 초기 조준은 숨기며, 후딜은 단발 0.35초/연사 0.5초로 단축했다. 총구 Niagara는 발사 때만 표시한다. 아래 초기 기록보다 [발사 효과·갑판 Focus 설정](Gunner_Shot_Cylinder_Niagara_and_Deck_Focus.md)을 우선한다.

최초 연결 시 에셋 조사: 메시 `SKM_Warrior3`, `Gun_Grip` 없음, `Gun_Muzzle` 있음, Default Stats Row 비어 있음이었다. 후속 사용자 변경과 구분한다. 기존 총구 소켓은 위치가 실제 총과 맞는지 확인해야 한다.

## 2026-10-31 조준·발사 애니메이션 적용

`/Game/Characters/Mannequins/Anims/Rifle`의 클립을 바탕으로 아래 자산을 `T1/Animations`에 생성하고 `BP_T1Boss_Enemy` 및 현재 Combat Data에 연결했다.

- `AS_T1Boss_RifleAim`: `MF_Rifle_Idle_ADS`의 전용 복사본.
- `AS_T1Boss_RifleFire`: `MM_Rifle_Fire`의 원본 키를 사용하는 전용 일반 포즈 복사본. 원본은 Mesh Space Additive로 유지한다. 조준과 같은 FullBody 트랙에서 사용하기 위해 복사본만 비 Additive로 저장했다.
- `AM_T1Boss_RifleShot`: Aim → Fire → Recover 섹션. 준비/후딜은 GA가 유지하며 발사 클립은 원래 길이로 재생한다.
- `AM_T1Boss_RifleBurst`: 같은 구성. 발사 동작 전체가 발 사이 간격에 들어가도록 Fire 세그먼트 재생 속도만 조정한다.
- `ABP_T1Boss_Rifle`: 기존 보스 AnimBP 복사본에 최종 출력 직전 `GunnerFullBody` Slot을 추가했다. 기존 이동 그래프는 보존한다. 보스의 Anim Class에 연결했다.

공유 `SK_Mannequin`에는 슬롯 이름 `GunnerFullBody`를 등록했다. 원본 Rifle 시퀀스 및 원본 AnimBP는 수정하지 않았다. 발사 피해는 기존 서버 타이머가 처리하며 새 피해 Notify는 없다.

조준선은 Tracking/Locked 준비 시간에만 빨강으로 보인다. 발사 확정 즉시 준비선을 숨기고 주황색 발사선을 0.18초 표시한다. 발사선은 확정된 시작/끝을 사용해 플레이어를 따라가지 않는다. 대기·이동·발 사이 대기·후딜에는 준비선을 표시하지 않는다. LockTime=0이던 설정은 준비 시간 범위 내에서 단발 0.6초/연사 0.35초의 조준 고정 구간을 갖도록 했다.

재현: Editor 빌드 후 `Scripts/author_t1_gunner_animations.py`. 재로드 검사: `Scripts/audit_t1_gunner_animations.py`. 애니메이션 적용 전 BP/Combat Data/Skeleton 복사본은 `Saved/GunnerAnimationBackup`에 있다. 실제 플레이에서의 자세·총구 정렬과 네트워크 시각 검사는 별도로 확인해야 한다.

검증: Editor Development 빌드 성공(`Saved/GunnerAnimationFinalBuild.log`), 애니메이션 생성 및 BP 컴파일/저장 성공(`Saved/GunnerAnimationAuthor.log`), 새 프로세스에서 Anim Class·두 몽타주·섹션·Slot·색상·원본 Additive 보존 검사 통과(`Saved/GunnerAnimationAudit.log`, `T1_GUNNER_ANIMATION_RELOAD_AUDIT_OK`). PIE 시각 검증은 미실행.

## 전용 에셋 연결

`GunnerBossAuthoring` 에디터 Commandlet은 아래 전용 에셋을 만들고 대상 BP에 연결한다. 기존 메시/AnimBP/스탯/스토리 설정은 보존한다. 공용 DA_Weapon과 기존 보스 전투 트리는 저장하지 않는다.

모든 신규 에셋의 폴더는 `/Game/GameplayAbilitySystem/Enemy/Balancing/T1`이다.

| 에셋 | 역할 및 연결 |
|---|---|
| BP_T1Boss_Gun | BaseWeapon 자식, Vetterli Rifle, 충돌/물리 끔 |
| T1_DA_GunnerWeapons | Gun 태그 → BP_T1Boss_Gun, 장착 소켓 Gun_Grip, Granted Abilities 비움 |
| T1_DA_GunnerCombat | 단발/3연사·사거리·조준선·연출 설정 |
| BT_T1Boss_GunnerCombat | Simple Parallel: Gunner Boss Combat + (Wait 20초 → Summon One Deck Enemy). 각 Task Focus Service 적용 |
| T1_DA_GunnerAI | 기존 T1 BehaviorSet 복제, Combat만 전용 트리로 변경 |

대상 BP: Combat Data/Weapon Registry/Behavior Set을 위 자산으로 연결한다. Behavior Tree는 `BT_BossBase`, Default Weapon Tag는 `Item.EnemyWeapon.Gun`, Equip Weapon On Spawn은 true다. Starting Abilities의 공격은 기본 사격 `GA_BossGunAttack`만 사용한다.

전용 에셋이 이미 있으면 전투 수치·소켓명·메시 조정은 보존한다. 전용 Registry의 Gun 항목과 BP 연결은 재설정한다. 재실행 시 Blueprint를 닫고 저장한 뒤 진행한다.

## 직접 넣거나 조정할 항목 — 순서대로

| 위치 | 사용자가 넣을 것 | 확인할 사항 |
|---|---|---|
| BP_T1Boss_Enemy → Mesh | 적용된 ABP_T1Boss_Rifle 자세 확인 | Anim Class 연결 완료. 필요 시 소총 이동/상하 조준을 추가 보완 |
| 보스 캐릭터의 Skeleton 또는 Mesh Socket | 손 본의 `Gun_Grip` | 총의 위치/회전/크기를 실제 손에 맞춤. 공유 Skeleton 변경을 피하려면 Mesh Socket 사용 |
| 같은 캐릭터 Mesh에서 조회되는 Socket | `Gun_Muzzle` | 장착 총의 실제 총구에 맞춤. 총 Static Mesh에만 만들면 현재 사격 원점에서 찾지 못함 |
| T1_DA_GunnerCombat → Basic / Burst → Montage | 연결된 단발/연사 몽타주 자세 확인 | 몽타주와 Aim/Fire/Recover 연결 완료 |
| AnimBP | 필요 시 상하 조준/Aim Offset/왼손 IK | FullBody Slot 연결 완료. 총신 방향과 Trace 정합성 확인 |
| T1_DA_GunnerCombat → Shot Sound | 소총용 Sound Wave 또는 Sound Cue | 확정 발사마다 재생. 거리 감쇠는 음원 자산에 설정 |
| T1_DA_GunnerCombat → Muzzle Flash | Niagara System | +X가 사격 방향. 확정 총구 위치에서 1회 생성되므로 자동 종료되는 효과 사용 |
| T1_DA_GunnerCombat → Impact Cue | 필요하면 기존 피격 GameplayCue 태그 | 일반 피격 피드백과 중복 여부 확인 |
| BP_T1Boss_Enemy → Default Stats Row | 총잡이용 EnemyBaseStatsRow | 체력·Strength 결정. 발사 계수는 Basic 1.0 / Burst 발당 0.7 |
| 테스트 함선 → BossEncounter → Boss Class | BP_T1Boss_Enemy | 유효 Boss Spawn Point와 DeckWalkArea. 정식 InitializeBoss 경로 사용 |
| 보스/함선 캠페인 설정 | 등장 티어·스토리 노드·보상 | 실제 보스만 보상에 등록, 경호원 잔존 시 잠금 유지 |
| 캐릭터 Hurtbox / 엄폐물 충돌 | WeaponAim 채널 Block | 벽·난간에서 선과 피해 판정이 함께 막히는지 확인 |

총 BP에 발사 Event, Projectile 또는 피해 AnimNotify를 추가하지 않는다. 서버 GA가 발사·피해를 처리한다. 빨간/주황 조준선은 코드로 표시하므로 AimLine BP, Warning Mesh, Niagara 조준선은 필요 없다.

## 초기 전투 규칙

- 단발: 준비 2초(마지막 0.6초 고정), 후딜 1.2초, 다음 공격 대기 0.6초.
- 연사: 매 발 준비 1초(마지막 0.35초 고정), 3발, 발 사이 0.2초, 마지막 후딜 2.5초, Commit 기준 쿨다운 12초.
- 현재 정책은 연사 우선이다. 단발만 관찰하려면 테스트용 Combat Task/Starting Abilities에서 연사를 일시 제외한 뒤 복구한다.
- 준비·발 사이·후딜에는 이동을 잠그고 피해는 받는다. 스턴·넉백·사망·표적 상실은 공격을 중단한다.
- 조준 준비선 빨강, 발사선 주황(0.18초). 고정 방향은 배의 로컬 공간이라 배가 회전하면 월드 사선도 회전한다.
- 거리: 후퇴 진입 400 / 선호 700 / 접근 진입 1,000cm. 사거리 1,600cm.
- 소리/섬광은 서버 확정 발사에서 각 화면으로 전달되는 일회성 연출이다. 늦게 참가한 플레이어에게 과거 발사음을 재생하지 않는다. 네트워크 손실 시 연출이 생략될 수 있으며 피해에는 영향이 없다.

## 수동 플레이 완료 기준

1. 단발 1회 피해, 고정 후 옆 이동 회피, 엄폐물 차단.
2. 연사 정확히 3발, 같은 발 중복 피해 없음, 긴 후딜 이후 정상 행동.
3. 갑판 끝/막힌 경로에서 추락·무한 재탐색 없음, 움직이는 배에서도 총구/선/명중 일치.
4. 준비/연사/후딜 중 스턴·넉백·사망 시 추가 발사·조준선·영구 이동 잠금 없음.
5. 풀 반납/재접근/저장 복원 뒤 이전 예약 발사나 유령 무기 없음. 보상 중복 없음.
6. 2인 PIE → 전용 서버 → 패키지에서 선·총·몽타주·음향·피해 확인.

에셋 생성과 BP 컴파일 성공은 위 플레이 완료 기준 통과를 의미하지 않는다. 애니메이션 연결 이후에도 음향·소켓 정렬·정식 조우 배치 및 시각 검증은 별도로 확인한다.

## 재현과 검증 기록

에디터를 닫고 Editor Development 빌드 후 다음과 같이 재연결할 수 있다. 대상 BP의 연결 전 복사본은 `Saved/BP_T1Boss_Enemy.before-gunner-authoring.uasset`에 보관했다.

```powershell
& 'C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' `
  "$PWD/ArtisticSW2026.uproject" -run=GunnerBossAuthoring -unattended -nullrhi -nop4
```

`Scripts/inspect_t1_gunner.py`는 새 에디터 프로세스에서 저장된 BP/DA 참조, 단발·연사 각각 1개, 기존 보스 공격 제거, Registry와 Combat Subtree 연결을 확인한다. 플레이 테스트를 실행하는 스크립트는 아니다.

2026-10-31 검증 결과:

- Editor Win64 Development 빌드 성공 (`Saved/T1GunnerAuthoringBuild.log`).
- 전용 5개 에셋 생성 및 대상 BP 컴파일/저장 성공. 재실행으로 기존 전용 에셋도 정상 재사용 (`Saved/T1GunnerAuthoring.log`).
- 새 프로세스에서 저장 에셋 재로드 검사 성공: `T1_GUNNER_SAVED_BINDINGS_OK` (`Saved/T1GunnerInspect.log`). Starting Abilities는 단발/연사 2개다.
- 최초 Anim Class는 `ABP_Unarmed`였다. 후속 애니메이션 작업에서 `ABP_T1Boss_Rifle`과 공격 몽타주 Slot을 연결했다. 상하 조준/그립 정렬은 실제 장면에서 추가 확인한다.
- PIE, 멀티플레이, 전용 서버, 패키지 전투 검증은 실행하지 않았다.
