# 지상·이동·회전 갑판 공통 Projectile 조준 재설계 계획

작성일: 2026-09-28. 대상: 현재 작업 트리, Unreal Engine 5.7. 상태: 조사 및 제안이며 미구현.

> 후속 적용: 같은 날 사용자 승인으로 기본 화살 발사 구조와 공통 월드 속도 초기화를 구현했다. 아래 본문은 조사 당시 계획을 보존하며, 실제 적용 범위·선택 확장 제외 사항·수동 확인 절차는 `docs/Projectile_Shot_Refactor.md`에 기록했다.

현재 구현이 제대로 동작하지 않는다는 사용자 보고를 출발점으로 삼는다. 기존 `Moving_Platform_Projectile_Plan.md`는 정상 동작의 증거가 아니라 이전 구현의 규칙을 설명하는 자료로 취급한다. 이 문서는 기존 파일과 작업 중인 코드·에셋을 보존한 별도 재설계안이다.

**권고: 조준 의도 → 일관된 시점의 발사 상태 → 명시적인 탄도 정책 → 월드 공간 비행을 분리한다. Player와 Enemy는 조준 의도만 다르게 만들고, 지상과 배 위는 동일한 발사 경로를 사용한다. 배 속도 가산을 모든 무기의 공통 보정으로 적용하지 않는다.**

## 1. 현재 구현의 문제부터 확정한다

### 1.1 확인 범위와 증거 수준

- 현재 C++ 구현, 관련 충돌 설정, 기존 계획과 로그, 설치된 UE 5.7 엔진 소스, Epic 공식 문서를 확인했다.
- PIE를 직접 실행해 재현하거나 BP의 런타임 override 값을 검사한 것은 아니다. 아래의 수식·데이터 전달 문제는 코드로 확인했고, 프레임 순서에 따른 실제 오차량과 에셋 충돌 상태는 계측이 필요하다.
- 작업 트리에 이미 다수의 변경 사항이 있다. 이 조사에서는 구현 코드, 설정, 바이너리 에셋을 수정하지 않았다.

### 1.2 확인된 발사 경로

| 대상 | 현재 경로 | 핵심 파일 |
|---|---|---|
| Player | 마우스 버튼 해제 때 ViewRay 저장 → GA BeginRelease → 애니메이션 Notify → 저장한 시선 재구성 → 소켓 → 발사자 점속도 → 보정 수식 → LaunchArrow | `BasePlayer.cpp:1692`, `GA_BowAimFire.cpp:350,431,496`, `PlayerAimComponent.cpp:39,74,106` |
| Enemy | Notify → 현재 표적 위치와 소켓, 직선 LOS → 발사자 점속도 → 같은 보정 수식 → LaunchArrow | `GA_RangedEnemyAttack.cpp:120,161`, `RangedEnemy.cpp:449,504,511` |
| 배/탑승 상태 | MovementBase 또는 AttachParent → Provider → BuoyancyRoot의 점속도 | `MovementFrameVelocity.cpp:10,30,68`, `Ship.cpp:67` |
| 비행 | 월드 초기 속도 확정 → ProjectileMovement → 공통 충돌 Query → 서버 피해 | `ArrowProjectile.cpp:157`, `ArrowProjectileMovementComponent.cpp:6` |

파일 위치는 `Source/ClassFeature`, `Source/Enemy`, `Source/ArtisticSWCore`, `Source/WaterAndShip`의 각 Private 하위 경로이다. 번호는 조사 시점 기준이다.

### 1.3 확정 문제 A: 보존하는 방향의 기준계가 요구와 다르다

`ProjectileLaunchMath::BuildShooterFrameVelocity`는 현재 다음 식을 사용한다.

```text
D = normalize(AimPoint - MuzzlePosition)
Vparallel = dot(Vshooter, D)
Vlateral = Vshooter - Vparallel * D
Vworld = Vlateral + sqrt(Speed² - |Vlateral|²) * D
```

이 식은 초기 월드 속력과 `Vworld - Vshooter`의 방향을 보존한다. 횡속도가 있으면 **Vworld 자체는 D와 다르다.** 따라서 월드 조준점을 얻은 뒤 이 식을 적용하면 초기 비행 방향이 그 조준점에서 벗어난다. 코드의 주석과 기존 계획도 이를 '발사자의 translating frame' 규칙으로 명시한다.

수치 반례: 중력 0, D=(1,0,0), 무기 속력 6,000 cm/s, 발사자 횡속도 (0,1,000,0), 정지 목표 (10,000,0,0).

