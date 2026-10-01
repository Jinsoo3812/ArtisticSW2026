# Player 화살 발사 파이프라인

현재 구현 기준: 2026-10-01. 이 문서는 Player 활의 입력, 조준, 발사 확정, 서버 생성, 비행과 에디터 확인을 한곳에 정리한다. Enemy 활은 [Enemy 화살 발사 파이프라인](Enemy_Projectile_Firing.md)을 따른다.

## 한 발의 흐름

```text
우클릭 조준·Draw/Hold → 좌클릭 발사 요청·Release → ShotId 생성
→ NockArrow / FireArrow Notify → ProjectileShotComponent 예약
→ TG_PostUpdateWork에서 로컬 화면 중앙 ViewRay 캡처·서버 검증
→ 월드 Arrow_socket + 최종 조준점 + 저장된 속력으로 Snapshot 확정
→ 서버 Deferred Spawn → FinishSpawning → Strength 피해 Spec 초기화
→ LaunchPlayerShot → 월드 초기 속도 적용 → 화살 비행 sweep·서버 적중
→ ShotId 성공/실패 응답과 요청 정리
```

`GA_BowAimFire`는 충전과 몽타주, Notify, 서버 발사 승인과 수명을 관리한다. `NockArrow`가 없거나 Release 상태·장비·소켓·Spawn Class·시선·속력·피해 Spec이 유효하지 않으면 발사를 거부한다. FireArrow Notify는 즉시 생성하는 대신 `ProjectileShotComponent`에 예약한다. 이 컴포넌트는 캐릭터 이동과 Mesh tick을 선행 조건으로 두고 `TG_PostUpdateWork`에서 처리한다. 몽타주가 먼저 끝나도 예약이 끝날 때까지 필요한 상태를 유지하며, 취소·장비 변경·어빌리티 종료 시 요청을 지운다. 중복 Notify와 ShotId 재사용은 반복 발사로 이어지지 않는다.

`PlayerAimComponent`는 입력 해제 때 발사 식별자인 ShotId를 만들고, 실제 발사 확정 때 로컬 화면 중앙 ViewRay를 캡처한다. 원격 클라이언트는 이를 서버에 보내며 서버는 시선 원점·방향·시각·ShotId를 검증한다. 서버는 Notify 허용 상태와 검증된 시선이 모두 준비되어야 발사한다. 서버 쪽 예약 제한은 1초, 원격 owning client의 결과 대기는 2초다. 클라이언트는 성공/실패 응답으로 해당 요청을 끝낸다. 따라서 Release 입력 뒤 FireArrow Notify까지 카메라가 움직이면 **Notify에 대응하는 실제 캡처 시선**이 사용된다.

## 조준점과 초기 속도

`PlayerBowAimResolver`는 화면 중앙 ray 시작을 화살 소켓보다 1 cm 앞쪽 깊이로 옮긴다. `WeaponAim`과 `ArrowObstacle` 두 query의 유효한 전방 충돌 중 더 가까운 점을 최종 조준점으로 선택한다. 시작 겹침, `Time=0`, 소켓 뒤의 충돌은 선택하지 않는다. 유효한 충돌이 없으면 같은 ray의 먼 끝점(`FarPoint`)을 사용한다. 선택된 **하나의 최종 조준점**이 흰 디버그 표시와 초기 속도에 함께 쓰인다. 시작 겹침을 조준점으로 선택하지 않아도, 실제 화살은 소켓에서 출발하므로 앞에 겹친 벽과 충돌할 수 있다.

`PlayerBowShotPreparation`은 다음 월드 초기 속도와 출발 Transform을 확정한다.

```text
WorldVelocity = Normalize(FinalAimPoint - ArrowSocketLocation) × FireSpeed
SpawnTransform = (WorldVelocity 방향, ArrowSocketLocation)
```

