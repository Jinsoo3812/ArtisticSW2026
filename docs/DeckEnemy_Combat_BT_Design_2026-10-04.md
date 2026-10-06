# 갑판 일반 적 Combat BT 구현 및 설정
작성일: 2026-10-04

## 1. 확정한 동작

- Passive는 기존 보행면 순찰의 목적지 선택 → 이동 → 대기 반복을 사용한다.
- 선택한 Combat BT의 첫 실행에서 Alarm Task를 실행한다. 경보 위치는 발견한 **Player의 발생 당시 위치**다.
- 경보를 들은 주변 AI는 Investigating으로 전환한다. 직접 Sight로 Player를 발견했을 때 Combat으로 전환한다. 경보 수신으로 TargetActor를 설정하지 않는다.
- 근거리/원거리 모두 Task의 TargetDistance를 사용해 Player–Enemy 선 방향의 목표점을 선택한다. 현재 거리보다 설정 거리가 크면 Enemy 뒤쪽으로 선을 연장하여 물러난다.
- 공격 가능한 순간 공격 분기의 Lower Priority 중단으로 이동을 끊고 공격한다. 즉시는 기본 0.1초 검사 주기와 다음 BT 평가 안에서의 전환을 뜻한다.
- 공격을 확정한 뒤에는 Player의 사거리 이탈, 시야 상실, 타깃 변경·사망으로 몽타주를 중단하지 않는다. 몽타주의 종료 콜백 뒤에 다음 작업과 상태 전환을 처리한다.
- 원거리는 Player가 사거리를 벗어나도 발사 시점의 Player 위치를 조준한다. 일반 피격 중에는 공격을 유지하고 피해는 받는다. Enemy 자신의 사망·풀 복귀, 기절·Frozen은 즉시 중단한다.
- 공격 종료 후 공격 쿨타임 동안 지정 거리로 이동하고 Player를 바라보며 좌우로 움직인다. 공격 쿨타임 수치는 사용자가 조정한다.
- PostAttackMove 강제 잠금, 최소 100 cm 강제 변위, 공격 후 0.5 ± 0.3초 강제 Wait는 사용하지 않는다. 이동이 충분히 나오도록 공격 후 남는 쿨타임을 설정한다.
- 일반 갑판 적의 공격/조사에 NavMesh 이동을 사용하지 않는다. 보스 및 지상 적의 기존 이동과 공격은 해당 전용 BT를 사용한다.

## 2. BT 배치와 우선순위

```text
BT_EnemyBase
├─ Passive 조건 → Run Behavior Dynamic: 기존 Deck Passive
├─ Investigating 조건 → Run Behavior Dynamic: BT_DeckEnemy_Investigating
└─ Combat 조건 → Run Behavior Dynamic: 아래 적별 Combat

BT_DeckMelee_Combat / BT_DeckRanged_Combat
└─ Selector [BTS_MaintainDeckCombatFocus]
   ├─ Sequence [DeckCombatCondition: AlarmPending, Aborts=None]
   │  └─ Broadcast Alarm At Player
   ├─ Sequence [DeckCombatCondition: AttackReady, Aborts=Lower Priority]
   │  └─ Deck Weapon Attack
   ├─ Sequence [DeckCombatCondition: RecoveryPending, Aborts=None]
   │  └─ Selector
   │     ├─ Sequence: Select Recovery Goal → Move On Deck → Wait 0.3
   │     └─ Wait 0.3 (경로 선택 실패 재시도)
   ├─ Sequence [DeckCombatCondition: AttackCooldown, Aborts=None]
   │  └─ Move Around Player On Deck (Cooldown)
   ├─ Sequence
   │  ├─ Select Deck Walk Goal (Combat, TargetDistance)
   │  ├─ Move On Deck Walk Area (StopWhenAttackReady=true)
   │  └─ Wait 0.2
   └─ Wait 0.3 (일반 실패 재시도)

BT_DeckEnemy_Investigating
└─ Selector
   ├─ Sequence
   │  ├─ Select Deck Walk Goal (Investigation)
   │  ├─ Move On Deck Walk Area (StopWhenAttackReady=false)
   │  ├─ Wait 1.0
   │  └─ Set Enemy State: Passive
   └─ Sequence: Wait 0.3 → Set Enemy State: Passive
```

