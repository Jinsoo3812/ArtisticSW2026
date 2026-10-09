# Samurai 연속 SlashDash 에디터 설정

## 빠른 실행

1. 에디터를 종료한 상태에서 `ArtisticSW2026Editor / Win64 / Development`를 빌드하고 다시 연다. 새 클래스와 구조체가 있으므로 Live Coding만으로 갱신하지 않는다.
2. `GA_BossChainSlashDash`를 부모로 `BPGA_SamuraiChainSlashDash`를 만든다. 기존 `BPGA_SlashDash`의 `Montage Config`, `Path Presentation`, 돌진 시간·피해 설정을 복사한다. 기존 단발 능력은 계속 사용한다.
3. Class Defaults → `Boss / Chain Slash Dash / Steps`의 두 항목에서 `Distance Beyond Player`(cm)를 설정한다. 기본 예시는 1회차 `2.0초 / 400cm`, 2회차 `0초 / 500cm`이다. `Wait Before Dash`는 첫 회차에만 적용하며 Windup 진입 애니메이션 길이/PlayRate 이상이어야 한다. 두 번째 이후 회차는 기존 BP에 대기 값이 저장되어 있어도 재충전 없이 바로 돌진한다. 전체 대기에 기존 WindupHoldDuration을 다시 더하지 않는다. 필요하면 `Selection Settings / Maximum Dash Distance`를 늘린다.
4. 실제 스폰되는 Samurai BP의 `Starting Abilities`에 신규 BP를 추가한다. Combat BT에서 `Can Activate Ability By Tag = GameplayAbility.Boss.ChainSlashDash`(Observer Aborts = None) → `Activate Boss Ability`를 연결한다. 같은 태그를 지정하고 **Require Preselected Destination 끄기**, **Prefer Current Weapon Ability 끄기**, **Clear Destination When Finished 켜기**로 설정한다. 별도 목적지 선택·Wait·Repeat·BasicAttack 노드는 필요 없다. 기존 단발보다 높은 우선순위에 두면 신규 능력의 쿨타임 중 단발이 선택된다.
5. `/Game/GameplayCues` 아래에 `SW Gameplay Cue Looping Feedback`을 부모로 Cue BP를 만들고, `Gameplay Cue Tag = GameplayCue.Boss.Samurai.Charging`으로 지정한다. `Niagara System`에 루프형 충전 효과를 넣고 `Local Offset`으로 Samurai 캡슐 중심 기준 위치를 조절한다. 능력의 `Boss / Dash / Presentation / Charging Gameplay Cue Tag`는 같은 태그를 사용한다. 시작·종료 그래프는 작성할 필요 없다. 기존 단발에도 같은 충전 Cue가 적용된다. 태그를 비우면 해당 능력의 충전 효과를 생략한다.
6. 필요하면 `Boss / Vanish`의 첫 진입 준비·은신·재등장 대기와 출발/도착 Cue를 설정한다. 후속 회차는 준비 몽타주·준비 대기를 생략하고 은신과 재등장 대기를 각각 0.01초로 사용한다(실제 시간은 서버 프레임 단위로 처리된다). 기본 출발·도착 태그는 Rogue와 공유하며 각각 다른 태그를 지정할 수도 있다. 준비 몽타주는 선택 사항이다. Compile/Save 후 PIE를 실행한다.

## Decal

기존 `SWPathGameplayCueNotify` 계열 경로 Decal은 기본적으로 `/Game/GameplayCues/Path/Materials/M_EnemyAttackTelegraph`를 사용하고 빨간색을 표시한다. Cue BP의 `GameplayCue / Path / Decal`에서 `Decal Color` 또는 머티리얼을 바꿀 수 있다. 새 머티리얼의 색 파라미터는 `DecalColor`, 투명도 파라미터는 `DecalOpacity`다. 충전 시간 동안 경고 경로를 표시하고 돌진 시작 시 제거한다.

