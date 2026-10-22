# Enemy 화살 발사 파이프라인

현재 구현 기준: 2026-10-01. 이 문서는 Enemy 활의 표적 캡처, 같은 배 보정, 발사 승인, 서버 생성, 비행과 에디터 확인을 한곳에 정리한다. Player의 화면 중앙 조준 경로는 [Player 화살 발사 파이프라인](Player_Projectile_Firing.md)을 따른다.

## 한 발의 흐름

```text
AI 표적·사거리 확인 → 공격 몽타주 FireArrow Notify
→ ProjectileShotComponent 예약 → TG_PostUpdateWork에서 현재 소켓·표적 캡처
→ 같은 배 조건 판정 및 초기 월드 속도·Snapshot 확정
→ 현재 위치 기준 LOS 승인(막히면 취소·재배치)
→ 서버 Deferred Spawn → FinishSpawning → Strength 피해 Spec 초기화
→ LaunchEnemyShot → 월드 초기 속도 적용 → 화살 비행 sweep·서버 적중
```

`GA_RangedEnemyAttack`은 서버에서만 표적·장비·Notify·발사 수명을 관리한다. Notify는 즉시 생성하지 않고 `ProjectileShotComponent`에 ShotId와 함께 예약한다. 이 컴포넌트는 캐릭터 이동과 Mesh tick 뒤 `TG_PostUpdateWork`에서 확정한다. 몽타주 BlendOut 뒤에도 완료/발사 결과까지 GA와 BT 작업을 유지한다. 몽타주가 Notify 없이 끝나거나 공격이 취소되면 발사를 끝내며, 중복 Notify는 두 번째 화살을 만들지 않는다. Player의 ViewRay RPC와 `PlayerBowAimResolver`는 Enemy가 사용하지 않는다.

## 표적 캡처와 탄도

`ARangedEnemy::CaptureRangedAim`은 현재 표적·사거리를 검증하고 Enemy 캐릭터 `Arrow_socket`의 월드 Transform과 표적의 현재 `GetActorLocation()`을 캡처한다. Player 표적에서는 보통 캡슐 중심이다. 과거 `TargetAimHeightOffset`의 +60 cm는 적용하지 않는다. 활의 `Projectile Speed`가 월드 초기 속력이고 화살 BP의 `Flight Gravity Scale`이 비행 중 중력을 정한다.

`EnemyBowShotPreparation`은 발사자와 표적이 **실제로 같은 배를 발판 또는 부착 부모로 사용하고**, 표적이 Falling이 아니며, 배의 `BuoyancyRoot`가 물리 시뮬레이션 중이고 유효한 질량 중심·선속도·각속도를 제공할 때만 같은 배 보정을 시도한다. `HostShip`/Owner 정보만으로 같은 배라고 판단하지 않는다. 현재 위치에서 배에 대해 정지한 표적을 가정하고 **배의 강체 운동과 중력만** 보정한다. Player 걷기·달리기 속도를 예측하지 않는다.

```text
TargetAt(t) = ShipCOM + ShipLinearVelocity × t
              + ShipRotationDelta(t) × (CurrentTarget - ShipCOM)
RequiredVelocity(t) = (TargetAt(t) - Socket - 0.5 × Gravity × t²) / t
|RequiredVelocity(t)| = ProjectileSpeed
```

최대 10초에서 먼저 도달할 수 있는 시간을 찾아 초기 월드 속도와 SpawnTransform을 확정한다. 이 경로는 같은 배의 운동과 예상 낙차를 상쇄하도록 초기 방향을 올릴 수 있다. 배 속도를 화살에 다시 더하지 않는다. 다른 배·지상·공중 표적, 보정 비활성화, 배 속도 샘플 실패, 도달 해 없음이면 **소켓→현재 표적 위치 직선 방향 × ProjectileSpeed**로 발사하고 자연 낙차를 적용한다. 보정 실패만으로 사격을 취소하지 않는다. 발사 후 유도·재조준은 없고 배 가속·각속도 변화·파도와 표적의 자발적 움직임은 미래에 확정할 수 없다. 과거 EnemyBow `LaunchProfile`은 이 계산에 관여하지 않는다.

Snapshot에는 ShotId, 현재 소켓·표적 위치, 속력·중력, 실제 월드 초기 속도, 확정 프레임/시각과 이동 진단값이 들어간다. 움직이는 플랫폼의 점속도 샘플은 진단용이며 초기 속도에 추가하지 않는다.

## 발사 승인, 생성과 비행

`HasClearRangedLaunch`는 **현재 소켓→현재 표적 위치**로 전투 LOS를 검사한다. 미래 요격점을 현재 배 형상에 대해 검사하면 유효한 발사를 잘못 막을 수 있기 때문이다. 막히면 `HandleRangedReleaseLineOfSightBlocked`로 기존 취소·재배치를 수행한다. 이 검사는 미래 탄도 전체의 충돌 보장이 아니며 난간·갑판·선체의 실제 충돌은 비행 sweep이 처리한다. 발사자와 활은 충돌 query에서 제외한다.

승인된 발사는 `RangedEnemyProjectile` 계열 BP를 서버에서 `SpawnActorDeferred`로 생성한다. 발사자·활의 이동 충돌을 제외하고 `FinishSpawning`한 다음, Strength 공격 계수로 피해 Spec을 만들고 `LaunchEnemyShot`을 호출한다. 화살은 BP Construction 후 루트를 확정 소켓 Transform으로 다시 놓고 Mesh 물리·충돌을 끈다. `ProjectileLaunchInitialization`이 이미 계산된 월드 속도를 PMC에 그대로 적용한다(`InitialSpeed=0`, local-space 초기 속도 사용 안 함). 실패 시 생성한 화살을 제거한다. 피해는 발사 시점의 Spec으로 고정된다.

