# Level_Jang 지상 적 Network/EQS MVP 프로파일 분석 — C7 Server Run01

- 작성일: 2026-08-14
- 대상 맵: `/Game/Level/Level_Jang`
- 측정 대상: `MeleeEnemy`, `RangedEnemy`, 원거리 투사체, EQS/BT/Perception, 서버 복제
- 실행 형태: 동일 PC의 UnrealEditor Development 전용 서버 + 별도 게임 클라이언트 1개
- 이번 시나리오: `Count=7`, `Mode=Combat`, `Seed=100`
- 종합 판정: **측정 파이프라인은 통과했지만, C7 steady-state 기준값으로는 부분 유효하다.**

## 1. 이 문서의 목적

이 문서는 첫 번째 `Level_Jang` 측정에서 생성된 서버 trace, CSV, 서버/클라이언트 로그를 서로 대조하여 다음을 판단한다.

1. 측정 파일이 정상적으로 생성되고 종료되었는가.
2. 캡처 구간이 의도한 전투 시나리오를 정확히 포함하는가.
3. 서버 프레임, AI/EQS, 네트워크 복제, 메모리 중 어디가 실제 병목 후보인가.
4. 기존 MVP 계획의 B0/E0, Idle, Combat, stress 단계를 앞으로 어떤 지표로 검증해야 하는가.
5. 최종 목표인 **Melee 4 + Ranged 2** 구성을 재현하려면 무엇을 보완해야 하는가.

이번 수치는 최종 최적화 결론이 아니라 **측정 시스템 검증 및 병목 후보 분류용 Run01**이다. 비교 기준인 B0와 I7이 없고, 캡처가 적 스폰보다 먼저 시작되었기 때문에 `C7 - B0` 비용이나 클래스별 bytes/sec를 아직 확정할 수 없다.

## 2. 분석에 사용한 결과물

| 종류 | 파일 | 역할 |
|---|---|---|
| 서버 trace | `Saved/Profiling/Enemy/Level_Jang/C7_Server_Run01.utrace` | Timing/Networking Insights의 원본 |
| 서버 CSV | `Saved/Profiling/CSV/Profile(20260814_200913).csv` | 600프레임의 프레임별 CPU, AI, 복제, Actor count 통계 |
| 서버 로그 | `Saved/Logs/EnemyProfile_C7_Server_Run01.log` | 시작 옵션, 접속·스폰·캡처 타임라인, 네트워크 누계, 경고 확인 |
| 클라이언트 로그 | `Saved/Logs/EnemyProfile_C7_Client_Run01.log` | 서버 접속, 맵 이동, 연결 종료 원인 확인 |

CSV는 UE 5.7 형식에 따라 맨 앞과 끝에 헤더가 있고 마지막 줄에 메타데이터가 있다. 실제 데이터는 600행이며 163개 열을 포함한다.

### 측정 환경

| 항목 | 값 |
|---|---|
| Engine | Unreal Engine 5.7.4, CL 51494982 |
| Configuration | Development / WindowsEditor |
| CPU | AMD Ryzen 5 9600X 6-Core Processor |
| 서버 렌더링 | `-NullRHI`, `-NoSound` |
| 클라이언트 수 | 1 |
| Iris | 비활성화(`Iris=0`), 기존 NetDriver replication |
| trace 채널 | `cpu,frame,bookmark,net`, `-NetTrace=1` |
| CSV 캡처 | warm-up 5초 후 600프레임 |
| CSV CaptureDuration | 28.056초 |
| Trace 기록 메모리 | 약 32.756 MB |

Editor 기반 Development 서버 측정이므로 절대 CPU·메모리 수치는 패키징된 Dedicated Server의 출시 기준값이 아니다. 다만 동일 환경에서 반복하는 MVP A/B 비교와 병목 위치 확인에는 사용할 수 있다.

## 3. Run01 타임라인과 유효성 판정

### 3.1 실제 타임라인

서버 로그 시각은 다음 순서였다.

