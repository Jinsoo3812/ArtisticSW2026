# Render Thread / GPU Profile 01

작성일: 2026-07-16  
상태: 1차 병목 분석, 후보 A/B, 디버그 렌더 최적화 및 재검증 완료

## 1. 목적

Ship Overlap 병목 제거 후 `Test_Level`은 Game Thread 제한에서 Render/GPU 제한으로 이동했다. 이번 문서는 다음 순서를 따른다.

1. 실제 FHD 별도 Client에서 Render/GPU baseline 수집
2. `ProfileGPU` 이벤트 트리로 pass별 비용 분해
3. 가장 비싼 후보를 격리 A/B
4. 품질 손실과 성능 이득 비교
5. 실제 불필요 렌더 경로 수정
6. 같은 조건에서 전후 비교

## 2. Render Thread, RHI Thread, GPU의 차이

### Render Thread

Game Thread가 만든 Scene 상태를 받아 어떤 오브젝트를 어떤 pass로 그릴지 준비한다. Visibility, shadow setup, mesh draw command, RDG graph 구성 등이 여기에 포함된다. CPU 작업이다.

### RHI Thread

Render Thread의 고수준 명령을 D3D12/Vulkan 같은 그래픽 API 명령으로 변환하고 제출한다. 역시 CPU 작업이며 GPU와의 동기화나 command submission 비용을 포함한다.

### GPU

제출된 Shadow, BasePass, Lumen, Water, Post Process 등을 실제로 실행한다. GPU 시간이 10ms이면 이상적으로 약 100FPS가 상한이고, 14ms이면 약 71FPS가 상한이다. CPU와 GPU는 겹쳐 실행되므로 단순 합산하지 않고 가장 늦게 끝나는 축이 프레임을 제한한다.

이번 baseline은 Frame/Render Thread 약 15ms, GPU 약 13.74ms, Game Thread 약 5.3ms였다. 따라서 렌더 제출과 GPU가 최종 프레임을 제한했다.

## 3. 측정 조건

- Map: `/Game/New/Level/Test_Level`
- Enemy Ship: 9척
- Dedicated Server: NullRHI
- Client: 별도 프로세스, D3D12, AMD Radeon RX 6700 XT
- Viewport: 실제 1920×1080
- TSR 내부 해상도: 1400×788 → 1920×1080
- Epic 계열 Scalability, Lumen 및 Virtual Shadow Map 활성
- Warmup 5초, CSV 600프레임
- 통계는 선두 120프레임을 제외한 480프레임
- 단일 프레임 pass 분해는 `ProfileGPU`, 장기 평균은 CSV Profiler 사용

프로파일 컨트롤러에 `-SWProfileGPU`를 추가하여 측정 시작 시 UI/Screenshot 없이 정렬된 `ProfileGPU` 로그를 자동 수집한다.

## 4. Baseline GPU 이벤트 트리

단일 GPU 캡처의 주요 이벤트는 다음과 같다.

### Graphics Queue

| 이벤트 | Inclusive |
|---|---:|
| Graphics Queue Frame | 13.868ms |
| ShadowDepths | 5.006ms |
| PostProcessing | 2.681ms |
| Temporal Super Resolution | 1.226ms |
| CompositeDebugPrimitives | 1.037ms |
| VolumetricCloud | 1.503ms |
| RenderDeferredLighting | 0.862ms |
| BasePass | 0.726ms |
| SingleLayerWater | 0.639ms |
| Nanite VisBuffer | 0.509ms |

### Async Compute Queue

| 이벤트 | Inclusive |
|---|---:|
| Compute Queue Frame | 4.297ms |
| DiffuseIndirectAndAO | 2.774ms |
| LumenScreenProbeGather | 2.262ms |
| LumenSceneLighting | 1.038ms |
| Radiosity | 0.744ms |
| TranslucencyVolumeLighting | 0.509ms |

핵심 해석:

- 물의 `SingleLayerWater`는 0.639ms로 의미 있는 비용이지만 1순위 병목은 아니다.
- Graphics의 가장 큰 실부하는 ShadowDepths다.
- `CompositeDebugPrimitives`가 1ms를 넘는 것은 실제 게임 외형이 아니라 개발용 그리기가 프레임마다 남아 있다는 신호다.
- Compute의 Lumen은 Graphics와 일부 겹쳐 실행되므로 Graphics와 단순 합산하지 않는다.

## 5. 후보 A/B: Enemy Ship 그림자 제거

`-SWProfileDisableEnemyShipShadows`를 프로파일 전용 옵션으로 추가하고 9개 `ShipVisualMesh`의 Cast Shadow만 런타임에 껐다.

| 장기 평균 | Baseline | 적 선박 그림자 OFF | 변화 |
|---|---:|---:|---:|
| Frame | 15.031ms | 14.818ms | -1.4% |
| GPU | 13.739ms | 13.519ms | -1.6% |
| Draw Calls | 273.6 | 271.7 | -0.7% |

단일 `ProfileGPU`에서는 다음과 같았다.

- Graphics Queue: 13.868→13.350ms
- ShadowDepths: 5.006→4.624ms

성능 이득은 존재하지만 9척의 동적 그림자를 전부 잃는 대가에 비해 작다. 따라서 선박 그림자 전체 OFF는 최종 구현으로 채택하지 않았다. 이 스위치는 앞으로 거리별 Shadow 정책이나 LOD/Nanite 후보를 격리할 때 사용할 수 있도록 프로파일 전용으로 유지한다.

## 6. 채택한 최적화: 무조건 실행되던 Debug Render 제거

### 6.1 발견된 원인