- 현재 초기 속도는 약 (5,916.08, 1,000, 0) cm/s이다.
- 초기 조준 오차는 약 9.59도이다.
- 목표의 X 평면에 도달할 때 Y는 약 1,690.31 cm, 즉 16.9 m 벗어난다.
- 이는 측정한 게임 수치가 아니라 현재 함수의 문제를 분리해서 보여주는 계산 예시다. 지상에서 옆으로 이동하며 쏴도 같은 종류의 편향이 생긴다.

즉 단순히 배의 각속도를 더 가져오는 수정으로는 해결되지 않는다. 현재 코드에는 이미 `GetPhysicsLinearVelocityAtPoint`가 있다.

### 1.4 확정 한계 B: 회전하는 같은 배의 표적에도 보장이 없다

배의 두 점은 회전 중 서로 다른 속도를 가진다.

```text
Vpoint = Vcom + AngularVelocity × (Point - CenterOfMass)
Vtarget - Vmuzzle = AngularVelocity × (TargetPoint - MuzzlePoint)
```

현재 함수는 발사자 점속도만 사용한다. 표적의 점속도, 비행 시간, 이후 배의 회전을 계산하지 않는다. 같은 배에서 두 사람이 가만히 서 있어도 위 상대속도는 0이 아닐 수 있다. 발사자와 표적이 동일한 속도로 등속 평행 이동하는 특수 상황과, 파도로 상하 운동하며 회전하는 배를 구분해야 한다.

발사 뒤 계속 회전·가속하는 갑판의 한 점에 반드시 맞히는 것은 일반적인 비유도 탄도의 보장이 아니다. 필요하면 선행 조준 또는 별도 게임 규칙으로 설계해야 한다.

### 1.5 확정 문제 C: 저장된 조준 데이터가 실제 발사 화면과 동일하지 않다

`CaptureReleaseView`는 버튼 해제 시점의 실제 화면 중앙을 읽는다. 이후 `ResolveShotAim`은 새 화면 중앙을 읽지 않고 다음처럼 복원한다.

- 현재 PawnViewLocation에 과거 OriginOffset을 더한다.
- 과거 Direction과 OriginOffset에 배의 yaw 변화만 적용한다.
- CMC의 `bIgnoreBaseRotation`으로 카메라가 배 yaw를 계승할 것인지 추정한다.

이는 입력 시점 조준 고정이라는 기존 규칙에는 의도가 있지만, 실제 발사 시점 화면과 일치한다는 계약은 아니다. 현재 BasePlayer에는 기본적으로 카메라 회전 지연도 활성화되어 있다. SpringArm의 보간·충돌에 따른 카메라 이동과 실제 ViewRotation을 이 방식으로 재현할 수 없다.

해결은 pitch/roll을 무조건 추가하는 것도 아니다. 캐릭터 카메라가 수평을 유지하면 배의 전체 회전을 적용하는 쪽이 잘못된다. **조준 좌표계는 실제 카메라 정책이 소유해야 한다.**

### 1.6 확정 문제 D: 네트워크 입력에는 시점과 플랫폼 기준이 없다

`FGameplayAbilityTargetData_ViewRay`는 Origin과 Direction만 전송한다. 서버의 `CaptureTime`, OriginOffset, CarrierRotation은 서버가 이벤트를 처리할 때 만든다. 클라이언트가 실제 본 순간의 배 Transform과 서버 수신 시점의 배 Transform을 구분할 수 없다.

현재 reliable 이벤트 하나에 입력과 시선을 실은 것은 순서 경쟁을 줄이는 장점이다. 하지만 reliable 전송은 카메라·CMC·배의 네트워크 물리 상태를 같은 시점으로 맞춰 주지는 않는다. `MaxReleaseAimAge` 역시 서버 저장 이후의 시간만 제한한다.

### 1.7 확인된 구조적 위험 E: Notify 즉시 발사에 상태 확정 장벽이 없다

Player/Enemy 모두 Notify 콜백에서 곧바로 발사 함수를 호출한다. 발사 코드에는 물리 결과, CMC의 based movement, 소켓 갱신, 카메라 갱신의 완료를 명시적으로 맞추는 단계가 없다.

설치된 UE 5.7 소스에서는 물리 기반 플랫폼의 based movement를 PostPhysics로 미룰 수 있다. 따라서 'Notify에서 읽었으니 최신 소켓과 최신 배 상태다'라고 가정해서는 안 된다. 실제 한 프레임 지연 발생 여부는 Tick 계측으로 확인해야 하며, 이것을 이미 재현된 단일 원인으로 단정하지 않는다.

