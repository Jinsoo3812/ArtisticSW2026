# AShip Network Physics와 파도 동기화 이론

- 작성일: 2026-07-13
- 대상: Unreal Engine 5.7, `AShip`, Chaos Async Physics, Network Physics Resimulation, Water/Gerstner Waves
- 목적: 물리 예측과 네트워크 동기화, 파도 시간축, 부력, rollback/resimulation을 하나의 일관된 모델로 설명하고 이후 구현·디버깅의 기준으로 사용한다.
- 관련 누적 기록: `Planning/AShip_NetworkPhysics_Worklog.md`

## 1. 왜 이 문제가 어려운가

네트워크 물리는 단순히 서버 위치를 Client에 복제하는 문제가 아니다. 다음 네 시스템이 동시에 맞아야 한다.

1. **물리 시뮬레이션**: 질량, 중력, 부력, 감쇠, 충돌을 작은 timestep마다 적분한다.
2. **네트워크 시간**: 서버와 Client는 서로 다른 시점에 시작하고 패킷은 지연되어 도착한다.
3. **예측과 history**: Client는 패킷을 기다리지 않고 미래를 계산하며, 과거 상태를 보관해야 한다.
4. **렌더링**: 화면은 physics tick과 다른 주기로 그려지며 부드러워야 한다.

배는 특히 어렵다. 지면 위 차량과 달리 매 frame 여러 폰툰이 움직이는 파도 표면을 샘플링한다. 위치가 조금 달라지면 수면 높이와 잠김 깊이가 달라지고, 부력이 달라져 다음 위치 차이가 더 커진다. 이것은 다음 feedback을 만든다.

```text
작은 위치 차이
  → 서로 다른 폰툰 좌표
  → 서로 다른 파도 높이와 잠김 깊이
  → 서로 다른 부력과 토크
  → 더 큰 위치·회전 차이
```

Network Physics의 역할은 모든 오차를 영원히 0으로 만드는 것이 아니다. Client가 부드럽게 예측하게 하면서, 권위 상태가 도착했을 때 **같은 과거 frame끼리 비교하고 필요할 때만 되감아 오차를 통제**하는 것이다.

## 2. 가장 중요한 개념: 세 종류의 시간

### 2.1 서버 gameplay world time

`AGameStateBase::GetServerWorldTimeSeconds()`는 모든 peer가 대략 같은 서버 시간축을 보게 한다. 수영 query, 효과 시작 시간, Water 렌더 MPC처럼 gameplay와 visual 시스템에 적합하다.

하지만 Client의 이 값은 네트워크로 추정·보정·스무딩된다. 서버와 Client가 같은 순간 호출해도 수 ms~수십 ms 차이가 날 수 있다. 따라서 이것을 physics rollback의 frame identity로 사용하면 안 된다.

### 2.2 physics local frame

각 프로세스의 Chaos solver가 시작된 뒤 센 local physics step이다.

- 서버가 먼저 8초 실행했다면 약 `LocalFrame=480`에 있을 수 있다.
- 그때 접속한 Client는 `LocalFrame=0`부터 시작한다.

두 숫자는 다르지만 각 프로세스 내부 history slot을 찾을 때 필요하다.

### 2.3 authoritative server physics frame

네트워크 물리의 공통 좌표계다. 서버 physics step을 기준으로 모든 state와 input의 신원을 표시한다.

Client에서는 다음 관계를 사용한다.

```text
ServerFrame = ClientLocalFrame + NetworkPhysicsTickOffset
ClientLocalFrame = ServerFrame - NetworkPhysicsTickOffset
```

예를 들어 서버가 frame 506만큼 먼저 시작했다면:

```text
Client LocalFrame 28 + Offset 506 = ServerFrame 534
```

서버 frame 600의 authoritative state가 도착하면 Client는 자신의 history `LocalFrame=94`와 비교해야 한다.

```text
600 - 506 = 94
```

