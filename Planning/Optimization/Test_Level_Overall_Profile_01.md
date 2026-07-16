# Test_Level 전체 프로파일 01: EnemyShip 물리 동기화 병목

작성일: 2026-07-16  
대상 엔진: Unreal Engine 5.7  
대상 맵: `/Game/New/Level/Test_Level`  
측정 목적: 요소가 많은 실제 맵에서 PIE Client 프레임 급락의 주 병목을 찾고, 최소 변경 A/B로 원인을 검증한다.

## 1. 결론

현재 `Test_Level`의 첫 번째 위험 요소는 Ripple, Enemy AI, Network Physics 계산량, 패킷 직렬화가 아니다.

가장 큰 병목은 9척의 `BP_EnemyShip` 물리 결과를 Game Thread에 동기화할 때 각 선박의 `BuoyancyRoot`가 overlap을 갱신하는 경로다. 실제 FHD 네트워크 클라이언트에서 루트 9개의 `GenerateOverlapEvents`만 프로파일 전용으로 끄자 다음과 같이 변했다.

| 지표 | 수정 전 | 루트 overlap만 끔 | 변화 |
|---|---:|---:|---:|
| 평균 Frame | 45.58ms | 15.68ms | -65.6% |
| 환산 FPS | 21.9 | 63.8 | +190.6% |
| Frame P95 | 50.34ms | 17.10ms | -66.0% |
| 평균 Game Thread | 45.58ms | 8.77ms | -80.7% |
| 평균 `SyncBodies` | 41.82ms | 5.64ms | -86.5% |
| 평균 GPU | 13.51ms | 14.36ms | +6.3% |
| 평균 Network Incoming CPU | 0.255ms | 0.101ms | 작은 비용 |

GPU 시간이 조금 증가한 것은 그래픽이 무거워졌다는 증거가 아니다. 수정 전에는 Game Thread가 너무 느려 Render/GPU 파이프라인이 자주 기다렸고, 수정 후에는 더 많은 프레임을 실제로 생산하기 때문이다. 병목 제거 후에는 FHD Render Thread/GPU가 새 제한 요소가 되었다.

이 A/B는 아직 게임 에셋을 수정한 것이 아니다. 명령줄 프로파일 옵션으로 런타임에만 overlap을 껐다. 따라서 성능 원인은 강하게 확인됐지만, 아래 기능 검증 없이 Blueprint의 설정을 바로 영구 변경하면 안 된다.

- 선박의 수면 진입 Ripple 발생
- 선박과 WaterBody의 접촉 판정
- 선박 대 선박/월드 충돌
- 공격·탑승·상호작용 overlap
- Network Physics 예측/재현

## 2. 테스트 맵 구성

에디터 월드에서 확인한 주요 구성은 다음과 같다.

| 객체 | 수량 |
|---|---:|
| `BP_EnemyShip_C` | 9 |
| `BP_Cannon_C` | 19 |
| `BP_TestShip_SingleMesh_C` | 1 |
| 전체 에디터 Actor | 50 |

각 EnemyShip에는 다음과 같은 구성 요소가 함께 존재한다.

- 물리 루트 `BuoyancyRoot` (`StaticMeshComponent`)
- 신규 `SWBuoyancyComponent`
- 엔진 기본 `BuoyancyComponent`
- `NetworkPhysicsComponent`
- Camera, SpringArm
- Interactable 3개
- Widget, CameraProxy, DrawFrustum
- ASC, Health

EnemyShip 한 척당 6개의 Primitive가 overlap을 켜고 있었고, 9척에서 총 54개였다. 그러나 54개 전체를 끈 결과와 `BuoyancyRoot` 9개만 끈 결과가 거의 같았다. 따라서 Interactable 등의 overlap은 이번 급락의 원인이 아니다.

## 3. 측정 방법

### 3.1 실행 구조

PIE 체감 문제를 재현하되 Editor 자체의 Slate·에디터 월드 비용을 분리하기 위해 다음 구조를 사용했다.