공격 분기가 이동 분기보다 왼쪽에 있어야 한다. 필수 조건은 서브트리 내부 자식 Sequence에 부착한다. Run Behavior Dynamic의 서브트리 루트 데코레이터에 전투/공격 조건을 넣지 않는다.

AttackReady는 대상 자격, 무기와 해당 무기에서 부여된 Ability, 같은 보행면, 공격 사거리, LOS, GAS 쿨다운, 원거리 타이머 쿨다운, 공격/피격 상태, 전투 밸런스 준비 상태를 검사한다. 위치와 LOS는 Blackboard의 TargetActor 값이 바뀌지 않아도 변하므로 데코레이터가 제한된 주기로 다시 검사한다.

공격 중 조건이 false가 되더라도 Lower Priority는 실행 중 공격 자체를 중단하는 설정이 아니다. 공격을 확정하면 Controller는 시야 상실, 타깃 교체·해제, 일반 상태 전환을 몽타주 종료 뒤로 미룬다. 공격 관련 기존 데코레이터도 확정한 공격 중에는 분기를 유지한다. Enemy 자신의 사망·풀 복귀와 기절·Frozen은 즉시 중단한다. 공격 Task의 Abort는 자신이 시작한 Ability만 취소하고 Controller의 TargetActor를 지우지 않는다.

0.2/0.3초 대기는 목적지에서 공격을 지연시키는 필수 전투 연출이 아니다. 경로 실패나 반복 평가가 매 프레임 반복되지 않도록 하는 재시도 간격이다. 공격 조건의 Lower Priority는 이 대기도 중단한다.

## 3. Alarm: Player 위치에서 발생, 조사 후 발견

BTT_BroadcastEnemyAlarm은 서버에서 살아 있는 관측 대상과 Combat 상태를 확인하고, UEnemyAlarmComponent에 발신을 요청한다.

발신은 Combat 세션당 한 번이다. 세션은 Controller의 실제 비전투 → Combat 상태 전환에서 증가한다. 같은 전투의 BT 재시작이나 타깃 교체는 세션을 증가시키지 않는다. 풀 재사용은 세션을 유지하며 증가시켜 이전 생애 이벤트와 구별하고, 처리 상태와 스냅샷을 초기화한다.

경보의 Hearing 위치는 Player의 발생 당시 Actor 위치이며 Instigator는 관측 Enemy다. 갑판 조사에는 Player의 발생 당시 바닥 위치를 HostShip 로컬 좌표로 별도 저장한다. Player가 움직여도 스냅샷은 갱신하지 않는다. 배가 움직이면 로컬 스냅샷을 현재 World 위치로 변환한다.

수신 정책:
1. 전용 경보 태그와 발신자의 현재 이벤트 ID를 확인한다. 3초보다 오래된 이벤트, 중복 이벤트, 이전 풀 생애 이벤트는 폐기한다.
2. 갑판 경보는 같은 HostShip의 활성 갑판 적만 수신한다. 층간 Hearing은 허용하되 이동은 연결된 실제 보행 경로를 요구한다.
3. Dead/Frozen/Combat 상태는 새 경보로 조사 상태를 덮어쓰지 않는다.
4. 기존 Territory 규칙 안의 경보만 조사한다.
5. Investigating은 고정 스냅샷으로 이동하며 Sight가 발견한 경우만 TargetActor와 Combat 상태를 획득한다.
6. 경보를 들었다는 이유만으로 재발신하지 않는다. 조사 후 직접 발견해도 최근 동일 Player 경보를 들었다면 보조 쿨다운으로 발신을 억제한다.
7. 경로가 없는 다른 층/갑판 밖 위치는 텔레포트나 직선 이동으로 보완하지 않는다.

ReportNoiseEvent는 실제 사운드를 재생하지 않는다. Task의 AlarmSound는 선택 사항이며 설정하면 Player의 발생 위치에서 한 번 재생한다. 연출은 복제된 Enemy의 Alarm 컴포넌트가 Unreliable Multicast로 전달한다. 청각 인지, 수신 정책, 조사 위치와 BT는 서버에서만 처리하며, 사운드 수신 실패가 AI 판단에 영향을 주지 않는다. 현재 Task는 즉시 발신하며 외침 몬타주/Notify 대기는 구현하지 않는다.

