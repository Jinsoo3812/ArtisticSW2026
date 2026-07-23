# Ripple 최적화 01: 기준선과 Revision Gate

작성일: 2026-07-16  
대상 엔진: Unreal Engine 5.7  
대상 프로젝트: `ArtisticSW2026`  
프로파일 맵: `/Game/New/Level/Profiling/KKH_Profile_Ripple`

관련 문서:

- [How to Profile](./How%20to%20Profile.md)
- [물·파동·수영·부력·물리·렌더 최적화 분석](./Water_Wave_Swimming_Buoyancy_Physics_Render_Optimization_Analysis.md)

## 1. 이번 작업의 결론

Ripple을 첫 대상으로 고른 것은 아직 프로젝트 전체에서 가장 무겁다고 증명됐기 때문이 아니다. 다음 비용이 한 경로에 모여 있어, 통제된 R0/R8/R32 비교로 여러 관점을 동시에 확인하기 좋았기 때문이다.

- CPU: 위치 쿼리마다 활성 이벤트 배열 순회 및 파형 계산
- 동기화: `FRWLock` 읽기/쓰기와 snapshot 복사
- 메모리: `TArray` snapshot, render upload staging 배열
- 렌더: 매 프레임 32x2 float texture 갱신과 render command enqueue
- 네트워크: 서버 원천 `FastArray` 이벤트 복제
- cache 관점: 상태 revision이 같은데도 동일 데이터를 다시 만드는 논리적 cache miss

측정 결과, 현재 최대 32개 제한에서는 Ripple 수치 계산이나 네트워크가 첫 병목으로 드러나지 않았다. 가장 명확한 낭비는 **상태 revision이 변하지 않은 프레임에도 texture staging과 upload를 반복하는 것**이었다.

따라서 첫 변경으로 `StateRevision` gate를 적용했다. Ripple texture는 상태가 바뀔 때만 다시 만들고 올리며, 물 머티리얼의 `ServerTime` 갱신은 계속 매 프레임 수행한다. 파동 애니메이션의 시간 진행은 유지된다.

## 2. 실행 체크리스트

### 2.1 테스트 기반

- [x] `KKH_Test`를 원본으로 전용 프로파일 맵 생성
- [x] 원본 `KKH_Test`는 수정하지 않음
- [x] 프로파일 플래그가 있을 때만 transient controller 자동 생성
- [x] 프로파일 맵에서는 자연 overlap Ripple 생성을 차단하여 R0 오염 방지
- [x] R0, R8, R32를 동일 배치와 고정 query grid로 생성
- [x] 서버 시각 기준으로 서버와 클라이언트 phase 정렬
- [x] Development Editor 빌드 성공

프로파일 맵 파일:

`Content/New/Level/Profiling/KKH_Profile_Ripple.umap`

프로파일 활성화:

```text
-SWProfileRipple
```

주요 인자:

```text
-SWProfileRippleCount=0|8|32
-SWProfileRippleQueries=256
-SWProfileWarmup=14
-SWProfileSettle=1
-SWProfileDuration=5
-SWProfileAutoQuit
```

### 2.2 계측

- [x] query 호출 수와 총 CPU cycle
- [x] 순회 이벤트 수, active 판정 수, envelope 계산 수
- [x] full/active snapshot 호출 수, 논리적 복사 bytes, CPU cycle
- [x] texture update 호출 수와 CPU cycle
- [x] render upload enqueue 수와 bytes
- [x] revision 미변경 update 수
- [x] water material 순회, parameter write 수와 CPU cycle
- [x] 서버 이벤트 생성, 클라이언트 적용, 제거 수
- [x] NetDriver 실제 송수신 bytes, packets, bunches
- [x] Unreal Memory Insights용 memory trace
- [x] Unreal Insights CPU scope와 bookmark

### 2.3 검증

- [x] R0에 자연 Ripple이 섞이지 않음
- [x] 서버와 클라이언트 모두 `StoredEvents=32`, `Revision=32` 확인
- [x] 최적화 후 texture resource 정상 확인
- [x] 최적화 후 `Texture Active=32` 확인
- [x] 최적화 후 build 성공
- [ ] 실제 플레이 화면에서 Ripple 시각 회귀 최종 확인
- [ ] Shipping/Test 빌드에서 최종 수치 재확인

