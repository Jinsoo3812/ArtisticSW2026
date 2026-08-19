# Ground Enemy Network/EQS MVP 프로파일링

작성일: 2026-08-14  
대상: `MeleeEnemy`, `RangedEnemy`, Unreal Engine 5.7

## 1. 이번 MVP 구현 범위

| 주제 | 구현 파일 | 적용 내용 |
|---|---|---|
| 적 relevancy/update | `Source/Enemy/Private/BaseEnemy.cpp` | `AlwaysRelevant` 제거, 거리 150 m, 30 Hz 상한/5 Hz 하한 |
| Adaptive update | `Config/DefaultEngine.ini` | `net.UseAdaptiveNetUpdateFrequency=1` |
| ASC | 기존 구현 사용 | Enemy ASC의 `Minimal` replication 유지 |
| 부착 무기 | `Source/Enemy/Private/Weapon/BaseWeapon.cpp` | owner relevancy, movement replication 비활성, 10/1 Hz |
| 적 투사체 | `Source/Enemy/Private/RangedEnemy/RangedEnemyProjectile.cpp` | 30/20 Hz, 기존 10초 수명 유지 |
| 사망 Actor 정리 | `BaseEnemy.h/.cpp` | DeathFinished 후 기본 5초 뒤 서버에서 제거 |
| 반복 시나리오 | `EnemyNetworkProfileSubsystem.h/.cpp` | 명령행 전용 E0/E3/E7/E28, Idle/Combat 스폰 |
| 회귀 검사 | `EnemyNetworkPolicyTests.cpp` | CDO 네트워크 정책 자동화 테스트 |

새 `.umap`이나 Blueprint 프로파일 Actor는 만들지 않았다. 기존 `Test_Level`의 PlayerStart와 NavMesh를 그대로 사용하고, `-EnemyNetProfile`이 있을 때만 transient subsystem이 실행된다. 일반 게임 실행에는 스폰 비용이 없다.

## 2. 사전 준비

1. `Test_Level`을 열어 적이 생성될 플레이 영역을 덮는 `NavMeshBoundsVolume`이 있는지 확인한다.
2. 에디터에서 `P`를 눌러 녹색 NavMesh가 Combat 반경 약 7~15 m, Idle 반경 약 40~50 m까지 이어지는지 확인한다.
3. `BP_MeleeEnemy`, `BP_RangedEnemy`가 해당 맵에서 정상 possession되고 플레이어를 인식하는지 1회 PIE로 확인한다.
4. 최종 수치는 PIE가 아니라 별도 서버/클라이언트 프로세스로 측정한다.
5. 서버와 클라이언트를 같은 PC에서 실행했다면 결과에 `single-machine`이라고 기록한다. 최종 bandwidth 기준은 가능하면 별도 PC에서 다시 측정한다.

프로파일 옵션:

| 옵션 | 기본값 | 의미 |
|---|---:|---|
| `-EnemyNetProfile` | 꺼짐 | 프로파일 subsystem 활성화 |
| `-EnemyNetProfileCount=N` | 7 | 0~128개 적 스폰. MVP 표준은 0/3/7/28 |
| `-EnemyNetProfileMode=Idle` | Combat | Idle은 플레이어 약 40 m 밖, Combat은 약 7 m 주변 |
| `-EnemyNetProfileSeed=N` | 100 | 동일 배치 재현용 seed |
| `-EnemyNetProfileSpawnDelay=S` | 3 | 플레이 시작 후 최초 스폰 시도 시각 |

Dedicated Server는 실제 플레이어 Pawn이 접속할 때까지 최대 60초 기다린다. 로그에서 아래 두 줄을 반드시 확인한다.

```text
[ENEMY-NET-PROFILE][BEGIN] ...
[ENEMY-NET-PROFILE][SPAWNED] Requested=7 Spawned=7 ...
```

## 3. 빠른 기능 검증

먼저 PIE `Play As Listen Server`, 플레이어 2명으로 실행한다. Additional Launch Parameters에 다음을 넣는다.

```text
-EnemyNetProfile -EnemyNetProfileCount=7 -EnemyNetProfileMode=Combat -EnemyNetProfileSeed=100
```

검증 항목:

1. 서버에 적 7개가 생성되고 클라이언트에서도 7개가 보인다.
2. 적 이동, 근접 공격, 원거리 공격과 피해가 서버 판정으로 동작한다.
3. 멀리 이동했다가 돌아왔을 때 적이 다시 정상 표시된다. 150 m 경계 주변에서는 실제 월드 동선을 이용해 pop-in 여부를 확인한다.
4. 원거리 투사체가 100 ms RTT에서도 눈에 띄는 순간이동 없이 적중한다.
5. 적을 죽이면 DeathFinished 이후 기본 5초가 지난 뒤 서버와 클라이언트에서 제거된다. 시체 Storage가 별도 Actor로 남는 것은 정상이다.
6. `Mode=Idle`에서는 적이 전투하지 않고, `Mode=Combat`에서는 EQS/BT가 실행되는지 확인한다.

이 단계는 기능 게이트이며 성능 수치로 사용하지 않는다.

## 4. 재현 가능한 서버/클라이언트 실행

PowerShell 두 개를 연다. 경로는 필요하면 설치 위치에 맞게 바꾼다.

### 4.1 서버

```powershell
$UEEditor = "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe"
$ProjectPath = "C:\Users\wonkii\Documents\GitHub\ArtisticSW2026\ArtisticSW2026.uproject"
$TracePath = "C:\Users\wonkii\Documents\GitHub\ArtisticSW2026\Saved\Profiling\Enemy_E7_Combat_Server.utrace"
& $UEEditor $ProjectPath /Game/New/Level/Test_Level -server -log -unattended -NoSound -NullRHI `
  -EnemyNetProfile -EnemyNetProfileCount=7 -EnemyNetProfileMode=Combat -EnemyNetProfileSeed=100 `
  -SWProfileLevel -SWProfileLevelMap=Test_Level -SWProfileLevelWarmup=10 -SWProfileLevelFrames=1800 `
  -trace=cpu,frame,bookmark,net -NetTrace=1 "-tracefile=$TracePath"
```

### 4.2 클라이언트

서버가 열린 뒤 60초 안에 실행한다.

```powershell
$UEEditor = "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe"
$ProjectPath = "C:\Users\wonkii\Documents\GitHub\ArtisticSW2026\ArtisticSW2026.uproject"
$TracePath = "C:\Users\wonkii\Documents\GitHub\ArtisticSW2026\Saved\Profiling\Enemy_E7_Combat_Client1.utrace"
& $UEEditor $ProjectPath 127.0.0.1 -game -log -NoSound -windowed -ResX=1280 -ResY=720 -NoVSync `
  -EnemyNetProfile `
  -SWProfileLevel -SWProfileLevelMap=Test_Level -SWProfileLevelWarmup=10 -SWProfileLevelFrames=1800 `
  -trace=cpu,frame,bookmark,net -NetTrace=1 "-tracefile=$TracePath"
```

`SWLevelProfileController`가 warm-up 뒤 1,800프레임 CSV를 자동 캡처하고 종료 로그에 NetDriver의 In/Out bytes, packets, bunches 합계를 출력한다. CSV는 기본적으로 `Saved/Profiling/CSV` 아래에 저장된다. 서버 자동 종료가 필요하면 `-SWProfileLevelAutoQuit`을 추가한다.

## 5. 네트워크 측정 절차

### 5.1 실험 행렬

각 조건은 같은 seed, map, build, warm-up으로 최소 3회 실행한다.

| ID | Count | Mode | 목적 |
|---|---:|---|---|
| E0 | 0 | Combat | 맵/플레이어/기본 네트워크 비용 |
| E3-I | 3 | Idle | 전투하지 않는 적의 유지 비용 |
| E3-C | 3 | Combat | 소규모 실전 비용 |
| E7-I | 7 | Idle | 한 encounter의 비전투 비용 |
| E7-C | 7 | Combat | MVP 승인 기준 |
| E28-C | 28 | Combat | 선형 증가와 burst를 보는 stress 기준 |

적의 순증 비용은 `E7-C - E0`처럼 계산한다. 절대 수치만 보면 플레이어, 물, Ship 등 맵 공통 트래픽이 섞인다.

### 5.2 Network Insights