| Task/Enemy 설정 | 초기값 | 의미 |
| --- | --- | --- |
| MaxRange | 2000 cm | Player 스냅샷 위치 기준 발신 범위 |
| Loudness | 1.0 | Hearing 이벤트 강도 |
| CooldownSeconds | 5초 | 전투 재진입 및 최근 동일 Player 경보의 보조 제한 |
| AlarmSound | 없음 | 선택 사운드. 실제 인지에는 필요하지 않음 |
| Enemy HearingRange | Enemy BP 설정 | 발신 범위와 함께 수신 가능 여부 결정 |

발신 범위가 2000 cm여도 청자의 HearingRange가 1500 cm라면 전체 범위를 듣는다고 가정하지 않는다. 청각의 벽 차폐는 기본 정책에 포함하지 않는다.

## 4. Player–Enemy 선 기준 거리 목표

배 로컬 바닥 위치에서 다음 식을 사용한다.

```text
P = Player 바닥 위치
E = Enemy 바닥 위치
D = Task의 TargetDistance
Direction = normalize2D(E - P)
Goal = P + Direction × D
```

D가 현재 거리보다 크면 Enemy 뒤쪽으로 물러난다. 두 위치가 겹쳐 방향이 없는 경우 배 로컬 +X를 사용한다. 목표점은 Player와 같은 보행면에 투영하며, 다른 층은 실제 연결 경로가 있을 때만 이동한다.

에디터의 목적지 선택 Task:
- 클래스: BTT_SelectDeckWaypoint, 표시명: Select Deck Walk Goal.
- SelectionMode=Combat: 위 거리 목표를 선택한다.
- TargetDistance: Player로부터 원하는 거리. 근거리/원거리 공통이다.
- ProjectionTolerance: 정확한 목표가 장애물/점유로 불가능할 때 허용할 최대 목적지 오차.
- SelectionMode=Investigation: 경보/PointOfInterest의 고정 위치 조사.
- SelectionMode=ReleaseLineOfSightReposition: 기존 에셋 호환용 enum 이름이며, 지금은 실패 위치 스냅샷을 향한 LOS 복구다.

정확한 선 위 목표점의 바닥·캡슐 공간·그래프 연결을 검증하고, 마지막 경로 구간에는 해당 정확한 점을 추가한다. 정확한 목표가 불가능하면 ProjectionTolerance 안의 도달 가능한 보행면 노드를 가까운 순서로 확인한다. 허용 범위 안에 후보가 없으면 실패하고 짧은 재시도로 넘긴다. 임의로 먼 무작위 지점을 선택하지 않는다.

목표 이동 거리와 실제 공격 사거리는 별개다. 공격 사거리는 장착 무기와 원거리 최소 사거리를 따른다. 이동 목표를 공격 사거리 밖에 두면 해당 거리에서는 공격하지 못한다. 목표점은 Player 캡슐과 겹치는 위치를 제외한다.

기존 BTT_MoveToDeckWaypoint는 보행면 경로만 실행한다. StopWhenAttackReady=true일 때 Combat 상태에서 **쿨타임까지 포함해** 공격 가능한 순간 이동을 종료한다. Passive/Investigation 이동은 공격 준비 판정으로 종료하지 않는다. Arrival 허용 반경은 신규 기본값 30 cm다. 기존 에셋에 저장된 수치는 에디터에서 확인한다.

## 5. 쿨타임 중 시선 유지와 좌우 이동

BTS_MaintainDeckCombatFocus를 Combat Selector에 둔다. Actor Focus를 유지하고 CharacterMovement의 ControllerDesiredRotation과 RotationRate를 사용한다. 직접 매 Tick Actor 회전을 덮어쓰지 않는다. 타깃 교체 시 갱신하고, Combat 종료/사망/풀 복귀 시 기존 회전 설정을 복원한다.

