# 이동·회전 갑판 Projectile 발사 구조와 에디터 확인

2026-09-28 구현 기록. `Planning/Moving_Platform_Projectile_Redesign_2026-09-28.md`의 기본안을 적용했다.

## 적용된 발사 규칙

Player/Enemy 화살의 기본 정책은 **WorldAim**이다. 실제 발사점에서 조준점을 향하는 월드 초기 속도를 한 번 확정하고, 발사자/배의 횡속도로 방향을 다시 꺾지 않는다. 지상·움직이는 배·점프 중에도 같은 정책을 사용한다.

무기의 `Launch Profile → Velocity Policy`에서 **PhysicalInheritance**를 명시하면 발사자 이동과 플랫폼 발사점의 점속도를 한 번 상속한다. 이 모드의 속력은 발사자 기준이며 월드 조준점 명중을 보장하지 않는다. 기본 화살의 낙차, 움직이는 목표에 대한 수동 선행 조준은 그대로 적용된다.

기존 `Planning/Moving_Platform_Projectile_Plan.md`의 월드 속력을 유지하면서 횡속도를 더하는 수식은 더 이상 사용하지 않는다.

## 책임과 호출 흐름

| 구성 | 책임 |
|---|---|
| `PlayerAimComponent` | 화면 중앙 ViewRay, ShotId, 시각 검증, 서버 전송/응답. 배 회전을 추정해 시선을 바꾸지 않음 |
| `ARangedEnemy::CaptureRangedAim` | 서버의 현재 표적과 캐릭터 소켓을 캡처. 탄도 계산 뒤 `HasClearRangedLaunch`에서 최종 초기 방향의 LOS 검사 |
| `BowComponent` / `EnemyBow` | 무기 속력과 명시적 LaunchProfile 제공 |
| `ProjectileShotComponent` | Notify 요청을 TG_PostUpdateWork에서 처리. 대기·취소·완료 수명 관리 |
| `ProjectileShotPreparation` | 발사점·조준·이동 상태를 조합해 Snapshot 생성. 플랫폼이 없는 지상도 정상 입력 |
| `ProjectileLaunchMath` | 3인칭 시차 방향과 정책별 월드 속도를 순수 수학으로 계산 |
| `MovementFrameVelocity` / Ship Provider | 실제 기반/부착 상태에서 점속도 취득. 무기 조준과 속도 정책을 결정하지 않음 |
| `ProjectileLaunchInitialization` | 이미 결정한 월드 속도를 PMC에 적용. local-space 재해석과 InitialSpeed 덮어쓰기를 방지 |
| Player/Enemy GA | 공격 허용·충전·서버 Spawn·GAS Spec 초기화 |
| Arrow movement/query/impact | 비행 구간 충돌과 서버 피해. 준비 단계의 가림 검사는 피해를 발생시키지 않음 |

Player의 버튼 해제 입력에는 ShotId만 실린다. 실제 FireArrow Notify가 발생하면 공통 컴포넌트에 발사를 예약하고, 로컬 플레이어가 카메라 갱신 이후의 화면 중앙을 캡처한다. 서버는 자신의 Notify 허용 상태와 해당 ShotId의 검증된 ViewRay가 모두 준비되어야 발사한다.

서버 대기는 최대 1초, 원격 owning client의 응답 대기는 최대 2초다. 클라이언트는 서버의 성공/실패 응답을 받고 발사 요청을 종료한다. 몽타주가 먼저 끝나도 예약된 발사 처리가 끝날 때까지 필요한 상태를 유지하며, 공격 취소·무기 변경·어빌리티 종료는 요청을 취소한다. 중복 Notify/중복 View packet은 같은 발사를 반복하지 않는다. 최근 ShotId 재사용도 거부한다.