## 3. 기준선 조건

Standalone CPU 기준선은 다음 조건을 사용했다.

- 맵: `KKH_Profile_Ripple`
- 해상도: 1280x720
- 실행: `-game -RenderOffscreen -novsync`
- 측정: settle 후 5초
- 고정 부하: 프레임마다 Ripple query 256개
- trace: `cpu,frame,bookmark,counters,task,gpu`
- Ripple 수: R0, R8, R32

절대 frame time은 맵 전체 부하와 프로파일러 오버헤드를 포함한다. 아래 표는 controller가 정확한 측정 phase에서 누적한 Ripple 경로의 delta다.

## 4. CPU 및 연산 기준선

| 항목 | R0 | R8 | R32 |
|---|---:|---:|---:|
| Query batch 호출 | 524 | 521 | 519 |
| Query 256개 평균 | 0.03975 ms | 0.06843 ms | 0.08835 ms |
| 전체 Evaluate 호출 | 137,344 | 136,567 | 136,045 |
| 스캔한 이벤트 | 0 | 1,092,536 | 4,353,440 |
| Evaluate당 평균 이벤트 | 0 | 8 | 32 |
| Envelope 계산 | 0 | 69,476 | 275,524 |
| Full snapshot CPU/5초 | 0.0354 ms | 0.5025 ms | 0.4163 ms |
| Active snapshot CPU/5초 | 0.0578 ms | 0.6756 ms | 0.9856 ms |
| Texture update GT/5초 | 2.1776 ms | 2.7784 ms | 2.9153 ms |
| Material bind GT/5초 | 14.4129 ms | 14.4778 ms | 12.5246 ms |

해석:

1. 256개 인위 query를 포함해도 R32 query batch는 평균 약 0.088 ms다. 이벤트 수에 따라 증가하지만 현재 32개 상한에서는 첫 번째 절대 병목으로 판단하기 어렵다.
2. Material bind 비용은 Ripple 수와 무관하다. 대부분 WaterBody 탐색과 매 프레임 두 parameter를 다시 쓰는 고정비다.
3. Active snapshot과 texture update는 상태가 정적인 측정 구간에도 매 프레임 실행됐다.
4. R8/R32 CPU 수치가 완전 선형이 아닌 것은 query 위치별 전파 반경 조기 탈락과 실행 잡음의 영향이다.

## 5. 메모리 부하

`FSWRippleEvent`의 현재 C++ 크기는 56 bytes다.

R32, 5초 기준:

| 항목 | 호출 | 논리적 데이터 이동 |
|---|---:|---:|
| Full snapshot | 519 | 930,048 bytes |
| Active snapshot | 519 | 930,048 bytes |
| Render upload staging | 519 | 531,456 bytes |

한 프레임의 R32 active snapshot은 `32 x 56 = 1,792 bytes`, texture upload는 `32 x 2 x 16 = 1,024 bytes`다. 양 자체는 작지만 상태가 변하지 않았는데 매 프레임 할당·복사·render command를 만드는 것이 문제다.

Memory Insights trace도 별도로 수집했다.

```text
Saved/Profiling/RippleBaseline/R32_Standalone_Memory.utrace
파일 크기: 821,958,785 bytes
```

Memory trace는 callstack 수집 자체의 오버헤드가 크므로 CPU 기준선과 직접 비교하지 않는다. 이번 결정에는 측정 phase의 논리적 bytes와 호출 횟수를 사용했다. 추후 Insights에서 allocation callstack을 비교할 때 이 trace를 사용한다.

## 6. Cache hit 관점

이번 단계에서 확인한 cache는 하드웨어 L1/L2 miss가 아니라 **상태 revision cache의 논리적 hit/miss**다.

| 조건 | Texture update | Revision 미변경 | 실제 upload |
|---|---:|---:|---:|
| R0 기준선 | 524 | 524 | 524 |
| R8 기준선 | 521 | 521 | 521 |
| R32 기준선 | 519 | 519 | 519 |

측정 구간에서는 세 조건 모두 상태가 변하지 않았는데 100% 재업로드했다. 즉 revision 정보가 이미 있는데도 cache hit를 활용하지 못하고 있었다.