`FireSpeed`는 충전 상태에서 저장된 값이다. 화살 BP의 `Flight Gravity Scale`은 비행 중 자연 낙차를 결정한다. Player 경로에는 자동 상향 보정, 이동 표적 선행 조준, 배 속도 가산이 없다. 지상·점프·이동/회전 갑판에서 같은 `WorldAim` 규칙을 쓴다. 이동 속도와 Carrier 샘플은 Snapshot 진단 정보일 뿐 초기 속도에 더하지 않는다. BowComponent의 과거 `Launch Profile / Velocity Policy`와 공용 `ResolveMuzzleDirection`은 현재 Player 발사 계산에 관여하지 않는다. 먼 표적의 낙차와 이동 표적의 선행은 플레이어가 직접 조준한다.

Snapshot에는 ShotId, 캡처 시각, 소켓, 최종 조준점, 속력, 중력, 월드 속도, 확정 프레임/시각과 이동 진단값이 포함된다. 발사 뒤 화살은 배를 따라가거나 목표를 추적하지 않는다.

## 서버 생성, 충돌과 피해

`GA_BowAimFire`는 `PlayerArrowProjectile` 계열 BP를 `SpawnActorDeferred`로 만들고 발사자와 활의 이동 충돌을 제외한 뒤 `FinishSpawning`한다. 이후 서버 Strength 피해 Spec을 만들어 화살에 초기화하고 `LaunchPlayerShot`을 호출한다. 피해는 발사 시점의 Strength·공격 계수·차지 배율로 확정되며 발사 뒤 무기나 능력치가 바뀌어도 기존 화살의 Spec은 유지된다. 실패하면 생성한 화살을 제거한다.

`LaunchPlayerShot`은 BP Construction 이후 루트를 확정된 소켓 Transform으로 다시 놓고 발사자·활을 비행 query에서도 제외한다. 시각 Mesh의 물리와 충돌을 끄며, `ProjectileLaunchInitialization`은 이미 계산된 **월드 속도**를 PMC에 그대로 적용한다(`InitialSpeed=0`, local-space 초기 속도 사용 안 함). 비행은 `ArrowProjectileMovementComponent`와 `ArrowCollisionQuery`의 명시적 sweep이 처리한다. 장애물의 작은 Box와 캐릭터의 별도 판정 크기를 구분하고, 장애물과 캐릭터 판정이 겹치면 장애물을 우선한다. 실제 충돌 뒤 서버가 피해·상태효과·연출을 처리한다. 늦게 Spawn한 화살은 생성 프레임 전체 시간을 다시 적분하지 않는다.

소켓에서 초기 방향으로 수행하는 작은 장애물 검사는 십자선 가림 표시와 발사 진단용이다. 가려져도 Player 화살은 발사하고 **실제 비행 sweep**에서 벽에 맞는다. 빨간 십자선은 초기 방향의 가림이며 중력을 포함한 전체 미래 궤적 예측은 아니다.

## 에디터 설정

1. 네이티브 빌드 후 에디터를 재시작하고 Player 활·화살 BP를 Compile한다. 활의 Spawn Class가 `PlayerArrowProjectile`을 상속하는지 확인한다.
2. 캐릭터 `Arrow_socket`, 몽타주의 `NockArrow`·`FireArrow` Notify를 확인한다. 화살 BP의 `Flight Gravity Scale`이 실제 낙차 설정이다. Mesh 긴 축은 루트 전방 X축에 맞추고 BP Tick/Timeline이 발사 후 위치·속도를 덮어쓰거나 다시 활에 붙이지 않도록 한다.
3. 실제 벽·지형·난간은 Arrow를 Block하고 유효한 Hurtbox는 WeaponAim과 Arrow를 Block하도록 둔다. PCG 생성 범위처럼 장애물이 아닌 보조 Brush는 `NoCollision` 또는 `WeaponAim=Ignore`, `Arrow=Ignore`로 설정한다. 생성된 실제 바위·나무 Mesh의 충돌은 유지한다. 화살 자신의 BoxComp가 런타임 NoCollision이어도 명시적 비행 sweep은 정상이다.

## 진단 및 수동 확인

PIE에서 발사 **전** 입력한다. 중력 override는 이후 생성하는 Player 화살에만 적용된다. 멀티 프로세스에서는 실제 비행 로그를 볼 서버에도 입력한다.