BTT_MoveAroundDeckTarget는 독립적으로도 Focus를 획득/해제할 수 있는 쿨타임 이동 Task다.
- TargetDistance: 유지하려는 거리.
- StrafeAngle: 좌우 구간의 각도, 기본 20도.
- ProjectionTolerance: 목표 투영 오차, 기본 100 cm.
- MoveSpeed: 기본 200 cm/s.
- MaximumDuration: 한 실행의 최대 시간, 기본 6초. 길게 남은 쿨타임은 BT가 Task를 다시 실행한다.
- 시작 시 좌/우를 적별 RandomStream으로 선택한다.
- 먼저 지정 거리로 접근/후퇴하고, 해당 거리의 ±100 cm 범위에 들어오면 같은 보행면의 거리 띠 안에서 좌우 경로를 찾는다.
- 구간 도착 또는 1.5초가 지나면 반대쪽을 선택한다. 막히면 이동을 멈추고 0.3초 간격으로 반대쪽을 시도한다.
- 쿨타임이 끝나면 성공으로 종료한다. 공격이 아직 사거리/LOS를 만족하지 않으면 일반 위치 확보로 이어진다.
- 타깃 교체, 사망, 풀 복귀, Abort에서는 경로·예약·자신이 획득한 Focus를 해제한다.

좌우 이동은 실제 경로와 바닥을 따르므로 완벽한 원 궤적이 아니다. 장애물 우회 때문에 Player 쪽으로 가로지르지 않도록 거리 띠 경로를 사용하며, 안전한 경로가 없으면 정지한다. Player 이동에 따른 재계획은 기본 100 cm 변화/최소 0.35초 간격으로 제한한다.

## 6. 공격 쿨타임으로 공격 후 이동 확보

사용자가 공격 쿨타임을 조절하여 공격 후 이동할 시간을 확보한다. 강제 PostAttack Phase로 다음 공격을 잠그지 않는다.

- 근거리: GA_BasicAttack의 AttackCooldownDuration, 기본 2초. Cooldown.Enemy.BasicAttack 태그가 부여된 GAS 효과를 사용한다.
- 원거리: Enemy BP의 AttackCooldown, 기본 2초. 서버 NextAttackTime 타이머와 Ability 자체 GAS 쿨다운이 있다면 함께 확인한다.
- Encounter의 AttackInterval이 양수이면 기존 GetBalancedAttackInterval 경로에서 해당 값이 우선한다.
- 현재 두 쿨타임 모두 공격 시작/Commit 기준이다. 예를 들어 몬타주가 1.2초, 쿨타임이 2초면 공격 종료 후 이동 시간은 약 0.8초다.
- 쿨타임이 몬타주보다 짧으면 종료 직후 다시 공격할 수 있고, 이동할 시간이 거의 없을 수 있다. 필요한 이동 시간만큼 여유를 준다.
- 초기 구현은 기존 쿨타임 수치를 변경하지 않는다. 사용자가 적용 에셋에서 튜닝한다.
- 공격 가능 시 이동을 끊는 규칙이 항상 우선한다. 목적지 완주나 최소 변위는 보장하지 않는다.

신규 Combat BT의 초기 TargetDistance는 Melee 150 cm, Ranged 500 cm다. 위치 확보 Task와 쿨타임 이동 Task에서 각각 편집할 수 있으므로 같은 거리를 원하면 두 값을 같이 조정한다.

## 7. 확정한 공격의 수명, 결과와 LOS 복구

BTT_DeckAttack는 Melee/Ranged 공통 Task다. 현재 무기가 부여한 해당 공격 Ability만 실행하고, 정확한 AbilitySpecHandle의 종료를 기다린다. 동기 종료도 처리한다. 종료 누락에 대비한 MaximumAttackTime의 기본값은 15초이며, 확정한 공격 몽타주가 실제 재생 중이면 이 시간만으로 중단하지 않는다. 긴 몽타주와 낮은 재생 속도에도 마지막 종료 콜백까지 기다린다. BlendOut은 후딜의 시작이므로 다음 BT 작업을 시작하는 신호로 사용하지 않는다.

### 7.1 시작 조건과 공격 확정 이후

사거리·같은 보행면·LOS·쿨다운·타깃 자격은 공격을 **시작할 때** 검사한다. GAS Commit 이후 Combat 컴포넌트가 해당 시도의 AbilitySpecHandle과 몽타주를 보관한다. 이후 사거리 조건을 다시 적용해서 Ability를 취소하지 않는다.

