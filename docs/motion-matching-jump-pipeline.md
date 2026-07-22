# Motion Matching Jump Pipeline

## 목표

점프 시작은 한 번 선택한 non-loop asset을 끝까지 유지하고, 공중 조향 입력은 JumpStart asset 재선택이 아니라 CharacterMovement의 수평 이동에만 반영한다.

## C++ 동작

1. `HandleJumpStarted()`가 jump event가 승인된 순간의 수평 속도와 로컬 방향을 snapshot으로 저장한다.
2. `bJumpStartWasMoving`으로 stand/moving JumpStart PSD를 선택한다. 새 PSD가 지정되지 않은 프로젝트는 기존 `JumpStartDatabase`를 fallback으로 사용한다.
3. JumpStart query의 미래 trajectory는 live WASD 입력 대신 launch snapshot으로 만든다. 정지 점프의 future XY는 정지 상태를 유지한다.
4. Motion Matching node가 한 번 선택한 JumpStart asset을 lock한다. 이후 engine 내부 search result가 바뀌거나 시간이 rewind되어도 lock된 asset/time으로 복구한다.
5. JumpStart 동안 blend stack은 top player 하나만 남긴다. 이전 locomotion, 다른 JumpStart, 중복 player가 공중에서 겹치지 않는다.

## 필수 에디터 설정

`ABP_Player`의 `UMotionMatchingAnimInstance` 기본값에서 다음 PSD를 지정한다.

| 속성 | 넣을 asset |
| --- | --- |
| `JumpStartStandDatabase` | Stand, 전/후/좌/우 정지 점프 시작만 포함 |
| `JumpStartMovingDatabase` | Walk/Run/Sprint 이동 점프 시작만 포함 |
| `JumpStartDatabase` | 위 두 PSD가 비어 있을 때의 하위 호환 fallback |

`PSD_Jump_Start`에는 현재 normal, walk/run/sprint, cliff, across가 함께 들어 있다. 이를 fallback으로만 사용하고, 다음 asset은 normal stand/moving PSD에서 제외한다.

- `*_Start_Cliff_*`
- `*_Start_Across_*`
- traversal, vault, ledge 전용 시작 asset

Cliff/Across는 별도 traversal event와 별도 PSD에서만 선택한다.

## Notify 규칙

- 선택된 JumpStart sequence에는 `AN_LocomotionFinished`의 `StartFinished` notify를 하나만 둔다.
- notify가 누락된 asset을 위해 `JumpStartMaxDuration` timer는 fallback으로 유지한다.
- JumpStart PSD에 loop asset을 넣지 않는다.

## 검증 시나리오

1. 정지 상태에서 점프한 뒤 체공 중 WASD를 누른다. 첫 JumpStart asset과 시간이 바뀌지 않아야 한다.
2. 걷기, 달리기, sprint, 전후좌우 및 대각 방향에서 점프한다. launch 당시 분류에 맞는 PSD만 선택되어야 한다.
3. 짧은 턱, 경사, ledge off, 착지 직후 재점프를 반복한다. JumpStart stack이 하나를 초과하지 않아야 한다.
4. listen server와 client에서 동일한 시험을 한다. 원격 pawn도 jump event 이후 다른 JumpStart로 rewind되지 않아야 한다.

`p.MMDebugging 1`을 켠 뒤 `Saved/Logs/MMCapture.log`에서 JumpStart 구간의 `Stack`, `PostTop`, `Sel`을 확인한다. 같은 jump event에서 `Stack`은 1이고, `PostTop` asset은 고정되어야 한다.