`ArrowProjectileMovementComponent`와 `ArrowCollisionQuery`는 실제 비행을 명시적 sweep으로 처리한다. 작은 장애물 Box와 캐릭터 판정 크기를 구분하고, 가림이 있으면 장애물을 우선한다. 적중 피해·상태효과·연출은 서버 화살 경로가 처리한다. 늦게 Spawn한 화살은 생성 프레임 전체 시간을 다시 적분하지 않는다.

## 에디터 설정

1. 네이티브 빌드 후 에디터를 재시작하고 Enemy 캐릭터·활·화살 BP를 Compile한다. 활의 `Projectile Class`가 `RangedEnemyProjectile` 상속 BP여야 한다. 다른 부모이면 `[EnemyBow] ProjectileClass must derive...`로 거부한다.
2. Enemy Mesh의 `Arrow_socket`, 캐릭터의 `Ranged Attack Socket Name`, 공격 몽타주의 FireArrow GameplayEvent Notify가 일치하는지 확인한다. 활의 `Projectile Speed`와 화살 BP의 `Flight Gravity Scale`을 확인한다. BP Tick/Timeline이 발사 뒤 화살 위치·속도를 덮어쓰거나 활에 다시 붙이지 않도록 한다.
3. 실제 벽·난간·갑판은 Arrow를 Block한다. 화살 query는 PCGVolume의 범위 Brush 및 `PCGVolumeBounds` Profile 컴포넌트를 자동 제외한다. Custom Profile인 범위 Brush도 통과하며, 같은 PCGVolume 소유의 생성 Mesh 충돌은 유지한다. 화살 Mesh는 시각용이며 명시적 sweep이 비행 충돌을 맡는다.
4. 적 화살 BP의 **Class Defaults → Arrow → Movement → Initial Launch Speed**에서 초기 속력을 cm/s로 설정한다. **0이면 EnemyBow의 Projectile Speed를 사용**하고, 양수이면 화살 BP의 값을 우선한다. 표적 요격과 중력 보정도 이 속력으로 계산한 뒤 최종 월드 속도를 발사한다. ProjectileComp의 Initial Speed는 런타임 월드 속도 적용 시 0으로 유지되므로 이 새 속성에서 설정한다.

## 진단 및 수동 확인

PIE 서버에서 발사 **전** 입력한다. 보정 기본값은 1이고 중력 override -1은 BP 설정 사용을 뜻한다. 변경은 이후 생성하는 Enemy 화살에만 적용된다.

```text
sw.Projectile.DebugLaunch 1
sw.EnemyBow.SameShipCompensation 1
sw.EnemyBow.GravityScale -1
```

흰 점은 현재 표적, 파란 점은 같은 배 예상 도달점, 노란 선은 초기 속도, 자홍은 중력 포함 예상 궤적, 초록은 실제 이동, 빨간 점은 충돌이다. 같은 Id의 `EnemyBowCompensation`에서 `Applied`, `Reason`, `FlightTime`, `Current/Intercept`, `ShipV/ShipW`를 확인한다. `EnemyBowAim`은 현재 LOS와 Blocker, `EnemyBowLaunch`는 `OriginError`·`AimAngle`, `EnemyBowFirstStep`은 첫 이동, `EnemyBowHit`은 실제 충돌을 기록한다. `AimAngle`은 계산된 발사 방향과 실제 초기 속도의 차이이며 현재 표적 직선과의 각도가 아니다. FirstStep 전에 즉시 Hit가 나올 수 있고 `Time=0`, `StartPenetrating=1`은 시작 겹침을 뜻한다.

1. **정지 배:** 장애물 없는 표적을 Enemy 주위 45도 간격으로 배치해 반복한다. `OriginError`와 `AimAngle`은 거의 0이어야 한다. 속력과 자연 낙차를 확인한다.
2. **이동·선회 배:** Player를 같은 배에 대해 정지시킨 채 선수·중앙·선미에서 공격받는다. `Applied=1 Reason=SameShip`과 파란 예상 도달점을 확인한다. 이동 보정만 분리하려면 `sw.EnemyBow.GravityScale 0`을 쓴다. Player가 직접 옆으로 걷거나 점프하면 그 움직임을 예측하거나 추적하지 않아야 한다.
3. **대조 조건:** `sw.EnemyBow.SameShipCompensation 0`으로 현재 위치 직접 조준과 비교한다. 다른 배·지상·Falling 표적은 `Applied=0`이어야 한다. 물리 배 샘플이나 도달 해가 없어도 직접 조준으로 발사되어야 한다.
4. **가림과 네트워크:** 난간 뒤에서 `EnemyBowAim Clear=0`의 실제 Blocker와 취소·재배치를 확인한다. 2 Players / Listen Server와 지연 환경에서 발사당 서버 화살·피해가 한 번인지 확인한다. 현재 LOS가 깨끗해도 이후 이동하는 구조물과의 실제 충돌은 별도로 발생할 수 있다.

종료 시 `sw.EnemyBow.GravityScale -1`, `sw.EnemyBow.SameShipCompensation 1`, `sw.Projectile.DebugLaunch 0`으로 복구한다. BP·레벨·PIE 검증 결과는 이 절차를 실행해 확인해야 한다.

## 관련 구현

`Source/Enemy/Private/RangedEnemy/RangedEnemy.cpp`, `EnemyBowShotPreparation.cpp`, `RangedEnemyProjectile.cpp`, `Source/Enemy/Private/GAS/Ability/GA_RangedEnemyAttack.cpp`, `Source/ArtisticSWCore/Private/Item/Projectiles/ProjectileShotComponent.cpp`, `ArrowCollisionQuery.cpp`.
