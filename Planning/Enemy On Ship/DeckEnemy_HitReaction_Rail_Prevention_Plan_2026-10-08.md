# Deck Enemy 피격 밀림에 의한 난간 올라탐 방지 계획

작성일: 2026-10-08 (한국 시간)  
상태: 설계 제안. 이번 단계에서는 코드·Blueprint·레벨·설정을 변경하지 않는다.

추가 검토: 사용자가 캡슐 겹침을 원인으로 관찰했다. 이 경우 구현 우선순위는 아래 일반 예방안보다 **겹침 상대별 충돌 관계/형상 확인 → 안전한 겹침 해소 → StepUp/착지 추가 차단** 순서로 조정한다. `MaxStepHeight`나 피격 변위 제한만으로 겹침 해소의 상향 보정까지 막히는 것은 아니다. 상세 근거와 난간 추적 이후 BT 변화는 같은 폴더의 `DeckEnemy_Capsule_Penetration_and_Rail_BT_Analysis_2026-10-08.md`에 정리했다.

## 1. 목표와 기본 정책

일반 Deck Enemy가 피격으로 밀릴 때 갑판 안에서는 정상적으로 이동하고, 난간을 계단이나 착지 가능한 바닥으로 인정하지 않도록 한다. 잘못된 난간 접지와 갑판 경계 이탈을 이동 과정에서 예방한다.

일반 피격으로 적이 갑판 밖으로 떨어지는 것은 오류로 보고 안전 보행면 안에 유지하는 안을 기본으로 제안한다. 의도적인 띄우기·추락·사망 ragdoll은 별도 정책으로 구분한다. 모든 강제 이동을 일반 피격과 같은 제한으로 처리하면 전투 의도가 바뀔 수 있으므로 이동 원인별 허용 범위를 명시한다.

일반 Deck Enemy는 별도 은신 능력을 도입하지 않는다. 이미 잘못된 위치에 있는 경우에만 마지막 안전 위치로 제한 복구하는 안전장치를 두며, 주된 해결책은 올라타기·이탈 예방이다.

## 2. 현재 구현과 아직 확인해야 할 원인

### 확인한 사실

- `ADeckEnemy`는 기본 CharacterMovementComponent를 사용하며 이동 Base의 attachment root를 따른다. 현재 검색한 Enemy 코드에는 별도 CMC의 난간 `CanStepUp()` / `IsWalkable()` 필터가 없다.
- `DeckWalkRouteComponent::TickRoute()`는 그래프 경로에 따라 `AddMovementInput()`을 주지만 캡슐 이동·충돌은 CMC가 처리한다. 그래프 경로가 안전해도 피격 변위까지 자동 제한하지는 않는다.
- `GA_HitReaction.cpp`는 서버 AI의 Brain을 잠그고 현재 AI Move를 일시 정지한다. CMC MovementMode를 강제로 Walking으로 바꾸지는 않는다.
- 현재 `BaseHitReactionGameplayAbility.cpp`의 몽타주 Root Motion 이동 배율은 `1.0f`이다. 예전 `HitReaction_RootMotion_Network_Plan.md`의 배율 0 결함 설명은 현재 코드에 그대로 적용되지 않는다.
- 난간/갑판이 하나의 Mesh인 구성이 기존 문서에 명시되어 있다. 컴포넌트 단위 발판 금지를 전체 Mesh에 적용하면 실제 갑판도 영향을 받는다.
- `DeckWalkAreaComponent`의 `MaximumStepHeight`는 그래프 샘플링·연결 판정용이다. CMC의 실제 `MaxStepHeight`와 별개이며 하나만 바꾸면 둘이 불일치할 수 있다.
- 기존 타깃 추적은 난간 위 Player를 추적할 수 있게 설계되어 있다. 추적용 지지점과 Enemy 자신의 보행 가능 바닥은 분리해야 한다.
- `CanMoveOnDeck()`은 현재 풀 활성·생존·Host 상태 등을 보지만 Damaged 태그는 직접 검사하지 않는다. 실제 BT/Task의 피격 정지와 custom Route 실행 경합을 재현에서 확인한다.