### 1.8 확정 불일치 F: 카메라 조준과 실제 장애물 정책이 다르다

Player 조준은 `WeaponAim` Trace를 쓴다. 현재 `ShipDeck`과 배 Query Hull 계열 설정에는 `WeaponAim=Ignore`, `Arrow=Block` 조합이 있다. 카메라는 배 구조물을 지나 먼 점을 선택할 수 있지만 화살은 그 전에 막힌다.

3인칭 카메라가 장애물 위로 보는 경우까지 포함해, 조준점을 정하는 Trace와 총구/활 소켓에서 실제 통과 가능한 경로를 확인하는 검사는 목적이 다르다. 서로 다른 목적을 유지하되 가림 상태를 UI와 발사 준비 단계에 전달해야 한다.

### 1.9 이미 올바른 방향인 부분과 범위

- 실제 `BuoyancyRoot`에서 회전 성분을 포함한 점속도를 읽는 Provider 경계는 유지할 가치가 있다.
- Arrow는 `LaunchArrow`에서 월드 Velocity를 설정하고 `InitialSpeed=0`, `bInitialVelocityInLocalSpace=false`, `MaxSpeed=0`, 비유도·비바운스를 재설정한다. 이 경로에서 '속도가 로컬로 한 번 더 회전된다'는 주장을 원인으로 삼지 않는다.
- Arrow의 비행 충돌/피해 경로 분리는 보존한다. 조준 문제 해결을 위해 기존 충돌 리팩터링을 다시 섞지 않는다.
- 수류탄·투척물·대포·Enemy 선박 스킬은 각자 별도의 발사 경로다. Arrow 수정만으로 전체 Projectile이 해결되었다고 판정하지 않는다.
- 이전 `Saved/MovingPlatformProjectileTests.log`에는 성공과 실패가 섞여 있고 실패에 Crafting 데이터 오류가 포함된다. 현재 함수와 다른 과거 Intercept 테스트 기록도 있어 현재 구현 검증으로 재사용할 수 없다.

## 2. 먼저 명시할 발사 계약

정석은 한 가지 속도 보정 공식이 아니라 좌표계·시점·물리 규칙을 명시하는 것이다. 아래 두 정책은 서로 다른 게임 규칙이며 자동 혼합하지 않는다.

| 정책 | 초기 속도 | 의미와 용도 |
|---|---|---|
| WorldAim: 이번 Player/Enemy 화살 기본 권고 | `Vworld = Speed * normalize(AimPoint - Muzzle)` | 현재 조준점 방향으로 발사. 무기 속력은 월드 초기 속력. 배/캐릭터의 이동 속도 때문에 초기 방향이 옆으로 꺾이지 않음 |
| PhysicalInheritance: 물리 상속이 필요한 무기에만 명시 | `Vworld = VmuzzleCarrier + VrelativeCharacter + Speed * Dbarrel` | 무기 속력은 발사자 기준 상대 속력. 자연스러운 관성은 얻지만 월드 조준점으로의 명중은 별도 해 계산이 필요 |

이번 권고는 '조준한 방향으로 발사'와 기존 구현의 초기 월드 속력 규칙을 우선한 게임플레이 선택이다. 물리적 관성 보존의 유일한 정답이라는 뜻이 아니다. 지상/배 여부에 따라 숨겨서 정책을 바꾸지 않고 무기 정의에 정책을 둔다.

기본 화살에서는 자연 낙차와 사용자의 선행 조준을 유지하는 안을 제안한다. '방향이 맞는다'와 '중력이 있어도 화면 중앙의 점에 반드시 떨어진다'를 구분한다. 후자가 필요하면 BallisticPoint 정책을 별도로 선택한다.

Player 조준의 기준 시점은 실제 화살을 놓는 Release 단계로 통일하는 안을 권고한다. 기존의 버튼 해제 시점 고정 규칙을 유지해야 한다면 `InputLocked`라는 명시적 대안으로 남긴다. 그 경우 고정된 조준 의도를 표시하고 현재 화면과 일치한다고 약속하지 않는다. 이 문서는 규칙 변경 제안이지 적용 완료 기록이 아니다.

## 3. 책임 분리와 모듈 경계