| 로그 시각 | 이벤트 | 해석 |
|---|---|---|
| 11:09:08.034 | `ENEMY-NET-PROFILE BEGIN` | Count=7, Combat 시나리오 시작 |
| 11:09:08.034 | `SW-LEVEL-PROFILE Ready` | warm-up 5초, 600프레임 예약 |
| 11:09:13.294 | `CaptureBegin` | 아직 클라이언트와 적이 없는 상태에서 CSV 시작 |
| 11:09:15.957 | connection accepted | 클라이언트 연결 시작 |
| 11:09:16.005 | client connection added | NetConnection 생성 |
| 11:09:17.968 | join request | 플레이어가 월드 참가 요청 |
| 11:09:18.547 | `SPAWNED Requested=7 Spawned=7` | 4 Melee + 3 Ranged 생성 완료 |
| 11:09:21 이후 | Melee attack 로그 | 실제 전투 시작 확인 |
| 11:09:41.482 | `CaptureEnd` | 600프레임 캡처 완료 |
| 11:09:41.483 | Memory/Network summary | 종료 시점 누계 기록 |
| 직후 | 서버 AutoQuit | 클라이언트의 `ConnectionLost` 원인 |

### 3.2 핵심 유효성 문제

`CaptureBegin`이 적 스폰 완료보다 약 **5.25초 빠르다**. CSV 기준으로는 다음과 같다.

- 최초 112프레임: 적 스폰 전
- 이후 488프레임: 적 스폰 후 전투 진단 구간
- 최초 약 58프레임: 클라이언트 연결 자체가 없음
- 그 뒤 구간: 연결 및 초기 replication 진행 중
- 스폰 직전 일부: 플레이어는 참가했지만 적은 없음

따라서 최초 112프레임을 B0 기준값으로 사용할 수 없다. 연결 전, 초기 접속, 플레이어 스폰 및 초기 replication이 섞인 구간이기 때문이다. 반대로 적 스폰 이후 488프레임은 C7의 진단 표본으로는 쓸 수 있지만, 스폰·초기 AI 활성화·asset warm-up이 포함되어 완전한 steady-state는 아니다.

### 3.3 이번 Run의 판정

| 검증 항목 | 판정 | 근거 |
|---|---|---|
| 서버/클라이언트 실행 | PASS | 접속 후 Combat가 정상 동작 |
| 600프레임 CSV 완결 | PASS | 정상 헤더·메타데이터와 600행 존재 |
| `.utrace` 완결 | PASS | Unreal Insights가 세션과 CPU scope를 정상 파싱 |
| AutoQuit 종료 | PASS | CaptureEnd 및 summary 출력 후 종료 |
| 적 7마리 생성 | PASS | `Requested=7 Spawned=7` |
| 의도한 4 Melee + 2 Ranged | FAIL | Count=7 교대 생성은 4 Melee + 3 Ranged |
| 순수 B0 구간 | FAIL | 연결 수립 과정이 섞임 |
| 순수 C7 steady-state | PARTIAL | 488프레임만 스폰 후이고 초기화 구간 포함 |
| 최종 성능 기준 채택 | 보류 | 동일 조건 B0/I7/C7 반복 측정 필요 |

## 4. 주제 1 — 서버 프레임과 CPU

### 4.1 전체 600프레임

| 지표 | 평균 | Median | P95 | P99 | Max |
|---|---:|---:|---:|---:|---:|
| FrameTime | 46.764 ms | 46.759 ms | 48.316 ms | 49.043 ms | 75.965 ms |

### 4.2 적 스폰 후 488프레임

| 지표 | 결과 |
|---|---:|
| 구간 길이 | 22.802초 |
| 평균 FrameTime | 46.726 ms |
| Median | 46.787 ms |
| P95 | 48.296 ms |
| P99 | 48.938 ms |
| Max | 61.243 ms |
| 관측 프레임 빈도 | 약 21.40 Hz |
| 33.33 ms 초과 | 486 / 488프레임 |
| 50 ms 초과 | 2 / 488프레임 |
| 100 ms 초과 | 0프레임 |

겉으로만 보면 약 21 Hz이므로 CPU 병목처럼 보일 수 있다. 그러나 적 스폰 전 혼합 구간도 평균 46.93 ms로 거의 같고, 스폰 후 CSV의 `Exclusive/GameThread/*` 평균 합은 약 **4.05 ms/frame**뿐이다. 즉 현재 근거로는 46.7 ms 대부분을 실제 게임 스레드 계산량으로 설명할 수 없다.

가장 가능성이 높은 해석은 서버의 tick rate 제한, editor/unattended 환경의 pacing, sleep/wait 또는 frame cap이다. 따라서 `FrameTime 46.7 ms = CPU가 46.7 ms 동안 과부하`라고 결론내리면 안 된다.

### 4.3 스폰 전·후 선별 Exclusive 비용 변화

| 구간 | 선별 GameThread Exclusive 합 평균 |
|---|---:|
| 스폰 전 혼합 112프레임 | 1.476 ms/frame |
| 스폰 후 488프레임 | 4.048 ms/frame |
| 차이 | **+2.572 ms/frame** |