- 서버: 별도 `UnrealEditor-Cmd.exe`, Dedicated Server, NullRHI
- 클라이언트: 별도 `UnrealEditor.exe -game`
- 클라이언트 실제 Viewport: 1920×1080
- 맵: `Test_Level`
- Warm-up: 5초
- CSV capture: 600프레임
- 통계 구간: 시작 120프레임 제외 후 480프레임
- 네트워크 에뮬레이션: 없음
- 서버/클라이언트: 같은 PC에서 실행

같은 PC에서 서버와 클라이언트를 함께 실행했으므로 최종 제품 하드웨어 수치로 보아서는 안 된다. 하지만 전/후 프로세스 구조와 조건이 같아 현재 병목의 인과관계를 검증하기에는 충분하다.

### 3.2 해상도 고정에서 발견한 함정

처음에는 `-ResX=1920 -ResY=1080`만 사용했으나 프로젝트 설정의 `r.SetRes=1280x720`이 이를 다시 덮었다. 따라서 이름에 `FHD`가 들어간 일부 초기 파일도 실제 Viewport는 1280×720이었다.

최종 비교에서는 다음 옵션을 사용했고, 캡처 시작 로그의 `Viewport=1920x1080`도 확인했다.

```text
-windowed -ForceRes -ResX=1920 -ResY=1080
```

파일 이름이나 CSV 초기 metadata만 믿지 말고 실제 캡처 시작 시 GameViewport 크기를 기록해야 한다.

### 3.3 프로파일 전용 제어기

다음 파일에 일반 맵 프로파일 제어기를 추가했다.

- `Source/ClassFeature/Public/Profiling/SWLevelProfileController.h`
- `Source/ClassFeature/Private/Profiling/SWLevelProfileController.cpp`
- `Source/ClassFeature/Private/RippleSubsystem.cpp`

`-SWProfileLevel`이 없으면 생성되지 않으므로 일반 플레이에는 영향을 주지 않는다.

지원 옵션:

```text
-SWProfileLevel
-SWProfileLevelMap=Test_Level
-SWProfileLevelWarmup=5
-SWProfileLevelFrames=600
-SWProfileLevelAutoQuit
-SWProfileEnemyShipLimit=N
-SWProfileDisableEnemyOverlaps
-SWProfileDisableEnemyRootOverlaps
```

제어기는 다음 항목을 자동 기록한다.

- 실제 캡처 시작 Viewport
- CSV 600프레임 시작/종료
- 시작/종료 프로세스 물리 메모리
- NetDriver bytes, packets, bunches 증분
- A/B에서 찾은 EnemyShip 수와 overlap을 끈 Component 수

## 4. 실제 FHD 네트워크 A/B

### 4.1 CPU·GPU·프레임

| 지표 | Baseline | Root overlap OFF |
|---|---:|---:|
| Frame 평균 | 45.581ms | 15.684ms |
| Frame 중앙값 | 44.945ms | 16.083ms |
| Frame P95 | 50.340ms | 17.102ms |
| Frame P99 | 55.969ms | 17.482ms |
| Game Thread 평균 | 45.578ms | 8.774ms |
| Render Thread 평균 | 6.305ms | 15.679ms |
| RHI Thread 평균 | 3.402ms | 7.692ms |
| GPU 평균 | 13.509ms | 14.360ms |
| `SyncBodies` 평균 | 41.820ms | 5.642ms |
| `TickActors` 평균 | 0.796ms | 0.783ms |
| Game Thread Physics 평균 | 0.251ms | 0.148ms |
| Network Incoming 평균 | 0.255ms | 0.101ms |

Baseline에서는 Frame과 Game Thread가 거의 같고 `SyncBodies` 하나가 Game Thread의 약 91.8%를 차지한다. AI Tick, Actor Tick, Network Incoming은 각각 1ms 미만이다.

