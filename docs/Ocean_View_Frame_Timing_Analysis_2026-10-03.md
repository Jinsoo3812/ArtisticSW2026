# 바다 시점 프레임 시간 분석 (2026-10-03)

사용자 제공 스크린샷만 분석했다. 첫 번째는 바다, 두 번째는 지형 시점이다. 코드 및 런타임 트레이스는 확인하지 않았으며 구현 변경은 없다.

## 관측값 (Avg, ms)
| 항목 | 바다 | 지형 | 차이 |
|---|---:|---:|---:|
| Graphics Queue Total 작업 | 5.62 | 5.09 | +0.53 |
| Graphics Queue Total Wait | 0.03 | 0.00 | +0.03 |
| Graphics Queue Total Idle | 12.93 | 3.52 | +9.41 |
| 위 세 열의 산술합 | 18.58 | 8.61 | +9.97 |
| BeginOcclusionTests | 0.54 | 0.09 | +0.45 |
| HZB | 0.55 | 0.09 | +0.46 |
| Basepass | 0.92 | 0.66 | +0.26 |
| Prepass | 0.25 | 0.03 | +0.22 |
| Shadow Depths | 1.41 | 1.67 | -0.26 |
| SingleLayerWater | 0.20 | 0.21 | -0.01 |
| SingleLayerWaterDepthPrepass | 0.03 | 0.03 | 0.00 |

## 개발자 관점의 해석
차이는 GPU 작업 증가보다 Graphics Idle 증가에 집중된다. CPU 렌더 처리/명령 제출 지연, GPU 결과를 기다리는 CPU, Present 및 프레임 제한에 따른 페이싱을 구분해야 한다. Graphics Queue Wait가 작아도 CPU 대기를 배제할 수 없다. 수면 패스 시간은 비슷하지만 숨겨진 패스나 다른 이름의 물 관련 작업까지 배제하지 않는다.

BeginOcclusionTests와 HZB 증가는 가시성/오클루전 경로를 확인할 단서다. CPU의 오클루전 결과 대기를 증명하지는 않는다. Draw 증가도 순수 작업과 대기를 구분해야 한다. Frame은 Game/Draw/GPU의 합 또는 최댓값에 고정 동기화 시간을 더한 값이 아니다.

Queue Total의 세 열 산술합은 stat unit Frame과 동일하다고 단정하지 않는다. 개별 패스는 중첩 가능성이 있어 합산하지 않는다. 서로 다른 프레임에서 발생할 수 있는 Max 값도 합산하지 않는다. 첫 번째 Compute Queue의 작업 1.08 ms, Wait 3.38 ms는 Graphics와 겹칠 수 있으며 두 번째에는 Compute 정보가 없다.

## 후속 확인
1. 같은 위치에서 방향만 바꾸고 해상도/Screen Percentage/실행 조건을 고정하여 stat unit 및 stat unitgraph를 기록한다. Frame/Game/Draw/GPU와 가능한 경우 RHIT를 비교한다.
2. VSync, t.MaxFPS, Smooth Frame Rate를 확인하고 기존 값을 기록한 뒤 제한 해제 전후를 비교한다.
3. Unreal Insights에서 RenderThread/RHIThread의 작업, 대기와 명령 제출 공백을 확인한다. 오클루전 결과 읽기, 프레임 끝 동기화, Present 경로를 구분한다. 트랙 지원과 이벤트 이름은 엔진 버전에 따라 확인한다.
4. 물 렌더링 가시성만 끄는 비교로 연관성을 확인한다. 개선되더라도 GPU 셰이더 비용과 CPU 메시/가시성 처리 비용을 구분한다.

코드 상태와 원인이 확인되지 않아 구현 계획은 보류한다.