하드웨어 cache miss는 아직 측정 우선순위가 아니다. R32 query batch가 0.1 ms 미만이므로, 먼저 명확한 중복 작업을 제거한 뒤 query가 실제 frame 병목으로 올라올 때 VS Profiler/ETW의 cache-miss sampling을 수행한다.

## 7. 네트워크 패킷 기준선

전용 서버와 클라이언트를 분리 실행했다. 서버가 Ripple을 만들기 1초 전부터 settle과 5초 측정 종료까지 동일한 7초 창의 NetDriver 누적값을 기록했다.

### 7.1 서버 송신과 클라이언트 수신

| 항목 | R0 | R32 | 차이 |
|---|---:|---:|---:|
| 서버 OutBytes | 45,321 | 49,303 | +3,982 bytes |
| 클라이언트 InBytes | 45,253 | 49,129 | +3,876 bytes |
| 서버 OutPackets | 210 | 213 | +3 packets |
| 클라이언트 InPackets | 209 | 211 | +2 packets |
| 서버 OutBunches | 537 | 551 | +14 bunches |
| 클라이언트 InBunches | 527 | 544 | +17 bunches |

서버와 클라이언트 수치가 완전히 같지 않은 것은 packet framing, ack, 샘플 창 경계 때문이다. 약 3.9 KB의 방향과 규모는 일치한다.

### 7.2 판단

- 32개 Ripple 생성은 한 번의 작은 burst다.
- 이벤트가 생성된 뒤에는 FastArray dirty가 다시 발생하지 않으므로 Ripple 때문에 매 프레임 상태 패킷을 보내지 않는다.
- 현재 32개 상한에서는 packet compression이나 custom bit packing을 첫 최적화로 선택할 근거가 없다.
- 동시 Ripple 상한을 크게 늘리거나 이벤트 생성 빈도가 높아지면 R0/R32 차분이 아니라 `events/second` 부하 단계로 다시 측정한다.

Net trace 원본:

```text
Saved/Profiling/RippleBaseline/R32_ServerNet_v3.utrace
Saved/Profiling/RippleBaseline/R32_ClientNet_v3.utrace
```

## 8. 실패 및 폐기한 실행

프로파일 결과를 오염시키거나 양쪽 phase가 맞지 않은 실행은 채택하지 않았다.

1. 최초 R0는 맵의 자연 overlap Ripple 한 개가 들어와 폐기했다. 프로파일 맵에서 자연 발생을 차단한 뒤 R0를 다시 수집했다.
2. 첫 network 실행은 클라이언트 로딩이 서버의 14초 warmup보다 길어 클라이언트가 측정 phase를 놓쳤다.
3. 두 번째 network 실행은 양쪽 상태가 `32/revision 32`로 맞았지만 서버가 먼저 AutoQuit하여 클라이언트 Summary가 끊겼다.
4. 최종 실행은 warmup을 늘리고 서버 AutoQuit을 끈 뒤, 클라이언트 Summary가 완료된 후 서버를 정리했다.

## 9. 선택한 최적화

### 9.1 변경

`URippleSubsystem::UpdateTexture()`는 다음 순서로 동작한다.

1. `USWRippleStateSubsystem::GetRevision()` 확인
2. 마지막으로 GPU에 올린 revision과 같으면 즉시 반환
3. revision이 바뀐 경우에만 active snapshot 생성
4. 32x2 float staging 데이터 구성
5. render command enqueue
6. 성공한 revision과 active count 저장

중요하게, `BindRippleDataToWaterMaterials()`의 `ServerTime` 갱신은 그대로 유지한다. Texture에는 Ripple의 정적 시작/종료/파형 파라미터가 있고 Shader가 현재 server time으로 움직임을 계산하므로 매 프레임 texture를 재업로드할 이유가 없다.

### 9.2 A/B 결과

동일한 R32/256 query/5초 조건이다.