| 책임 | 소유 위치/후보 | 입력 → 출력 | 소유하지 않는 것 |
|---|---|---|---|
| Player 조준 의도 | ClassFeature / PlayerAimComponent | 실제 ViewRay, 조준 시점, ShotId → AimIntent | 배 물리, 무기 속도, 피해, Projectile 생성 |
| Enemy 조준 의도 | Enemy / RangedAimPolicy | 표적/지정 부위, 현재 또는 예측 조준 정책 → AimIntent | Player 카메라, 배 직접 조작, Projectile 비행 |
| 플랫폼 상태 | WaterAndShip / 기존 Provider | 시점과 월드 점 → 기준 Transform, 선/각속도, 점속도, 유효성 | 조준점 변경, 속도 상속 여부 결정 |
| 발사자 이동 상태 | ArtisticSWCore / MovementFrame 계층 | 실제 MovementBase/부착/CMC 상태 → MotionSample | HostShip 소유 관계로 탑승 추정, 표적 선행 조준 |
| 무기 발사 정의 | BowItem/BowComponent 및 무기 데이터 | 소켓, 충전 결과, 탄도 설정 → Muzzle/LaunchProfile | 배 타입 분기, 최종 방향 중복 보정 |
| 발사 상태 확정 | ArtisticSWCore / ShotPreparation 또는 얇은 발사 컴포넌트 | AimIntent + Muzzle + Motion + LaunchProfile → 불변 ShotSnapshot | GAS 피해 수식, 입력, AI 표적 선택 |
| 탄도 계산 | ArtisticSWCore / ProjectileLaunchMath | 숫자로 된 Snapshot → LaunchSolution 또는 명시적 실패 | UObject 탐색, Trace, 네트워크, 배 Cast |
| 공격 실행 | Player/Enemy GA | Notify/자원/쿨다운 + 준비된 Shot → 서버 Spawn/Spec 초기화 | 독자적인 좌표계 변환과 탄도 보정 |
| 비행·충돌 | ProjectileMovement / ArrowCollisionQuery | 확정된 월드 초기 조건 → 이동과 최초 충돌 | 매 Tick 발사자의 배를 조회해 따라가기 |
| 피해·표현 | ArrowProjectile / 기존 GAS·Impact 경로 | 검증된 충돌 → 피해 1회와 FX | 조준 재계산 |

공통 모듈은 `AShip`, Player 또는 Enemy 구체 클래스를 참조하지 않는다. WaterAndShip이 Core의 인터페이스를 구현한다. 처음부터 모든 무기를 거대한 상속 구조로 합치지 않고, 공통 데이터와 순수 계산·발사 상태 확정 경계를 먼저 만든다. 기존 함수의 재사용/이동을 우선한다.

ShotSnapshot에 필요한 정보: ShotId, 서버 확정 시각, 클라이언트 Aim 시각/sequence, World Muzzle Transform, AimPoint/ViewDirection, LaunchProfile, 중력, MotionSample의 출처·시점·유효성. 조준 시각과 서버 Spawn 시각은 다를 수 있으며 동일하다고 위장하지 않는다. 게임플레이 계산용 Transform과 화면 보간용 Transform도 구분한다.

## 4. Player와 Enemy의 실제 처리 흐름

### 4.1 Player: 지상과 배 위의 공통 경로

1. 충전 해제 입력은 충전량과 ShotId를 확정한다.
2. 실제 발사 허용 Notify는 발사 요청을 기록한다. 콜백 내부에서 무조건 모든 계산과 Spawn까지 끝내지 않는다.
3. owning client는 선택한 조준 시점에 실제 화면 중앙의 ViewRay를 캡처한다. 카메라 yaw/pitch/roll은 카메라 결과를 사용하며 CMC 옵션에서 유추하지 않는다.
4. 서버는 해당 ShotId의 발사 허용 상태와 Aim 데이터가 모두 준비된 경우만 처리한다. 네트워크 지연 시 bounded 대기, timeout 취소를 명시하고 오래된 시선을 조용히 재사용하지 않는다.
5. 확정 단계에서 서버의 현재 물리 결과, based movement, 유효한 소켓 상태를 확보한다. 조준 데이터는 아래 네트워크 정책에 맞춰 서버 좌표계에 해석한다.
6. 카메라 Trace로 AimPoint를 결정하고 소켓→AimPoint 방향을 구한다. 3인칭 시차를 이 단계에서 해소한다. 목표가 소켓 뒤/너무 가까운 경우의 fallback 또는 발사 차단을 명시한다.
7. 작은 발사체 충돌 크기로 발사 직전 가림/초기 겹침을 확인한다. 막히면 '가려짐'을 표시하고 무기가 정한 차단/즉시 충돌 규칙을 적용한다. 카메라 위치에서 Projectile을 생성하지 않는다.
8. 선택한 정책으로 최종 월드 속도를 한 번 계산한다. 서버 Spawn과 Spec 초기화가 성공한 뒤 활성화한다.

