# Rogue 연속 은신 공격 구현 및 에디터 설정

## 동작

- 기본 은신: BT가 선택한 목적지로 1회 은신 이동 후, 공격 가능한 위치라면 기존 보스 기본공격을 1회 실행한다.
- 연속 은신 (`GA_BossChainVanish`): 최대 3회까지 은신 이동한다. 각 재등장 직후 공격 가능 여부를 판단하고, 가능하면 즉시 1회 공격한 뒤 몽타주가 완전히 끝날 때 능력을 종료한다. 이미 첫 은신으로 공격이 가능하면 1회만 사용한다.
- 기본 은신의 공격 가능 여부: 타깃 유효성, 무기 장착, 같은 갑판 Surface, 현재 무기 AttackRange 이내의 거리, Visibility 채널 장애물 검사를 사용한다. 실제 명중·피해는 기존 무기 HitScan이 결정한다.
- 연속 은신의 공격 판단 거리는 `max(0, 무기 AttackRange - Attack Range Inset)`이다. 차감값은 능력 BP에서 cm 단위로 조절하며 기본값은 0이다. 실제 검의 피해 판정 크기는 변경하지 않는다.
- 능력이 시작될 때 플레이어 한 명을 서버에서 고정한다. 이후 AI/Blackboard 타깃이 바뀌어도 모든 재배치·방향·공격 위치 판단은 처음 플레이어를 사용한다. 서버 전용 약한 참조로 보관하고 종료 시 해제한다.
- 두 번째 이후 목적지는 고정한 플레이어의 최신 위치와 방향을 기준으로 `BehindTarget` 정책으로 다시 선택한다. 첫 목적지는 기존과 동일하게 BT가 선택한다.
- 추적 대상 고정은 피해 대상 제한이 아니다. 검에 다른 플레이어도 함께 닿으면 기존 피해 판정에 따라 모두 피해를 받는다.
- 고정한 플레이어가 사망·삭제되거나 할당된 Combat Territory를 벗어나면 서버가 최대 0.1초 간격으로 확인해 기술을 취소하고 쿨타임을 적용한다. 점프 자체는 타깃 상실로 취급하지 않는다. 갑판 밖이라 안전한 다음 목적지를 선택할 수 없는 경우에도 해당 시도는 종료된다.
- 3회 후에도 공격할 수 없거나 안전한 다음 목적지를 선택할 수 없으면 공격 없이 종료한다.
- 기본 은신 사용 횟수 제한·강제 교대는 없다. 독립적인 GAS 쿨타임과 BT 우선순위로 선택한다.

## 쿨타임

| 능력 | 설정 위치 | 기본값 | 시작 기준 |
|---|---|---|---|
| 기본 은신 | 능력 BP → Class Defaults → Boss / Cooldown → Cooldown Duration | 7초 | 후속 공격 몽타주의 OnCompleted |
| 연속 은신 | 능력 BP → Class Defaults → Boss / Cooldown → Cooldown Duration | 10초 | 후속 공격 몽타주의 OnCompleted |
| 연속 은신 첫 사용 | 같은 Cooldown Duration | 10초 | AI.State.Boss.Combat 진입 |

연속 은신의 초기 대기는 별도 설정값이 없고 사용 후 쿨타임과 같은 값을 사용한다. 보스 Spawn/BeginPlay 시점부터 세지 않는다. 능력이 이미 Combat 상태인 보스에게 나중에 부여되면 부여 시점부터 초기 쿨타임을 적용한다.

은신 중간에는 쿨타임을 적용하지 않는다. 실제 공격이 실행된 경우 기존 일반 공격의 공유·공격 종류별 쿨타임도 종료 시점에 적용한다. 이들은 은신 자체의 `Cooldown Duration`과 별개의 기존 공격 설정이다.

예외: 실제 은신에 들어간 후 피격 취소·타깃 상실·횟수 소진으로 종료되면 종료 시점부터 은신 쿨타임을 적용한다. 은신 시작 전에 무기/목적지/몽타주 설정이 잘못되어 실패하면 은신 쿨타임은 소모하지 않는다.

## 책임 분리

- `UGA_BossVanish`: 실행 타깃과 전용 시선 우선순위를 고정하고, 은신 작업과 기존 공격 실행을 연결하며 종료 시 쿨타임을 적용한다. 종료 시 전용 시선과 타깃 검증 타이머를 해제한다.
- `UGA_BossChainVanish`: 반복 횟수·조기 공격 종료·재시도 목적지 요청 및 첫 Combat 쿨타임을 관리한다.
- `UAbilityTask_BossVanishRelocation`: 전달받은 고정 타깃으로 준비 몽타주, 은신 전후 대기, 이동, 재등장, 은신 상태 정리를 관리한다. 출발·도착 이벤트에 서버 좌표를 전달하고 시각 효과 자산은 알지 않는다.
- `BossVanishFeedback`: 전달된 서버 좌표를 GameplayCue로 전파하는 효과 실행만 담당한다.
- `UBossDeckPointSelector`: 안전한 갑판 목적지를 선택한다.
- `UBossAttackPositionLibrary`: 공격 위치의 거리·갑판·장애물을 판단한다.
- `UGA_BossBasicAttack` / `UGA_BasicAttack`: 공격 선택, 몽타주, 무기 피해 판정, 타격 타이머를 재사용한다. 은신 준비 중의 공격 Notify는 차단한다.