- 근거리: Player가 벗어나도 무기를 휘두른다. 실제 무기 타격 영역과 개별 피격 대상의 LOS로 피해를 판정하므로 빗나갈 수 있다.
- 원거리: 발사 Notify 뒤 실제 서버 발사 시점의 Player 위치와 총구 Transform을 읽는다. 최대·최소 사거리를 다시 검사하지 않는다. 배 운동 보정은 기존 발사 계산을 따른다.
- 원거리 Player가 사망하거나 무효가 되면 발사는 생략하고 몽타주는 끝까지 재생한다. 새 타깃을 향해 현재 공격의 발사를 바꾸지 않는다.
- 타격 창/발사 직전에 장애물이 확인되면 해당 타격 창/발사를 차단하고 몽타주는 유지한다. 발사 실패나 피해 설정 실패도 재생 중인 몽타주를 정상 종료까지 기다린다.
- 일반 피격은 피해·상태 효과를 그대로 적용하되, 확정한 공격 동안 GameplayAbility.HitReaction Ability의 활성화를 차단한다. 종료·취소·풀 정리에서 자신이 추가한 차단 수만 해제한다. 별도의 기절 Ability와 Frozen 상태는 즉시 공격을 취소한다.
- Enemy 자신의 사망·풀 복귀는 예약된 발사, 타격 창, 대기 중인 상태 전환과 피격 반응 차단을 즉시 정리한다.

### 7.2 몽타주 뒤의 판단

Controller는 공격 중 기존 TargetActor와 Combat 상태를 유지하고 전환 요청을 서버의 예약 상태에 저장한다. 명시적 타깃/상태 요청이 인지 변화 재평가보다 우선한다. 타깃 교체 요청은 약한 참조로 보관한다. 몽타주 종료 후 다음 Controller Tick에서 적용하며, 예약 판단이 남아 있으면 다음 공격의 시작을 막는다.

시야를 잃거나 Player가 사망·제거된 경우 종료 시점의 인지 결과를 다시 확인한다. 현재 Player를 다시 보게 됐다면 유지하고, 유효한 다른 Player가 보이면 선택하며, 없으면 타깃을 해제하고 Passive로 돌아간다. 공격 중 들린 일반 소음과 새 경보는 Combat을 조사 상태로 덮어쓰지 않는다. 명시적으로 타깃 해제/Passive 전환 뒤 조사를 요청한 경우에만 종료 후 조사하며, 배 로컬 조사 스냅샷을 사용한다.

### 7.3 결과와 복구

| 결과 | 의미 |
| --- | --- |
| Ready | 현재 공격 시작 가능, 또는 아직 실제 실행되지 않은 시도 |
| Executed | 투사체 발사/근접 타격 창이 실제 실행됨. 명중 여부와 무관 |
| BlockedLOS | 실행 직전 장애물이 확인되어 공격 차단 |
| OutOfRange | 공격 시작 전에 사거리/보행면 조건 불충족. 확정 후 사거리 이탈에는 사용하지 않음 |
| Cooldown | 쿨다운/전투 밸런스 준비 제한 |
| TargetInvalid | 타깃 사망·무효로 시작 또는 원거리 발사를 실행하지 못함 |
| Interrupted | 기절·Frozen 또는 명시적 취소 등으로 실행 중단. 일반 피격은 해당하지 않음 |
| InvalidSetup | 무기/몽타주/Notify/설정 실패 |

실행 여부는 GAS Ability가 Combat 컴포넌트에 기록한다. AttackAttemptId는 풀 재사용/전투 종료에서도 증가하여 이전 비동기 콜백이 새 결과를 덮어쓰지 못하게 한다. 실제 타격 창/지연 발사 직전에도 시도 ID와 Enemy의 활성 생애를 확인한다. 타깃의 사거리/인지 변화는 시도 ID를 무효화하지 않는다. 발사 후 후딜 취소는 Executed를 유지한다. 정상 몽타주 종료만으로 Executed로 처리하지 않는다. 원거리 발사 Notify는 한 Deck 공격 시도에서 한 번만 처리한다.