발사 직전 가림 확인은 기존 ArrowCollisionQuery의 작은 장애물 정책을 재사용한다. 준비 단계의 검사는 피해를 발생시키지 않는다. 초기 겹침을 즉시 충돌로 처리하는 정책이라면 실제 피해/정지는 기존 비행 충돌 경로 한 곳에서만 처리한다.

지상에서는 Carrier 없음/정지 플랫폼이 정상 상태다. WorldAim 계산은 플랫폼 속도 취득에 실패했다는 이유만으로 지상 발사를 거부하지 않는다. 반면 PhysicalInheritance가 필요한데 실제 이동 상태를 얻을 수 없으면 0으로 위장하지 않고 실패 사유를 반환한다.

### 4.2 Enemy

Enemy는 서버에서 실제 발사 준비 시점의 표적 조준 부위를 얻는다. 이후 발사 상태 확정·무기 탄도·충돌 정책은 Player와 동일하다.

기본 정책은 현재 지점 조준이다. 빠르게 움직이거나 회전하는 배 위의 적중률을 높이고 싶다면 Enemy AimPolicy에만 예측을 추가한다. 정확도, 최대 예측 시간, 선행 조준 정도는 난이도 데이터가 소유한다. Projectile이 표적을 추적하도록 몰래 바꾸지 않는다.

현재 Enemy의 직선 LOS는 가시성 확인이다. 이후 다른 방향이나 곡선 해를 선택하면 원래 직선 LOS를 그 궤적의 통과 보장으로 취급할 수 없다. 최종 LaunchSolution 기준의 장애물 확인 책임을 분리한다.

## 5. 이동 상태와 회전 처리

- 접지한 일반 지상: 캐릭터 자체 이동 속도, 정지 플랫폼 속도 0.
- 움직이는 갑판 접지: 플랫폼의 발사점 점속도 + 플랫폼 운반분을 제외한 캐릭터 이동 성분. 실제 CMC 모드별 Velocity 의미를 확인하여 중복 합산을 막는다.
- 부착 탑승 + MOVE_None: 플랫폼 점속도. 비활성 CMC의 잔여 속도는 제외한다.
- 공중: CMC가 이미 이탈 시 상속한 이동 성분을 다시 더하지 않는다. 과거 HostShip이나 이전 MovementBase를 현재 Carrier로 사용하지 않는다.
- 배 A→배 B, 승선/하선, 점프/착지: Carrier 세대/참조와 샘플 시점을 갱신하고 오래된 기준 변환을 폐기한다.
- 물리 배: authoritative BuoyancyRoot의 점속도 API 사용. 직접 계산한다면 선속도의 기준점과 질량중심을 맞추고 각속도 단위는 rad/s로 통일한다.
- 물리 시뮬레이션이 꺼진 회전 플랫폼: ComponentVelocity만으로 회전 속도를 알 수 없다. Provider에서 Transform 이력 또는 명시적 선/각속도를 제공한다. 텔레포트/재보정은 정상 속도로 차분하지 않는다.
- 기본 상속 대상은 플랫폼 운반과 캐릭터 이동이다. 애니메이션 활 시위/손 소켓의 순간 움직임까지 임의로 더하면 무기 속력이 흔들리므로 별도 물리 설계가 없는 한 제외한다.