이 차이도 순수 적 비용은 아니다. 스폰 전 구간에는 안정적으로 연결된 플레이어가 없으므로, 스폰 후 증가분에는 플레이어 이동 RPC와 네트워크 수신 처리도 포함된다.

### 4.4 증가분 상위 항목

| 항목 | 스폰 후 평균 증가량 | 해석 |
|---|---:|---|
| Animation | +1.094 ms | 현재 가장 큰 적 관련 CPU 후보 |
| Physics | +0.538 ms | 캐릭터·충돌·투사체 활성화 영향 |
| HandleRPC | +0.401 ms | 주로 연결된 플레이어 movement RPC 영향 가능 |
| TickActors | +0.281 ms | 적·컨트롤러·투사체 등 Actor Tick 증가 |
| CharacterMovement | +0.236 ms | 플레이어와 AI 캐릭터 이동 포함 |
| NetworkIncoming | +0.066 ms | 접속 후 수신 처리 증가 |
| BehaviorTreeTick | +0.057 ms | 7 AI의 BT 실행 |
| AIPerception | +0.046 ms | 적 감지 비용 |
| ProjectileMovement | +0.041 ms | 원거리 투사체 이동 비용 |
| FlushNet | +0.038 ms | 송신 flush |
| BehaviorTreeSearch | +0.014 ms | BT search 비용 |
| EQS/EnvQueryManager | +0.009 ms | 이번 표본에서는 매우 작음 |
| ServerReplicateActors | +0.007 ms | 연결 spike를 제외하면 안정적 |
| Pathfinding | +0.006 ms | NavMesh 경로 탐색 비용 |

### 4.5 현재 결론과 다음 검증

- 이번 Run에서 EQS보다 Animation과 Physics의 평균 비용이 훨씬 크다.
- CPU 최적화 전에는 먼저 서버 목표 tick rate를 결정해야 한다. 예를 들어 30 Hz라면 전체 frame budget은 33.33 ms다.
- 다음 캡처에서는 시작 로그에 실제 `GetMaxTickRate`, fixed frame rate 관련 설정, smooth frame rate/cap 값을 출력하는 것이 좋다.
- Timing Insights에서 `FEngineLoop::Tick` 내부의 work와 wait/sleep 구간을 분리해 46.7 ms의 정체를 확인한다.
- B0와 C7에서 동일한 tick rate를 강제로 유지하지 못하면 FrameTime과 bytes/frame 비교가 왜곡된다.

## 5. 주제 2 — EQS, Behavior Tree, Perception, Navigation

### 5.1 스폰 후 488프레임의 AI 비용

| 지표 | 평균 | P95 | P99 | Max | 비고 |
|---|---:|---:|---:|---:|---|
| EnvQueryManager | 0.0098 ms | 0.0902 ms | 0.1380 ms | 0.3599 ms | Median 0.0006 ms |
| AIPerception | 0.0487 ms | 0.0772 ms | 0.1320 ms | 0.2771 ms | 감지 처리 |
| BehaviorTreeSearch | 0.0146 ms | 0.0336 ms | — | 0.0769 ms | 72.75% 프레임에서 non-zero |
| BehaviorTreeTick | 0.0595 ms | 0.1784 ms | 0.4777 ms | 0.9292 ms | 97.54% 프레임에서 non-zero |
| Pathfinding | 0.0060 ms | 0.0257 ms | 0.0372 ms | 0.0600 ms | 30.12% 프레임에서 non-zero |

Perception + EQS + BT Search + BT Tick + Pathfinding 합은 평균 약 **0.139 ms/frame**다. 기존 MVP 임시 게이트인 `E7 Combat EQS+Navigation 평균 0.50 ms 이하`, `P95 1.50 ms 이하`와 비교하면 이번 진단 표본에서는 충분한 여유가 있다.

### 5.2 해석

- **현재 표본만으로는 EQS CPU 과부하 증거가 없다.**
- EQS는 매 프레임 큰 비용을 쓰는 형태가 아니라, 대부분 프레임에서는 거의 0이고 특정 실행 시점에 최대 0.36 ms 정도의 burst를 만든다.
- BT Tick이 EQS보다 크지만 평균 약 0.06 ms로 여전히 작다.
- Pathfinding도 평균 약 0.006 ms이므로 NavMesh 전체가 병목이라는 증거는 없다.
- trace 문자열에서 `EQS_MeleeEnemy`, `EQS_RangedEnemy_CombatPosition`, `EQS_Context_AttackTarget`이 확인되어 원하는 query가 trace에 포함된 것은 확인했다.

