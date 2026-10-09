# Samurai 연속 SlashDash 구현 계획서

작성일: 2026-10-06

후속 변경: 연속 공격의 간격을 최소화하도록 중간 Recover와 두 번째 이후 Windup/준비 몽타주를 생략하고 후속 은신·재등장 대기를 각각 0.01초로 변경했다. 후속 `WaitBeforeDash`는 사용하지 않는다. 모든 Point 선정 Task의 최종 실패는 `[Logtemp]` 로그 후 소유 능력 취소 또는 BT 분기 실패로 처리하며 목적지/경로를 정리한다. 아래 원래 계획의 회차별 2초/1초 대기와 매회 Recover 규칙은 이 변경으로 대체한다. 현재 실행 설정은 `docs/Samurai_ChainSlashDash_Editor_Setup.md`를 따른다.

상태: 이 계획을 기반으로 코드 구현을 진행했다. 후속 요청에 따라 타깃 사망 시 마지막 갑판 위치로 계속 실행, 빨간 경로 Decal, 지속 충전 GameplayCue를 추가했다. 자동화 테스트는 생략한다. 에디터 연결은 `docs/Samurai_ChainSlashDash_Editor_Setup.md`를 참고한다. Blueprint/BT의 실제 전투 연결 및 PIE 동작은 별도 확인이 필요하다.

## 1. 목표와 동작 기준

기존 단발 `BPGA_SlashDash`를 유지하고, Samurai 전용 연속 SlashDash를 별도 능력으로 추가한다.

기본 동작은 다음 순서를 **2회** 수행하는 것이다.

1. 현재 회차의 안전한 후방 재등장 위치를 선택한다.
2. Rogue와 같은 은신 이동을 수행한다.
3. Player 뒤에 나타나 Player 쪽을 바라본다.
4. 해당 회차의 SlashDash Point를 계산하고 경고 경로를 표시한다.
5. 해당 회차에 설정한 시간만큼 준비한 뒤 SlashDash를 수행한다.
6. 돌진과 공격 애니메이션, Recover가 완료되면 다음 회차로 넘어간다.
7. 마지막 회차가 끝나면 능력을 종료하고 AI가 다음 행동을 선택한다.

매 회차 은신 → 재등장 → 대기 → SlashDash를 반복한다. 첫 회차에 공격이 명중해도 두 번째 회차를 수행한다. Rogue ChainVanish의 ‘공격 가능한 위치가 되면 반복 중단’ 조건은 가져오지 않는다.

반복 수는 회차 설정 배열의 길이로 결정하고 기본 배열에 2개를 제공한다. 별도 RepeatCount와 배열 길이가 불일치하는 구조를 피한다.

## 2. 확인한 기존 구조

| 구성 | 현재 역할 | 적용 계획 |
|---|---|---|
| `UGA_BossDashSlash` | 경로 확정, 경고 표시, 몽타주 단계, 돌진, 피해, Recover, 정리 | 단일 돌진 실행 부분을 재사용 가능한 작업으로 분리 |
| `UAbilityTask_BossVanishRelocation` | 준비, 은신, 이동, 재등장, 은신 상태 복구 | 새 능력에서 직접 재사용 |
| `BossVanishFeedback` | 서버의 출발·도착 좌표로 GameplayCue 실행 | Samurai에서 선택한 Cue 태그로 재사용 |
| `UGA_BossChainVanish` | 공격 가능 여부에 따라 최대 횟수 내 은신 재시도 | 타깃 고정·취소 처리 참고. 능력 자체는 호출하지 않음 |
| `UBossDeckPointSelector` | 안전한 갑판 위치 선택 | 새 능력용 직선 연장 목적지 계산 추가 |
| `UDeckWalkAreaComponent` | 정확한 바닥 위치, 경로 지원 여부, 점유 검사 | 기존 `ResolvePreciseLocalFloor` 등의 검사 재사용 |
| `UBossGameplayAbility` | 서버 실행, Busy/Attacking 상태, 쿨타임, 피해 | 새 능력의 부모로 사용 |