바다의 파고나 수면 속도를 Projectile 코드가 직접 조회하지 않는다. 바다→부력→배 강체→플랫폼 상태라는 경계를 유지한다. 배의 실제 이동 결과가 플랫폼 인터페이스를 통해 전달되면 충분하다. 점속도를 제공하는 Epic API는 월드 좌표 점을 받는다. [Epic: Get Physics Linear Velocity at Point](https://dev.epicgames.com/documentation/unreal-engine/BlueprintAPI/Physics/GetPhysicsLinearVelocityatPoint?lang=en-US)

## 6. Tick과 네트워크 계획

### 6.1 Tick 순서

발사 Snapshot은 물리 결과 반영, CMC based movement 반영, 필요한 애니메이션 소켓 갱신이 끝난 뒤 만든다. Player의 화면 조준 캡처는 실제 카메라 결과를 기준으로 한다.

Epic 문서는 물리 상태를 쓰는 Trace에 PostPhysics가 유용하며, 카메라 갱신은 그 이후이고 PostUpdateWork는 카메라 갱신 뒤라고 설명한다. 따라서 모두를 PostPhysics로 옮기는 것만으로 카메라까지 최신이라는 보장은 없다. [Epic: Actor Ticking](https://dev.epicgames.com/documentation/unreal-engine/actor-ticking-in-unreal-engine)

권고 구현 후보는 늦은 프레임의 발사 확정 단계와 명시적 prerequisite이다. CMC의 Primary Tick뿐 아니라 실제 활성화된 PostPhysics based-movement 경로, 스켈레탈 평가 완료를 검증한다. 단순 Timer 지연이나 매 Tick 강제 RefreshBoneTransforms를 해법으로 고정하지 않는다.

늦게 생성된 Projectile의 첫 이동이 언제 실행되는지도 지정한다. 해당 프레임 전체 DeltaTime을 중복 적분하지 않도록 Spawn 시각과 첫 시뮬레이션 구간을 기록한다. Deferred Spawn은 초기화 수단이며 자동으로 프레임 일관성을 만들어 주는 기능은 아니다.

### 6.2 서버 권한과 조준 프로토콜

- 서버가 발사 허용, 무기 속력, 소켓, 탄도 정책, Spawn, 충돌과 피해를 확정한다. 클라이언트는 ViewRay/조준 의도를 보낸다.
- Aim packet에는 ShotId, sequence, 동기화된 시간 기준의 sample time, WorldRay를 둔다. 배 기준 변환이 필요한 경우 실제 이동 기준의 ID/bone과 local view 정보를 함께 설계한다.
- 서버는 시간 범위, 카메라 거리/방향, 현재 공격 상태, 참조한 base의 적합성을 검증한다. Client가 보내는 위치나 base ID를 그대로 권위 있는 판정으로 사용하지 않는다.
- 기본은 **서버 현재 상태에 대한 발사**다. Aim 시점과 서버 시점의 차이를 기록하고, 이동 플랫폼의 상대 정보를 사용할 때도 방향이 world 고정인지 camera/base 상대인지 명시한다. WorldRay를 받았다는 이유로 추가 배 회전을 적용하지 않는다.
- historical rewind가 필요하다면 별도 단계로 도입한다. 클라이언트가 본 base Transform을 검증하려면 서버의 제한된 이력이 필요하다. 클라이언트의 예측·보간 결과와 authoritative 이력이 완벽히 같다고 가정하지 않는다.
- Projectile catch-up을 도입하려면 과거 시간의 충돌 경로까지 sweep해야 한다. 현재 위치로 순간이동시켜 따라잡거나 과거 조준과 현재 표적을 섞는 방식은 피한다. 1차 수정에서 필수로 끼워 넣지 않는다.
- 시각용 예측 화살이 필요하면 ShotId로 서버 화살과 대조·전환한다. gameplay collision root와 보간하는 visual은 분리하며 피해는 서버에서 한 번만 적용한다.
- 배 NetworkPhysics의 resimulation 또는 중복 Notify가 같은 ShotId를 다시 발사하지 않게 한다. 서버 Spawn을 배 물리 callback 안으로 옮기지 않는다.

CMC의 네트워크 예측·보정과 mesh smoothing은 별도 절차이다. 화면에 보이는 mesh 위치를 그대로 물리 속도 출처로 사용하지 않는 이유다. [Epic: Networked Movement in CMC](https://dev.epicgames.com/documentation/unreal-engine/understanding-networked-movement-in-the-character-movement-component-for-unreal-engine)

## 7. 탄도·예측이 필요한 경우의 확장 경계

정지한 월드 점에 중력까지 보정해 맞히려면 다음 식을 만족하는 초기 속도를 구한다.

```text
P(t) = Muzzle + Vworld * t + 0.5 * Gravity * t²
P(t) = AimPoint
```

WorldAim의 직선 방향 발사와 다른 BallisticPoint 정책이다. 고정 목표와 속력에 대한 해는 `SuggestProjectileVelocity`를 검토할 수 있다. 해 없음/막힘은 실패로 돌려주고 조용히 다른 목표로 바꾸지 않는다. [Epic: Suggest Projectile Velocity](https://dev.epicgames.com/documentation/unreal-engine/BlueprintAPI/Game/SuggestProjectileVelocity)

물리 상속과 움직이는 목표를 함께 다루려면 아래 제약을 풀어야 한다.

```text
Muzzle + (Vinherited + Speed * D) * t + 0.5 * Gravity * t² = Target(t)
|D| = 1, t > 0
```

등속 목표는 Target(t)=Target0+Vtarget*t로 시작할 수 있다. 회전하는 배의 목표는 표적 위치의 점속도를 사용해야 하며, 더 높은 정확도가 필요하면 제한된 선/각속도 가정으로 배 Transform과 local target의 미래 위치를 계산한다. 실제 미래의 파도·선회 입력까지 알 수는 없으므로 최대 예측 시간을 두고 정확한 적중 보장으로 표현하지 않는다.

`PredictProjectilePath`는 **이미 선택한 초기 속도의 경로 예측**이다. 움직이는 배의 미래 Transform이나 목표 요격 해를 자동으로 구해 주지 않는다. 미리보기와 발사에는 동일한 LaunchProfile/중력/속도를 사용한다. [Epic: Predict Projectile Path parameters](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FPredictProjectilePathParams)

비행은 월드 공간에서 지속한다. 배 부착, 발사 후 매 Tick 배 회전 적용, 재계산한 배 속도 누적을 일반 화살에 도입하지 않는다. 그런 동작은 별도의 갑판 종속/유도 투사체 설계에 해당한다.

`UProjectileMovementComponent`의 InitialSpeed, local-space 초기화, MaxSpeed 제한과 물리 시뮬레이션 takeover 조건도 무기마다 검사한다. 현재 Arrow 경로가 이미 보장하는 설정은 유지한다. substepping은 곡선 적분·충돌 정밀도 개선용이며 틀린 조준 방향을 해결하지 않는다. [Epic: ProjectileMovementComponent](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UProjectileMovementComponent)

## 8. 단계별 구현 계획과 완료 조건

| 단계 | 작업 | 완료 조건 |
|---|---|---|
| 0. 재현/계측 | 현재 상태를 기준선으로 보존. 지상/직진/회전/파도/클라이언트 사격을 기록 | 조준 수식 편향, ViewRay 차이, 소켓 시차, 충돌 차이를 분리한 재현 자료 |
| 1. 계약 확정 | WorldAim 기본, Release 기준 시점, 중력/예측 정책을 데이터에 명시 | 속력의 기준계와 '조준 성공' 의미가 Player/Enemy에서 동일 |
| 2. 상태 확정 | ShotId와 Snapshot, 이동 상태 adapter, 발사 대기/실패 경계 구성 | 같은 Shot을 중복 확정하지 않고 출처·시점이 기록됨 |
| 3. Player/Enemy 화살 전환 | 동일한 준비·수학 경로 사용. GA/Bow의 중복 방향 보정 제거 | 지상과 갑판에서 초기 월드 방향 계약을 만족 |
| 4. Tick/네트워크 | late commit 순서, 클라이언트 Aim packet, authority 초기화/표현 확인 | 낮은 FPS와 지연에서도 오류 유형과 한계가 예측 가능 |
| 5. 충돌/UI 일치 | 배 hull, 난간, 소켓 초기 겹침 및 조준 가림 표시 점검 | 배/벽을 무시하지 않고 조준 가능과 실제 발사 가능을 구별 |
| 6. 나머지 발사체 감사 | 수류탄·Cluster·Vortex·ThrowItem·대포·선박 스킬 정책 목록화 | 각 무기의 속도/중력/상속/미리보기 정책이 명시됨 |
| 7. 선택 확장 | 필요한 Enemy 예측, 탄도 조준, 시각 예측/rewind만 추가 | 기본 지상·갑판 계약을 보존하고 각각 독립 검증 |

단계 6은 모든 기존 특수탄을 같은 비행 모델로 바꾸는 작업이 아니다. 부력 어뢰, 지정 지점 도달형 대포, 분열탄 등의 고유 규칙은 유지하고 공통 입력·초기화 경계 적용 여부를 정한다. 파생 탄은 부모 탄에서 이미 상속된 속도를 다시 배 속도와 중복 합산하지 않게 한다.

구현 파일 후보: 기존 PlayerAimComponent, GA_BowAimFire, BowComponent, ProjectileLaunchMath, MovementFrameVelocity, Provider, GA_RangedEnemyAttack, RangedEnemy. 공통 Snapshot/LaunchProfile과 발사 확정 컴포넌트는 필요한 최소 단위로 추가한다. ArrowProjectile/충돌은 초기화 계약과 새 검사 연결만 우선 검토한다.

## 9. 검증 행렬과 통과 기준

아래는 앞으로 구현 시 실행할 검증 계획이며 이번 조사에서 실행한 테스트가 아니다.

| 축 | 필수 시나리오 |
|---|---|
| 발사자 | Player, 지상 Enemy, Deck Enemy |
| 바닥/이동 | 지상 정지·좌우 이동, 정지 배, 등속 직진 배, 제자리 yaw, pitch/roll/heave, 파도+선회 |
| 발사 위치 | 배 중심, 선수/선미, 좌우 갑판 끝. 중심에서 멀수록 회전 점속도 차이가 드러나게 구성 |
| 표적 | 지상 정지, 같은 배, 다른 배, 이동 캐릭터, 빈 하늘/최대 사거리 |
| 상태 전환 | 충전 중 승하선, 점프 후 발사, 착지, Carrier 교체, 플랫폼 파괴/텔레포트 |
| 가림 | 카메라만 보이는 난간 너머, 가까운 벽, 선체 내부 시작, 큰 hurtbox 뒤 작은 장애물 |
| 시간 | 30/60/120 FPS, 빠른 카메라 회전과 rotation lag, Notify-입력 간격 변화 |
| 네트워크 | Standalone, Listen Server의 host/client, Dedicated Server, 0/50/100/200 ms RTT와 지터·손실 |

검증을 두 층으로 나눈다.

1. **방향/적분 검증:** 중력과 장애물·표적 이동을 제거하고 Snapshot의 Muzzle, AimPoint, Speed로 독립적인 기준 궤적을 만든다. WorldAim의 초기 각도 오차는 로컬 기준 0.1도 이하, 속력 상대 오차 0.1% 이하를 초기 목표로 둔다. 정책이 WorldAim이면 플랫폼 속도를 바꿔도 같은 Snapshot의 월드 초기 방향은 변하지 않아야 한다.
2. **실제 게임 검증:** 중력·표적 이동·충돌·네트워크를 차례로 켠다. 정적 기준 장면의 경로 오차 목표는 예를 들어 10 m에서 2 cm 이내로 시작하되 충돌 크기/적분 오차를 고려해 확정한다. 움직이는 표적의 명중률은 선택한 예측 정책에 맞춰 별도로 측정한다. 비예측 정책의 미래 표적 미스는 초기 조준 오류와 분리한다.

플랫폼 속도 정책은 별도로 검증한다. 순수 이동/순수 회전의 점속도, 접지·공중 전환 중 중복 상속, 물리 비활성 회전 플랫폼을 실제 movement 상태와 비교한다. 단순히 구현 수식을 그대로 복사한 테스트만 두지 않는다.

ShotId별 로그: client aim time/receive time/commit time, net role, base ID, movement mode, physics frame, camera ray, muzzle transform, aim point, linear/angular/relative velocity, 정책, 최종 Vworld, 첫 이동 구간, 차단 이유. 현재 DebugLaunch는 일부 속도만 기록하므로 원인 분리에 필요한 항목을 확장한다.

네트워크는 서버 궤적 오차와 화면 표현 오차를 따로 기록한다. 서버가 선택한 초기 조건이 정확하더라도 화면 보간 지연이 남을 수 있다. 동일 ShotId에 서버 Projectile 1개/유효 최초 충돌 1개/피해 1회가 보장되어야 한다. 큰 RTT에서의 화면 오차를 정확한 명중 보장으로 숨기지 않는다.

최종 완료 판정은 빌드 성공이 아니라 위 재현 행렬의 통과이다. 중력 없는 정지 표적에서 이동·회전 여부 때문에 초기 방향이 달라지지 않고, 낙차·미래 표적 이동·가림·네트워크 지연은 선택한 규칙대로 설명 가능해야 한다.

## 10. 조사 출처와 적용 주의

Epic 웹 문서는 조회 시 최신 5.8 페이지로 제공되는 경우가 있다. 프로젝트는 5.7이므로 중요한 세부 사항은 설치된 아래 5.7 소스로 대조했다. 계획 구현 시에도 5.8 전용 API를 그대로 도입하지 않는다.

- `Engine/Source/Runtime/Engine/Private/Components/ProjectileMovementComponent.cpp:83`: 초기 속력 및 로컬 공간 처리. `:469`: MaxSpeed. `:687`: 중력/유도에 따른 substep.
- `Engine/Source/Runtime/Engine/Private/Components/CharacterMovementComponent.cpp:648,2416`: CMC PostPhysics Tick과 simulated base 처리.
- `Engine/Source/Runtime/Engine/Private/Character.cpp:627,667`: movement base 선속도와 접선속도.
- `Engine/Source/Runtime/Engine/Private/PrimitiveComponentPhysics.cpp:423`: 점속도 API의 BodyInstance 위임.

위 엔진 경로의 루트는 `C:/Program Files/Epic Games/UE_5.7/`이다. 표준 엔진 동작의 근거와 이 프로젝트에 대한 설계 권고를 구분했으며, WorldAim 기본 선택·Release 시점 선택·검증 오차 한계는 본 계획의 제안이다.