다만 현재 CSV는 `EnvQueryManager` 총량만 제공한다. Melee/Ranged query별 호출 수, 평균 응답 시간, 생성 Item 수, Test별 비용을 분리하지 못한다. 따라서 “Ranged EQS가 Melee보다 비싸다” 같은 클래스별 결론은 아직 낼 수 없다.

### 5.3 Timing Insights에서 수동으로 확정할 항목

`C7_Server_Run01.utrace`를 열고 스폰 완료 bookmark부터 CaptureEnd까지 선택한 뒤 다음을 기록한다.

1. Timers에서 `EQS_MeleeEnemy` 검색: Count, Inclusive, Average, Max.
2. `EQS_RangedEnemy_CombatPosition` 검색: 동일 항목.
3. `UEnvQueryManager::Tick`, `EnvQuery`, `EQS_NumInstances` 검색.
4. 같은 시간축에서 `BehaviorTree`, `Navigation`, `Pathfinding` spike가 겹치는지 확인.
5. 첫 스폰 직후 burst와 안정 구간을 분리한다.
6. C7-I와 C7-C의 query Count/sec 및 CPU ms/sec 차이를 계산한다.

Headless UnrealInsights는 trace 자체를 정상 파싱했지만, 현재 설치된 실행 흐름에서는 분석 완료 명령으로 timers CSV가 자동 생성되지 않았다. 이 항목은 수동 Insights 선택 결과를 다음 Run 문서에 추가해야 한다.

### 5.4 다음 단계의 판정 기준

| 검증 | 권장 기준 |
|---|---|
| C7 Combat EQS+Navigation 평균 | 0.50 ms/frame 이하 |
| C7 Combat EQS+Navigation P95 | 1.50 ms/frame 이하 |
| 동시 전투 진입 burst | 3.0 ms/frame 이하, 2프레임 이상 연속 반복 없음 |
| query response | BT 서비스 주기 안에 완료, timeout/abort 누적 없음 |
| 호출 빈도 | 의도한 상태 진입/서비스 주기와 일치, 매 프레임 재요청 금지 |
| C28 scaling | C7 대비 대체로 선형, 초선형 증가 원인 없음 |

## 6. 주제 3 — 네트워크 전송량과 서버 복제 비용

### 6.1 전체 캡처 NetworkSummary

| 항목 | 전체 누계 | 28.056초 기준 |
|---|---:|---:|
| In bytes | 123,115 B | 4,388 B/s |
| Out bytes | 117,537 B | 4,189 B/s, 약 33.5 kbps |
| In packets | 1,344 | 47.9 packets/s |
| Out packets | 520 | 18.5 packets/s |
| In bunches | 2,783 | 99.2 bunches/s |
| Out bunches | 2,657 | 94.7 bunches/s |

이 평균에는 클라이언트가 없던 약 2.7초, 접속 handshake, 플레이어 초기 replication, 적 스폰 초기 replication이 모두 포함된다. 그러므로 **C7 steady-state가 클라이언트당 4.19 KB/s를 쓴다**고 확정해서는 안 된다.

접속 생성부터 CaptureEnd까지 25.477초만 단순 정규화하면 Out은 약 4.61 KB/s지만, 이 값 역시 초기 월드 replication과 스폰 burst를 포함한다. 최종값은 Networking Insights에서 `Spawn End` 이후 안정 구간만 선택해 계산해야 한다.

### 6.2 스폰 후 프레임별 replication 상태

| 지표 | 평균 | Median | P95 | Max |
|---|---:|---:|---:|---:|
| 연결 수 | 1 | 1 | 1 | 1 |
| Active network actors | 24.87 | 25 | 26 | 27 |
| Open channels | 27.89 | 28 | 29 | 30 |
| Ticking channels | 3 | 3 | 3 | 3 |
| In packets/frame | 2.72 | — | 3 | 4 |
| Out packets/frame | 1.00 | — | 1 | 4 |
| ReplicateActor calls/connection/frame | 7.63 | 7 | 12 | 20 |
| ServerReplicateActors CPU | 0.232 ms | 0.224 ms | 0.337 ms | 0.551 ms |
| ReplicateActorTimeMS | 0.169 ms | — | 0.253 ms | 0.417 ms |