### 원인 가설과 구분 방법

| 후보 원인 | 확인할 증거 | 우선 대응 |
| --- | --- | --- |
| Walking 중 난간을 StepUp | 충돌 후 CMC StepUp 실행, 발 높이 증가, Base/CurrentFloor가 난간으로 변경 | 적 전용 올라타기 거부 |
| 강제 Launch 또는 공중 상태에서 난간에 착지 | 먼저 Falling·상향 속도 발생 후 난간이 walkable floor로 인정 | 발판 금지와 별도로 착지/바닥 인정 거부 |
| Root Motion 또는 외력으로 경계 통과 | 피격 프레임의 요구 변위·실제 변위와 지지 구간 불일치 | 이동 과정에서 변위 제한 |
| 공유 Mesh의 난간 윗면을 갑판으로 오인 | 바닥 hit 위치·높이·Surface band·주변 노드 판정 확인 | 위치·실제 지지 기반 판정 |
| 캡슐 겹침 해소에 의한 밀어올림 | start penetration, depenetration, 캡슐·난간 단순 충돌 형태 | 충돌 형상/초기 겹침과 접촉 보정 검토 |
| 피격 중 AI 이동 재발행 | State.Damaged 중 Route tick·BT 이동 입력 및 Brain 잠금 관찰 | 이동 입력/Route 정지 계약 보강 |
| Mesh만 시각적으로 난간 위로 이동 | 서버 캡슐은 그대로이고 Mesh/Root bone만 이동 | 몽타주 추출·Root Lock·AnimGraph 점검 |

현재 C++ 검토만으로 어떤 가설이 실제 원인인지 확정하지 않는다. 실제 최종 Blueprint와 몽타주, 레벨 충돌 설정 및 서버 재현이 1단계 작업이다.

## 3. 권장 해결 구조

세 층으로 적용한다.

1. **난간 발판 인정 차단:** 올라타기와 공중 착지를 각각 거부한다.
2. **피격 변위의 안전 경계 제한:** 갑판 안쪽의 밀림은 유지하되 외곽·구멍·난간을 통과하는 요구 변위를 제한한다.
3. **잔여 예외 복구:** 겹침 해소·함선 급변위 등으로 잘못된 위치가 발생했을 때 제한된 복구를 실행한다.

`MaxStepHeight`를 0으로 만들거나 Root Motion 배율을 0으로 만드는 전역 수정은 권장하지 않는다. 계단·단차 이동과 피격 감각이 함께 사라진다. 타깃 추적 투영을 넓혀 난간을 보행면으로 인정하는 방식도 문제를 숨긴다.

## 4. 1단계: 재현과 실제 에셋 감사

1. 최종 SpawnPlan이 사용하는 Melee/Ranged 및 Tier/Final 자식 Blueprint를 목록화한다.
2. 실제 피격 GA, 앞뒤좌우 몽타주, Root Motion 추출 여부·배율, root Z, 추가 Blueprint Launch/AddImpulse/직접 위치 이동 경로를 확인한다. 자식 오버라이드도 포함한다.
3. 문제 난간이 별도 컴포넌트인지 갑판과 공유 Mesh인지 확인한다. Simple/Complex collision, Pawn 응답, StepUp, Walkable Slope, 난간 높이·폭·낮은 돌출·틈을 기록한다.
4. 난간 정면·측면·모서리에서 네 방향 피격, 연속 피격, 다른 적과 끼인 피격을 재현한다. 정지/이동/회전/상하 운동 함선으로 확장한다.
5. 서버 캡슐과 클라이언트 Mesh를 함께 관찰한다. 요구/실제 변위, 속도, MovementMode, CurrentFloor hit, StepUp/착지, Base, Surface, Root Motion, 겹침 해소, AI 입력을 기록한다.

완료 조건: 최초 잘못된 높이 변화가 StepUp·착지·penetration·시각 이동 중 어디서 발생하는지 확인한다. 여러 원인이 있으면 각각 분리한다.