루트 overlap을 끈 뒤에는 Frame이 Render Thread/GPU에 가까워진다. 즉 병목의 중심이 Game Thread 물리 동기화에서 렌더 파이프라인으로 이동했다.

### 4.2 왜 `SyncBodies`가 비쌌는가

엔진 Water Plugin의 `UBuoyancyComponent::SetupWaterBodyOverlaps()`는 Simulating Component에 다음 설정을 강제한다.

```cpp
SimulatingComponent->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap);
SimulatingComponent->SetGenerateOverlapEvents(true);
```

확인 위치:

- `Engine/Plugins/Experimental/Water/Source/Runtime/Private/BuoyancyComponent.cpp:261`

한편 Chaos 결과가 Game Thread로 동기화될 때 `FPhysScene_Chaos::OnSyncBodies()`는 변경된 Component에 `MoveComponent()`를 호출한다.

확인 위치:

- `Engine/Source/Runtime/Engine/Private/PhysicsEngine/Experimental/PhysScene_Chaos.cpp:2337`
- 같은 파일의 `MoveComponent()` 호출: 약 2546행

이 두 경로가 결합되면 다음 일이 반복된다.

```text
Chaos 물리 결과
  → Game Thread SyncBodies
  → BuoyancyRoot MoveComponent
  → UpdateOverlaps
  → WaterBody/WorldStatic overlap 후보 갱신
  → 9척에서 매 프레임 반복
```

Unreal Insights의 UObject timer 추출에서도 같은 방향을 보였다.

| Timer | 추적 구간 누적 |
|---|---:|
| `USceneComponent::UpdateOverlaps` | 27.990초 |
| `BuoyancyRoot` inclusive | 28.213초 |
| `BuoyancyRoot` exclusive | 27.349초 |
| `NetworkPhysicsComponent` object scope | 0.007초 |

이 누적값은 CSV 480프레임 구간이 아니라 trace 전체 구간이므로 프레임 평균으로 직접 나누면 안 된다. 다만 `BuoyancyRoot → UpdateOverlaps`가 대부분을 차지한다는 구조 증거이고, 루트 overlap만 끄는 최소 A/B의 86.5% `SyncBodies` 감소와 일치한다.

`NetworkPhysicsComponent` object scope가 작다고 해서 비동기 Chaos 및 예측 비용 전체가 0.007초라는 뜻은 아니다. 다만 현재 40ms 급락이 Network Physics callback 계산 자체가 아니라 Game Thread로 결과를 반영하는 overlap 부작용에서 발생한다는 결론은 충분히 지지한다.

### 4.3 AI는 현재 주 병목이 아니다

Standalone capture에서 9개 Behavior Tree의 평균 Game Thread 비용은 약 0.13~0.14ms였고, `TickActors` 전체도 약 0.8~0.9ms였다. 따라서 “EnemyShip 9개가 있으니 AI가 무겁다”는 가설은 이번 데이터에서 기각된다.

다만 이는 현재 단순 Task와 9척 기준이다. 항로 탐색, EQS, Perception 자극, 전투 판단이 늘어나면 별도의 scaling 검사가 필요하다.

## 5. 해상도가 FHD보다 작아도 괜찮은가

결론은 “검사 대상에 따라 다르다.” 해상도 비용은 단순 비례라고 가정하면 안 된다.

1280×720은 약 92만 픽셀이고 1920×1080은 약 207만 픽셀이므로 픽셀 수는 2.25배다. 그러나 전체 프레임 비용에는 픽셀 수와 무관한 CPU, draw submission, physics, network, fixed pass 비용이 함께 있다.

루트 overlap을 끈 Standalone 비교는 다음과 같다.

| 실제 Viewport | Frame 평균 | Game Thread | GPU | `SyncBodies` |
|---|---:|---:|---:|---:|
| 1280×720 | 13.60ms | 8.99ms | 12.35ms | 5.47ms |
| 1920×1080 | 16.23ms | 9.30ms | 14.78ms | 5.65ms |
| 변화 | +19.4% | +3.4% | +19.6% | +3.2% |