현재 Dash 목적지 선택은 갑판 후보를 순회하며 플레이어를 지나는 경로와 거리 점수를 평가한다. 요청한 ‘Player를 지나 정확히 D만큼 떨어진 점’ 계산과 완전히 같지는 않다. 기존 선택 방식은 보존하고 신규 함수에서 명시적인 거리 계산을 제공한다.

## 3. 두 종류의 ‘뒤쪽’ 정의

### 재등장 위치

Rogue의 `BehindTarget`처럼 **Player의 시선 방향을 기준으로 한 후방**이다. 매 회차 시작 시 고정 타깃의 최신 위치와 방향으로 선택한다. 선택 후 은신 이동 동안 목적지를 계속 추적하지 않으며, 재등장 시 플레이어가 회전했다면 현재 시선 기준 후방이 아닐 수 있다. 이것은 기존 은신 이동과 같은 스냅샷 방식이다.

재등장 위치 선택 시 같은 갑판 Surface, 캡슐 배치, 점유 상태를 검사한다. 가능한 후보 중 이후 SlashDash 경로까지 유효한 후보를 우선 선택하여, 나타난 직후 돌진할 수 없는 상황을 줄인다. 실제 재등장 후에는 플레이어가 이동했을 수 있으므로 다시 검사한다.

### SlashDash Point

**재등장한 Samurai → Player 방향으로 Player를 넘어선 지점**이다. Player의 Forward Vector를 사용하지 않는다.

```text
S = 재등장한 Samurai의 갑판 평면 위치
P = Player의 갑판 평면 위치
U = Normalize(ProjectToDeckPlane(P - S))
D = 해당 회차의 DistanceBeyondPlayer
Point = P + U × D

Samurai(S) ───────── Player(P) ─── D ─── Point
```

후방에서 나타난 Samurai가 Player를 관통하여 반대편으로 이동하는 형태다.

- 갑판 평면에서 계산하고 바닥 높이 및 캐릭터 캡슐 높이는 기존 갑판 API로 해석한다.
- `ResolvePreciseLocalFloor`로 정확한 도착점을 표현한다. 가장 가까운 노드로 바꾸면서 설정 거리나 직선을 조용히 변경하지 않는다.
- 시작점과 Player가 겹쳐 방향을 정의할 수 없으면 해당 패턴을 취소한다.
- 바닥, 동일 Surface, 목적지 점유, 캡슐 장애물, 경로 연속성, 최소·최대 돌진 거리를 검사한다.
- Point를 만들 수 없으면 다른 재등장 후보를 제한된 후보 순회 내에서 검토한다. 재등장 후에도 실패하면 추가 재은신 없이 능력을 종료한다.
- 목적지 확정은 재등장 직후 경고 경로를 표시하기 전에 한다. 이후 Player가 이동해도 경로를 추적 변경하지 않는다. 두 번째 회차에서는 최신 위치로 새로 계산한다.
- 확정 경로는 기존 SlashDash처럼 선박 로컬 좌표로 보관하여 움직이는 선박을 따라간다.

## 4. 에디터 설정

신규 `BPGA_SamuraiChainSlashDash`의 Class Defaults에 `Boss / Chain Slash Dash` 설정을 제공한다.

### 회차별 설정

`FChainSlashDashStepConfig` 구조체의 `Steps` 배열로 편집한다.

| 설정 | 의미 | 단위 |
|---|---|---|
| `WaitBeforeDash` | 재등장 완료부터 실제 돌진 시작까지의 총 대기 시간 | 초 |
| `DistanceBeyondPlayer` | Player에서 직선 연장 방향의 Point까지 거리 | cm |

초기 편집 예시이며 최종 밸런스 값은 PIE에서 조정한다.