## 빠른 에디터 설정

1. 에디터를 종료한 상태에서 `ArtisticSW2026Editor / Win64 / Development`를 빌드한 뒤 다시 연다. 기존 `GA_BossVanish`의 C++ 부모가 `GA_BossBasicAttack`으로 변경되었으므로 Live Coding만으로 갱신하지 않는다.
2. `GA_BossChainVanish`를 부모로 Blueprint `BPGA_RogueChainVanish`를 만든다. Blueprint의 ActivateAbility 그래프를 새로 작성할 필요는 없다.
3. Class Defaults에서 다음을 설정한다.
   - `Boss / Cooldown / Cooldown Duration`: `10.0` (빠른 확인용으로 `3.0`도 가능).
   - `Boss / Chain Vanish / Maximum Vanish Count`: `3` (1~3 설정 가능, 런타임도 3회 상한 적용).
   - `Boss / Chain Vanish / Attack Range Inset`: 공격 판단 거리에서 뺄 cm 값. 예를 들어 무기 범위 500cm, 차감값 100cm이면 400cm 안에서 공격한다. 기본값 0으로 기존 판단 거리를 유지한다. 무기 범위 이상으로 설정하면 공격 가능한 거리가 0이 되어 공격 없이 최대 은신 횟수 후 종료된다.
   - `Boss / Vanish`: 기존 은신과 같은 Preparation Montage, Preparation Delay, Hidden Duration, Relocation Settle Time을 복사한다. 준비 몽타주는 선택 사항이다.
   - `Boss / Chain Vanish / Retry Selection Settings`: 첫 BT 목적지 선택과 비슷한 설정을 사용한다. 현재 위치와 충분히 떨어진 목적지가 필요하므로 Minimum Travel Distance를 지나치게 크게 잡지 않는다.
4. 실제 스폰되는 Rogue 보스 BP의 `Starting Abilities` 배열에 `BPGA_RogueChainVanish`를 추가한다. 같은 기술의 native 클래스와 BP 클래스를 중복 부여하지 않는다. 공통 ShipBoss 부모 배열 전체에 추가할 필요는 없다.
5. Rogue Combat 서브트리에서 기존 기본 은신 Sequence를 복제해, 기본 은신보다 왼쪽(높은 우선순위)에 연속 은신 Sequence를 둔다.
   - `Can Activate Ability By Tag`: `GameplayAbility.Boss.ChainVanish`.
   - 실행 중 쿨타임·Busy 상태 때문에 자기 분기가 중단되지 않도록 이 데코레이터의 Observer Aborts는 `None`으로 설정한다. 초기 확인에는 선점 없이 각 행동 종료 후 다음 패턴을 선택한다.
   - `Select Boss Walk Area Destination`: Selection Purpose = `Vanish`, Destination Relation = `BehindTarget`, DestinationLocation 키 사용.
   - `Activate Boss Ability`: Ability Asset Tag = `GameplayAbility.Boss.ChainVanish`, Require Preselected Destination = 켜기, Clear Destination When Finished = 켜기, Prefer Current Weapon Ability = 끄기.
   - 별도 BasicAttack Task를 이 Sequence 뒤에 붙이지 않는다. 은신 능력이 공격 완료까지 처리한다.
6. 기존 기본 은신 분기도 `GameplayAbility.Boss.Vanish`를 실행한 뒤 별도 BasicAttack Task가 붙어 있다면 제거한다. 이제 기본 은신 자체가 후속 공격을 수행한다. `VanishV2`를 사용 중인 분기도 동일하다.
7. 기존 은신 BP를 Compile/Save한다. 기존 `BPGA_Vanish`가 Blueprint ActivateAbility 그래프에서 자체 은신·공격을 구현한다면 그 그래프를 그대로 실행하지 말고, native `GA_BossVanish`를 부모로 한 단순한 능력 BP로 교체하고 준비 설정을 복사한다. 이미 native 구현을 사용하는 BP는 설정만 유지한다.
8. Rogue의 `Basic Attack Set`, 현재 무기와 몽타주 연결을 확인한다. 공격 몽타주에는 기존 `ANS_HitScanWindow`가 있거나 Attack Set의 Timed Hit Scan Window가 설정되어 있어야 한다. 능력용 별도 Cooldown Gameplay Effect를 지정할 필요는 없다.

설정할 BT는 실제 Rogue BP의 Behavior Set에서 Combat에 연결된 서브트리를 사용한다. 저장소에 Rogue 이름의 서브트리가 여러 경로에 있으므로 이름만 보고 선택하지 않는다.

## 출발·도착 GameplayCue 설정