픽셀 수가 2.25배여도 GPU 시간이 2.25배가 되지 않은 이유는 모든 GPU pass가 픽셀 수에 완전히 비례하지 않고, 프레임 파이프라인에 고정 비용과 대기 시간이 있기 때문이다.

실무 판단:

- CPU Physics, `SyncBodies`, AI, 직렬화 위치를 찾는 초기 검사에는 작은 Viewport도 유효하다.
- Render, Water material, post process, translucency, GPU memory, 최종 FPS 결정에는 목표 해상도가 필수다.
- 현재 병목 수정 전에는 CPU 45ms가 지배하므로 해상도를 낮춰도 체감 FPS가 크게 회복되지 않는다.
- 루트 overlap을 제거한 뒤에는 GPU 14~15ms가 가까워지므로 FHD 검사가 중요해진다.
- PIE 창 크기는 매번 달라질 수 있으므로 비교 실험에는 `-ForceRes`와 실제 Viewport 로그를 사용한다.

## 6. 메모리와 캐시 관찰

| 지표 | Baseline | Root overlap OFF | 해석 |
|---|---:|---:|---|
| Physical Used 평균 | 3387.5MB | 3515.7MB | 별도 실행 간 변동이 커 직접 전후 효과로 해석 금지 |
| GPU Local Used 평균 | 3595.0MB | 3571.2MB | 큰 구조 변화 없음 |
| Capture 중 Physical delta | -69.9MB | -21.4MB | 두 실행 모두 단조 증가/누수 신호 없음 |

이번 600프레임 검사는 누수나 캐시 포화 검사가 아니다. 프로세스 시작 시 asset residency, DDC, OS page cache, cannonball 수가 달라 평균 Physical Used가 흔들릴 수 있다.

다음 메모리 검사는 별도 시나리오로 진행해야 한다.

- 최소 10~30분 soak
- 동일 카메라와 동일 Actor 수
- Ripple 생성/만료 반복
- `memreport -full` 시작/종료 diff
- Memory Insights allocation tag와 callstack
- 증가 후 회수되는 transient와 계속 남는 retained allocation 분리

CPU cache hit/miss는 CSV만으로 결론 낼 수 없다. 이번 병목은 overlap 후보와 Component 이동에 따른 포인터 추적·broadphase/overlap 갱신이 많아 cache locality도 좋지 않을 가능성이 있지만, 이는 하드웨어 카운터 또는 VTune/WPA 등으로 별도 검증해야 한다.

## 7. 네트워크·직렬화·패킷 관찰

600프레임 capture는 전후의 실제 시간이 다르다. 그러므로 총 bytes를 그대로 비교하면 안 되고 초당 값으로 정규화해야 한다.

| 클라이언트 NetDriver | Baseline 약 27.32초 | Root OFF 약 9.61초 |
|---|---:|---:|
| In bytes 총합 | 1,305,947 | 452,743 |
| In bandwidth | 약 47.8KB/s | 약 47.1KB/s |
| Out bytes 총합 | 54,012 | 38,012 |
| Out bandwidth | 약 2.0KB/s | 약 4.0KB/s |
| In packets/sec | 약 62.7 | 약 62.1 |
| Out packets/sec | 약 22.0 | 약 45.7 |
| In bunches/sec | 약 383.6 | 약 374.8 |
| Out bunches/sec | 약 51.5 | 약 80.4 |

수신 대역폭과 패킷 빈도는 거의 같다. 수정 후 Out rate가 증가한 것은 클라이언트가 더 높은 프레임/입력 갱신 빈도로 실행된 영향일 가능성이 있다. 정확한 필드별 원인은 Network Insights에서 bunch/RPC/property 단위로 확인해야 한다.

현재 결론:

- 약 47KB/s 수신과 평균 0.1~0.25ms `NetworkIncoming`은 이번 프레임 급락의 원인이 아니다.
- 지금 패킷 압축을 도입해도 30ms급 프레임 개선은 나오지 않는다.
- 압축은 CPU 비용을 추가하므로 client 수, relevancy 범위, Replicated Physics state가 늘어난 뒤 bytes/sec와 saturation 근거로 결정한다.
- 이번 테스트는 packet loss 0%, local loopback이다. rollback/history 비용 검사는 RTT와 loss 시나리오가 별도로 필요하다.

## 8. 영구 수정 후보

### 8.1 권장 방향

SWBuoyancy를 사용하는 Ship Blueprint에서는 엔진 기본 `BuoyancyComponent`를 제거하고, 물리 루트의 WaterBody overlap 의존성을 없애는 방향이 가장 자연스럽다.

근거:

- 신규 부력 계산은 이미 `SWBuoyancyComponent`가 담당한다.
- 엔진 기본 Buoyancy가 남아 있으면 물리 루트에 overlap을 다시 강제할 수 있다.
- 루트 overlap 9개만 제거해도 전체 54개를 제거한 것과 성능이 거의 같다.
- Interactable collision/overlap은 그대로 유지할 수 있다.

### 8.2 Ripple 기능과의 충돌

현재 서버 Ripple 감지가 WaterBody의 `OnActorBeginOverlap`에 의존한다면 루트 overlap을 끄는 순간 선박 수면 진입 Ripple이 사라질 수 있다. 따라서 성능 수정은 단순 체크 해제로 끝내지 말고 Ripple 진입 감지 경로도 함께 바꿔야 한다.

권장 구조:

```text
SWBuoyancy 서버 계산
  → 이전 frame과 현재 frame의 submerged/contact 상태 비교
  → 수면 진입 edge + 하강 속도/질량/크기 평가
  → RippleSubsystem에 인증된 Ripple event 제출
```

이 방식은 이미 계산한 수면 query와 pontoon/contact 정보를 재사용하므로, 큰 물리 mesh 전체에 WaterBody overlap을 켜는 것보다 목적이 명확하고 비용 예측이 쉽다.

### 8.3 영구 수정 전 체크리스트

- [ ] `BP_EnemyShip` 및 모든 AShip 상속 BP에서 엔진 기본 `BuoyancyComponent` 사용 여부 확인
- [ ] SWBuoyancy와 엔진 Buoyancy의 중복 실행 여부 확인
- [ ] 엔진 Buoyancy 제거 후 pontoon 설정 보존 확인
- [ ] `BuoyancyRoot.GenerateOverlapEvents=false` 적용
- [ ] World/Ship blocking collision은 유지
- [ ] Interactable 3개의 overlap은 유지
- [ ] 서버 Ripple 감지를 SWBuoyancy contact transition으로 이전
- [ ] 선박 수면 진입 Ripple 서버/클라이언트 렌더 검증
- [ ] 선박 충돌·피격·탑승·상호작용 회귀 테스트
- [ ] Dedicated Server + FHD Client에서 동일 A/B 재측정

## 9. 다음 최적화 검사 후보

이번 원인을 영구 수정하고 회귀 검증한 뒤의 다음 1순위는 FHD Render/GPU 분해다.

이유:

- Root overlap OFF에서 Game Thread는 8.77ms지만 Render Thread는 15.68ms, GPU는 14.36ms다.
- 이제 60 FPS budget 16.67ms에 가장 가까운 부분이 렌더다.
- Water만의 CPU timer는 작지만 물 material, translucency, reflection, shadow, post process의 GPU 비용은 별도 GPU pass로 보아야 한다.

권장 순서:

1. 같은 FHD 카메라에서 `ProfileGPU`, GPU Visualizer, Insights GPU track 수집
2. Water coverage가 낮은 카메라와 높은 카메라 A/B
3. Shadow, translucency, reflection/Lumen, post process, volumetric 순으로 pass 비용 분해
4. 9개 EnemyShip의 mesh/widget/health bar 가시성 A/B
5. Screen percentage 50/75/100 scaling으로 pixel-bound 여부 판정