1. Unreal Insights에서 서버 `.utrace`를 연다.
2. `Networking Insights` 탭을 연 뒤 서버가 보낸 connection을 선택한다.
3. `EnemyNetProfile Spawn Begin/End`와 `SW Level Profile Measure Begin/End` 북마크 사이만 분석 범위로 잡는다.
4. Object/Replication 필터에서 다음 class 또는 instance 이름을 찾는다.
   - `BP_MeleeEnemy_C`
   - `BP_RangedEnemy_C`
   - `BaseWeapon` 및 파생 무기
   - `RangedEnemyProjectile`
   - `AbilitySystemComponent`
5. 각 class의 replicated object 수, update 수, total bytes, bunch 수, RPC 수를 기록한다.
6. 같은 방식으로 클라이언트 trace의 수신량과 packet 흐름을 확인한다.
7. E0/E3/E7/E28의 `bytes/sec`와 `updates/sec`를 그래프로 비교한다. E28이 E7의 약 4배 근처인지, 특정 class만 초선형으로 증가하는지 확인한다.

서버 로그의 `NetworkSummary`는 빠른 총량 비교용이다. 정확한 “어떤 Actor/Property가 보냈는가”는 Network Insights를 판정 근거로 쓴다.

기록할 값:

```text
Capture seconds:
Server Out bytes/sec, packets/sec, bunches/sec:
Client In bytes/sec, packets/sec:
Melee bytes/update count:
Ranged bytes/update count:
Weapon bytes/update count:
Projectile bytes/update count:
ASC bytes/update count:
Reliable RPC count and retransmit spikes:
```

### 5.3 Adaptive update A/B

MVP 구현 내부에서 Adaptive update의 효과만 분리하려면 서버 콘솔 또는 시작 명령의 `-ExecCmds`로 비교한다.

```text
net.UseAdaptiveNetUpdateFrequency 0
net.UseAdaptiveNetUpdateFrequency 1
```

각 값은 별도 프로세스로 시작해 동일한 E7-I와 E7-C를 3회씩 캡처한다. Idle에서 update 수와 bytes/sec가 줄고, Combat 이동의 품질과 hit 판정이 유지되는 것이 기대 결과다. `AlwaysRelevant`, cull distance, 무기 owner relevancy까지 포함한 전/후 비교는 변경 전 commit과 현재 commit을 각각 같은 명령으로 빌드해야 한다.

### 5.4 나쁜 네트워크 조건

기본 0 ms/0% 결과가 통과한 뒤 클라이언트 콘솔에서 별도 실행한다.

```text
Net PktLag=100
Net PktLagVariance=20
Net PktLoss=1
```

그 다음 150 ms/3% loss도 stress로 확인한다. 판정 항목은 적 이동의 과도한 snap, 투사체 순간이동, 공격 중복, 사망/시체 제거 불일치다. 에뮬레이션 수치는 bandwidth baseline과 섞지 않는다.

## 6. EQS CPU 측정 절차

EQS는 서버에서 실행되므로 서버 trace만 판정한다. 클라이언트 CPU 수치와 합치지 않는다.

### 6.1 현장 확인용 stat

서버 콘솔이 있는 Listen Server 또는 Standalone에서 다음을 켠다.

```text
stat unit
stat AI
stat AI_EQS
stat AIBehaviorTree
stat Navigation
```

`stat AI_EQS`에서 우선 볼 항목:

| 항목 | 의미 |
|---|---|
| `Tick - EQS work` | 해당 프레임에 EQS가 실제 소비한 Game Thread 시간 |
| `Generator Time` | 후보 위치 생성 비용 |
| `Test Time` | distance/pathfinding/trace 등 test 비용 |
| `Execute One Step Time` | time-sliced query step 비용 |
| `Num Instances` | 현재 동시에 실행 중인 query 수. 누적 호출 수가 아님 |
| `Num Items` | 현재 query가 보유한 후보 item 수 |
| `Avg Instance Response Time (ms)` | query 제출부터 완료까지의 평균 지연 |

화면 stat은 패턴을 찾는 용도다. 최종 평균/P95/Max는 Insights 범위 선택 결과로 기록한다.

### 6.2 Timing Insights 정량화