## 동작과 책임

- 기본은 첫 은신 → 재등장 → 충전 → 돌진 → 짧은 재배치 → 즉시 다음 돌진 → 마지막 Recover 순서다. 배열 길이를 바꾸면 회차 수가 바뀐다. 각 Slash 애니메이션과 돌진 이동이 모두 완료된 후 다음 회차로 넘어가며, 중간 Recover와 후속 Windup은 생략한다. 피해 중복 방지 창은 회차마다 새로 연다.
- 재등장은 Player 시선의 후방, 돌진 Point는 Samurai→Player 직선의 Player 너머 지정 거리다. 재등장 후 경고 경로를 확정하고 대기 중 추적 변경하지 않는다.
- 시작 시 선택한 Player를 계속 추적한다. 사망 또는 삭제 시 마지막 위치와 방향을 고정하고 안전한 경로가 있으면 남은 회차를 계속 실행한다. 시체 이동·다른 Player·리스폰 위치로 바꾸지 않는다. 고정 위치는 움직이는 선박의 갑판을 따라간다.
- Point가 갑판 밖이거나 경로가 막히면 `[Logtemp] Suitable Point selection failed` 로그에 Task/Avatar/실패 사유를 남기고 능력을 Cancel한다. 은신·단발 돌진·연속 돌진의 목적지/경로 검증에도 같은 규칙을 적용한다. Samurai 본인의 사망·피격 취소는 기존 GAS 규칙으로 처리한다.
- 새 능력의 쿨타임은 전체 종료 후 시작하며, 은신이 시작된 뒤 취소된 경우에도 적용한다. 단발 쿨타임은 별개다.
- 연속 능력은 순서·쿨타임, TargetSnapshot 작업은 타깃 좌표 수명, VanishRelocation 작업은 은신 이동, SlashDashExecution 작업은 한 번의 돌진·경고·충전·몽타주·충돌 정리를 담당한다. 피해 Spec과 적용은 능력이 담당하고 회차마다 피해 중복 방지 창을 다시 연다.
- 전투 실행은 서버 전용이다. 충전은 서버 ASC의 지속 Add/Remove GameplayCue로 전달하여 늦게 관찰한 클라이언트도 현재 충전 상태를 표시할 수 있다. Cue 액터는 각 클라이언트에서 Samurai에 붙고 돌진·취소·제거·풀 재사용 시 효과를 정리한다. 별도 클라이언트 RPC나 중복 로컬 Cue 실행은 추가하지 않는다.

## Point 선정 실패 공통 규칙

- 보스 목적지 선택(걷기/은신/돌진), 갑판 Patrol/Investigation/Combat/LOS Recovery 선택, Patrol Smart Object, 일반 Patrol, 후퇴 지점, Strafe 지점에 공통 `[Logtemp]` 로그를 적용한다. 후보마다 로그를 출력하지 않고 최종 선정 실패 시 기록한다.
- 능력 내부 실패는 소유 능력을 취소하고 기존 종료 경로에서 타이머·이동·충돌·Cue를 정리한다. 능력 활성화 전 BT 선택 Task는 `Failed`로 해당 분기를 종료하고 목적지/경로를 정리하여 후속 능력이 실행되지 않게 한다.
- 갑판 이동 주위회전 Task는 포인트 선정 실패 후 0.3초마다 반복하던 재시도를 제거하고 즉시 종료한다. 이미 다른 능력이 예약한 목적지는 기존 Busy 보호 규칙을 따른다.

검증: 최종 `ArtisticSW2026Editor / Win64 / Development` 빌드가 성공했고 빨간 Decal 머티리얼 자산을 생성·저장했다. 자동화 테스트는 사용자 요청에 따라 추가·실행하지 않았다. 실제 반복 동작과 충전 Niagara 표시, 2인 PIE 동기화는 아직 실행하지 않았으며 위 에디터 연결 후 확인한다.