Network Physics에서 가장 위험한 버그는 값이 조금 틀리는 것이 아니라, **서로 다른 frame의 올바른 값끼리 비교하는 것**이다.

## 3. physics용 공통 파도 시간

서버는 physics clock anchor를 한 번 정한다.

```text
ServerPhysicsTimeOrigin
ServerPhysicsStepSeconds
```

각 physics frame의 파도 시간은 다음처럼 계산한다.

```text
SimTime = ServerPhysicsTimeOrigin
        + ServerFrame × ServerPhysicsStepSeconds
```

현재 프로젝트는 60 Hz fixed step을 사용한다.

```text
ServerPhysicsStepSeconds ≈ 0.016666667
```

이 방식의 장점은 다음과 같다.

- 서버와 Client가 다른 실제 wall-clock 순간에 계산해도 같은 `ServerFrame`이면 같은 `SimTime`을 얻는다.
- rollback으로 과거 frame을 재생할 때도 그 frame의 파도 시간을 정확히 복원한다.
- Client의 스무딩된 GameState 시간 오차가 physics force에 들어오지 않는다.

## 4. 결정론이 의미하는 것

결정론적 시뮬레이션은 단순히 “같은 시간”만 넣는 것이 아니다. 다음 조건이 같아야 같은 결과가 나온다.

- 같은 과거 초기 상태 `X/R/V/W`
- 같은 input
- 같은 fixed timestep
- 같은 physics frame과 파도 시간
- 같은 파도 파라미터와 순서
- 같은 폰툰 위치·반경
- 같은 질량, 중력, 부력 계수, damping, clamp
- 같은 외부 force와 torque
- 같은 충돌 조건과 solver 순서

하나라도 history에 포함되지 않은 값이 서버와 Client에서 달라지면 rollback은 완전한 재생이 아니다.

### 4.1 같은 좌표에 대한 오해

Client prediction에서는 서버와 Client의 **현재 좌표를 항상 강제로 같게 만들면 안 된다.** Client는 자신의 예측 particle 상태로 미래를 계산해야 한다.

올바른 요구사항은 다음이다.

> 동일한 과거 server frame을 재시뮬레이션할 때, 그 frame에 복원된 동일 상태와 동일 입력으로 동일 파도 함수를 샘플링한다.

Client live position을 매 frame 서버 위치로 덮으면 예측이 아니라 transform replication이 된다.

## 5. UE Network Physics의 기본 폐루프

### 5.1 서버

각 physics step에서:

1. frame의 input을 읽는다.
2. 파도, 부력, 조종 force를 적용한다.
3. Chaos가 상태를 적분한다.
4. `FNetStatePhysicsShip`에 상태를 저장한다.
5. frame identity와 함께 Client에 전송한다.

### 5.2 Client normal prediction

각 physics step에서:

1. local frame을 server frame으로 변환한다.
2. 해당 frame의 input을 history에 저장한다.
3. 같은 server-frame `SimTime`으로 파도를 샘플링한다.
4. local Ship을 독립적으로 적분한다.
5. 예측 state history를 저장한다.

### 5.3 authoritative state 수신

서버 state가 늦게 도착하면 그것은 현재가 아니라 과거 정보다.

```text
현재 Client LocalFrame = 1000
수신 ServerFrame = 1450
Offset = 506
비교할 과거 Client LocalFrame = 944
```

현재 frame 1000과 직접 비교하지 않는다. history 944의 예측 state와 비교한다.

### 5.4 compare와 threshold

현재 Ship state는 위치와 회전을 correction 기준으로 사용한다.

```text
PositionError = Distance(AuthPosition, PredictedPosition)
RotationError = AngularDistance(AuthRotation, PredictedRotation)
```

현재 정책:

- 위치 threshold: 5 cm
- 회전 threshold: 5°

threshold가 너무 작으면 사소한 float/solver 차이도 correction storm을 만든다. 너무 크면 눈에 보이는 분리를 허용한다. threshold는 원인 수정 전에 올려서 문제를 숨기는 값이 아니라, 동기화가 정상일 때 허용할 게임 오차 예산이다.