## 5. 2단계: 난간의 올라타기와 착지 금지

### 5.1 별도 난간 충돌 컴포넌트가 있는 경우

- 적의 캡슐을 막는 충돌은 유지한다. 난간 충돌을 Ignore로 만들면 적이 바로 통과한다.
- `CanCharacterStepUpOn = No`를 후보로 검토하되 Player에게도 적용되는 공유 설정인지 먼저 확인한다.
- StepUp 금지만으로 공중 착지 금지까지 해결되었다고 보지 않는다. 난간 윗면을 적의 유효 floor로 거부한다.
- Player의 난간 이용을 유지해야 하면 적 전용 CMC의 `CanStepUp()` / `IsWalkable()` 판정 또는 적 전용 난간 blocker 구성을 사용한다. 별도 blocker는 공격·화살·Player 충돌 채널을 함께 검토한다.

### 5.2 갑판·난간이 하나의 Mesh인 경우

- 컴포넌트 전체의 StepUp/Walkable 설정을 끄지 않는다.
- 해당 Enemy 전용 CMC가 Hit의 실제 위치·법선·높이·HostShip·Surface와 등록된 보행면을 확인하게 한다.
- 보행면에 속한 실제 계단·경사·단차는 엔진 기본 검사와 함께 허용하고, 보행면 밖 난간 윗면은 거부한다.
- 필요하면 난간 전용 의미 영역/충돌 프록시를 함선 로컬 좌표로 authoring한다. 주변 보행 노드 하나만으로 난간을 갑판으로 인정하지 않는다.
- 기존 보행면 샘플 자체에 난간 윗면이 포함되어 있다면 Floor source/height band와 의미 영역부터 정리한다. 그래프가 잘못되면 그래프 검사만 추가해도 해결되지 않는다.

### 5.3 CMC 적용 범위

`UDeckEnemyCharacterMovementComponent` 같은 신규 클래스를 제안한다. 일반 Deck Enemy 생성자에서만 대체하고 Player와 Ground Enemy의 기본 CMC는 유지한다. 보스에도 같은 예방 정책을 적용할지 별도 설정으로 판단할 수 있으며 보스 복구 계획과 책임을 섞지 않는다.

설치된 UE 5.7 헤더에서 `CanStepUp()`, `IsWalkable()`, `ApplyRootMotionToVelocity()`, `MoveAlongFloor()`, `PhysWalking()`, `PhysFalling()`의 virtual 지점을 확인했다. 실제 구현에서는 최소한의 지점만 확장하고 엔진의 이동 루프 전체를 복제하지 않는다. `IsWalkable()`의 내부 probe에 다시 Character floor 질의를 호출하여 재귀하지 않도록 순수 공간 질의와 호출 의존성을 분리한다.

CMC 바닥 판정 중 매번 전체 보행 그래프를 탐색하지 않는다. 함선별 의미 영역/노드 공간 조회를 활용하고 동일 이동 업데이트 내 결과를 재사용한다. 서버 Ready 상태와 클라이언트의 그래프 가용성이 다른 점도 고려한다.

## 6. 3단계: 피격 이동을 안전 영역 안에서 제한

### 6.1 안전 영역의 정의

- 유효 보행면에서 실제 캡슐 반경과 여유를 고려한 영역. 외곽과 갑판 구멍도 경계에 포함한다.
- 현재 HostShip의 로컬 좌표를 사용하되 실제 이동·충돌은 CMC의 좌표계와 중력 기준을 따른다. 함선 Up과 세계/캐릭터 중력을 무조건 같다고 취급하지 않는다.
- 시작과 끝만 검사하지 않고 이동 중 지지 구간·캡슐 장애물을 검사한다. 구멍을 건너 반대편 노드로 이동하는 것을 허용하지 않는다.
- 그래프의 유효 segment 판정과 실제 캡슐 크기·바닥 probe를 함께 사용한다. 현재 CellSize보다 촘촘한 검사가 필요한 고속 변위는 길이에 따라 샘플링하거나 제한된 부분 구간 탐색을 수행한다.
- 함선 이동에 따른 Base 변위와 피격 자체의 상대 변위를 구분한다. 함선 속도를 피격으로 오인하여 제거하지 않는다.

