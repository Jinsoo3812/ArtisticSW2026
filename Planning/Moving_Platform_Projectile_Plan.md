# 이동 공간 화살 발사 및 충돌 리팩터링

> 2026-09-28: 아래는 이전 구현 기록이다. 발사 방향 수식과 조준 시점은 후속 리팩터링으로 대체되었다. 현재 구조와 에디터 확인 절차는 `docs/Projectile_Shot_Refactor.md`를 참고한다.

2026-09-27. 최종 사용자 요구사항을 반영한 구현 설명. 이전의 속도 벡터 가산 및 AI 선행 요격 설계는 이 문서로 대체한다.

## 발사 규칙

플레이어와 적 모두 발사 순간의 Instigator 점속도만 사용한다. 무기의 설정 속력은 초기 월드 속력으로 유지하고 방향만 보정한다. 표적이나 표적의 배 속도, 미래 위치, 비행 시간은 구하지 않는다. 중력에 의한 낙차도 자동 조준하지 않는다.

정규화한 조준 방향을 D, 무기 속력을 S, 발사자 점속도를 V라고 하면:

```
Parallel = dot(V, D)
Lateral = V - Parallel * D
Forward = sqrt(S*S - dot(Lateral, Lateral))
WorldLaunchVelocity = Lateral + Forward * D
```

이렇게 하면 초기 월드 속력은 S이고, 발사자와 함께 등속 이동하는 기준계에서는 `WorldLaunchVelocity - V`가 D와 나란하다. `normalize(S*D + V)*S`처럼 전체 합을 다시 정규화하지 않으므로 대각선 조준에서도 횡방향 보정량이 축소되지 않는다.

이는 초기 월드 속력을 유지하기 위한 게임 규칙이다. 물리적인 전체 속도 상속과는 구분한다. 표적이 발사자와 다른 속도로 움직이거나 배가 발사 후 선회/가속하면 자동으로 명중시키지 않는다. 플레이어가 방향과 낙차를 예측한다.

발사자의 횡방향 속도가 S를 초과하거나 발사자 기준 전진 성분을 만들 수 없는 설정은 발사를 거부한다. 속력을 올리거나 다른 표적 예측으로 대체하지 않는다.

## 책임 분리

- `MovementFrameVelocity`: 발사 위치의 순간 월드 속도만 수집한다. 지상에서는 캐릭터 이동 속도, 접지한 배에서는 배 점속도와 캐릭터 자체 이동 속도를 합친다. 공중에서는 이미 상속된 CMC 속도만 사용한다. 이동이 꺼진 부착 탑승자의 오래된 CMC 속도는 제외한다.
- `MovementFrameVelocityProvider` / `AShip`: 물리 배의 `BuoyancyRoot`에서 선속도와 회전 접선속도를 포함한 점속도를 제공한다. 단순 HostShip 소유 관계는 탑승 근거로 사용하지 않는다.
- `ProjectileLaunchMath`: 발사자 속도와 현재 조준 방향으로 위 수식을 계산한다. 액터, 충돌, GAS에 의존하지 않는다.
- 플레이어/적 GA: 실제 Release Notify에서 조준점, 소켓, 발사자 속도를 모아 공통 계산을 호출하고 데미지 Spec을 초기화한다.
- `ArrowCollisionQuery`: 장애물 검사 정책과 비행 구간의 최초 충돌 선택을 담당한다.
- `ArrowProjectileMovementComponent`: UE의 중력 적분과 substep을 유지하면서 각 이동 구간의 충돌을 공통 Query에 맡긴다.
- `ArrowProjectile`: 선택된 충돌 한 건의 GAS 피해, 효과, 정지/제거를 처리한다.

플레이어의 입력 ViewRay는 기존 검증 후 저장하고, 실제 발사 시 현재 위치와 탑승 회전을 반영해 다시 Trace한다. 이는 입력과 몽타주 발사 사이의 조준 기준 갱신이며 표적 예측이 아니다.

## 두 충돌 크기와 하나의 판정 경로

화살 BP의 Class Defaults → Arrow → Collision에서 설정한다. 값은 cm 단위의 반 크기이다.

| 설정 | 용도 |
|---|---|
| Character Hit Half Extent | 기존 CollisionHalfExtent 값을 보존한다. 적대 캐릭터의 유효 Hurtbox 명중 보조에만 사용한다. |
| Obstacle Hit Half Extent | 새 작은 Box. 기본 (8, 1, 1). 모든 배, 바닥, 난간, 벽 등 장애물에 사용한다. |

기존 BoxComp는 유지하고 작은 ObstacleCollisionComp를 추가했다. 두 컴포넌트는 크기 표시/설정용이며 이동 컴포넌트가 명시적으로 Sweep한다. 컴포넌트의 Hit/Overlap 이벤트를 별도 데미지 경로로 사용하지 않는다.