속도 오차는 현재 진단하지만 rollback trigger의 직접 기준은 아니다. 위치가 아직 작아도 큰 속도 오차가 다음 frame의 threshold crossing을 예고할 수 있으므로 분석 지표로 중요하다.

### 5.5 apply와 resimulation

오차가 threshold를 넘으면 과거 state를 적용한다.

복원해야 하는 값:

- `X`: 현재 위치
- `R`: 현재 회전
- `P`: Chaos 다음 적분 위치
- `Q`: Chaos 다음 적분 회전
- `V`: 선속도
- `W`: 각속도

`P/Q`를 복원하지 않으면 `X/R`만 맞춘 직후 solver가 이전 예측 transform 방향으로 다시 튈 수 있다.

그 뒤 저장된 input을 과거 frame부터 현재까지 빠르게 재실행한다. 이 모든 과정은 화면의 현재 한 frame 안에서 일어날 수 있다.

## 6. AShip payload 설계

### 6.1 Input history

`FNetInputShip`은 시간에 따라 변하고 재생해야 하는 조종 입력을 담는다.

- `MovementInput`
- `SteeringInput`
- `ServerFrame`

input serializer는 상속된 `FNetworkPhysicsPayload::ServerFrame`이 자동 전송된다고 가정하면 안 된다. 현재 구현은 packed integer로 명시적으로 직렬화한다.

### 6.2 State history

`FNetStatePhysicsShip`은 다음을 담는다.

- Position
- Rotation
- LinearVelocity
- AngularVelocity
- correction thresholds
- ServerFrame

### 6.3 정적 설정은 input과 다르다

파도 배열, 폰툰 배열, 질량 관련 설정, 부력 계수는 매 frame의 조종 입력과 성격이 다르다. 현재 구현은 이를 GT→PT async producer input으로 전달해 PT cache에 보관한다.

이 분리가 필요한 이유:

- resim 중 `ApplyInput`이 Move/Steer만 역직렬화할 때 정적 설정을 생성자 기본값으로 덮지 않는다.
- 큰 wave 배열을 매 input packet에 반복 직렬화하지 않는다.
- GT가 PT에서 읽는 `TArray`를 직접 수정하는 data race를 피한다.

단, runtime 중 파도 asset이나 부력 설정이 바뀐다면 “정적”이 아니다. 그때는 config version과 적용 server frame을 history에 기록해야 과거 replay가 정확하다.

## 7. AShip custom 부력의 개념

각 폰툰 local offset을 Ship particle state로 world position으로 바꾼다.

```text
PontoonWorld = ShipPosition + ShipRotation × PontoonLocalOffset
```

동일 server frame의 파도 높이를 구하고 잠김 깊이를 계산한다.

```text
Depth = WaveHeightZ - PontoonWorldZ
```

Depth가 양수면 폰툰이 잠겼다. 구의 잠긴 체적, 물 밀도, 중력, coefficient를 이용해 위쪽 force를 만들고 damping을 적용한다. 폰툰 위치가 무게중심에서 떨어져 있으므로 force는 torque도 만든다.

```text
Torque = LeverArm × BuoyantForce
```

현재 중요한 안정화 정책:

- Chaos built-in gravity만 사용하고 manual gravity를 중복 적용하지 않는다.
- 60 Hz fixed step을 사용한다.
- 폰툰별 `MaxBuoyantForce` 상한을 둔다.
- 위치·회전·속도를 같은 frame에서 계산한다.
- 부력 계산 준비 전에는 custom force를 적용하지 않는다.

질량, radius, coefficient, damping은 서로 독립적이지 않다.

- 질량 증가: 흘수 증가, 관성 증가
- radius/체적 증가: 같은 깊이에서 부력 증가
- coefficient 증가: 복원력이 강해지지만 과하면 튀고 발산하기 쉬움
- damping 증가: 상하 진동 억제, 과하면 물속에서 끈적해짐
- 폰툰 간격 증가: roll/pitch 복원 torque 증가