### 6.2 프레임별 처리 제안

```text
피격 상태와 이동 원인 확인
  → Root Motion/외력/이동 입력에서 요구 변위 계산
  → 실제 CMC 이동을 적용할 지점에서 지지·캡슐 구간 검사
  → 안전하면 원래 변위 유지
  → 위험하면 마지막 안전 비율까지 줄임
  → 경계 바깥 방향의 잔여 속도/반복 힘만 정리
  → 기본 CMC 충돌·슬라이드·바닥 처리
  → 실제 결과 검증
```

- 안전 구간을 찾아 원래 피격 방향의 허용 거리만큼 밀린다. 일반적인 피격마다 가장 가까운 노드 중심으로 스냅하지 않는다.
- 벽·난간을 맞으면 CMC가 충돌을 처리하며, 접선 방향 밀림은 지지 구간이 안전한 경우에만 허용한다.
- Root Motion 속도를 사전에 줄이는 것만으로 StepUp·슬라이드·depenetration 결과까지 보장하지 않는다. 이 때문에 2단계의 난간 판정과 이동 후 검증도 필요하다.
- `bCanWalkOffLedges = false`는 Walking 예방의 보조 설정 후보로 검토한다. 이미 Falling인 상태, Launch, 직접 위치 이동까지 막는 완전한 해결책으로 사용하지 않는다.
- 일반 피격의 원치 않는 상향 Launch가 발견되면 해당 이동 원천을 우선 수정한다. 모든 Z 이동을 전역 제거하지 않는다.
- 여러 이동 원천이 같은 피격에 중첩되는 경우 Root Motion과 추가 impulse가 중복 적용되는지 확인한다. 요구 변위를 두 번 합산하지 않는다.

### 6.3 적용 위치와 상태 계약

서버 적 CMC의 실제 이동 경로에서 제한한다. 컴포넌트 Tick으로 이동이 끝난 뒤 매 프레임 `SetActorLocation()`하는 방식은 주된 해결책으로 쓰지 않는다. 몽타주·CMC 복제와 기존 Root Motion 처리를 유지한다. 서버의 피격 제한 결과가 simulated proxy에 자연스럽게 전달되는지 검증한다.

`GA_HitReaction`은 피격 시작/끝의 정책 활성화와 AI 입력 중지 계약을 맡는다. custom DeckWalk Route도 피격 중 새 이동 입력을 발행하지 않도록 연결한다. 피격 재발동·취소·사망 시 상태가 정확히 해제되어야 한다. 기존 Brain lock을 중복 해제하거나 다른 시스템의 lock을 지우지 않는다.

일반 피해, 강제 넉백, 의도적 띄우기, 사망은 별도 이동 정책으로 구분한다. 태그 하나만 확인하면 능력 취소 순간의 잔여 속도를 놓칠 수 있으므로 이동 원인·적용 세대와 잔여 힘 정리도 설계한다.

## 7. 4단계: 이동 후 예외 복구

예방을 통과했더라도 실제 발 위치가 난간/무효 바닥에 있거나 갑판 밖으로 이탈하면 다음 순서로 처리한다.

1. 현재 피격 변위와 custom Route 입력을 중지한다. Stun/피해 상태 자체를 무조건 해제하지 않는다.
2. 실제 안전 접지에서 기억한 로컬 위치·Surface를 현재 Revision으로 다시 해석한다.
3. 가까운 안쪽 후보의 바닥·캡슐·Claim을 검사하고 예약한다. 같은 층을 우선하며 아래/위층에 무조건 투영하지 않는다.
4. 난간 위에서 가까운 갑판으로 내려올 때도 sweep와 지지 경로가 안전하면 짧은 이동으로 복구한다. 이미 밖이거나 걸렸다면 안전한 후보로 서버에서 한 번 보정한다.
5. 새 바닥 검사 후 유효한 Base·MovementMode를 설정한다. 바닥이 없는데 Walking만 강제하지 않는다.
6. 실제 위치에서 경로를 다시 검증·계획하고 AI를 재개한다. 이전 경로 인덱스로 곧바로 재개하지 않는다.

