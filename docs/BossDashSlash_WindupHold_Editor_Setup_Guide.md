# 보스 DashSlash Windup Hold 에디터 설정 가이드

## 적용된 실행 구조

```text
서버 Ability: WindupEntering -> WindupHolding -> DashAttacking
               -> WaitingForCompletion -> Recovering -> End

Montage: Windup -> WindupHold(반복) -> DashSlash -> DashHold(반복) -> Recover
```

`UGA_BossDashSlash`는 `ServerOnly`로 실행된다. 서버가 Montage Section 길이와 `WindupHoldDuration`으로 상태 전환 시간을 결정하고, Hold가 끝나는 프레임에 `DashSlash` Section 이동과 실제 돌진/피해 판정을 함께 시작한다. AnimNotify는 이 진행 조건에 사용되지 않는다.

## 1. Dash Montage 열기

1. Content Browser에서 `/Game/Fab/Samurai/Animations/Montages/AM_Samurai_DashSlash`를 연다.
2. Montage가 Root Motion을 사용하지 않는 in-place 애니메이션인지 확인한다.
3. Slot이 보스 AnimBP의 FullBody Slot과 같은지 확인한다. 현재 프로젝트 기본 Slot은 `DefaultSlot`이다.

## 2. WindupHold Section 추가

현재 애셋에는 `Windup`, `DashSlash`, `DashHold`, `Recover`가 존재한다. 다음 과정으로 `WindupHold`를 추가한다.

1. `Windup`에서 보스가 검을 들고 준비 자세를 완성하는 프레임을 찾는다.
2. 해당 프레임에 새 Montage Section을 추가한다.
3. Section 이름을 정확히 `WindupHold`로 지정한다.
4. Section 순서가 다음과 같은지 확인한다.

```text
Windup
WindupHold
DashSlash
DashHold
Recover
```

5. `WindupHold`의 시작 Pose가 `Windup` 마지막 Pose와 자연스럽게 이어지는지 확인한다.
6. `WindupHold`의 마지막 Pose가 시작 Pose와 자연스럽게 이어지도록 구간을 짧게 조절한다.
7. 완전한 정지처럼 보이게 하려면 준비 자세 주변의 1~몇 프레임만 `WindupHold` 구간으로 사용한다.
8. Section Association을 다음과 같이 지정한다.

```text
Windup     -> WindupHold
WindupHold -> WindupHold
DashSlash  -> DashHold
DashHold   -> DashHold
Recover    -> None
```

9. 저장한다.

Ability도 런타임에 같은 Association을 설정한다. 에디터 Association까지 일치시키는 이유는 Preview와 실제 실행 결과를 같게 유지하기 위해서다.

`WindupHold`는 필수 Section이다. 누락되거나 길이가 0이면 DashSlash는 활성화를 실패하고 Output Log에 설정 오류를 남긴다.

## 3. 기존 Gameplay Event Notify 정리

1. Montage Notify 트랙에서 `Event.Boss.Dash.Start`를 찾는다.
2. `Event.Boss.Dash.SlashFinished`도 찾는다.
3. 두 Notify가 오직 Dash 진행을 위해 존재했다면 제거한다.
4. 같은 지점에서 VFX나 SFX가 필요하면 전용 Cosmetic Notify 또는 GameplayCue로 교체한다.

Notify를 남겨 두어도 Dash 이동과 피해에는 영향을 주지 않는다. 게임 판정은 서버 타이머만 사용한다.

## 4. BPGA_SlashDash 설정

1. `/Game/GameplayAbilitySystem/Ability/Enemy/Boss/BPGA_SlashDash`를 연다.
2. `Class Defaults`를 선택한다.
3. `Boss > Dash > Montage > Montage Config`를 펼친다.
4. 다음 값을 설정한다.

| 프로퍼티 | 설정값/의미 |
| --- | --- |
| `Montage` | `AM_Samurai_DashSlash` |
| `Windup Enter Section Name` | `Windup` |
| `Windup Hold Section Name` | `WindupHold` |
| `Attack Section Name` | `DashSlash` |
| `Travel Hold Section Name` | `DashHold` |
| `Recovery Section Name` | `Recover` |
| `Play Rate` | 먼저 `1.0` 사용 |
| `Windup Hold Duration` | 준비 자세 추가 유지 시간. 예: `1.5`초 |
| `Recovery Timeout` | Recover 실패 안전장치. 예: `1.5`초 |

5. `Boss > Dash`에서 기존 이동·피해 값을 확인한다.

| 프로퍼티 | 의미 |
| --- | --- |
| `Dash Duration` | 목적지까지 이동하는 서버 기준 시간 |
| `Dash Acceptance Radius` | 목적지 도착 허용 반경 |
| `Dash Hit Radius` | 이동 중 피해 Sweep 반경 |
| `Damage` | Dash 피해량 |
| `Damage Effect Class` | 실제 피해 GameplayEffect |

6. Blueprint를 컴파일하고 저장한다.

`Windup Hold Duration`은 `Windup` Section 재생시간을 포함하지 않는다. 실제 공격 시작까지 걸리는 시간은 대략 다음과 같다.