```text
sw.Projectile.DebugLaunch 1
sw.PlayerBow.GravityScale 0
```

파랑은 화면 중앙 탐색 ray, 흰 점/선은 최종 조준점과 소켓→조준점, 노랑은 초기 속도, 자홍은 중력 포함 1초 예상 궤적, 초록은 실제 이동, 빨강 점은 실제 충돌이다. 초록/주황 상자는 캐릭터/장애물 판정 크기다. `PlayerBowAimHit`에서 query별 Actor/Component/Profile과 `Usable`을, `PlayerBowAim`에서 `Selection`·`FinalAim`을 본다. `FarPoint`는 유효한 충돌이 없다는 뜻이지 실패가 아니다. 같은 Id의 `PlayerBowLaunch`에서 `OriginError`와 `AimAngle`이 거의 0인지 확인한다. `ConstructionOffset`은 BP Construction의 위치 변경량이다. `PlayerBowFirstStep`은 첫 이동, `PlayerBowHit`은 실제 충돌을 기록한다. 첫 스텝 전에 즉시 Hit가 나올 수 있다. `Time=0`, `StartPenetrating=1`이면 출발 시점에 장애물과 겹친 경우다.

1. **지상, 중력 0:** 정지 표적 주위 45도 간격 8방향에서 각 두 발씩 발사한다. 근·중·원거리, 화면 가장자리, 허공을 조준한다. 흰 선·노란 방향·장애물 없는 초록 경로가 일치하고 자가 충돌이 없어야 한다.
2. **가림:** 소켓 앞 실제 벽과 보조 PCG 볼륨을 구분한다. 실제 벽은 화살을 막고, 비충돌 보조 볼륨은 막지 않아야 한다. 난간 앞에서 십자선 가림 표시와 실제 충돌을 확인한다.
3. **자연 낙차:** `sw.PlayerBow.GravityScale -1`로 BP 중력을 복구해 먼 표적에 쏜다. 초기 방향은 흰 점을 향하고 실제 궤적은 아래로 내려가야 한다. 직접 상향 조준도 확인한다.
4. **이동과 네트워크:** 걷기·점프·배 중앙/선수/선미의 이동·선회 중, 빠른 카메라 회전·취소를 확인한다. 2 Players / Listen Server에서 host와 원격 client가 각각 쏘고, 약 100 ms/방향 지연과 `t.MaxFPS 30`에서도 서버 화살·피해가 발사당 한 번인지 확인한다. 서버 현재 상태에서 확정하므로 지연에 따른 화면/서버 위치 차이는 남을 수 있다.

종료 시 `sw.PlayerBow.GravityScale -1`, `sw.Projectile.DebugLaunch 0`으로 복구한다. BP·레벨·PIE 검증 결과는 이 문서의 절차를 실행해 확인해야 한다.

## 관련 구현

`Source/ClassFeature/Private/Combat/PlayerAimComponent.cpp`, `PlayerBowAimResolver.cpp`, `PlayerBowShotPreparation.cpp`, `Source/ClassFeature/Private/Attacker/GA_BowAimFire.cpp`, `Source/ArtisticSWCore/Private/Item/Projectiles/ProjectileShotComponent.cpp`, `PlayerArrowProjectile.cpp`, `ArrowCollisionQuery.cpp`.

`ProjectileLaunchInitialization`의 월드 속도 적용은 Player/Enemy 화살 외에도 Grenade·ClusterGrenade·ThrowItem의 Grenade, SubMunition, GravityVortex, Cannonball/Torpedo, Enemy TimeStop/ObstacleProjectile에서 사용한다. 각 투사체의 조준·투척 각도·바운스·수면 도달·부력·지정 지점 처리는 기존 전용 경로에 남아 있다. Player 화살의 ShotId/ViewRay 절차를 이 특수 투사체에 적용한 것은 아니다. 물리 배의 점속도 추정이나 AI 선행 조준, 과거 위치 rewind, 클라이언트 예측 화살도 이 파이프라인에 포함되지 않는다.