`AShip::Tick()`은 Development Client에서 모든 Ship의 네 Pontoon을 녹색 Debug Sphere로 매 프레임 그렸다. Simulated Proxy에서는 서버 복제 위치의 네 Pontoon을 빨간색으로 추가했다.

`Test_Level`의 선박 10척 기준으로 최대 80개의 Debug Sphere가 프레임마다 Line Batch에 들어간다. 이 코드는 Debug 플래그나 CVar 없이 무조건 실행되고 있었다.

또한 `UBTTask_NavalDrive`는 9척의 상태 문자열을 매 프레임 `AddOnScreenDebugMessage`로 생성했다. 실제 게임 기능이 아닌 진단 출력이므로 기본 실행 경로에 둘 필요가 없다.

### 6.2 변경

- `p.ShowShipNetworkBuoyancyDebug`
  - 기본값 0
  - 1일 때만 Local/Replicated Pontoon Debug Sphere를 그림
- `p.ShowNavalAIDebug`
  - 기본값 0
  - 1일 때만 Naval AI 화면 메시지와 범위 진단을 허용
- `bShowDebugRanges`의 C++ 기본값도 false로 변경

디버그 기능은 삭제하지 않았으며 필요할 때 콘솔에서 명시적으로 켤 수 있다.

## 7. 최적화 전후 결과

| 지표 | Baseline | Debug Gate 적용 | 변화 |
|---|---:|---:|---:|
| Frame 평균 | 15.031ms | 14.321ms | -4.7% |
| 환산 평균 FPS | 66.53 | 69.83 | +5.0% |
| Frame P95 | 15.904ms | 15.137ms | -4.8% |
| Game Thread | 5.305ms | 4.656ms | -12.2% |
| Render Thread | 15.025ms | 14.314ms | -4.7% |
| GPU | 13.739ms | 12.988ms | -5.5% |
| CPU Process 사용률 | 9.642% | 8.607% | -10.7% |
| Draw Calls | 273.6 | 269.0 | -1.7% |
| Primitives Drawn | 478,808 | 438,989 | -8.3% |
| GPU Local Memory | 3573.2MB | 3490.7MB | -2.3% |

단일 GPU 이벤트 트리에서 `CompositeDebugPrimitives` 1.037ms 항목이 최적화 후 목록에서 사라졌다. 이는 동적 gameplay actor 수 차이가 존재하는 장기 평균 비교와 별개로, 수정한 디버그 primitive 경로가 제거되었다는 직접 증거다.

Baseline의 Cannonball 평균은 21.69개, 최적화 후는 19.96개였으므로 동적 gameplay 부하가 완전히 동일하지는 않다. 따라서 작은 차이를 과대 해석하지 않는다. 다만 다음 증거가 같은 방향으로 일치한다.

- `CompositeDebugPrimitives` 이벤트 소멸
- Primitives Drawn 8.3% 감소
- GPU 평균 5.5% 감소
- Game/Render/CPU 사용률 동시 감소

## 8. 현재 병목과 다음 후보

최적화 후 GPU 단일 프레임은 Graphics 13.32ms였고 ShadowDepths는 여전히 약 5.29ms로 가장 크다. 현재 69~70FPS 수준에서 FHD 100FPS를 목표로 하려면 최종 프레임을 10ms 아래로 낮춰야 하므로 약 4.3ms를 추가로 줄여야 한다.

다음 순서는 품질 손실을 통제하면서 진행한다.

1. 선박 전체 Shadow OFF가 아니라 거리별/LOD별 Shadow 정책 A/B
2. `SM_TestShip` Nanite 여부 및 VSM Non-Nanite pass 기여 측정
3. Volumetric Cloud 1.4~1.5ms의 Scalability 단계 비교
4. TSR 내부 해상도와 품질 단계 비교
5. Lumen GI/Reflection 품질 계층 비교
6. 물은 현재 0.6ms 수준이므로 위 후보 이후 재검토

Render Thread의 `EventWait`는 GPU/RHI를 기다리는 시간이 섞여 있으므로 그 자체를 CPU 함수 최적화 대상으로 보지 않는다. 실제 CPU 제출 비용과 대기 시간을 Unreal Insights의 Render/RHI/GPU track으로 분리해야 한다.

## 9. 원시 자료

- `Saved/Profiling/TestLevel/TestLevel_RenderBaseline_FHD_Client.csv`
- `Saved/Profiling/TestLevel/TestLevel_RenderNoEnemyShadows_FHD_Client.csv`
- `Saved/Profiling/TestLevel/TestLevel_RenderDebugGated_FHD_Client.csv`
- `Saved/Logs/TestLevel_RenderBaseline_FHD_Client.log`
- `Saved/Logs/TestLevel_RenderNoEnemyShadows_FHD_Client.log`
- `Saved/Logs/TestLevel_RenderDebugGated_FHD_Client.log`

## 10. 체크리스트

- [x] 실제 FHD 별도 Client baseline
- [x] ProfileGPU Graphics/Compute 이벤트 트리 수집
- [x] Shadow 후보 격리 A/B
- [x] 품질 손실 대비 이득이 작은 전체 Shadow OFF 기각
- [x] 무조건 실행되던 Pontoon Debug Render 확인
- [x] Ship Debug Sphere 기본 OFF CVar 적용
- [x] Naval AI Debug UI 기본 OFF CVar 적용
- [x] Editor build 성공
- [x] 최적화 후 FHD CSV 및 ProfileGPU 재수집
- [x] 전후 통계 및 제한 기록
- [ ] Shipping/Development Game 빌드에서 재검증
- [ ] Shadow 거리/LOD/Nanite 2차 프로파일