서버 replication CPU는 이번 규모에서 낮다. 선별한 네트워크 관련 Exclusive scope인 `NetworkIncoming + HandleRPC + NetworkOutgoing + ServerReplicateActors + FlushNet`을 합치면 약 **0.871 ms/frame**이다. 이것은 네트워크 경로의 근사치이지 엔진 전체 네트워크 비용의 완전한 합은 아니다.

주요 세부 CPU:

| 항목 | 평균 | P95 | P99 | Max |
|---|---:|---:|---:|---:|
| HandleRPC | 0.407 ms | 0.648 ms | 0.773 ms | 0.934 ms |
| CharacterMovementServerMove | 0.311 ms | 0.514 ms | 0.657 ms | 0.824 ms |
| FlushNet | 0.049 ms | 0.078 ms | 0.105 ms | 0.250 ms |

`HandleRPC`와 `CharacterMovementServerMove`가 replication actor scan보다 큰 것은 한 명의 플레이어 이동 입력을 서버가 처리하기 때문이다. 적 복제 최적화와 플레이어 movement RPC 최적화를 같은 항목으로 묶어 해석하면 안 된다.

### 6.3 현재 네트워크 정책과 연결되는 해석

현재 코드의 핵심 정책은 다음과 같다.

| 대상 | 현재 정책 | Run01에서 볼 항목 |
|---|---|---|
| BaseEnemy | AlwaysRelevant=false, 150 m cull, 30/5 Hz | 거리 밖 channel/relevancy 감소, Idle update 감소 |
| BaseWeapon | owner relevancy, movement replication off, 10/1 Hz | 무기가 독립적으로 지속 전송되지 않는지 |
| Projectile | 30/20 Hz, 10초 lifespan | 투사체 동시 개수와 bytes/s burst |
| NetDriver | Adaptive update 활성화 | Idle에서 update rate와 bytes/s 감소 |
| Enemy ASC | Minimal replication | GameplayEffect 상세 데이터의 불필요한 복제 여부 |

Run01 Combat에서는 fully dormant actor가 0이었다. 근접한 7명이 계속 전투 중인 상황에서는 자연스러운 결과다. Dormancy/adaptive update 효과는 Idle 및 거리 밖 시나리오에서 확인해야 한다.

### 6.4 Network Insights에서 다음에 측정할 요소

각 B0/I7/C7 Run에서 안정 구간만 선택하고 다음을 기록한다.

| 분류 | 필수 지표 |
|---|---|
| Connection | 전송/수신 bytes/sec, packets/sec, bunches/sec |
| Enemy class | Melee/Ranged 각각 total bytes, update count, bytes/update |
| 부착 Actor | Weapon total bytes와 update count |
| Projectile | spawn/despawn 수, total bytes, RPC/property 비율 |
| ASC | total bits/bytes, GameplayCue/RPC 수, reliable 비율 |
| Relevancy | active actor, dormant actor, opened/closed channel 수 |
| 안정성 | packet loss, retransmit, reliable backlog, saturation |
| 서버 CPU | ServerReplicateActors 평균/P95, ReplicateActor calls/frame |

최종 적 순비용은 동일 구간의 `C7 - B0`, Idle 전투 전환 비용은 `C7 - I7`으로 계산한다. 로컬 loopback의 0 ms ping과 손실 0%는 전송 안정성 최종 검증을 대신하지 못한다.

## 7. 주제 4 — Actor, Tick, 투사체의 확장성

### 7.1 Actor와 AI 활성 상태

- Combat 구간의 Total Actor count는 평균 약 64.3, 최종 최대 71이었다.
- 캡처 시작 초기 31개에서 최종 71개로 약 40개 증가했다.
- 증가분에는 적 7, AIController, 무기, 투사체 및 전투 지원 Actor가 함께 포함된다.
- CSV Tick 기준으로 Melee 4, Ranged 3, Melee 계열 Controller 4, Ranged Controller 3이 확인됐다.
- BehaviorTreeComponent는 평균 약 3.05개/frame, P95 5, 최대 7개가 tick했다.
- PathFollowingComponent는 평균 약 4.06개/frame, P95 6, 최대 7개가 tick했다.

### 7.2 원거리 투사체

| 지표 | 값 |
|---|---:|
| ProjectileMovement 평균 CPU | 0.0410 ms/frame |
| P95 | 0.0850 ms |
| P99 | 0.2516 ms |
| Max | 1.0318 ms |
| 동시 투사체 평균 | 5.68 |
| Median | 7 |
| P95 / Max | 14 / 14 |
| 투사체가 존재한 프레임 비율 | 54.71% |

