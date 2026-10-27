# Samurai Boss 시간 조정

C++ 빌드 후 에디터를 다시 열면 기존 Blueprint에서도 새 설정이 표시된다.

## 기절

`/Game/GameplayAbilitySystem/Enemy/Balancing/T4/T4_BP_ShipBoss_Samurai`의 **Class Defaults → Boss → Status**:

- **Head Hit Stun Duration**: 공격 중 머리 피격 기절. 기본 **4초**.
- **Health Threshold Stun Duration**: 체력 임계치 기절. 기본 **4초**.

선택된 GameplayEffect의 기본 지속 시간 대신 위 값을 서버에서 적용한다. 기절 면역, 사망 시 해제, 기절 중 재피격으로 시간이 갱신되지 않는 기존 규칙은 유지한다. 별 VFX는 실제 기절 효과가 제거될 때 함께 종료된다. 두 항목은 ShipBossEnemy 공통 속성이므로 다른 보스도 기본 4초를 상속하며 각 보스 Blueprint에서 개별 조정할 수 있다.

## 단발 Dash Slash

`/Game/GameplayAbilitySystem/Ability/Enemy/Boss/BPGA_SlashDash`의 **Class Defaults → Boss → Dash → Dash Windup Duration**:

- 기본 **2초**. 충전 진입 애니메이션과 충전 유지 구간을 합친 총 준비 시간이다.
- Encounter Balance의 `Major Attack Telegraph Seconds`보다 우선한다. 이 능력에서는 기존 `Montage Config → Windup Hold Duration`을 추가로 더하지 않는다.
- 몽타주 `Windup` 구간 길이 / `Play Rate`보다 짧게 설정하면 진입 애니메이션 길이까지 보정한다. 애니메이션 진입 후 남은 시간을 `WindupHold`에서 기다린다.
- 기존 돌진 속도, 피해 판정 및 Recover 시간은 그대로 사용한다.

연속 Dash Slash인 `BPGA_Samurai_SeqDashSlash`는 기존 **Boss → Chain Slash Dash → Steps → 첫 항목의 Wait Before Dash**를 사용한다. 후속 돌진은 재충전 없이 바로 실행한다.

## 확인

자동화 검증은 실제 기절 Effect의 지속 시간이 4초이고, 각 기절 설정을 개별 변경했을 때 적용되는지 확인한다. Dash Slash는 기존 몽타주로 준비 시간 조정과 최소 진입 시간 보정을 확인한다. 게임 화면에서의 체감 난이도와 실제 타격 가능 시간은 PIE로 확인한다.

검증 결과: Editor Development 빌드 성공. `ArtisticSW.Enemy.Boss.StatusTriggers`, `ArtisticSW.Enemy.BossMVP.DashSlashServerPhases` 두 테스트 통과. 별도 Editor 프로세스에서 기존 Samurai 및 BPGA_SlashDash를 재로드하여 기본값 4초 / 4초 / 2초를 확인했다. PIE 플레이 검증은 실행하지 않았다.