복구가 필요한 피격은 로그로 남겨 예방 실패 사례로 추적한다. 캐릭터 위치를 반복 보정하는 정상 동작으로 정착시키지 않는다.

사망·ragdoll·풀 비활성·스냅샷 복원 중·함선 파괴에서는 일반 피격 제한과 복구를 정지한다. 일반 Deck Enemy는 풀 재사용이 있으므로 안전 위치·Host·Revision·복구 타이머·활성화 세대를 반환/재활성화 때 정리한다. 이전 함선에서 시작된 callback이 새 개체 생애를 보정하지 못하게 한다.

안전 후보가 없으면 제한된 시간 동안 이동/공격을 정지하고 재시도한다. 상한 초과 후에는 기존 spawner의 제거/풀 반환 흐름으로 종료하는 안을 제안한다. 이때 자연 사망·보상·처치 카운트·웨이브 완료·재스폰 수량과의 관계를 별도 정리하여 임의 제거로 처치 보상이 발생하거나 웨이브가 멈추지 않도록 한다. 일반 실패 종료를 정상 처치와 같게 처리하지 않는다.

## 8. 구현 순서와 영향 범위

| 단계 | 내용 | 완료 조건 |
| --- | --- | --- |
| 1 | 재현, 실제 난간/몽타주/BP 감사 | 최초 올라탐 원인과 서버 캡슐 이동 여부 확인 |
| 2 | 실제 안전 바닥·난간 의미 판정 공통 질의 | 공유 Mesh에서도 갑판과 난간을 구분 |
| 3 | 적 전용 StepUp/착지 차단, 필요한 충돌 자산 수정 | Walking과 Falling 경로 모두 난간 바닥 거부 |
| 4 | 피격 변위 안전 구간 제한, AI Route 정지 계약 | 정상 밀림 유지, 외곽/구멍 통과 방지 |
| 5 | 마지막 안전 위치와 제한 복구, 풀·Host 수명 관리 | 잔여 예외 및 재사용 경합 처리 |
| 6 | 멀티플레이·계단·전투·풀 회귀와 튜닝 | Player/Ground Enemy 및 정상 Deck 이동 영향 없음 |

단계 3만으로 재현이 해결되더라도 Root Motion·Launch·외곽 구간을 검증하고 필요한 예방 범위를 판단한다. 별도 난간 설정으로 충분한 함선에 불필요한 충돌 프록시를 추가하지 않는다.

예상 코드 대상: 신규 Deck 전용 CMC, `DeckRangedEnemy.{h,cpp}`의 `ADeckEnemy` 구성·풀 수명, `GA_HitReaction.{h,cpp}`, `DeckWalkAreaComponent.{h,cpp}`의 지지/구간 질의, Route/이동 Task의 정지·재개 계약, 필요한 테스트. 공통 `BaseHitReactionGameplayAbility`는 전역 배율을 바꾸는 대상이 아니며, 필요할 때만 공통 정책 hook을 최소 추가한다.

예상 자산 대상: 실제 함선의 난간 충돌/의미 영역, 보행면 Floor source 및 Height band, 최종 Deck Enemy Blueprint의 CMC/정책 값, 문제 피격 몽타주. Source 변경과 자산 변경은 별도 검증 단위로 관리한다.

## 9. 검증 행렬