튜닝은 정지 수면에서 흘수와 안정 자세를 먼저 맞추고, 그 다음 파도, 마지막에 충돌과 승객을 추가해야 한다.

## 8. 파도 함수와 렌더링 수면

### 8.1 physics peer 간 일치

AShip PT의 핵심 요구는 서버와 Client가 같은 함수, 파라미터, 좌표, `SimTime`을 사용하는 것이다. M20에서는 offset 정착 후 100개 공통 server frame에서 wave/depth/force가 로그 정밀도 기준 일치했다.

### 8.2 physics와 화면이 같은가

이것은 별도 질문이다.

- 물 렌더링: WaterSubsystem/MPC와 머티리얼 vertex displacement
- gameplay query: UE Water Waves wrapper와 ripple
- Ship physics: custom PT-safe base Gerstner 계산

시간축이 같아도 수식이 다르면 표면 Z는 다를 수 있다. UE full Gerstner는 steepness/Q 기반 수평 변위 역산, Water Body transform, attenuation, LWC 등을 포함할 수 있다. custom cosine 합이 이것을 모두 재현하지 않으면 “서버와 Client Ship physics끼리 동일”하지만 “보이는 물과 물리 수면이 완전히 동일”하지는 않다.

최종 정책은 둘 중 하나를 명시적으로 선택해야 한다.

1. 렌더와 physics가 같은 canonical wave library를 사용한다.
2. physics는 단순하고 결정론적인 근사 수면을 사용하며 렌더와의 허용 오차를 정의한다.

### 8.3 ripple

ripple이 Client local overlap으로만 생기면 peer마다 다르다.

- cosmetic ripple: Ship authoritative physics에서 제외한다.
- gameplay ripple: 원점, 시작 server frame/time, amplitude, radius, lifetime 등을 서버가 복제하고 history-safe하게 재생한다.

정책 없이 local ripple을 부력에 넣으면 결정론은 깨진다.

## 9. Simulated Proxy와 Controller 함정

비소유 Ship은 Client에서 `ROLE_SimulatedProxy`이며 Ship 자신의 Controller가 없다. 이것은 정상이다. Controller가 없어도 Client Chaos와 Network Physics prediction은 실행될 수 있다.

문제는 custom code가 다음을 준비 조건으로 사용했을 때 발생했다.

```cpp
NetworkPhysicsComponent->IsNetworkPhysicsTickOffsetAssigned()
```

UE 5.7에서 이 함수는 Component owner인 Ship의 Controller를 찾는다. Simulated Proxy Ship에는 Controller가 없으므로 영원히 false였다.

그러나 UE internal async Network Physics는 World의 첫 PlayerController에서 tick offset을 얻는다. 현재 AShip custom PT도 같은 방식으로 수정됐다.

수정 전에도 Client 화면에서 배가 어느 정도 움직인 이유:

1. Client Chaos 자체는 실행됐다.
2. UE internal Network Physics state/history도 실행됐다.
3. 서버가 계산한 `X/R/V/W`가 계속 도착했다.
4. Client가 그 속도로 패킷 사이를 적분했다.
5. 오차가 커질 때마다 authoritative state를 적용했다.

즉 custom 부력을 양쪽이 독립 계산해서가 아니라, Client가 서버 state에 자주 교정되며 따라간 것이다. 이 때문에 평소에는 부드러워 보이다가 correction이 몰릴 때 제자리 진동이 생겼다.

## 10. 충돌은 history에 없는 외력이다

Character의 `Enable Physics Interaction=false`는 CharacterMovement가 명시적으로 가하는 Push/Touch/StandingDownward/Repulsion force를 막는다. 그러나 Capsule과 dynamic Ship이 Block이면 Chaos contact constraint는 별도로 작동한다.