그 다음 후보는 EnemyShip의 비활성 Component tick 정리다.

현재 매 프레임 관측된 tick 수:

- CameraComponent 30
- SpringArmComponent 11
- WidgetComponent 9
- NetworkPhysicsComponent 10
- EnemyShip 9
- Cannon 19

현재 `TickActors` 전체가 1ms 미만이므로 이것은 즉시 대수술할 대상은 아니다. 그러나 선박 수가 9척에서 50척 이상으로 늘어날 때 Camera/SpringArm/Widget이 선형 증가할 가능성이 높다. 다음과 같은 scaling sweep이 적절하다.

```text
EnemyShip 0 / 3 / 6 / 9
각 단계에서 Camera·SpringArm·Widget 활성/비활성 A/B
평균, P95, per-ship 기울기(ms/ship) 기록
```

그 이후 후보:

- Cannonball 20~50개 구간의 pooling, relevancy, ProjectileMovement scaling
- RTT/loss 조건에서 Network Physics rollback/history 비용
- Ripple 장시간 생성/만료의 allocation과 cache locality
- 넓은 레벨에서 relevancy/dormancy와 RepGraph 필요성

패킷 압축과 custom serialization은 현재 1순위가 아니다. 먼저 Actor/Component relevancy로 보내지 않아도 되는 데이터를 줄이고, 그래도 bandwidth가 목표 예산을 넘을 때 검토한다.

## 10. 원시 데이터와 재현 자료

주요 결과:

- `Saved/Profiling/TestLevel/TestLevel_ActualFHD_Client_Baseline.csv`
- `Saved/Profiling/TestLevel/TestLevel_ActualFHD_Client_NoEnemyRootOverlaps.csv`
- `Saved/Profiling/TestLevel/TestLevel_ActualFHD_NoEnemyRootOverlaps.csv`
- `Saved/Profiling/TestLevel/TestLevel_Client_SyncBodies_Timers.csv`
- `Saved/Profiling/TestLevel/TestLevel_FHD_Client_v2.utrace`

주요 로그:

- `Saved/Logs/TestLevel_Profile_ActualFHD_Baseline_Client.log`
- `Saved/Logs/TestLevel_Profile_ActualFHD_Baseline_Server.log`
- `Saved/Logs/TestLevel_Profile_ActualFHD_RootOnly_Client.log`
- `Saved/Logs/TestLevel_Profile_ActualFHD_RootOnly_Server.log`

초기 1280×720 결과는 파일 이름에 `FHD`가 포함된 경우가 있으므로 실제 Viewport 확인 없이 해상도 비교에 사용하지 않는다. 이 문서의 핵심 FHD 네트워크 A/B는 로그에서 `Viewport=1920x1080`을 확인한 두 실행만 사용했다.

## 11. 이번 단계 완료 상태

- [x] Test_Level Actor 및 EnemyShip Component 구성 확인
- [x] Standalone 전체 CPU/GPU baseline
- [x] 별도 서버 + 클라이언트 네트워크 baseline
- [x] 실제 1920×1080 Viewport 검증
- [x] CSV 600프레임 및 P50/P95/P99 분석
- [x] Unreal Insights로 `SyncBodies → BuoyancyRoot → UpdateOverlaps` 확인
- [x] Enemy primitive overlap 전체 OFF A/B
- [x] Enemy root overlap만 OFF 최소 A/B
- [x] Physical/GPU memory snapshot
- [x] NetDriver bytes/packets/bunches 정규화
- [x] 프로파일 전용 옵션이 일반 플레이에 영향을 주지 않도록 격리
- [ ] Blueprint 영구 수정
- [ ] Ripple 진입 감지 경로 이전
- [ ] 기능 회귀 테스트
- [ ] 수정 후 최종 FHD/PIE 재측정
- [ ] 다음 단계 GPU pass 분해