3 Ranged에서 최대 14개가 동시에 존재했다. 현재 10초 lifespan이므로 짧은 Run에서는 큰 문제가 없지만, 더 긴 전투에서는 투사체 누적과 lifetime이 bandwidth·collision·Actor channel 수를 증가시킬 가능성이 있다.

다음 Run에서는 다음을 추가로 비교한다.

1. Ranged 수와 발사 주기를 고정한다.
2. `spawned/sec`, `destroyed/sec`, `concurrent P95`, `bytes/projectile`, `CPU/projectile`을 기록한다.
3. 전투 종료 후 10초 이내에 동시 투사체 수와 channel 수가 0 또는 기대값으로 회복되는지 확인한다.
4. 10~30분 soak에서 TotalActorCount가 계속 증가하지 않는지 확인한다.

## 8. 주제 5 — 메모리

| 기준 | 값 |
|---|---:|
| CSV 시작 physical memory | 2,266.78 MB |
| CSV 종료 physical memory | 2,301.48 MB |
| CSV 증가량 | +34.70 MB |
| 서버 로그 process memory delta | +37,830,656 B, 약 36.08 MiB |
| 적 스폰 시점 이후 증가량 | +24.56 MB / 22.80초 |
| 단순 환산 기울기 | 약 64.6 MB/min |

이 기울기를 memory leak으로 판정하면 안 된다. 현재 구간에는 다음이 모두 들어 있다.

- 플레이어 접속 및 초기 replication
- 적·Controller·무기·투사체 생성
- 늦은 asset load
- trace/CSV buffer
- animation 및 ability 초기화

메모리 leak 판정은 warm-up이 끝난 뒤 10~30분 Combat soak에서 해야 한다. 1분 단위 working set, UObject/Actor count, projectile count, NetGUID/channel count를 함께 기록하고, 전투가 안정된 뒤에도 기울기가 지속될 때만 Memory Insights와 allocation callstack을 연다.

절대 약 2.3 GB는 WindowsEditor 전용 서버 수치이므로 패키징된 Server executable의 출시 메모리 예산으로 사용하지 않는다.

## 9. 주제 6 — 로그 오류와 기능 안정성

### 9.1 확인된 경고

| 경고 | 횟수/시점 | 판정 | 후속 조치 |
|---|---|---|---|
| 누락된 mannequin AnimNotify/PSD package LoadErrors | 약 420회 | 중요 | 최종 baseline 전에 asset reference 정리 |
| `State.Attacking` tag가 명시적으로 없는데 제거 시도 | 2회 | 경미한 로직/로그 문제 | ability/tag lifecycle 점검 |
| RecastNavMesh를 찾지 못해 CrowdManager 생성 실패 | CaptureEnd 직후 | teardown 가능성이 큼 | 실제 플레이 중 재발 여부만 추적 |
| 클라이언트 `ConnectionLost` | 서버 AutoQuit 직후 | 정상 | 측정 실패로 집계하지 않음 |

대량 LoadErrors는 실행 직전 asset load와 로그 flush 비용을 불안정하게 만들 수 있고 실제 Notify 기능 누락 가능성도 있다. 최종 반복 측정 전에 해결하는 편이 좋다. `State.Attacking` 경고는 2회뿐이라 성능 병목은 아니지만 상태 tag 불일치가 공격 중복/취소 문제로 이어질 수 있으므로 기능 버그로 따로 수정한다.

Recast/Crowd 경고는 캡처가 끝난 뒤 종료 과정에서 발생했다. 이 한 줄만으로 플레이 중 NavMesh가 없었다고 판단할 수 없다. 실제 Combat 중에는 EQS, navigation, network 관련 오류가 없었고 적 이동·공격이 정상 동작했다.

### 9.2 기능 판정

- 클라이언트는 서버가 안내한 `Level_Jang`으로 정상 이동했다.
- 7명 적의 Combat, Melee attack, Ranged projectile가 확인됐다.
- 이번 Run 중 플레이어 사망 로그는 없었다.
- 서버 종료 전 CaptureEnd와 summary가 존재하므로 우측 상단 종료가 아니라 AutoQuit에 의해 이 Run은 정상 마감됐다.

## 10. 기존 MVP 계획에 따른 단계별 측정 계획

### 단계 0 — 프로파일 하네스 교정

목적은 캡처가 “클라이언트 참가 완료 + 시나리오 준비 완료” 이후에만 시작되게 만드는 것이다.

필수 보완:

1. `SWLevelProfileController`가 단순 WorldTime warm-up만 보고 시작하지 않게 한다.
2. 서버에서 최소 연결 수와 플레이어 Pawn 존재를 확인한다.
3. `EnemyNetProfile Spawn End` 또는 `Count=0 Scenario Ready` 상태를 공유한다.
4. 준비 완료 뒤 안정화 warm-up 5~10초를 추가하고 CSV를 시작한다.
5. CaptureBegin 로그에 `ConnectionCount`, `PlayerCount`, `SpawnedMelee`, `SpawnedRanged`, `ActiveProjectile`, `MaxTickRate`를 출력한다.

이 보완 전의 모든 Run은 연결 순서에 따라 결과가 달라질 수 있다.

### 단계 1 — B0: 연결된 플레이어 기준점

구성: `Melee=0`, `Ranged=0`, 클라이언트 1명, 같은 위치·입력 패턴.

측정 요소:

- FrameTime Median/P95와 active CPU work
- Network Out/In bytes/sec, packets/sec, bunches/sec
- Player movement `HandleRPC`, `ServerMove` 비용
- ActiveNetworkActors, OpenChannels
- Physical memory 안정 구간 기울기

B0는 적 비용을 빼기 위한 기준이다. 이번 Run의 최초 112프레임은 B0로 재사용하지 않는다.

### 단계 2 — I7: 7명 Idle 유지 비용

구성: 플레이어로부터 충분히 멀리 배치하되 NavMesh와 relevancy 조건을 명확히 기록한다.

측정 요소:

- B0 대비 적 Actor/Controller의 상시 Tick 비용
- Adaptive update로 인한 update count/sec와 bytes/sec 감소
- Perception, BT, EQS가 의도 없이 반복 실행되는지
- dormant/active actor 및 channel 변화
- 150 m cull boundary 안/밖에서 channel과 전송량 변화

이 단계가 낮지 않다면 Combat 로직보다 idle tick, BT service 주기, relevancy/update policy부터 수정한다.

### 단계 3 — C7: 7명 Combat 비용

구성: 이번 Run과 같은 4 Melee + 3 Ranged. B0/I7과 동일 seed, 클라이언트 입력, 캡처 시간.

측정 요소:

- `C7 - B0`: encounter 전체의 CPU 및 bandwidth
- `C7 - I7`: Combat 활성화로 증가한 EQS/BT/Perception/attack/replication 비용
- EQS query별 Count/sec, 평균/P95/Max
- Animation, Physics, CharacterMovement, ProjectileMovement
- Enemy/Weapon/Projectile/ASC의 class별 bytes 및 update count
- simultaneous projectile P95와 Actor/channel 회복

각 시나리오는 최소 3회, 가능하면 5회 반복하고 Run median과 P95를 비교한다.

### 단계 4 — 목표 조합 M4R2

사용자가 실제 확인하려는 조합은 4 Melee + 2 Ranged다. 현재 `EnemyNetProfileCount=N`은 인덱스를 교대로 배치하므로 Count=6이면 3+3, Count=7이면 4+3이다. 따라서 현재 옵션만으로 정확한 4+2는 자동 생성할 수 없다.

프로파일 subsystem에 다음 옵션을 추가하는 것이 필요하다.

```text
-EnemyNetProfileMeleeCount=4
-EnemyNetProfileRangedCount=2
```

이후 `M4R2-I`와 `M4R2-C`를 별도로 측정한다. C7은 기존 계획과 확장성 비교를 위한 공통점으로 유지하고, M4R2-C를 실제 encounter 출시 기준으로 삼는다.

### 단계 5 — Adaptive update A/B

I7과 C7/M4R2-C를 각각 아래 두 조건으로 분리 실행한다.

```text
net.UseAdaptiveNetUpdateFrequency 0
net.UseAdaptiveNetUpdateFrequency 1
```

기대 결과는 Idle에서 bytes/sec와 update count/sec가 감소하고, Combat의 hit 판정·이동·projectile 체감이 유지되는 것이다. 동일 프로세스 중 console 값만 바꿔 이어 측정하기보다 별도 실행으로 상태 잔류를 막는다.

### 단계 6 — 네트워크 열화

clean baseline을 만든 뒤에만 100 ms/1% loss와 150 ms/3% loss를 시험한다.

측정 요소:

- reliable backlog와 retransmission
- 적 위치 snap/teleport, 공격 중복, hit 지연
- projectile 시간 이동 또는 이중 hit
- correction/replay 증가
- 손실 없는 baseline 대비 bandwidth와 CPU 증가