Character Capsule은 CharacterMovement가 위치를 지정하는 kinematic collider에 가깝다. dynamic Ship과 겹치면 solver는 움직일 수 있는 Ship을 밀어 비관통 조건을 만족시킬 수 있다.

서버 Character와 Client Character는 위치·착지 시점이 조금 다르다. Ship만 rollback하고 Character Capsule을 같은 과거 frame으로 함께 rollback하지 않으면 contact impulse를 동일하게 재생할 수 없다.

현재 해결 구조:

```text
Dynamic BuoyancyRoot
  - Pawn Ignore
  - 실제 물리와 부력 담당

QueryOnly Deck / Rails
  - Simulate Physics false
  - Pawn Block
  - BuoyancyRoot transform에 attach
  - Auto Weld false
  - Character floor/sweep만 담당
```

이 구조에서 Character는 갑판 위에 서지만 Capsule이 Ship rigid body에 raw contact impulse를 주지 않는다.

승객 무게가 필요하면 contact를 다시 켜는 대신 다음을 deterministic input으로 모델링한다.

- 승객 질량
- Ship 기준 상대 위치
- grounded 여부
- 적용 server frame

PT에서 `Mass × Gravity`와 상대 위치 torque를 같은 frame에 적용하면 rollback에서도 재현할 수 있다.

## 11. late join과 warm start

서버가 오래 실행된 뒤 Client가 접속하면 Client의 map 초기 Ship 위치는 이미 움직인 서버 Ship과 크게 다르다. 또한 첫 authoritative packet이 가리키는 Client local history slot이 생성되기 전일 수 있다.

따라서 첫 비교에 수 m 오차가 생기는 것은 steady-state 결정론 실패와 다르다.

위험한 해결:

- frame 없는 최신 replicated transform을 현재 PT particle에 즉시 적용
- 과거 ServerFrame의 snapshot을 현재 추정 frame에 fast-forward 없이 적용
- history가 준비될 때까지 Client simulation을 무작정 중단

이 방식은 서로 다른 시간의 상태를 섞어 history를 더 오염시킬 수 있다.

안전한 기본 정책은 첫 authoritative correction을 허용하고 빠르게 정확한 궤도로 수렴하는 것이다. 완전한 warm start가 필요하다면 엔진이 history slot을 생성하는 정확한 시점에 **그와 동일한 local frame의 authoritative state**를 seed해야 한다.

## 12. 렌더 부드러움과 물리 정합성은 다르다

frame-matched physics가 정확해도 화면은 떨릴 수 있다.

- physics는 60 Hz
- 렌더는 가변 FPS
- replicated diagnostic snapshot은 더 낮은 빈도
- Character based movement와 camera update 순서가 다름

따라서 다음을 구분해야 한다.

1. same-frame physics error
2. 최신 snapshot과 현재 local state의 시간차
3. mesh visual interpolation error
4. camera/passenger jitter

`SHIP-SYNC LocDiff`처럼 frame 없는 최신 ReplicatedState와 현재 Actor를 비교한 값은 network latency를 포함한다. 40 cm가 나왔다고 same-frame history 오차가 40 cm라는 뜻이 아니다.

물리 정합성을 먼저 same-frame 로그로 확정한 뒤 mesh/camera smoothing을 조정해야 한다.

## 13. 로그를 읽는 법

### `NETPHYS-CLOCK`

서버 physics origin과 dt가 정해졌는지 확인한다.

### `NETPHYS-OFFSET`

Client custom PT가 local→server frame mapping을 수신했는지 확인한다.

```text
PT accepted synchronized tick offset at LocalStep=28 Offset=506
```

### `NETPHYS-COMPARE`

반드시 다음을 함께 본다.

- AuthServerFrame
- AuthLocalFrame
- PredServerFrame
- PredLocalFrame
- Dist/Rot
- velocity difference

frame identity가 다르면 거리 수치는 의미가 없다.

### `PHYSICS-PT-CL-APPLY`

authoritative state가 실제 particle에 적용된 횟수다. 정상 steady-state에서 계속 쏟아지면 correction storm이다.