발사 Snapshot은 시각·프레임, 월드 소켓, 조준점, 무기 속력/정책/중력, 이동 속도 샘플의 유효성을 포함한다. 서버 Spawn 후 화살은 이 값을 소비하며 발사자의 배를 계속 따라가지 않는다. 늦은 프레임에 Spawn한 화살이 생성 프레임 전체 DeltaTime을 다시 적분하지 않도록 첫 이동도 제한한다.

CMC Primary Tick과 스켈레탈 Tick prerequisite를 두고, 물리와 CMC PostPhysics based movement 및 카메라 갱신보다 뒤인 PostUpdateWork에서 확정한다. 매 발사마다 강제로 전체 뼈를 새로 계산하는 방식은 사용하지 않는다. 기존 서버 공격 중 pose refresh 수명 관리는 유지한다.

## 카메라·가림 표시

카메라 조준은 WeaponAim 채널에 더해 ArrowObstacle 정책의 ray를 검사해, 배 구조물이 조준 Trace에서는 무시되고 화살에서는 막히는 차이를 줄인다. 자기 배도 장애물이다.

조준 중 소켓에서 초기 방향으로 작은 장애물 Box 검사를 수행하고, 목표 앞에 장애물이 있으면 기존 활 십자선을 빨갛게 표시한다. 실제 발사 시에도 같은 Query 정책으로 확인한다. Player는 발사할 수 있지만 화살이 실제 장애물에서 충돌한다. Enemy는 막힌 발사를 거부하고 기존 재배치 처리를 호출한다.

이 표시는 **초기 방향의 가림**이며 중력이 있는 전체 미래 탄도를 예측한 결과는 아니다. 큰 캐릭터 명중 보조 크기와 작은 장애물 크기의 기존 구분도 유지한다.

## 다른 투사체에 적용된 범위

| 발사체/경로 | 적용 내용 |
|---|---|
| Player/Enemy 화살 | 조준·ShotId·프레임 후반 확정·Snapshot·속도 정책·서버 승인·가림 표시까지 전체 경로 전환 |
| Grenade / ClusterGrenade / ThrowItem에서 사용하는 Grenade | 공통 월드 속도 초기화. 기존 투척 각도·중력·바운스·폭발 규칙 사용 |
| SubMunition | 부모가 생성한 최종 월드 분산 속도를 공통 초기화. 배 속도 추가 합산 없음 |
| GravityVortex | 공통 월드 속도 초기화. 스킬의 기존 조준·수면 도달 처리 사용 |
| Cannonball 및 이를 상속하는 Torpedo | 공통 초기화. 대포에서 이미 계산한 상속 속도를 다시 가산하지 않음. 수중 부력 전환 유지 |
| Enemy TimeStopProjectile / ObstacleProjectile | 공통 초기화. 속력 제한과 지정 지점 도달 등 각 스킬 규칙 유지 |

기본 화살의 Release 화면 조준 프로토콜을 모든 특수 스킬에 강제로 적용하지는 않았다. 투척 궤적 미리보기/특수 스킬의 타기팅은 기존 계산 경로를 사용한다. 이번 공통 초기화는 이미 구한 월드 속도의 적용을 통일한 것이다.

물리 시뮬레이션이 꺼진 배나, 점속도 Provider가 없는 움직이는 비물리 플랫폼은 회전 속도를 추정하지 않고 unavailable을 반환한다. WorldAim은 정상 발사하며 PhysicalInheritance는 명시적으로 실패한다. 비물리 플랫폼에서 관성 상속까지 사용하려면 그 플랫폼이 신뢰할 수 있는 점속도를 제공해야 한다.

선택 확장인 AI 자동 선행 조준, 중력 자동 영점 보정, 과거 월드 rewind, 클라이언트 예측 화살/catch-up은 도입하지 않았다. 서버는 현재 상태에서 발사하므로 네트워크 지연으로 발생하는 화면/서버 위치 차이까지 없어지는 것은 아니다.

## 에디터에서 간단히 확인하기