| 회차 | WaitBeforeDash | DistanceBeyondPlayer |
|---|---:|---:|
| 1 | 2.0초 | 400cm |
| 2 | 1.0초 | 500cm |

### 공통 설정

- 기존과 같은 SlashDash MontageConfig, DashDuration, DashHitRadius, AttackCoefficient, PathPresentation.
- 최소·최대 돌진 거리와 재등장 위치 선택 설정.
- 은신 준비 몽타주, PreparationDelay, HiddenDuration, RelocationSettleTime.
- 출발·도착 GameplayCue 태그.
- 신규 능력의 독립 CooldownDuration.

재등장 거리와 `DistanceBeyondPlayer`는 다른 개념이다. 전자는 은신 위치 선택 설정에서, 후자는 각 회차에서 관리한다. 최소 돌진 거리에는 `Samurai→Player 거리 + DistanceBeyondPlayer`가 적용된다.

### 대기 시간과 몽타주의 관계

기존 단발은 Windup 진입 구간 길이와 WindupHoldDuration, 밸런스 보정으로 대기 시간이 결정된다. 새 능력은 **WaitBeforeDash가 총 대기 시간을 직접 결정**하도록 실행 시 별도 값을 전달한다.

```text
WindupHold 시간 = WaitBeforeDash - (Windup 구간 길이 / PlayRate)
```

WaitBeforeDash가 Windup 진입 구간보다 짧으면 유효하지 않은 설정으로 보고 에디터 검증 오류와 런타임 실패 사유를 제공한다. 자동으로 애니메이션 속도를 바꾸거나 설정 시간에 별도 준비 시간을 더하지 않는다. 새 능력의 회차별 대기에는 기존 전역 TelegraphDuration 보정을 중복 적용하지 않는다. 기존 단발의 시간 계산은 유지한다.

## 5. 구현 구조

### 신규 능력: `UGA_BossChainSlashDash`

구현은 `UGA_BossDashSlash`를 상속하여 기존 설정과 단일 실행 연결을 공유한다. 공통 기반은 `UBossGameplayAbility`이며 다음 흐름을 소유한다.

```text
Validate → LockTarget → Commit
  → SelectRelocation → Vanish → Reveal
  → CommitDashPath → Windup → Dash → Recover
  → 다음 Step 또는 End
```

- 능력 시작 시 Player 한 명을 고정한다. AI 타깃 변경으로 회차 사이에 대상이 바뀌지 않게 한다.
- 각 회차의 재등장 및 경로 계산에는 고정 Player의 최신 위치를 사용한다.
- 능력 전체 동안 Busy/Attacking을 유지한다.
- 각 회차가 시작할 때 타격 중복 방지 기록을 초기화한다. 한 회차에서 같은 액터를 중복 타격하지 않으며 다음 회차에서는 다시 피해를 줄 수 있다.
- 조준 타깃 고정과 피해 대상 제한은 구분한다. 돌진 경로상의 다른 유효 피해 대상은 기존 피해 규칙을 따른다.

### 단일 돌진 실행 재사용

신규 `UAbilityTask_BossSlashDashExecution`으로 기존 단일 돌진 실행 수명주기를 옮긴다. 경로·실행 설정과 회차별 시간 옵션을 입력받고, 피해 요청 및 완료/실패를 소유 능력에 전달한다.

- 이동, 경고 경로, 몽타주, 돌진 충돌, Recover, 타이머 정리는 실행 작업이 담당한다.
- GAS Commit, 능력 태그, 쿨타임, 피해 Spec과 적용 정책은 능력이 담당한다.
- 기존 `UGA_BossDashSlash`는 기존 설정과 검증·쿨타임 정책으로 이 작업을 1회 실행한다.
- 새 능력은 같은 작업을 회차마다 실행하고 완료 이벤트로 다음 회차를 시작한다.
- 기존 UPROPERTY 이름과 직렬화 호환 처리, `BPGA_SlashDash` 부모와 설정을 유지한다.