### `PHYSICS-PT-WAITING FrameOffset=Missing`

custom force 준비가 안 됐다는 뜻이다. 로그는 60 step마다만 출력될 수 있으므로 한 줄 사이의 모든 frame도 skip됐을 수 있다.

### `NETPHYS-DET`

동일 server frame의 state, time, wave, depth, pontoon force, 총 force/torque를 서버와 Client에서 직접 대조한다. 결정론 검증의 가장 강한 증거다.

## 14. 이번 과정에서 확인한 대표 실패 원인

1. 상속 payload의 `ServerFrame`이 custom `NetSerialize`에서 자동 전송될 것이라는 가정
2. 조종 input과 정적 파도/부력 config를 한 payload에 섞음
3. resim input load가 static cache를 생성자 기본값으로 오염
4. 존재하지 않는 `AsyncPhysicsTickHz` 설정으로 실제 30 Hz 실행
5. built-in gravity와 manual gravity 이중 적용
6. 무상한 부력 force 피크
7. rollback에서 `P/Q`를 복원하지 않음
8. Simulated Proxy Ship의 Component-owned Controller readiness를 사용
9. Character blocking contact를 deterministic history 없이 외력으로 허용
10. 다른 frame의 최신 snapshot을 warm-start state로 주입
11. frame 없는 빨강/초록 디버그 거리를 same-frame 오차로 해석

이 문제들은 모두 “숫자 하나를 튜닝”해서 해결되지 않는다. 시간, frame identity, state ownership, history에 포함되는 데이터의 경계를 먼저 바로잡아야 한다.

## 15. 최종 수용 테스트 설계

각 run은 최소한 다음 수치를 남겨야 한다.

- offset assigned 시점과 값
- first authoritative state와 first valid history frame
- compare MATCH/MISMATCH 횟수
- ApplyState 횟수와 초당 빈도
- 최대/평균 위치·회전·속도 오차
- resim frame 수와 길이
- 동일-frame wave/depth/force 차이
- startup과 steady-state를 분리한 통계

테스트 행렬:

- Dedicated Server / Listen Server
- Client 1 / Client 2 이상
- 동시 접속 / 8초 / 60초 late join
- 비소유 Simulated Proxy / 소유·조종 Ship
- 정지 / 직진 / 조향 / 급격한 입력 전환
- 승선 / 걷기 / 점프 / 난간 충돌
- packet lag / jitter / loss / reorder
- 여러 AShip
- 장시간 운항
- 큰 월드 좌표와 LWC 경계

완료 판정은 “눈으로 괜찮아 보임”만으로 하지 않는다. 체감 안정성과 frame-matched 수치를 둘 다 통과해야 한다.

## 16. 현재 확보된 증거와 아직 남은 범위

M20 Dedicated Server + 1 Client, 8초 late join, 60초 run에서:

- Client offset 506 수신
- `FrameOffset=Missing=0`
- startup 이후 correction storm 없음
- SF 1200~3360 주기 compare 위치 오차 0 cm
- offset 정착 후 공통 100개 frame의 파도·부력·torque 일치

따라서 **정지한 단일 Ship의 core Network Physics와 custom base-wave 부력 동기화**는 검증됐다.

아직 최종 검증되지 않은 범위:

- 실제 Ship 조종 input replay
- 2개 이상 Client와 여러 Ship
- 60초 이상 late join
- packet impairment
- runtime config 변경 history
- dynamic external collision
- full UE Water/render surface equivalence
- ripple authority
- engine-history-aware zero-pop warm start

## 17. 한 문장으로 정리한 설계 원칙

> 서버와 Client가 지금 같은 위치에 있게 강제하는 것이 아니라, 같은 server physics frame을 재생할 때 같은 상태·입력·설정·파도 시간·외력을 사용하게 만들고, 차이가 허용 오차를 넘을 때만 그 과거 frame으로 되감는 것이 Network Physics 동기화다.