1. **에디터를 재시작**한다. 네이티브 타입과 RPC/TargetData 변경이 있으므로 Live Coding만으로 확인하지 않는다. Player 활, Enemy 활, 화살 BP를 열어 Compile한다. 기본 `Launch Profile → Velocity Policy`가 `WorldAim`인지 확인한다. Player는 BowComponent의 `Bow|Fire`, Enemy는 EnemyBow의 `Enemy Bow|Projectile`에서 설정한다. 기존 NockArrow/FireArrow Notify와 캐릭터 화살 소켓은 필요하다.
2. 콘솔에서 **`sw.Projectile.DebugLaunch 1`**을 켠다. 처음에는 화살 BP의 **Flight Gravity Scale을 0**으로 두고 지상의 정지 표적에 정지/좌우 이동 중 발사한다. 이후 움직이고 선회하는 배의 중심·선수·선미에서 같은 표적에 발사한다. 흰 목표점 방향과 노란 초기 속도 방향이 맞아야 한다. 테스트용으로 바꾼 중력 값은 이후 원래 설정으로 되돌린다.
3. 배 위 Enemy에도 사격하게 하고, 점프·착지·승하선·빠른 카메라 회전·사격 취소를 확인한다. 난간이나 벽 앞에서는 가림 상황에 십자선이 빨개지고 화살은 벽을 통과하지 않아야 한다. Enemy는 발사선이 막히면 발사를 거부/재배치해야 한다. 충전 해제 뒤 FireArrow Notify까지 시선을 움직였을 때 **실제 Release 시점의 화면**을 기준으로 발사하는지 확인한다.
4. PIE를 **2 Players / Listen Server**로 실행하고 host/client 양쪽에서 반복한다. Play의 Network Emulation으로 평균 지연 약 100 ms/방향을 적용해 RTT 약 200 ms 환경도 확인한다. 마지막으로 **`t.MaxFPS 30`**에서 반복하고, 사격 1회에 서버 화살과 피해가 1회인지 본다. 원래 FPS 제한과 네트워크 설정을 복구하고 **`sw.Projectile.DebugLaunch 0`**으로 끈다.

흰 점/노란 선이 맞는데 회전하는 같은 배의 표적을 빗나가는 경우, 발사 이후의 표적 이동이나 중력 낙차를 먼저 구분한다. 현재 정책은 미래 표적을 자동 추적하지 않는다. 배 내부에서 즉시 충돌하면 Query Hull이 빈 실내까지 막고 있는지 에셋 충돌을 확인한다.

추가 회귀 확인: 수류탄 한 번 투척, Cluster 분열, Vortex 수면 도달, 대포/Enemy 선박 스킬을 사용해 공통 초기화 뒤에도 각각의 바운스·중력·지정 지점·부력 전환이 유지되는지 본다.

## 로그와 검증 결과

- `[ProjectileAim]`: ShotId, 네트워크 role, 조준 sample 시각, 서버 수신 시각, 실제 Origin/Direction.
- `[ProjectileShot]`: ShotId, 확정 프레임/시각, 조준 데이터 age, 정책, Carrier, MotionValid, 소켓/조준점/최종 속도/중력.
- `[ProjectileLaunch]`: 실제 적용 속도와 기존 궤적/충돌 크기 디버그. 파랑 계열은 발사자 속도, 노랑은 화살 초기 속도, 자홍은 참고 탄도다.
- `Rejected/timed out`이면 같은 ShotId의 Aim 수신, Notify, 무기/소켓 상태를 확인한다. `Missing NockArrow notify`는 NockArrow 이벤트가 없는 몽타주 설정을 뜻한다.

`ArtisticSW2026Editor Win64 Development` 빌드와 `git diff --check` 통과. 자동화 테스트는 요청에 따라 추가·실행하지 않았다. 실제 PIE/네트워크 사격과 BP 에셋 검증은 위 절차로 확인해야 한다. 빌드 중 기존 `UGameplayEffect::StackingType` API 경고가 관찰되었으며 이번 발사 코드의 오류는 없었다.