열화 Run의 성능 수치는 baseline과 직접 합치지 않고 기능 안정성 표로 분리한다.

### 단계 7 — C28 stress와 scaling

목적은 출시 encounter가 아니라 초선형 증가와 burst를 찾는 것이다.

측정 요소:

- C7 대비 C28의 EQS query count와 CPU 배율
- Perception pair/trace 증가
- Pathfinding 요청 queue와 응답 지연
- ReplicateActor calls 및 bytes/sec 배율
- projectile P95, actor/channel 수, memory slope
- 한 프레임에 동시 전투 진입할 때 P95/P99 burst

대체로 4배 규모에서 비용이 4배 안팎이면 선형이다. 4배를 크게 초과하면 query 중복, perception N² 관계, pathfinding burst, reliable RPC 적체를 조사한다.

### 단계 8 — 장시간 soak와 패키징 전환

Editor MVP가 안정되면 Development Server 패키지를 만들고 동일 B0/I7/M4R2-C를 다시 잰다.

- 10~30분 memory/Actor/projectile/channel slope
- 서버 CPU 평균, P95, P99
- 별도 PC 클라이언트 또는 실제 NIC를 사용한 bandwidth
- 출시 목표 tick rate에서의 budget
- 최소 3회 반복의 run-to-run 편차

Editor 수치와 packaged 수치를 섞지 말고 별도 표로 관리한다.

## 11. 다음 측정에서 사용할 비교표

| Metric | B0 | I7 | C7 | M4R2-C | C28 | 판정 방식 |
|---|---:|---:|---:|---:|---:|---|
| Frame active work 평균/P95 | — | — | 진단 4.05 ms 평균 | — | — | tick pacing과 분리 |
| EQS 평균/P95 | — | — | 0.010 / 0.090 ms | — | — | C-I 차이 |
| BT 평균/P95 | — | — | 약 0.074 / 별도 합산 | — | — | query 주기와 함께 |
| AI 전체 평균 | — | — | 약 0.139 ms | — | — | Perception+EQS+BT+Path |
| ServerReplicateActors 평균/P95 | — | — | 0.232 / 0.337 ms | — | — | B0 차이 |
| Out bytes/sec | — | — | 전체 혼합 4.19 KB/s | — | — | 안정 구간만 재측정 |
| Active network actors | — | — | 평균 24.87 | — | — | Idle/Combat 비교 |
| Projectile concurrent P95 | — | — | 14 | — | — | Ranged 수로 정규화 |
| Physical memory slope | — | — | 미확정 | — | — | warm-up 후 soak |

현재 C7 열의 값은 “Run01 진단값”이며 최종 표준값이 아니다.

## 12. 최종 결론과 우선순위

1. **프로파일 파일 생성과 자동 종료는 정상이다.** trace, CSV, 양쪽 로그가 모두 분석 가능하다.
2. **Run01은 clean C7 baseline이 아니다.** 캡처가 클라이언트 참가와 적 스폰보다 먼저 시작했다.
3. **EQS 과부하 증거는 없다.** 스폰 후 EQS 평균 0.0098 ms, P95 0.0902 ms로 임시 gate보다 매우 낮다.
4. 현재 적 관련 CPU 후보는 EQS보다 **Animation과 Physics**다. 다만 B0/I7 차분이 없어 순수 적 비용 확정은 보류한다.
5. replication CPU는 7명·클라이언트 1명에서 낮지만, 전체 4.19 KB/s는 초기 접속/스폰이 섞인 값이라 class별 최종 bandwidth로 쓸 수 없다.
6. 약 46.7 ms FrameTime은 active work 4.05 ms와 불일치한다. **서버 tick cap/pacing을 먼저 확정**해야 한다.
7. 3 Ranged에서 투사체 P95 14개는 장기 전투에서 추적할 가치가 있다.
8. 최우선 구현은 최적화 코드가 아니라 **ready handshake와 Melee/Ranged 개별 count 옵션**이다.
9. 그 다음 B0 → I7 → C7 → M4R2-C를 동일 조건으로 각 3~5회 측정하고, 그 결과로 adaptive update와 실제 병목 최적화를 A/B 검증한다.

Run01의 올바른 사용법은 “EQS를 당장 최적화해야 한다”는 결론이 아니라, **측정 하네스의 타이밍 오류를 발견했고 현재 규모에서 EQS는 우선순위가 낮다는 근거**로 삼는 것이다.