기존 SlashDash GA를 다른 GA 안에서 단순 활성화하면 Busy/Attacking 차단 및 단발 쿨타임과 충돌하므로, 내부 실행을 재사용하는 구성을 사용한다. Rogue의 기본 공격까지 연결된 Vanish GA도 직접 호출하지 않는다.

## 6. 쿨타임 및 AI 연결

신규 태그 제안:

- `GameplayAbility.Boss.ChainSlashDash`
- `Cooldown.Boss.ChainSlashDash`

새 능력의 쿨타임은 전체 패턴 종료 시 1회 적용한다. 실제 은신 시작 이후 취소되면 종료 시 적용하고, 설정 검증이나 목적지 선택에서 실패하여 은신을 시작하지 못했다면 소모하지 않는다. Commit 시에는 비용 및 활성화 검사를 수행하되 쿨타임 적용은 지연한다.

기존 단발 쿨타임과는 독립적으로 관리한다. Rogue ChainVanish의 전투 진입 초기 쿨타임은 이번 요청에 포함되지 않아 기본 도입하지 않는다.

에디터 연결 계획:

1. 신규 부모를 사용해 `BPGA_SamuraiChainSlashDash`를 생성한다.
2. 실제 스폰되는 Samurai BP의 Starting Abilities에 신규 BP를 추가한다. 기존 SlashDash 부여는 유지한다.
3. 실제 Behavior Set이 가리키는 Samurai Combat 서브트리에 신규 분기를 추가한다.
4. `Can Activate Ability By Tag`에 신규 태그를 지정하고 Observer Aborts는 `None`으로 시작한다.
5. `Activate Boss Ability`는 신규 태그, Require Preselected Destination 끄기, Clear Destination When Finished 켜기, Prefer Current Weapon Ability 끄기로 설정한다. 첫 목적지부터 능력이 선택한다.
6. 기존 단발 분기보다 높은 우선순위에 배치하여 새 패턴의 쿨타임 중 단발을 사용할 수 있게 한다. 매번 강제 교대하지는 않는다.
7. BT에 별도 Wait, Repeat, BasicAttack을 덧붙이지 않는다. 능력이 전체 패턴을 관리한다.

능력이 실행 중 확보한 목적지와 경로는 능력이 소유한다. 기존 SlashDash의 BT abort 생존 정책을 검토하여 일반 재평가로 경로가 사라지지 않게 하되, 사망·강제 취소·타깃 상실 시에는 즉시 종료한다.

## 7. 실패와 종료 처리

- 후속 요청 반영: 타깃 사망·삭제 시 마지막 위치·방향을 선박 로컬 좌표로 고정하고 남은 회차를 진행한다. 타깃 교체나 리스폰 위치로 재조준하지 않는다. 점프 자체는 타깃 상실로 간주하지 않는다. 안전한 목적지·돌진 경로 확보가 실패하면 종료한다.
- 재등장 또는 Point 선택 실패 시 패턴을 종료한다. 실패를 성공한 회차로 세거나 무한 재시도하지 않는다.
- 몽타주 중단, 피해에 의한 취소, 갑판 재구축, 선박 또는 갑판 참조 소멸을 처리한다.
- 은신 해제, 충돌 응답 복원, 피해 센서 비활성화, 이동 잠금 해제, Focus 해제, 경고 효과 제거, 타이머·이벤트 해제를 모든 종료 경로에서 보장한다.
- 사망 후 이동 모드를 복원하거나 뒤늦은 콜백이 보스를 다시 숨기지 않도록 한다.
- 이전 회차의 완료 이벤트가 다음 회차에 반영되지 않게 작업 정리 및 회차 식별 검증을 수행한다.
- 서버에서 회차·위치·피해를 결정하고 기존 네트워크 이동 및 GAS Cue 경로로 클라이언트에 전달한다.

## 8. 예상 변경 파일