```text
Windup Section 재생시간 / Play Rate + Windup Hold Duration
```

## 5. 보스 Blueprint 연결 확인

1. `/Game/GameplayAbilitySystem/Enemy/BP_Ship_BossEnemy`를 연다.
2. `Class Defaults > Starting Abilities`를 확인한다.
3. `BPGA_SlashDash`가 포함되어 있는지 확인한다.
4. 네이티브 `GA_BossDashSlash`와 `BPGA_SlashDash`를 동시에 추가하지 않는다.
5. 컴파일하고 저장한다.

현재 프로젝트의 보스 Blueprint에는 `BPGA_SlashDash`가 등록되어 있다.

## 6. Behavior Tree 확인

기존 DashSlash 분기는 변경하지 않는다.

```text
BTD_CanActivateAbilityByTag(GameplayAbility.Boss.DashSlash)
-> BTT_SelectBossDestinationPoint(Purpose=Dash)
-> BTT_ActivateBossAbility(GameplayAbility.Boss.DashSlash)
```

목적지는 Ability 활성화 전에 선택되어 있어야 한다. Ability가 활성화된 뒤에는 서버가 Windup, Dash, Recovery 전체 생명주기를 소유한다.

1. `BT_Subtree_RogueBoss_Combat`에서 DashSlash용 `BTT_ActivateBossAbility`를 선택한다.
2. `Ability Asset Tag`가 `GameplayAbility.Boss.DashSlash`인지 확인한다.
3. `Require Preselected Destination`이 켜져 있는지 확인한다.
4. `Cancel Ability On Abort`는 켜 두어도 된다. DashSlash는 Commit 이후 자신의 정책으로 BT Abort를 무시한다.
5. `Clear Destination When Finished`도 켜 두어도 된다. BT Abort 후 DashSlash가 계속 실행 중이면 BT가 목적지를 지우지 않고 GA가 완료/취소 시 정리한다.
6. 거리 Decorator는 공격 활성화 전 조건으로만 사용한다. Hold가 시작된 뒤 플레이어가 범위를 벗어나도 Montage와 Dash는 계속되어야 한다.

## 7. PIE 검증 순서

1. 먼저 `Windup Hold Duration=0`으로 실행하여 기존과 비슷한 속도로 공격하는지 확인한다.
2. 값을 `1.5~3.0`초로 늘린다.
3. `Windup`이 한 번 재생된 뒤 `WindupHold` 자세만 반복되는지 확인한다.
4. Hold 중에는 보스 위치가 변하지 않고 Dash Damage Volume도 활성화되지 않는지 확인한다.
5. Hold 종료 시 `DashSlash`로 전환되는 프레임과 돌진 시작이 일치하는지 확인한다.
6. 목적지에 먼저 도착해도 검 휘두르기가 끝까지 재생되는지 확인한다.
7. 검 휘두르기가 먼저 끝나면 `DashHold`를 유지하다가 도착 후 `Recover`로 넘어가는지 확인한다.
8. Hold 또는 Dash 중 보스가 피격/사망하면 Montage, 타이머, 충돌 판정이 모두 정리되는지 확인한다.

## 8. 네트워크 검증

PIE의 Number of Players를 `2` 이상으로 설정하고 Dedicated Server 옵션을 켠 뒤 확인한다.

1. 서버와 모든 클라이언트에서 Hold 시작과 종료 순서가 동일한지 확인한다.
2. 클라이언트 화면에서만 Montage가 보이지 않는 문제가 없는지 확인한다.
3. Dash 위치는 서버 결과로 수렴하는지 확인한다.
4. 피해가 서버에서 한 번만 적용되는지 확인한다.
5. 클라이언트에서 발생한 AnimNotify가 추가 피해나 중복 Dash를 만들지 않는지 확인한다.

게임 판정은 Notify에 의존하지 않으므로 Dedicated Server에서 Mesh Notify가 생략되어도 Dash는 정상 진행해야 한다.

## 9. 문제 해결

- `Cannot activate DashSlash: MontageConfig.Montage is not assigned.`
  - `BPGA_SlashDash > Montage Config > Montage`를 지정한다.
- `missing required section`
  - 로그에 표시된 Section 이름과 Montage의 Section 이름을 일치시킨다.
- `preselected destination capture failed`
  - DashSlash BT 분기에서 목적지 선택 Task가 먼저 성공했는지, 보스가 유효한 Host Ship과 전투 Target을 가지고 있는지 확인한다.
- `montage phase transition rejected`
  - 실행 중인 Montage가 `Montage Config > Montage`와 같은지 확인하고 다른 Ability가 Montage를 강제로 중단하지 않았는지 확인한다.
- Hold 자세에서 튀는 현상
  - `WindupHold` 시작/끝 Pose를 맞추고 구간을 더 짧게 조절한다.
- Hold가 예상보다 길거나 짧음
  - `Play Rate`와 `Windup Hold Duration`을 함께 확인한다. Hold Duration 자체는 Play Rate의 영향을 받지 않는다.