원거리 발사 직전은 기존 HasClearRangedLaunch를 재사용한다. 근접은 타격 창 활성화 직전에 무기 기준 LOS를 다시 확인하고, 실제 개별 피격 대상에도 LOS를 확인해 장애물 너머 피해를 차단한다. 배·난간·기둥을 통째로 Ignore하지 않는다.

BlockedLOS는 Player의 실패 당시 배 로컬 바닥 위치·SurfaceId·대상·HostShip·시각을 저장한다. 무작위 회피로 바꾸지 않고 해당 방향으로 접근하며 현재 Player에게 공격 가능해지면 즉시 공격 분기로 전환한다. 복구 중 타깃의 이동으로 스냅샷을 덮어쓰지 않는다. 그래프 재생성은 저장 로컬 위치를 새 핸들로 투영한다.

복구 경로 선택 실패는 0.3초 간격으로 재시도하며, 공격 몽타주 종료부터 3초 안에 해결하지 못하면 타깃을 해제하고 저장 위치를 Investigating으로 조사한다. 재생 중인 몽타주는 복구 시간 만료로 끊지 않는다. 이후 직접 발견하면 새 전투를 시작한다. 타깃 교체·해제의 실제 적용과 풀 복귀는 이전 복구 상태를 지운다.

## 8. 책임과 네트워크

| 담당 | 책임 |
| --- | --- |
| BaseAIController | Sight/Hearing/상위 상태 전환, 공격 후 적용할 판단 예약, Combat 세션 시작/종료, 조사 위치의 World 표시 |
| EnemyAlarmComponent | 1회 발신, 이벤트 ID/중복 방지, 고정 조사 스냅샷, 선택 사운드 RPC |
| DeckEnemyCombatComponent | 공격 준비 조건, 확정한 공격 수명·일반 피격 반응 차단, 시도별 결과, LOS 실패 스냅샷, 공유 Focus 수명 |
| DeckEnemyNavigationComponent | 목표점 선택, 목적지 Claim, Player 변화에 따른 제한된 재계획 |
| DeckWalkRouteComponent | 실제 보행 경로 이동, 정체/시간 제한, 이동 바닥 갱신 |
| DeckWalkAreaComponent / DeckWalkGraph | 보행면/정확한 끝점 검증, 지원된 경로와 거리 띠 경로 |
| GAS Attack Ability | 시작 조건과 Commit, 몽타주 완료, 실행 직전 LOS, 실제 타격/발사, 결과 기록 |
| BT Task / Decorator / Service | 시작·중단·완료, 우선순위, 제한된 주기 판단 |

AI 판단/공격/경로/Claim/스냅샷과 확정·예약 상태는 서버만 실행한다. Blackboard와 경로 내부 데이터는 복제하지 않는다. 기존 CharacterMovement 기반 위치와 공격 복제를 사용하고, 사운드만 선택적으로 전송한다. 비활성 풀 적은 경보 수신·이동·공격을 수행하지 않는다.

## 9. 기존 코드 정리와 에셋

삭제:
- BTT_StartPath / BTT_EndPath: 동작 없는 기존 Task. 저장된 에셋의 클래스 참조가 없음을 확인하고 제거.
- UPathMovement: 삭제 Task만 포함하던 미사용 이동 컴포넌트. 직접 Transform 이동/별도 경로 복제를 제거.
- DeckEnemyNavigation의 이전 역할별 선호 거리, 무작위 release-LOS 후보 선정과 관련 상태/API.

보행면용으로 재구현:
- BTT_SelectDeckWaypoint / BTT_MoveToDeckWaypoint는 저장된 Passive BT의 클래스 호환성을 유지하며 목표 선택과 실제 이동 책임을 나눈다.
- BTT_WaitAtDeckWaypoint는 보행면 Passive 대기에 사용할 수 있어 유지한다.
- BTD_HasDeckReleaseLOSReposition은 기존 BT 호환용으로 유지하되 Melee/Ranged 공통 Combat 컴포넌트에 위임한다.
- 기존 BTT_EnemyBasicAttack, BTT_RangedAttack, MoveToWeaponRange, RetreatToWeaponRange는 지상/보스 에셋 참조가 있어 유지한다. 신규 일반 Deck Combat은 BTT_DeckAttack 및 보행면 이동만 사용한다.