1. 능력 BP의 Class Defaults → `Boss / Vanish / Feedback`에서 `Departure Gameplay Cue Tag`와 `Arrival Gameplay Cue Tag`를 설정한다. 기본 태그는 각각 `GameplayCue.Boss.Vanish.Departure`, `GameplayCue.Boss.Vanish.Arrival`이다. 기본 은신과 연속 은신이 모두 이 설정을 사용한다. 태그를 비우면 그 효과를 생략한다.
2. 프로젝트의 GameplayCue 검색 경로 아래에 두 `GameplayCueNotify_Static` Blueprint를 만든다. 각각 위 출발·도착 태그를 연결한다. 기존 GameplayCue 경로/검색 설정을 사용한다.
3. 각 Blueprint의 `OnExecute`에서 `Parameters.Location`에 Niagara 또는 효과를 `Spawn System at Location`으로 생성한다. 보스의 현재 위치를 다시 읽거나 보스 Mesh에 Attach하지 않는다. 출발 Cue 수신 시 보스는 이미 순간이동했거나 숨겨져 있을 수 있다.
4. 회전이 필요하면 `Parameters.Normal`(서버에서 기록한 갑판 Up 방향)을 사용한다. 효과 자산은 이번 변경에서 만들지 않았다.

출발 이벤트는 은신 시작 성공 직후에 보내되, 숨기기 직전의 서버 위치를 담는다. 도착 이벤트는 실제 이동과 기존 Relocation Settle Time이 끝나 정상 재등장할 때 보낸다. 이동 실패나 취소 정리에서는 도착 Cue를 보내지 않는다. 매 은신마다 각 이벤트는 한 번씩 실행된다.

서버가 GAS `ExecuteGameplayCue`로만 실행하고 클라이언트에서 추가 실행하지 않으므로 중복 효과를 만들지 않는다. 위치는 Cue Parameters에 명시하고 Minimal ASC 모드의 위치 전달 플래그도 설정한다. 이동·피해·타깃 선택은 Cue 수신 여부에 의존하지 않는다. GAS의 일회성 Execute Cue는 표준 unreliable 전달 방식이므로 극단적인 패킷 손실에서 시각 효과가 생략될 수 있지만 전투 결과에는 영향을 주지 않는다.

## PIE에서 확인

1. 전투 시작 직후 `Cooldown.Boss.ChainVanish`가 적용되고 지정 시간이 지나기 전에는 연속 은신이 선택되지 않는지 확인한다.
2. 플레이어를 제자리에 두어 첫 재등장으로 사거리·시야가 확보되면, 은신 1회 → 공격 1회로 종료되는지 확인한다.
3. 플레이어를 계속 이동시켜 첫 재등장 시 사거리를 벗어나면 다음 은신이 시도되는지 확인한다. 공격 가능해지면 그 시점에 반복을 멈춘다.
4. 끝까지 사거리를 벗어나면 최대 3회 이후 공격 없이 종료되는지 확인한다. 다음 목적지를 확보하지 못하면 더 일찍 종료될 수 있다.
5. 공격 몽타주가 재생되는 동안 은신 쿨타임이 시작되지 않고, 완전 종료 후 설정된 전체 시간이 적용되는지 확인한다.
6. 연속 은신 쿨타임 중에는 기본 은신·기존 다른 전투 행동이 자연스럽게 선택되는지 확인한다.
7. 은신/공격 중 피격·사망 시 모습, 충돌, 피해 판정과 행동 잠금이 정리되는지 확인한다. 멀티플레이는 2명 PIE로 순간이동 전후 은신 표시도 확인한다.
8. 다른 플레이어가 더 가까이 와도 진행 중인 은신은 처음 플레이어를 계속 추적하는지 확인한다. 두 플레이어가 검에 같이 닿으면 둘 다 피해를 받는지 확인한다.
9. Attack Range Inset을 늘려 재등장 후 공격 조건이 더 엄격해지는지 확인한다. 차감값만으로 반드시 3회 반복하는 것은 아니며, 줄어든 거리 안에 타깃이 있으면 첫 이동 후 공격한다.
10. 2명 PIE의 각 클라이언트에서 출발 효과가 원래 위치에, 도착 효과가 재등장 위치에 각각 한 번만 나타나는지 확인한다. 이동 중 취소에는 도착 효과가 발생하지 않는지도 확인한다.

Output Log의 `LogBossChainVanish`에서 초기 대기 시간, 실제 공격까지 사용한 은신 횟수, 횟수 소진을 확인할 수 있다. 자동화 테스트는 추가하거나 실행하지 않았다.

검증: 최종 변경을 포함한 `ArtisticSW2026Editor / Win64 / Development` 빌드가 성공했다. Blueprint/BT/무기 자산의 기존 사용자 수정은 보존했고, 이 작업에서는 C++와 안내 문서를 수정했다. 자동화 테스트와 PIE 실행은 생략했으며, 실제 효과와 멀티플레이 동작 확인은 위 에디터 연결 후 진행한다.