| 축 | 확인 사례 |
| --- | --- |
| 적 종류 | Melee/Ranged, 실제 사용 Tier/Final BP, 서로 다른 캡슐 크기 |
| 피격 | 앞뒤좌우, 연속/재발동, 중단, Root Motion, 추가 impulse/Launch, 강한 넉백 |
| 구조 | 낮고 높은 난간, 모서리·좁은 폭·틈, 별도/공유 Mesh, 갑판 외곽·구멍·천장·계단·경사 |
| 접촉 | 다른 적/Player에 끼임, 초기 penetration, 좁은 공간, 예약 경쟁 |
| 함선 | 정지·직선 이동·회전·상하/기울기 변화, Lower/Upper Surface, 다른 배 근접 |
| 수명 | 사망 ragdoll, 풀 반환·재활성화·Host 변경, 스냅샷 복원, 함선 파괴, Revision 변경 |
| 네트워크 | Standalone, Listen+원격, Dedicated+2 Clients, 지연·패킷 손실·관련성 이탈/복귀 |

자동화는 공유 Mesh의 난간 hit 분류, StepUp/착지 허용 차이, 구멍을 건너는 변위, 안전 비율 제한, 정상 계단/단차, 피격 재발동 정리, 풀 세대 변경 callback을 중심으로 추가한다. 기존 DeckEnemyMVP 및 HitReaction 관련 테스트를 회귀 실행한다. 실제 몽타주·함선 물리·시각 보정은 PIE 수동 검증을 병행한다.

### 수용 기준

- 일반 피격에서 난간 윗면이 적의 유효 floor/Base로 확정되지 않는다.
- 갑판 안쪽에서는 기존 피격 거리·몽타주와 피해가 유지된다. 외곽에서는 마지막 안전 구간까지만 밀린다.
- 계단·경사·정상 단차를 계속 이동하며 Player의 난간 이용·타깃 추적이 유지된다.
- 지속 penetration·매 프레임 위치 보정·반복 복구·AI 무한 실패가 발생하지 않는다.
- 안전 검사 실패 시 실제 바닥 없이 Walking을 강제하지 않는다.
- 사망 ragdoll과 의도적 강제 이동 정책은 정의한 동작을 유지한다.
- 풀·Host 변경 후 이전 안전 좌표와 callback이 재사용된 적에 적용되지 않는다.
- 서버 캡슐과 관찰 클라이언트 표시가 수렴하며 보정 떨림과 몽타주 이동 중복이 없다.

## 10. 튜닝·관측·참고

캡슐 반경 기반 경계 여유, 허용 발-바닥 오차, 구간 샘플 간격/최대 횟수, 공중 유예, 복구 반경·시도·시간 상한을 설정화한다. `CellSize = 75 cm`, 그래프 `MaximumStepHeight = 45 cm` 등 현재 값은 출발점이며 CMC·충돌·몽타주의 실제 측정 없이 고정 결론으로 쓰지 않는다.

로그는 피격 ID·개체 활성화 세대·Host·Surface·Revision·요구/제한/실제 변위·경계 이유·StepUp/착지 판정·CurrentFloor/Base·Root Motion/외력·복구 결과를 기록한다. 정상 프레임은 Verbose, 차단·복구는 요약 수준으로 남겨 다수 적의 비용을 관리한다.

- `docs/DeckEnemy_Rail_Tracking_and_Sight_Config.md`: Player 난간 추적과 Enemy 자기 바닥 판정의 기존 분리.
- `docs/HitReaction_RootMotion_Network_Plan.md`: 과거 조사. 현재 배율 1 구현과 구분해서 읽는다.
- 설치된 UE 5.7 `CharacterMovementComponent.h/.cpp`: CanStepUp과 IsWalkable은 다른 판정 지점이므로 올라타기와 착지를 별도로 확인한다.
- [Epic: UCharacterMovementComponent API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/GameFramework/UCharacterMovementComponent?application_version=5.5): 공개 API 의미 참고. 구현 지점은 프로젝트가 사용하는 로컬 UE 5.7 소스를 기준으로 확인했다.
- [Epic: 네트워크 이동과 Root Motion](https://dev.epicgames.com/documentation/unreal-engine/understanding-networked-movement-in-the-character-movement-component-for-unreal-engine): CMC·Root Motion의 기존 복제 경로를 유지한다.