신규 에셋 폴더:
`/Game/GameplayAbilitySystem/Enemy/AI/SubTree/DeckCombat`

- BT_DeckMelee_Combat / BT_DeckRanged_Combat
- BT_DeckMelee_Combat_NoAlarm / BT_DeckRanged_Combat_NoAlarm
- BT_DeckEnemy_Investigating

DA_DeckMeleeEnemy_AI / DA_DeckRangedEnemy_AI의 Combat과 Investigating 항목을 신규 트리에 연결한다. 경보 없는 적은 해당 BehaviorSet 변형의 Combat 항목을 NoAlarm 트리에 연결한다. 기존 BT 에셋은 보존하며, 작업 시작 시 수정되어 있던 DeckRanged Combat 에셋을 덮어쓰지 않는다.

DeckCombatAuthoringCommandlet는 이 다섯 트리의 편집 가능한 그래프와 두 BehaviorSet 연결을 생성한다. 기본 실행은 이미 생성된 신규 트리를 덮어쓰지 않는다. `-RebuildGenerated`를 명시하면 이 다섯 트리의 에디터 수정 내용을 초기 구조로 다시 생성하므로 초기 재생성이 필요할 때만 사용한다. 기존 다른 BT는 수정하지 않는다. 자동화 테스트 실행용 기능이 아니다.

## 10. 검증 및 에디터 확인

사용자 요청에 따라 자동화 테스트는 실행하지 않았다. Unreal Editor Development 빌드가 통과했으며, 신규 BT 다섯 개는 저장 후 별도 Unreal 프로세스에서 오류 없이 다시 로드되고 두 BehaviorSet에 연결됨을 확인했다. 공격 지속성 개편은 코드와 문서에 적용하며 기존 BT 에셋은 다시 생성하지 않았다. 실제 전투/멀티플레이 동작은 아직 실행 검증하지 않았으며 아래 항목으로 에디터에서 확인한다.

1. Player를 발견하면 Player의 당시 위치에 경보가 발생하고, 청자는 조사 후 직접 발견해야 Combat으로 진입한다.
2. 배 이동·회전 중 조사 지점이 갑판 로컬 위치를 유지하고 Player 이동을 따라가지 않는다.
3. 같은 전투 루프와 BT 재시작에서 경보를 재발신하지 않는다. 풀 복귀 후 이전 경보가 적용되지 않는다.
4. Melee/Ranged의 Task별 목표 거리로 접근/후퇴한다. 정확한 점이 막히면 허용 오차 안의 위치를 선택하거나 재시도한다.
5. 쿨타임 중 Player를 바라보며 좌우로 이동한다. 바닥 밖/막힌 구간에서는 멈춘다.
6. 사거리·LOS·쿨다운이 모두 충족되면 Lower Priority로 이동/대기를 끊고 공격한다.
7. 공격 확정 뒤 최대 사거리 밖/원거리 최소 사거리 안으로 이동해도 몽타주가 끝난다. 근접은 실제 타격 영역으로 판정하고, 원거리는 발사 시점 Player 위치로 한 번 발사한다.
8. 공격 중 시야 상실·재발견, 타깃 교체·사망·제거가 몽타주를 끊지 않고, 종료 후 최신 판단을 적용한다. 일반 소음은 전투 종료를 예약하지 않는다.
9. 일반 피격 시 피해는 받으면서 공격을 유지한다. 종료 후 일반 피격 반응이 정상 복원된다. 기절·Frozen은 즉시 공격을 중단한다.
10. 발사/타격 직전 기둥 뒤로 숨으면 실제 발사/타격은 차단하되 몽타주는 완료하고, 종료 뒤 실패 위치를 복구한다. 15초보다 긴 몽타주도 끝까지 재생한다.
11. Enemy 사망·풀 재사용·경로 실패에서 경로, Claim, Focus, Ability delegate, 발사 예약, 전환 예약, 피격 반응 차단이 남지 않는다.
12. 서버와 두 클라이언트에서 투사체/피해가 한 번 발생하고, 선택 사운드가 중복 실행되지 않는다.
13. 기존 보스 및 지상 Enemy BT가 기존 Task를 정상 로드한다.