| 파일/영역 | 변경 내용 |
|---|---|
| `Source/Enemy/Public` 및 `Private/GAS/Ability/Boss/GA_BossChainSlashDash.*` | 신규 능력, 회차 설정, 상태 관리 |
| `Source/Enemy/Public` 및 `Private/GAS/Tasks/AbilityTask_BossSlashDashExecution.*` | 단일 돌진 실행 작업 |
| `GA_BossDashSlash.h/.cpp` | 기존 외부 계약을 유지하며 실행 작업 사용 |
| `BossDeckPointSelector.h/.cpp` | 정확한 직선 연장 목적지와 안전성 검사 |
| `Source/ArtisticSWCore/Public/BaseGameplayTags.h` 및 `Private/BaseGameplayTags.cpp` | 신규 태그 |
| 자동화 테스트 | 후속 사용자 요청에 따라 추가·실행 생략 |
| Samurai 능력 BP·보스 BP·Combat BT | 신규 능력 설정 및 연결 |
| `docs/Samurai_ChainSlashDash_Editor_Setup.md` | 설정 및 PIE 확인 절차 |

현재 작업 트리에 Rogue 관련 코드와 자산을 포함한 사용자 변경이 있다. 구현 시 현재 변경을 기반으로 작업하고 되돌리거나 덮어쓰지 않는다.

## 9. 작업 순서와 검증

1. 기존 단발 SlashDash의 동작과 자산 설정을 기록한다.
2. 단일 돌진 실행 작업을 분리하고 기존 단발이 같은 대기·피해·경로·쿨타임으로 동작하는지 먼저 검증한다.
3. 정확한 Point 선택 함수를 추가하고 직선, Player 너머 방향, 지정 거리, 갑판·장애물 실패를 검증한다.
4. 신규 능력에 은신 작업과 2회 반복, 회차별 설정, 종료 정리를 연결한다.
5. 신규 태그와 Samurai BP/BT를 연결하고 에디터 설정 문서를 작성한다.
6. Editor 빌드를 수행한다. 자동화 테스트는 생략하고 실제 자산 연결 후 PIE로 동작을 확인한다.

완료 기준:

- 기본 설정에서 은신과 SlashDash가 각각 정확히 2회 실행된다.
- 1회차와 2회차의 대기·거리 변경이 각각 해당 회차에만 반영된다.
- 서버 기준 재등장~돌진 시작 시간이 설정값과 프레임 오차 범위 내에서 일치한다.
- Player가 정지해 있을 때 Point가 직선 위 Player 너머의 지정 거리에 있다.
- 준비 중 Player가 이동해도 이미 표시한 경로는 고정되고, 다음 회차는 최신 위치를 사용한다.
- 한 회차의 Sweep/Overlap 중복 피해가 없고 두 번째 회차는 다시 타격할 수 있다.
- 갑판 끝, 장애물, 최소·최대 거리, 점유 실패에서 갑판 밖 강제 이동이나 무한 재시도가 없다.
- 은신·대기·돌진·Recover 중 취소와 사망 후 모습·이동·충돌·Busy 상태가 남지 않는다.
- 움직이거나 회전하는 선박에서 경고 경로와 실제 돌진 경로가 일치한다.
- 2인 PIE에서 타깃 고정, 은신·재등장, 몽타주, 피해, Cue가 일관된다.
- 기존 단발 SlashDash와 Rogue 은신 능력의 동작이 유지된다.

## 10. 이번 계획의 기본 가정

- ‘2번 수행’은 은신을 포함한 전체 사이클을 기본 2회 실행한다는 뜻으로 해석했다.
- 대기는 재등장 후 시작하며 Point와 경고 경로는 대기 시작 시 확정한다.
- 기존 몽타주와 경로 연출을 재사용하며 신규 애니메이션·VFX 제작은 포함하지 않는다.
- 표의 2초/1초 및 400cm/500cm는 에디터 설정 예시다. 실제 몽타주 길이와 Samurai 갑판에서 유효한 값으로 조정한다.