1. 4장의 서버 명령으로 E0, E7-I, E7-C, E28-C trace를 만든다.
2. Unreal Insights의 `Timing Insights`에서 Game Thread를 펼친다.
3. `SW Level Profile Measure Begin/End` 사이를 선택한다. 적 asset load와 최초 perception 등록은 warm-up 밖으로 제외한다.
4. Timers/Stats 검색에서 다음을 찾는다.
   - `UEnvQueryManager::Tick` 또는 `Environment Query/Tick`
   - `Tick - EQS work`
   - `Generator Time`
   - `Test Time`
   - `Execute One Step Time`
   - 동적 query 이름의 `EQS_MeleeEnemy`
   - 동적 query 이름의 `EQS_RangedEnemy_CombatPosition`
5. 선택 구간의 Count, Inclusive/Exclusive average, P95, Max를 기록한다.
6. `E7-C - E7-I`로 전투 때문에 추가된 EQS 비용을 구하고, `E7-C - E0`으로 encounter 전체 AI 비용을 구한다.
7. E28-C의 query Count와 총 시간이 E7-C 대비 어떻게 증가하는지 확인한다. Count가 4배보다 훨씬 크면 BT service/decorator가 반복 query를 과도하게 요청하는지 조사한다.
8. 같은 시간축에서 `Navigation`, `BehaviorTree`, `GameThread` spike를 함께 본다. EQS의 pathfinding test는 Navigation 비용으로도 나타날 수 있으므로 EQS timer만 떼어 결론 내리지 않는다.

### 6.3 호출 횟수 검증과 CPU 검증을 분리

정확히 어떤 query가 언제 요청되는지 확인하는 진단 런에서는 다음처럼 EQS 로그를 켤 수 있다.

```text
log LogEQS Verbose
```

또는 Visual Logger/Gameplay Debugger의 EQS 표시로 개별 AI의 query와 선택 위치를 확인한다. 이 런에서는 60초 동안 각 Enemy가 상태 진입 시 1회인지, service 주기마다 반복되는지 세어 BT 설정 오류를 찾는다.

Verbose logging과 Visual Logger 저장은 CPU/디스크 부하를 추가한다. 따라서 이 결과의 ms 값은 버리고, 로그를 끈 별도 프로세스에서 6.2의 정량 캡처를 수행한다.

### 6.4 MVP 임시 성능 게이트

60 FPS 서버 frame budget 16.67 ms를 기준으로 한 초기값이다. 실제 전용 서버 목표 tick rate가 정해지면 그 budget에 맞춰 갱신한다.

| 조건 | 임시 통과 기준 |
|---|---:|
| E7-C steady-state EQS+Navigation 평균 | 0.50 ms/frame 이하 |
| E7-C steady-state EQS+Navigation P95 | 1.50 ms/frame 이하 |
| 7명이 동시에 전투 진입하는 burst | 3.00 ms/frame 이하, 연속 2프레임 이상 반복 금지 |
| E7-C query response time | BT가 요구하는 재배치 주기보다 짧고 timeout/aborted 폭증 없음 |
| E28-C scaling | E7-C 대비 query count/시간이 대체로 선형, 초선형 원인 없음 |

평균이 통과해도 P95가 실패하면 query 실행 간격에 jitter를 넣거나, 후보 item 수/trace/pathfinding test를 줄이거나, 위치 재사용 시간을 늘리는 순서로 개선한다. `MaxAllowedTestingTime` 같은 전역 EQS time-slice 값은 다른 AI에도 영향을 주므로 MVP 첫 조치로 바꾸지 않는다.

## 7. MVP 승인 순서

1. 자동화 테스트와 에디터 타깃 빌드 통과.
2. PIE 2-player 기능 게이트 통과.
3. E0/E3/E7 Idle/Combat을 각각 3회 측정.
4. E7-C가 네트워크 총량, actor별 bytes/update, EQS 평균/P95 임시 게이트를 통과.
5. 100 ms/1% loss에서 기능 불일치 없음.
6. E28-C로 선형 scaling과 burst를 확인.
7. 실패 시 한 변수만 바꿔 A/B하고, 같은 seed로 3회 재측정.

결과 표에는 Median과 P95를 모두 남긴다. MVP 승인 기준은 “절대 bytes가 작아 보인다”가 아니라 E0 대비 적 순증량, E3→E7 기울기, E7에서의 프레임 P95, 나쁜 네트워크에서의 기능 일관성이다.