| 항목 | 변경 전 | 변경 후 | 결과 |
|---|---:|---:|---:|
| Active snapshot 호출 | 519 | 0 | -100% |
| Active snapshot bytes | 930,048 | 0 | -100% |
| Active snapshot CPU | 0.9856 ms | 0 | -100% |
| Texture upload | 519 | 0 | -100% |
| Texture upload bytes | 531,456 | 0 | -100% |
| Texture update GT CPU | 2.9153 ms | 0.4268 ms | -85.4% |
| 관련 GT 합계 | 3.9009 ms | 0.4268 ms | -89.1% |
| Query 256개 평균 | 0.08835 ms | 0.09080 ms | 유의미한 변화 없음 |
| Material bind GT CPU | 12.5246 ms | 13.6031 ms | 실행 잡음 범위, 변경 대상 아님 |

측정 구간 전에 revision 32가 한 번 업로드됐고, 로그에서 `Texture Active=32 RevisionResource=true`를 확인했다. 측정 구간에는 상태가 정적이므로 upload 0회가 정상이다.

제거한 직접 데이터 이동은 5초당 최소 1,461,504 bytes다. 절대 CPU 절감은 작지만 render thread command와 staging allocation도 함께 제거하며, 변경의 위험과 복잡도가 낮다.

### 9.3 채택 결정

- [x] 기능 원리 보존
- [x] 서버 권위 및 replication 변경 없음
- [x] CPU 감소
- [x] 메모리 복사 감소
- [x] render command 감소
- [x] 네트워크 영향 없음
- [x] build 성공
- [x] 자동 진단에서 active 32 확인
- [ ] 사람이 보는 PIE 렌더 회귀 확인

**결정: 채택.** PIE에서 파동이 계속 움직이고 새 Ripple이 즉시 표시되는지만 마지막으로 눈으로 확인한다.

## 10. 다음 후보

이번 변경과 섞지 않고 다음 차수에서 각각 독립 측정한다.

1. Water material bind 고정비
   - 매 프레임 WaterBody iterator
   - 변하지 않는 `RippleTex` parameter 재설정
   - 필요한 `ServerTime` scalar 갱신과 정적 texture binding 분리
2. Full snapshot 복사
   - R32에서 약 1,792 bytes/frame
   - Ship/physics 소비자가 매 프레임 전체 배열을 복사하는지 호출자별 분리
3. Query evaluator
   - 실제 Player/Ship/Storage query 수를 먼저 계측
   - 32개 상한을 넘기기 전에는 spatial index를 도입하지 않음
4. 패킷 압축
   - 현재 약 3.9 KB/32 events이므로 보류
   - 이벤트 발생률 또는 최대 상한이 증가할 때만 재검토

## 11. 산출물

### 소스

- `Source/ArtisticSWCore/Public/Water/SWRippleProfile.h`
- `Source/ArtisticSWCore/Private/Water/SWRippleProfile.cpp`
- `Source/WaterAndShip/Public/Profiling/SWRippleProfileController.h`
- `Source/WaterAndShip/Private/Profiling/SWRippleProfileController.cpp`
- `Source/WaterAndShip/Private/RippleSubsystem.cpp`
- `Source/WaterAndShip/Public/RippleSubsystem.h`

### 주요 trace

```text
Saved/Profiling/RippleBaseline/R0_Standalone_v2.utrace
Saved/Profiling/RippleBaseline/R8_Standalone.utrace
Saved/Profiling/RippleBaseline/R32_Standalone.utrace
Saved/Profiling/RippleBaseline/R32_Optimized.utrace
Saved/Profiling/RippleBaseline/R32_Standalone_Memory.utrace
Saved/Profiling/RippleBaseline/R32_ServerNet_v3.utrace
Saved/Profiling/RippleBaseline/R32_ClientNet_v3.utrace
```

### 주요 로그

```text
Saved/Logs/RippleProfile_R0_Standalone_v2.log
Saved/Logs/RippleProfile_R8_Standalone.log
Saved/Logs/RippleProfile_R32_Standalone.log
Saved/Logs/RippleProfile_R32_Optimized.log
Saved/Logs/RippleProfile_R0_NetCounters_Server.log
Saved/Logs/RippleProfile_R0_NetCounters_Client.log
Saved/Logs/RippleProfile_R32_NetCounters_Server.log
Saved/Logs/RippleProfile_R32_NetCounters_Client.log
```