장애물은 `ArrowObstacle` 프로필로 검사한다. Pawn과 CombatHurtbox는 제외하고 기존 Arrow를 막던 WorldStatic, WorldDynamic, PhysicsBody, ShipDamage 등의 응답을 사용한다. 발사 캐릭터와 장착 활은 제외하지만 발사자가 타고 있는 배를 포함하여 모든 배가 차단 대상이다. 아군 피해를 막는 팀 규칙과 배에 막히는 규칙은 독립적이다.

비행 구간마다 작은 장애물 Sweep과 큰 캐릭터 Sweep의 결과를 공통 함수에서 비교한다. 가장 앞선 유효 충돌 한 건만 선택하며 동일 시점에는 장애물이 우선한다. 큰 보조 범위가 벽 너머 캐릭터에 닿는 경우에만 작은 장애물 Query로 접촉점까지 가림을 확인한다.

두 크기를 구분하기 때문에 엔진 Sweep 호출 자체가 항상 한 번인 것은 아니다. 통합 대상은 판정 책임과 중복 경로이다. 기존 Root 자동 Sweep/OnHit, 별도 발사점 Overlap, 미래 탄도 전체 검사, 화살 데미지 처리의 추가 WorldStatic Ray는 사용하지 않는다. 초기 겹침도 첫 비행 구간 검사에서 처리한다.

적의 실제 발사 LOS와 갑판 사격 위치 후보 LOS는 동일한 작은 장애물 Query를 사용한다. Visibility 검사와 HostShip 전용 갑판 재검사를 겹쳐 수행하지 않는다. BT 공격 Task의 쿨다운 대기/활성화, 공격 시작, GA 활성화에서 반복하던 LOS는 제거하고 실제 Release에서 다시 확인한다. 다른 시점의 AI 위치 선택과 실제 비행 충돌은 각각 필요하므로 유지한다.

## 에디터 수동 확인

자동화 테스트는 추가하거나 실행하지 않았다. 최종 코드의 `ArtisticSW2026Editor Win64 Development` 빌드와 `git diff --check`는 통과했다. 기존 `StackingType` API 사용 중단 예정 경고 2건이 남아 있다. 실제 PIE 사격/네트워크 동작은 아래 절차로 확인해야 한다.

1. 에디터를 재시작하여 네이티브 컴포넌트 타입과 `ArrowObstacle` 프로필을 로드한다. 플레이어 `BP_Arrow`와 적 `BP_EnemyProjectile`을 열어 Compile/Save한다.
2. Class Defaults의 `Character Hit Half Extent`는 기존 큰 값을 유지하고, `Obstacle Hit Half Extent`는 화살 굵기에 맞춰 작게 조절한다. ProjectileComp가 ArrowProjectileMovementComponent인지 확인한다.
3. 콘솔에서 `sw.Projectile.DebugLaunch 1`을 켠다. 청록은 발사자 속도, 노랑은 초기 화살 속도, 초록 Box는 캐릭터 보조, 주황 Box는 장애물 크기다. 로그의 InitialSpeed를 무기 설정값과 비교한다. 자홍선은 현재 초기 속도/중력으로 그린 참고 궤적이며 자동 조준 결과가 아니다.
4. 지상 정지/이동 사격과 등속 이동하는 같은 배 위 정면/대각선 사격을 플레이어와 적으로 확인한다. 캐릭터 자체 이동도 보정에 포함되는지 확인한다.
5. 자기 배와 상대 배의 난간/벽/선체를 향해 발사해 모두 막히는지 확인한다. 큰 캐릭터 범위만 벽 너머 적에 닿는 상황에서는 피해가 없어야 한다. 좁은 틈은 작은 장애물 크기를 기준으로 통과해야 한다.
6. 배 안의 Deck RangedEnemy가 열린 경로에서는 공격하고, 실제 벽/갑판이 있으면 공격을 거부하거나 재배치하는지 확인한다. 대상 배의 움직임을 자동 예측하지 않는지, Flight Gravity Scale을 올리면 일반 낙차가 남는지 확인한다.
7. Listen Server와 Client에서도 서버 기준 충돌 한 번/피해 한 번을 확인한다. 확인 후 `sw.Projectile.DebugLaunch 0`으로 끈다.

배의 단순 충돌 Hull 자체가 내부 공간을 통째로 감싼 경우에는 작은 화살도 내부에서 막힐 수 있다. 이 경우 배 메시의 실제 Query Collision을 내부 공간과 일치하도록 편집해야 한다. 코드에서 해당 배 전체를 무시하지 않는다. 이번 변경은 배/레벨 바이너리 에셋을 수정하지 않는다.
