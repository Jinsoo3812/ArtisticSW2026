# AShip Network Physics 감사 및 누적 작업 기록

- 작성 시작: 2026-07-13 (Asia/Seoul)
- 프로젝트: `C:\Unreal Projects\ArtisticSW2026`
- 기준 브랜치: `KKH/Network_Test`
- 목적: `AShip`에 Unreal Engine의 Network Physics를 이용한 물리 예측, 과거 상태 비교, 재시뮬레이션, 파도와 부력의 결정론적 동기화를 구축한다.
- 문서 성격: 현재 코드와 로그를 기준으로 한 감사 결과이며, 이후 대화에서 검증 결과와 변경 사항을 계속 누적한다.

## 0. 결론 요약

2026-07-13 M1~M14 구현·기각 실험과 headless late-join 검증 결과, **Network Physics의 동일 과거 프레임 비교 → 상태 적용 → rewind/resimulation 핵심 루프가 실제로 작동하고, 서버 프레임 기반 물리/파도 시간이 양쪽에서 정확히 일치하는 것을 확인했다.** 현재 채택 코드는 M10의 5 cm 정책과 완전 transform 복원을 유지하며, M11~M13의 별도 warm-start 주입은 시간축 불일치가 실증되어 모두 제거했다.

최초 병목은 `FNetInputShip::NetSerialize`와 `FNetStatePhysicsShip::NetSerialize`가 상속받은 `FNetworkPhysicsPayload::ServerFrame`을 직렬화하지 않은 것이었다. 이를 packed frame으로 명시적으로 전송한 후 다음이 실측됐다.

- 서버가 저장한 `ServerFrame 240 → 540`이 클라이언트에 같은 값으로 도착했다.
- 예: `AuthServerFrame=420`이 클라이언트 `AuthLocalFrame=222`로 매핑되고 `PredLocalFrame=222`와 비교됐다.
- 첫 M1 run에서 `CompareData` 진단 83회, mismatch 80회, `ApplyState` 78회, 주기 로그상 `bIsResim=true` 12회가 확인됐다.

M1에서 핵심 루프가 열린 뒤 반복 divergence의 다음 직접 원인이 드러났다. Network Physics input payload 안에 조종 입력과 정적 부력 설정이 섞여 있었지만 custom serializer는 조종값만 전송했다. 따라서 resim 때 실제 설정 `Radius=300 / Multiplier=0.10 / Damp=1000`이 생성자 기본값 `100 / 1.20 / 3`으로 바뀌었다. 이를 다음과 같이 수정했다.

- `FNetInputShip`은 조종 입력과 frame identity만 보유한다.
- 파도·폰툰·튜닝 값은 정상 GT→PT async input cache로 분리했다.
- GT가 PT용 배열/cache를 직접 쓰던 `SetBuoyancyStaticData_External` 경로를 제거했다.
- 서버가 `ServerFrame 0`의 서버 월드 시간과 실제 solver dt를 복제한다.
- 모든 peer와 rewind는 `Origin + ServerFrame × SolverDt`로 같은 파도 시간을 계산한다.
- static data, clock, frame offset이 모두 준비되기 전에는 custom force를 적용하지 않는다.
- Chaos built-in gravity가 활성화된 상태에서 추가하던 수동 중력을 제거했다.

동일한 8초 late-join M2 run에서 초기 두 correction을 제외한 기록 중 `>30cm` 오차가 `77/81`에서 `4/8`로 감소했고 최대 기록 오차도 `77cm`에서 `30.38cm`로 감소했다.

M3~M10에서 추가로 확인·수정한 사항:

- UE 5.7에는 `AsyncPhysicsTickHz`가 없으며 실제 설정 키는 `AsyncFixedTimeStepSize`다. 기존 headless solver는 의도한 60 Hz가 아니라 30 Hz로 실행되고 있었다. 현재 `0.016666667`로 수정해 server/client `TickRate=60`, replicated physics step `0.016666668`을 실측했다.
- 동일 `ServerFrame`의 `SimTime` 차이는 모든 비교 표본에서 `0`이었다. 상태가 거의 같을 때 wave sample과 force도 거의 동일했다. 예: M6 SF1080 위치 차이 `0.0004 cm`, 속도 차이 `0.0001 cm/s`, wave 차이 `0.000069 cm`.
- 큰 부력 피크는 작은 상태 오차를 빠르게 증폭했다. 커스텀 4-폰툰 모델에 `MaxBuoyantForce=5,000,000`의 폰툰별 상한을 적용해 총 부력 최대를 약 `39.5M → 20.0M`으로 제한했다.
- Water 플러그인의 정규화 `PontoonCoefficient`까지 적용한 M7은 총 부력이 `5M`에 묶여 배가 약 `Z=-1,500 cm`까지 가라앉았으므로 기각했다. 이 계수는 네 개의 구면 체적을 직접 합산하는 현재 커스텀 모델과 의미가 다르다.
- Blueprint의 30 cm 위치 임계치는 빨강/초록 폰툰이 눈에 띄게 분리된 상태도 MATCH로 허용한다. 프로젝트 목표에 맞춰 effective threshold를 5 cm로 제한한 M9/M10에서 보정 후 mismatch는 최대 `8.56 cm`, 평균 `6.13 cm`였다. 대신 약 15초에 33회 correction이라는 비용이 있다.
- `ApplyState_Internal`은 `X/R/V/W`와 함께 Chaos 적분 예측 transform인 `P/Q`도 복원한다.

현재 blocker는 Network Physics의 비작동이나 시간/파도 위상 불일치가 아니다. 남은 핵심은 (1) late join 직후 첫 authoritative state와 로컬 history 시작점의 초기화 순서, (2) 민감한 커스텀 부력 모델에서 correction 사이 수 cm가 다시 벌어지는 현상, (3) 60초 late join·다중 배·packet impairment·실제 조종 입력을 포함한 최종 수용 행렬이다. 초기 오차는 run timing에 따라 M10의 `8.56 m`, M14의 `3.96/3.64 m`, history가 기본값으로 비교된 M11~M13의 약 `113 m`처럼 크게 달라지므로 고정 물리 오차가 아니라 startup/history 경계 문제로 다뤄야 한다.

최초 첨부 로그에 `[RESIM-COMPARE-TRIGGER]`가 0회였다는 사실은 `CompareData` 호출 0회를 직접 증명하지 않는다. 당시 로그는 mismatch일 때만 출력됐기 때문이다. 최초 감사의 해당 표현은 이 문서에서 정정한다. 다만 frame 누락 자체와 M1 패치 전후 결과는 런타임 실험으로 확인됐다.

## 1. 조사 범위와 증거 우선순위

확인한 범위:

- `Source/WaterAndShip`: `AShip`, Network Physics payload, Async Physics, 부력/파도 계산
- `Source/ClassFeature`: Water Waves wrapper, Ripple subsystem, 수영 쿼리, Water 렌더 시간 주입 경로
- `Config/DefaultEngine.ini`: Physics Prediction, Physics History Capture, 관련 CVar
- 현재 사용 후보 Ship Blueprint들의 런타임 Class Default Object 설정
- UE 5.7 엔진의 `NetworkPhysicsComponent`, `FNetworkPhysicsPayload`, rewind history, `FInstancedStruct::NetSerialize`, Gerstner Water Waves 구현
- `debugging_log.md`의 Stage 1~24
- 이번 프롬프트에 첨부된 현재 PIE 로그와 Ship Blueprint 복제 설정 스크린샷

판단의 우선순위는 다음과 같다.

1. 현재 런타임 Blueprint CDO와 현재 로그
2. 현재 C++/Config 코드
3. UE 5.7 엔진 소스의 실제 실행 경로
4. 과거 `debugging_log.md`의 해석과 결론

`debugging_log.md`는 중요한 여정 기록이지만, 일부 단계의 “완료” 결론은 현재 코드·현재 런타임과 모순된다. 이후에는 각 항목을 **관찰 사실 / 가설 / 변경 / 검증 결과**로 분리해 기록해야 한다.

작업 시작 시 이미 사용자의 미커밋 변경이 있었으며 이를 보존했다. 이후 채택된 M1~M10 C++/Config 변경을 추가했고, M11~M13 실험 코드는 M14에서 제거했다. 기존에 dirty였던 Ship Blueprint asset은 직접 저장하거나 덮어쓰지 않았다.

## 2. 초기 계획 체크리스트

### 2.1 서버와 모든 클라이언트가 서버 월드 시간을 사용한다

판정: **부분 달성**

구현된 부분:

- `AShip::BeginPlay`와 `AShip::Tick`에서 `AGameStateBase::GetServerWorldTimeSeconds()`를 사용한다.
- `USWRippleWaterWaves`는 호출 시간이 로컬 월드 시간과 거의 같으면 GameState의 서버 시간을 대입한다.
- `USwimmingComponent`는 수면 쿼리에 서버 월드 시간을 명시적으로 전달한다.
- `ABasePlayerController::Tick`은 서버 월드 시간을 `WaterSubsystem`의 `SmoothedWorldTimeSeconds`에 주입해 Water 렌더 시간축을 맞춘다.
- `URippleSubsystem`은 서버 시간을 Water MPC에 전달한다.

미완성 또는 주의점:

- `GetServerWorldTimeSeconds()`는 절대 벽시계나 모든 peer에서 비트 단위로 동일한 물리 프레임 시간이 아니다. 클라이언트는 복제·스무딩된 시간 오프셋을 사용하므로 수십 ms 차이는 정상 범위다.
- 첨부 로그에서도 클라이언트/서버 GT 쿼리 시간이 `3.9169`와 `3.9543`으로 약 `37.4 ms` 다르다.
- `AShip`은 GameState가 없을 때 로컬 `GetWorld()->GetTimeSeconds()`로 fallback한다. 따라서 “항상 서버 시간”이라는 강한 보장은 없다.
- 물리 쪽 시각은 현재 복제된 `ServerPhysicsTimeOrigin + ServerFrame × 실제 solver dt`로 계산한다.
- 과거 `static bSpawnTimeSent` 공유 문제는 제거됐다. 다만 여러 배·Single Process PIE world·60초 late join의 최종 행렬 검증은 남아 있다.

재정의할 요구사항:

- 렌더/일반 gameplay 쿼리는 GameState 서버 시간축을 따른다.
- Network Physics의 판정과 재생은 raw float 월드 시간이 아니라 `ServerFrame`/`LocalFrame` 매핑을 기준으로 한다.
- late join anchor는 배 인스턴스별로 안전하게 설정되고, 실제 physics step/delta와 연결되어야 한다.

### 2.2 동일한 파도가 생성되도록 모든 쿼리와 렌더에 서버 시간을 넣는다

판정: **부분 달성**

구현된 부분:

- 렌더링 WaterSubsystem 시간 주입 경로가 존재한다.
- 수영과 wrapper water query는 서버 시간을 사용한다.
- Ship Async Physics에도 별도의 `SimTime`이 들어가며, 로그상 base wave phase는 상당히 잘 맞는다.

미완성 또는 불일치:

- Ship Async Physics의 `GetWaveHeightAtPosition`은 단순 cosine 합이다. UE의 full Gerstner query가 수행하는 steepness/Q 기반 2차 위치 보정, LWC tile 처리 등과 동일하지 않다.
- 현재 Ship 부력 계산은 수면 기준 Z, Water Body별 attenuation/depth, Water Body transform 등의 전체 수면 정의를 포함하지 않는다.
- ripple은 로컬 overlap으로 생성되고 authoritative replication이 없다. 전용 서버에서는 ripple이 0이며, 클라이언트별 ripple field가 달라질 수 있다.
- `USWRippleWaterWaves`는 gameplay query에도 ripple을 더한다. ripple이 물리에 영향을 주어야 한다면 이벤트의 원점·시작 서버 시간·파라미터를 복제해야 하고, cosmetic이면 authoritative 물리 쿼리에서 제외해야 한다.

### 2.3 동일 좌표와 동일 시간으로 파도를 쿼리한다

판정: **원래 전제의 수정이 필요함**

Network Physics prediction에서는 서버와 클라이언트의 “현재 좌표”를 강제로 동일하게 만드는 것이 목표가 아니다. 클라이언트는 자신의 예측 상태 좌표로 파도를 샘플링해야 한다. 권위 상태가 도착하면 다음이 일어나야 한다.

1. 권위 상태의 `ServerFrame`을 클라이언트 history의 대응 `LocalFrame`으로 매핑한다.
2. 같은 과거 프레임의 예측 상태와 권위 상태를 비교한다.
3. 임계치를 넘으면 그 과거 프레임으로 복원한다.
4. 저장된 입력과 동일한 결정론적 파도 함수로 현재 프레임까지 재실행한다.

따라서 올바른 요구사항은 **“재시뮬레이션하는 동일 프레임에서 동일 상태, 동일 입력, 동일 파도 파라미터, 동일 시간 매핑을 사용한다”**이다. 현재 live 좌표를 서버 값으로 강제하면 예측 자체를 훼손한다.

현재 구현은 각 peer의 physics particle `X/R`을 폰툰 월드 좌표로 변환한다. 이는 예측 구조상 맞는 방향이다. M1에서 payload의 `ServerFrame`을 복원한 뒤 어느 과거 frame끼리 비교하고 되감을지가 실제 로그로 확인됐다.

### 2.4 Unreal Network Physics를 이용한 과거 비교와 재시뮬레이션

판정: **M1~M14 핵심 폐루프·동일 frame clock/wave·5 cm correction 정책 런타임 검증 완료, 별도 warm-start 주입은 기각, engine history 초기화와 최종 수용 테스트는 진행 중**

구현된 부분:

- `UNetworkPhysicsComponent` 생성 및 replication
- `SetNetAddressable`, `SetIsReplicated`, `SetNetPushIDDynamic`
- `SetPhysicsReplicationMode(EPhysicsReplicationMode::Resimulation)`
- `CreateDataHistory<FNetInputShip, FNetStatePhysicsShip>`
- `FNetworkPhysicsPayload` 기반 input/state 구조체
- `BuildData`, `ApplyData`, `CompareData`, `InterpolateData`, `MergeData`
- 물리 상태 위치·회전·선속도·각속도 저장/적용
- Physics Prediction 및 Physics History Capture 활성화
- simulated proxy compare CVar 활성화
- `NetworkPhysicsComponent`가 Blueprint CDO에서도 replicated로 확인됨

확인된 결과:

- 양쪽 native `NetSerialize`가 상속 payload의 `ServerFrame`을 보내지 않았으며 M1에서 수정했다.
- 엔진은 수신 상태의 `ServerFrame - NetworkPhysicsTickOffset`으로 비교할 로컬 history frame을 결정한다.
- `FInstancedStruct`는 native `NetSerialize`가 있으면 그 함수를 직접 호출하므로 상속 UPROPERTY가 자동으로 추가 직렬화되지 않는다.
- 수정 후 matching history frame에서 `CompareData`, `ApplyState`, rewind가 모두 실측됐다.
- M2에서 control-only input과 권위 clock을 적용한 뒤 반복 mismatch가 크게 감소했다.
- M3/M6에서 동일 server frame의 시간, wave, force가 상태 오차 범위 안에서 결정론적으로 일치함을 확인했다.
- M9/M10에서 5 cm threshold와 완전 transform 복원 경로를 검증했다.
- M11~M13에서 별도 actor snapshot을 GT 또는 PT 현재 상태에 주입하는 warm-start를 시험했으나, snapshot의 `ServerFrame`과 적용되는 local physics frame이 달라 rewind history를 올바르게 초기화하지 못했다. 이 경로는 제거했다.
- M14에서 M10 계열 코드로 복귀한 뒤 정상 state load, Compare, Apply, resim을 다시 확인했다.
- 아직 전 시나리오의 수렴 품질과 오차 예산을 충족했다는 증거는 없으므로 최종 완료는 아니다.

## 3. 현재 구현 지도

### `Source/WaterAndShip/Public/Ship.h`

- `FNetInputShip`: `FNetworkPhysicsPayload` 상속, 조종 입력만 보유
- `FNetInputShip::NetSerialize`: packed `ServerFrame`, `MovementInput`, `SteeringInput` 전송
- `FNetStatePhysicsShip`: 위치, 회전, 선속도, 각속도와 resim 임계치 보유
- `FNetStatePhysicsShip::NetSerialize`: packed `ServerFrame`과 위 상태 값 전송
- `FNetStatePhysicsShip::CompareData`: 위치 또는 회전 오차가 임계치를 넘으면 resim 요청
- `FShipReplicatedState`: 빨강/초록 진단용 Location/Rotation만 보유하며 frame/time 없음
- `NetworkPhysicsComponent`, `ShipPhysicsAsync`, 부력 설정 및 파도 캐시 필드 보유

현재 상태:

- 조종 input history와 정적 부력/파도 설정을 분리했다.
- 정적 설정은 `FAsyncInputShip`으로만 GT→PT 전달되고 resim 중 기존 cache를 유지한다.

### `Source/WaterAndShip/Private/Ship.cpp`

- C++ 생성자는 Network Physics component를 만들고 `SetReplicateMovement(true)`를 호출한다.
- 런타임 `BP_TestShip_SingleMesh` CDO는 `Replicate Movement=false`로 이 값을 override한다.
- 서버는 매 Tick `FShipReplicatedState`에 현재 transform을 넣어 복제한다.
- 클라이언트는 자신의 현재 transform과 최신 복제 transform 사이를 빨강/초록 라인으로 표시한다.
- 모든 Water Body를 순회해 Gerstner wave 데이터를 Ship용 캐시에 복사한다.
- Async callback producer input으로 부력/파도/static config를 전달한다.
- authority는 `ServerPhysicsTimeOrigin`과 실제 solver dt를 계산해 복제한다.
- UE 5.7의 실제 async fixed-step 설정을 60 Hz로 고정한다.
- simulated proxy comparison을 매 Tick 활성화한다.

주의점:

- C++ 기본값과 Blueprint override가 달라 배 asset마다 replication 설정이 갈린다.
- `BP_TestShip`은 `Replicate Movement=true`, 현재 사용된 것으로 보이는 `BP_TestShip_SingleMesh`는 `false`다.
- 빨강/초록 비교에는 frame/timestamp가 없어서 Network Physics 성공 여부를 판정할 수 없다.
- 과거 함수 static clock flag와 GT→PT cache 직접 쓰기 경로는 제거됐다.
- `ShipMove/ShipTurn` RPC와 Network Physics input replication이 중복 경로로 존재한다.

### `Source/WaterAndShip/Private/ShipPhysicsAsync.cpp`

- `BuildState_Internal`: physics particle의 `X/R/V/W`를 history state에 저장
- `ApplyState_Internal`: 권위 상태로 particle의 `X/R/P/Q/V/W`를 복원
- `BuildInputs_Internal`/`ApplyInputs_Internal`: frame-tagged 조종 input history 작성/적용
- `ProcessInputs_Internal`: async static config cache, server-frame clock, pontoon 위치, wave query, buoyancy/damping 계산
- `OnPreSimulate_Internal`: 현재 비어 있음

주의점:

- `SimTime`은 `ServerPhysicsTimeOrigin + ServerFrame × ServerPhysicsStepSeconds`로 계산한다.
- 잘못된 `AsyncPhysicsTickHz` 키를 쓰던 headless run은 실제 `1/30`이었다. 현재 `AsyncFixedTimeStepSize=0.016666667` 적용 후 server/client 모두 `1/60`이다.
- `ParticleHandle->GetX/R`을 쓰는 것은 prediction 관점에서 정상이다.
- built-in gravity와 중복되던 수동 gravity force는 제거됐다.
- 폰툰별 `MaxBuoyantForce` 상한을 적용한다. Water 플러그인의 정규화 pontoon coefficient는 현재 커스텀 체적 합산 모델과 맞지 않아 적용하지 않는다.
- 커스텀 부력식은 Epic Buoyancy의 force ramp, water-body 기준면/attenuation 등을 완전히 재현하지 않는다.
- BuildState 로그 태그는 role을 단정하지 않는 `[PHYSICS-PT-STATE-BUILD]`로 변경됐다.

### Water/Ripple 관련 코드

- `SWRippleWaterWaves.cpp`: 서버 시간 치환, base wave query 위에 ripple 추가
- `RippleSubsystem.cpp`: 서버 시간 MPC 전달, 로컬 overlap 기반 ripple 생성
- `SwimmingComponent.cpp`: 서버 시간으로 물결 쿼리
- `BasePlayerController.cpp`: WaterSubsystem 렌더 시간 override

## 4. 런타임 Blueprint와 Project 설정 확인

스크린샷 및 `BP_TestShip_SingleMesh` CDO:

- Replicates: `true`
- Replicate Movement: `false`
- Always Relevant: `true`
- Net Load on Client: `true`
- Dormancy: `Awake`
- Net Update Frequency: `100`
- Min Net Update Frequency: `2`
- Net Priority: `3`
- Physics Replication Mode: `Resimulation`
- NetworkPhysicsComponent replicated: `true`
- BuoyancyRoot Simulate Physics: `true`
- Enable Gravity: `true`
- Mass override: `10000 kg`
- Async physics tick: `0.016666667 s` (`AsyncFixedTimeStepSize`, 60 Hz)
- Linear damping: `0.3`
- Angular damping: `5`
- Blueprint resim thresholds: `30 cm`, `5 deg`

Config:

- Physics Prediction: enabled
- Physics History Capture: enabled
- `np2.Resim.RpcStateChangeRequiredForResimulation`: enabled
- `np2.Resim.CompareSimProxyToServerState`: enabled

주의: 현재 임계치에는 세 경로가 공존한다.

- Project PhysicsSettings 기본값: 약 `10 cm / 4 deg`
- CVar 실험값: `5 cm / 2 deg`
- Ship Blueprint 저장값: `30 cm / 5 deg`
- 현재 `FAsyncInputShip`으로 전달하는 effective 위치 임계치: 최대 `5 cm` (M9/M10 검증)

커스텀 `CompareData`는 payload에 실린 값을 사용한다. 현재 위치는 코드에서 최대 5 cm로 맞췄지만 회전은 Blueprint 값 5 deg이므로, CVar만 보고 둘 다 `5 cm / 2 deg`라고 해석하면 안 된다.

## 5. 현재 첨부 로그가 제공하는 정보

이 절은 **사용자가 최초 첨부한 PIE 로그만을 당시 상태 그대로 해석한 기준선**이다. 아래의 “폐루프가 돌지 않는다”는 문장은 최초 첨부 로그에서 증거가 없었다는 뜻이며, 이후 M1~M10 자동 run에서는 Compare/Apply/resim이 실증됐다.

### 호출 횟수

- `PT-RESIM`: 9회, 모두 `false`
- `PHYSICS-PT-PARAMS`: 9회
- `PONTOON`: 36회
- `BuildState`: 9회
- `NET-SERIALIZE`: 5회
- `SHIP-SYNC`: 10회
- Owner/CVar 진단: 각각 10회
- `RESIM-COMPARE-TRIGGER`: 0회
- `PHYSICS-PT-CL-APPLY`: 0회

해석:

- callback과 force 계산은 돌아간다.
- state serializer를 통한 수신은 있다.
- 이 최초 첨부 로그만으로는 compare/apply/resimulation 폐루프가 돈다는 증거가 없다.
- `Owner=None`, simulated proxy는 비소유 물리 배에서 정상일 수 있으며 단독 원인이 아니다.
- 두 CVar가 모두 true이므로 과거의 “sim proxy flag가 꺼져 있음”은 이번 로그의 원인이 아니다.

### 빨강/초록 진단 거리

`LocDiffCm`은 대략 다음 순서다.

`1416.80, 537.21, 106.08, 63.28, 85.07, 94.14, 40.81, 106.59, 43.23, 26.90`

큰 폭으로 줄지만 단조 감소하지 않는다. 이 값은 현재 클라이언트 transform과 도착한 최신 서버 transform의 시점이 다른 비교이므로, resimulation의 품질 지표가 아니다.

올바른 지표는 `ServerFrame=N`의 권위 상태와 history에 저장된 대응 `LocalFrame=M`의 예측 상태 차이다.

### 물리 상태와 파도 샘플

동일 라벨 step에서 두 실행 주체의 로그로 추정되는 Z 위치 차이:

- Step 60: 약 `81.65 cm`
- Step 120: 약 `5.58 cm`
- Step 180: 약 `15.16 cm`
- Step 240: 약 `16.31 cm`

반면 같은 step의 폰툰 `WaveZ` 쌍은 최대 차이가 대략 다음과 같다.

- Step 60: `0.03 cm`
- Step 120: `0.13 cm`
- Step 180: `0.03 cm`
- Step 240: `0.01 cm`

로그에 process/role이 명시되지 않아 쌍짓기는 추론이지만, 적어도 이번 run에서는 base wave phase가 크게 따로 논다는 증거가 없다. 배 상태 차이가 먼저이고, 그 배 상태를 같은 과거 프레임끼리 비교/교정하지 못하는 것이 더 직접적인 문제다.

### 초기화 상태

Step 0에는 `Waves=0`, `Pontoons=0`, `Radius=100`인 input이 적용되고, Step 60부터 `Waves=16`, `Pontoons=4`, `Radius=300` 등 실제 값이 보인다.

이는 첫 physics step이 deterministic static data가 준비되기 전에 실행될 수 있음을 뜻한다. 초기화 완료 barrier가 필요하다.

### late join 검증 여부

현재 로그의 `Step 60 -> SimTime 1`, `Step 120 -> SimTime 2` 패턴은 정상 0초 시작만 보여준다. “서버 60초에 늦게 들어온 클라이언트가 서버의 시간축과 물리 frame history를 올바르게 매핑한다”는 목표는 이 로그로 검증되지 않았다.

## 6. `debugging_log.md` Stage 1~24 재평가

| Stage | 과거 주제 | 현재 판정 | 현재 근거/코멘트 |
|---:|---|---|---|
| 1 | ReplicateMovement 비활성화 | 부분/불일치 | 현재 C++는 `true`, `BP_TestShip_SingleMesh`는 `false`, 다른 `BP_TestShip`은 `true`. asset별 설정이 갈림. |
| 2 | Spawn server time | 구조 수정·8초 late join 검증 | frame-0 origin과 실제 solver dt를 authority가 복제하고 server frame으로 계산. 60초/장시간 검증은 남음. |
| 3 | static 초기화 문제 | 수정 | 함수 static flag 제거, actor별 replicated clock으로 교체. |
| 4 | resim threshold CVar | 구조 확인·5 cm 실험 채택 | 커스텀 `CompareData`는 payload threshold를 사용. Blueprint 30 cm가 CVar 5 cm를 덮던 경로를 effective 최대 5 cm로 제한했다. |
| 5 | PT 진입 로그 | 달성 | 실행은 확인. 단 `OnPreSimulate`는 비어 있고 핵심은 `ProcessInputs`에서 수행. |
| 6 | state build/apply | 달성 | matching history의 Compare, Apply, resim을 M1~M10에서 반복 실측. Apply는 `X/R/P/Q/V/W`를 복원한다. |
| 7 | component replication | 달성 | C++와 Blueprint CDO 모두 replicated 확인. |
| 8 | 모든 peer에 파도/폰툰 전달 | 1차 수정 | producer/consumer async marshalling만 사용하고 GT→PT 직접 cache 쓰기 제거. |
| 9 | simulated proxy compare | 현재 활성 | 두 관련 CVar true. 과거의 flag decay 가설은 현재 코드/엔진 흐름상 주원인 아님. |
| 10 | PhysicsObject valid | 달성 | 현재 로그에서 particle state/force 실행 확인. |
| 11 | pontoon 추출 | 현재 동작 | 현재 4개 pontoon/16 wave 확인. class-name/reflection 의존은 취약. |
| 12 | 진단 로그 | 1차 달성 | ServerFrame/LocalFrame, resim 여부, 동일-frame state/wave/force를 기록한다. NetMode/actor 식별의 일관화는 남음. |
| 13 | 수동 중력 | 수정 | Chaos built-in gravity만 유지하고 중복 manual gravity force 제거. |
| 14 | 부력식 조정 | 1차 검증 | 폰툰별 5M 상한은 부유 상태를 유지하며 force peak를 줄였다. 정규화 pontoon coefficient 적용은 침몰을 일으켜 기각. |
| 15 | Epic submerged volume 이식 | 부분 | 구면 cap 부피는 유사하지만 Epic 전체 force policy를 재현한 것은 아님. |
| 16 | 공식과 100% 동일 | 목표 재정의 필요 | clamp는 적용. 정규화 coeff는 커스텀 4-구체 합산 모델과 맞지 않아 기각. ramp/base water Z/attenuation은 남음. |
| 17 | NaN 방어 | 개선 | config/input을 분리하고 모든 async scalar 기본값을 초기화. 추가 장시간 finite 검증은 남음. |
| 18 | possessed input | 미검증 | local control 분기는 있으나 이번 로그는 Owner=None SimProxy. |
| 19 | template/signature 문제 | 과거 해결 | 현 빌드 코드에는 관련 함수들이 연결됨. |
| 20 | History Capture | 활성·resim 실증 | matching history 비교와 실제 resim이 확인됐다. |
| 21 | payload 상속 의심 | 가설 기각 | UE 5.7에서 `FNetworkPhysicsPayload`가 새 API의 올바른 기반. 실제 문제는 frame 직렬화 누락. |
| 22 | PT state 로그 추가 | 달성 | 단 `[SRV]` 태그가 실행 role을 보장하지 않음. |
| 23 | sim proxy CVar 누락 | 과거 해결 | 현재 둘 다 true이므로 이번 blocker가 아님. |
| 24 | 매 Tick compare 재활성화 | 불필요/비핵심 | 엔진 내부 bool은 지속된다. 호출 반복은 frame 누락을 해결하지 않음. |

## 7. 현재 위험과 결함 우선순위

### 해결된 P0/P1 구조 결함

1. input/state native serializer의 `ServerFrame` 누락
2. network input history에 static config를 섞어 resim cache를 기본값으로 오염시키던 문제
3. 함수 static time anchor와 고정 `1/60`/local-time fallback
4. GT에서 PT cache의 배열/설정을 직접 수정하던 data race 경로
5. static data와 clock 준비 전 custom force 실행
6. Chaos built-in gravity와 manual gravity의 이중 적용
7. UE 5.7에서 무시되던 `AsyncPhysicsTickHz` 설정과 실제 30 Hz 실행
8. 부력 force 무상한 피크와 `ApplyState`의 `P/Q` 미복원

### 현재 P1 — 최종 수렴과 수용 검증

1. late join 직후 첫 authoritative packet이 매핑될 때 local history가 몇 frame부터 유효한지 run마다 달라 초기 correction이 `3.6m~113m`까지 변함. 별도 actor snapshot을 현재 PT 상태에 주입하는 방식은 시간축 불일치로 기각됨
2. 5 cm effective threshold에서 correction 사이 위치 차이가 주로 `5~9cm`까지 다시 증가해 약 2.3회/초 correction이 발생함
3. 런타임 중 wave/config가 바뀔 경우 과거 frame의 config version을 보존하는 정책이 아직 없음
4. Network Physics tick offset이 한 frame 조정될 때 clock 연속성과 history mapping을 장시간 확인해야 함
5. frame 없는 빨강/초록 latest-snapshot 디버그는 여전히 resim 품질 지표로 사용할 수 없음
6. 60초 late join, 여러 배, Single Process PIE, packet lag/loss/reorder 검증이 남음

### P2 — 물리·수면 모델의 일관성과 유지보수

1. custom cosine wave와 UE full Gerstner/render query의 수식 불일치
2. ripple authority 정책 부재
3. Water Body 기준 수면 Z/attenuation 누락
4. Epic buoyancy force ramp와 custom 4-구체 모델의 물리 정책 정식화 필요
5. Blueprint asset 간 `Replicate Movement` 불일치
6. Water Body 전수 검색과 파도 배열 복사의 Tick 비용
7. RPC input과 Network Physics input의 중복 경로

## 8. 마일스톤

### M0. 관측 기준선 고정

- 모든 핵심 로그에 `NetMode`, `Role`, `World/PIE instance`, actor name, local physics step, payload `ServerFrame`, mapped `LocalFrame`을 기록한다.
- 빨강/초록 디버그는 동일 frame history끼리 비교하도록 바꾸거나 “latest snapshot lag visualization”이라고 명확히 이름 붙인다.
- 현재 수치와 asset CDO 설정을 기준선으로 보존한다.

완료 조건:

- 어느 프로세스/월드/role의 어느 frame 로그인지 한 줄만으로 식별 가능하다.

### M1. Network Physics frame identity와 재시뮬레이션 복구 — 검증 완료

- 입력과 상태의 `ServerFrame`을 명시적으로 직렬화한다.
- 수신 후 증가하는 `ServerFrame`과 정상 `LocalFrame` 매핑을 확인한다.
- 같은 frame의 예측/권위 상태에서 `CompareData`가 호출되는지 확인한다.
- 의도적인 큰 오차를 한 번 주어 `ApplyState` 및 `bIsResim=true`를 확인한다.

완료 조건:

- `ServerFrame`이 0에 고정되지 않고 증가한다.
- `CompareData`가 matching history frame에서 호출된다.
- 임계치 초과 시 `ApplyState`와 resim step이 발생한다.
- resim 후 오차가 임계치 안으로 돌아오며 매 frame 연속 재시뮬레이션하지 않는다.

### M2. 시간축과 late join — 구조 구현, 8초 late join/60 Hz 동일-frame 검증 완료

- actor별 time anchor로 변경하고 함수 static 상태를 제거한다.
- 실제 physics delta 또는 엔진 frame-time 변환을 사용한다.
- 서버 60초 이후 클라이언트 접속 시나리오를 자동/수동 테스트한다. (아직 미실행)
- GameState fallback 정책을 명확히 한다.

완료 조건:

- late join client가 서버 시간축을 한 physics step 이내로 매핑한다.
- 여러 배와 Single Process PIE 여러 world가 서로 anchor를 공유하지 않는다.

### M3. 결정론적 데이터 전달과 thread safety — 1차 구현 완료

- 파도/폰툰/static config를 정상 async input marshalling으로만 전달한다.
- GT에서 PT-read cache를 직접 수정하지 않는다.
- 조종 입력과 정적/튜닝 설정 payload를 분리한다.
- 모든 필드를 명시적으로 초기화한다.
- static data 준비 전 force simulation을 시작하지 않는 barrier를 둔다.

완료 조건:

- GT/PT 사이 공유 `TArray` mutation이 없다.
- readiness 이후 모든 peer가 같은 pontoon/wave/config version을 사용한다.
- resim 중 과거 frame이 당시 사용한 설정을 재사용한다.

### M4. 수면 함수 일치와 ripple 정책

- Ship 물리에 필요한 수면 함수를 하나의 결정론적 정의로 만든다.
- UE full Gerstner와 동일해야 하는 범위를 정하고 수식/파라미터를 맞춘다.
- Water Body base surface Z, attenuation, LWC 좌표 처리를 포함한다.
- ripple을 cosmetic 전용으로 할지 authoritative gameplay로 할지 결정한다.

완료 조건:

- 동일 frame/동일 좌표/동일 wave snapshot에서 server와 client PT wave sample이 허용 오차 이내다.
- 물리 query와 렌더 표면 차이가 정의된 오차 범위 안이다.
- ripple의 서버/클라이언트 권위 규칙이 테스트로 고정된다.

현재 상태:

- base Gerstner의 동일-frame 시간과 sample 결정론은 M3/M6에서 검증했다.
- full Gerstner, Water Body base Z/attenuation/LWC, ripple authority 정책은 남아 있다.

### M5. 부력과 중력 물리 검증

- 중력 source를 built-in 또는 manual 중 하나로 통일한다.
- 부력 단위, 질량, pontoon 반경, damping을 검증한다.
- 필요하면 Epic Buoyancy의 ramp/clamp/coefficient를 의도적으로 포팅한다.

완료 조건:

- 정수면에서 기대 draft와 안정 자세를 유지한다.
- 중력이 정확히 한 번만 적용된다.
- frame rate/async step 변화에도 힘 적분이 안정적이다.

### M6. 소유 pawn 입력 예측

- possessed autonomous proxy에서 input history를 검증한다.
- RPC 중복 경로를 제거하거나 책임을 분리한다.
- packet lag/loss/reorder 환경에서 steering과 movement input replay를 확인한다.

완료 조건:

- local input이 해당 frame history에 저장되고 서버에서 권위 처리된다.
- correction 후 같은 입력들이 현재까지 재생된다.

### M7. 시각 품질과 성능

- 승객/카메라 jitter, mesh smoothing, visual interpolation을 물리 정합성 이후 조정한다.
- Water Body 검색과 wave cache 업데이트를 event/version 기반으로 최적화한다.

### M8. 최종 수용 테스트 행렬

- Dedicated server / listen server
- 1 client / 2+ clients
- 시작 시 동시 접속 / 60초 late join
- 소유 배 / 비소유 simulated proxy
- packet lag, jitter, loss, reorder
- 장시간 운항 및 월드 원점/LWC 경계
- 여러 `AShip` 인스턴스

최종 완료 조건:

- 각 시나리오에서 frame-matched 오차, resim 빈도, 최대 correction, wave sample 오차가 정해진 예산 안이다.
- 재시뮬레이션은 필요할 때만 발생하며 발산하거나 계속 반복되지 않는다.
- 서버와 클라이언트가 동일한 과거 frame을 재생할 때 힘과 상태가 허용 오차 내에서 일치한다.

## 9. 다음 최소 실험

가장 정보량이 큰 다음 실험은 **상태를 더 주입하지 않고 late-join 첫 history 경계를 계측하는 것**이다.

1. client callback 생성, `CreateDataHistory`, `OnPostInitialize_Internal`, 첫 `BuildState_Internal`, 첫 replicated state 수신, `ReceiveNewData/CompareData`, tick-offset 확정 순서를 한 timeline으로 기록한다.
2. 첫 authoritative `ServerFrame`이 어느 `LocalFrame`으로 매핑되는지, 그 local history slot이 실제 particle state인지 기본 생성 상태인지 기록한다.
3. M10처럼 첫 비교가 `LocalFrame=26`에서 시작되는 run과 M11~M13처럼 `LocalFrame=0`에서 시작되는 run을 비교한다.
4. actor property, threshold, 부력, 파도, gravity는 바꾸지 않는다.
5. 엔진이 제공하는 history/state 초기화 지점이 확인된 뒤에만 그 **동일 local frame**에 초기 상태를 기록하는 방법을 설계한다.

금지할 접근:

- frame 없는 최신 actor transform을 현재 PT particle에 바로 주입
- `ServerFrame=N`의 snapshot을 현재 추정 frame `N+k`에 fast-forward 없이 적용
- first correction을 막기 위해 client simulation/history build를 중단

M11~M13에서 이 세 방식은 각각 default history 비교, 과거 snapshot의 현재-frame 오염, correction storm으로 이어졌다. 다음 단계는 보이는 배를 먼저 순간이동시키는 문제가 아니라 **어느 시간의 상태를 어느 history slot에 써야 하는가**를 해결하는 것이다.

## 10. 이후 기록 규칙

각 후속 작업은 아래 형식으로 이 문서 끝에 누적한다.

```text
날짜/세션:
목표:
변경 파일:
관찰 사실:
가설:
변경:
검증 방법:
검증 결과:
결론:
남은 위험:
다음 작업:
```

## 11. 변경 이력

### 2026-07-13 — 최초 감사

- 초기 계획 1~4의 달성도를 재평가했다.
- 현재 C++/Config, Ship Blueprint CDO, UE 5.7 엔진 Network Physics 경로를 대조했다.
- 현재 로그에서 base wave sample은 매우 근접하지만 frame-matched comparison/resimulation은 한 번도 확인되지 않았음을 기록했다.
- 가장 유력한 blocker를 custom payload serializer의 `ServerFrame` 누락으로 좁혔다.
- 시간, thread safety, 초기화, 중력, 수면 함수, ripple authority의 후속 위험을 분리했다.
- M0~M8 마일스톤과 수용 조건을 정의했다.

### 2026-07-13 — M1 ServerFrame 복구 및 실증

목표:

- 권위 상태가 어느 서버 물리 frame인지 보존하고 matching client history와 실제 비교되는지 확인한다.

변경:

- `FNetInputShip::NetSerialize`와 `FNetStatePhysicsShip::NetSerialize`에서 `ServerFrame + 1`을 packed uint로 저장/로드한다.
- load 시 `LocalFrame = ServerFrame`으로 임시 초기화하고 엔진이 network tick offset으로 재매핑하게 한다.
- serializer, comparison, state build/apply 로그에 `ServerFrame`과 `LocalFrame`을 추가한다.
- 정상 MATCH일 때도 주기적으로 `[NETPHYS-COMPARE]`를 출력한다.

검증:

- `ArtisticSW2026Editor Win64 Development` UHT/C++/link 성공.
- hidden headless dedicated server를 먼저 실행하고 8초 후 client를 접속해 약 22초 수집했다.
- 로그: `Saved/Logs/Codex_M1_Server.log`, `Saved/Logs/Codex_M1_Client.log`.
- server/client join 성공, `LogNet: Error`, ensure, fatal 0건.

결과:

- server save frame `240, 300, 360, 420, 480, 540`이 client load에 동일하게 나타났다.
- `AuthServerFrame=420 → AuthLocalFrame=222`, `PredLocalFrame=222`로 matching comparison이 확인됐다.
- client `CompareData` 진단 83회, mismatch 80회, `ApplyState` 78회, 주기 `bIsResim=true` 12회.
- M1 핵심 폐루프는 성공했지만 거의 매 수신 frame에서 30cm threshold를 넘는 새로운 품질 문제가 드러났다.

정정:

- 최초 첨부 로그의 trigger 로그 0회만으로 `CompareData` 호출 자체가 0회라고 단정한 표현은 증거가 부족했다. M1에서 정상 MATCH도 로깅하도록 바꿔 호출 여부를 직접 확인했다.

### 2026-07-13 — M2/M3 clock·static config·thread safety 1차 수정

관찰 사실:

- 정상 simulation 파라미터는 `Radius=300 / Multiplier=0.10 / Damp=1000`이었다.
- resim에서는 `FNetInputShip` 생성자 기본값인 `100 / 1.20 / 3`으로 바뀌었다.
- 이유는 static config가 network input struct에 섞여 있지만 custom serializer는 조종 입력만 전송했기 때문이다.
- server time은 과거 고정 1/60 계산에서 Step 60에 `1.4153`, client는 `1.0000`이었다.
- 실제 headless solver dt는 `0.033333335`로 1/30이었다.

변경:

- `FNetInputShip`을 frame-tagged movement/steering 전용 payload로 축소했다.
- pontoon/wave/static config를 `FAsyncInputShip` producer/consumer 경로로만 전달한다.
- `SetBuoyancyStaticData_External`과 Tick의 direct GT→PT cache mutation을 제거했다.
- authority가 `ServerPhysicsTimeOrigin = ServerWorldTime - UpcomingServerFrame × SolverDt`를 한 번 계산하고 origin/dt를 복제한다.
- PT는 prepared state에서 `ServerFrame - LocalFrame` tick offset을 얻는다.
- normal/resim 모두 `SimTime = Origin + ServerFrame × SolverDt`를 사용한다.
- pontoon, wave, clock, frame offset이 모두 준비되기 전 custom force를 적용하지 않는다.
- Chaos built-in gravity만 사용하고 manual `Mass × Gravity` force를 제거했다.

검증:

- UHT/C++/link 재성공.
- 동일 조건의 8초 late-join headless run 재실행.
- 로그: `Saved/Logs/Codex_M2_Server.log`, `Saved/Logs/Codex_M2_Client.log`.
- server clock: origin `0.433568805`, solver dt `0.033333335`.
- client local step 120, offset 204는 server frame 324와 time `11.233569`로 계산됐다.
- server frame 300의 양쪽 시간식은 `0.433568805 + 300 × 0.033333335 = 10.433569`로 동일하다.
- resim input 적용 로그에서 cache가 `Waves=16 / Pontoons=4 / Radius=300`으로 유지됐다.

A/B 결과:

| 지표 | M1 | M2 |
|---|---:|---:|
| Compare 진단 수 | 83 | 10 |
| 초기 2개 제외 표본 | 81 | 8 |
| 초기 2개 제외 `>30cm` | 77 | 4 |
| 초기 2개 제외 평균 기록 오차 | `37.98cm` | `19.95cm` |
| 초기 2개 제외 최대 기록 오차 | `77.00cm` | `30.38cm` |
| ApplyState | 78 | 5 |
| network/ensure/fatal error | 0 | 0 |

결론:

- frame identity, control/static separation, server-frame clock이 모두 실제 개선에 기여했다.
- M1과 M2의 실행 시간 및 로그 출력 표본 수가 완전히 같지는 않으므로 이 표는 성능 benchmark가 아니라 구조 결함 제거의 강한 회귀 증거다.
- 핵심 loop는 작동하지만 최종 목표 달성은 아니다. 남은 오차는 대부분 threshold 바로 위이며 초기 late-join correction은 약 9m로 매우 크다.

다음 작업:

1. 동일 server frame 기준 wave sample과 force contribution을 role별로 기록한다.
2. 첫 authoritative state 전 client physics/render warm start 정책을 정한다.
3. 위치뿐 아니라 선속도·각속도 오차가 다음 threshold crossing을 예측하는지 측정한다.
4. 60초 late join 및 packet lag/loss 테스트를 추가한다.
5. full Gerstner, Water Body base Z/attenuation, ripple authority를 M4에서 정리한다.

### 2026-07-13 — M3 동일 ServerFrame 결정론 진단

목표:

- “시간/파도 위상이 달라서 상태가 벌어지는가”와 “상태가 먼저 벌어진 뒤 다른 좌표의 파도를 샘플링하는가”를 분리한다.

변경:

- 30 server frame마다 `[NETPHYS-DET]`를 기록한다.
- `ServerFrame`, `LocalFrame`, resim 여부, double `SimTime`, `X/R/V/W`, 첫 폰툰 위치·파고·깊이·힘, 총 부력/토크, 측면 항력, 조종력/토크를 한 줄에 기록한다.

검증:

- 로그: `Saved/Logs/Codex_M3_Determinism_Server.log`, `Saved/Logs/Codex_M3_Determinism_Client.log`.
- 모든 common server frame의 `TimeDiff=0`.
- 상태가 거의 같은 SF630에서 위치 차이 `0.000264 cm`, 속도 차이 `0.000107 cm/s`, 첫 폰툰 위치 차이 `0.00245 cm`, wave 차이 `0.000221 cm`.

결론:

- 현재 반복 divergence의 직접 원인은 서버/클라이언트 물리 clock 위상 차이가 아니다.
- 같은 frame·같은 상태라면 custom wave와 force는 사실상 동일하다.
- 상태가 먼저 달라지고, 그 결과 서로 다른 좌표에서 wave/force가 달라지며 비선형적으로 증폭된다.

### 2026-07-13 — M4/M5 속도 임계값 실험 기각

가설:

- 위치가 30 cm를 넘기 전에 선속도/각속도 차이로 조기 보정하면 수렴이 좋아질 수 있다.

실험:

- M4: `30 cm/s / 2 deg/s`.
- M5: `200 cm/s / 15 deg/s`.
- 로그: `Codex_M4_Velocity_*`, `Codex_M5_TunedVelocity_*`.

결과:

- M4는 trigger 238회, Apply 230회, 그중 velocity-only 224회로 correction storm 발생.
- M5도 trigger 72회, Apply 73회, velocity-only 34회로 위치/회전 전용 기준선보다 나빴다.
- M5에서 Apply 직후 같은 SF570의 client DET가 server와 비트 수준으로 일치했다. `ApplyState` 자체가 위치를 못 쓰는 문제가 아니었다.

해석:

- 한 correction 뒤에도 이미 전송 중인 연속 권위 frame은 correction 전의 stale predicted history와 비교될 수 있다.
- 이 구간에서 velocity threshold를 켜면 같은 사건을 여러 번 correction하는 폭주가 생긴다.
- 따라서 속도 차이는 진단값으로만 남기고 rollback trigger에서는 제거했다.

### 2026-07-13 — M6 UE 5.7 실제 60 Hz fixed step 복구

관찰 사실:

- Config에 `AsyncPhysicsTickHz=60`이 있었지만 UE 5.7 `UPhysicsSettings`에는 이 property가 없다.
- 실제 M1~M5 runtime은 `TickRate=30`, solver dt `0.033333335`였다.

변경:

- `AsyncPhysicsTickHz=60.000000`을 `AsyncFixedTimeStepSize=0.016666667`로 교체했다.
- Physics History Capture 활성 상태를 유지했다.

검증:

- 로그: `Saved/Logs/Codex_M6_60Hz_Server.log`, `Saved/Logs/Codex_M6_60Hz_Client.log`.
- server/client history `TickRate=60`, history size 64.
- replicated clock step `0.016666668`.
- 27개 common DET frame 모두 `TimeDiff=0`.
- SF1080: 위치 차이 `0.000375 cm`, 속도 차이 `0.0000739 cm/s`, wave 차이 `0.000069 cm`.
- SF1110도 위치 차이 약 `0.0005 cm`로 사실상 동일했다.
- 이후 SF1140 `6.86 cm`, SF1170 `30.72 cm`로 다시 벌어졌다.
- network/ensure/fatal error 0.

결론:

- 60 Hz 설정 수정은 프로젝트 의도와 runtime을 일치시키는 correctness fix다.
- 정확히 정렬된 뒤 0.5~1초 동안 비선형 부력 운동에서 다시 벌어지는 것이 현재 현상이다.

### 2026-07-13 — M7/M8 부력 상한과 pontoon coefficient 분리 실험

엔진 대조:

- Epic Water plugin은 폰툰 raw force를 `0..MaxBuoyantForce`로 clamp한 뒤 정규화 `PontoonCoefficient`를 곱한다.
- 감쇠식은 현재 custom 코드와 동일한 단방향 식이므로 감쇠 부호 가설은 기각했다.

M7 변경/결과:

- `MaxBuoyantForce`와 `PontoonCoefficient`를 모두 적용.
- BP runtime 값은 radius 300, buoyancy coefficient 0.1, damp 1000, damp2 1, max force 5M.
- 정규화 coefficient 합이 1이므로 총 부력이 최대 5M에 묶였다.
- 배가 수면 근처 대신 약 `Z=-1,500 cm`까지 가라앉음.
- 이 커스텀 “네 개 구체 체적 합산” 모델에는 Water plugin의 정규화 coefficient 의미가 맞지 않아 기각했다.

M8 변경/결과:

- 정규화 coefficient는 제거하고 각 물리 폰툰을 독립적으로 `0..5M` clamp.
- 로그: `Saved/Logs/Codex_M8_PerPontoonClamp_Server.log`, `Saved/Logs/Codex_M8_PerPontoonClamp_Client.log`.
- 서버 마지막 Z `-112.69 cm`로 부유 상태 유지.
- 총 부력 최대 M6 약 `39.5M`에서 M8 `20.0M`으로 제한.
- 초기 보정 뒤 common-frame 위치 차이는 대체로 `0.94~32.48 cm`.
- mismatch 로그의 보정 후 최대는 `35.88 cm`였고 network/ensure/fatal error 0.

주의:

- `CompareData` 로그는 모든 mismatch와 60 frame 주기 match만 기록한다. 따라서 M6의 33줄과 M8의 11줄을 전체 compare 호출 수로 직접 비교하면 안 된다.
- M8은 force sensitivity를 낮춘 강한 증거지만, 여러 반복 run/장시간 수용 테스트 전에는 통계적 성능 확정으로 표현하지 않는다.

### 2026-07-13 — M9/M10 5 cm 정책과 완전 transform 복원

M9 위치 임계값:

- Blueprint 저장값 30 cm는 폰툰 디버그에서 눈에 띄는 분리까지 정상 MATCH로 허용한다.
- custom payload에 전달하는 effective threshold를 프로젝트 CVar와 같은 최대 5 cm로 제한했다.
- 로그: `Saved/Logs/Codex_M9_5cmThreshold_Server.log`, `Saved/Logs/Codex_M9_5cmThreshold_Client.log`.
- 초기 late-join mismatch 뒤 33개 trigger의 최대 `8.76 cm`, 평균 `6.34 cm`.
- 약 15초에 Apply 34회로 초당 약 2.3회 correction. velocity threshold 실험의 230회 폭주보다는 낮지만 비용은 명확하다.

M10 완전 transform:

- Chaos의 완전 transform setter가 `X/R`과 함께 적분 예측 상태 `P/Q`를 맞추는 점을 반영해 `ApplyState_Internal`에서 `X/R/P/Q/V/W`를 복원한다.
- 로그: `Saved/Logs/Codex_M10_FullTransform_Server.log`, `Saved/Logs/Codex_M10_FullTransform_Client.log`.
- 보정 후 trigger 최대 `8.56 cm`, 평균 `6.13 cm`, Apply 33회, error 0.
- 주기 comparison SF540 `1.48 cm`, SF600 `1.09 cm`로 MATCH 확인.

현재 채택 상태:

- 60 Hz `AsyncFixedTimeStepSize` 유지.
- 폰툰별 5M clamp 유지, 정규화 coefficient 미적용.
- 위치 effective threshold 최대 5 cm 유지.
- velocity는 비교 로그에만 남기고 trigger에는 사용하지 않음.
- ApplyState에서 `X/R/P/Q/V/W` 복원.

다음 작업:

1. late join 첫 권위 state 전 proxy의 warm-start/표시 정책을 정해 초기 8~9m correction을 제거한다.
2. 실제 PIE에서 빨강/초록 최신 snapshot 시각화와 동일-frame history 오차를 구분해 확인한다.
3. 60초 late join, 다중 배, Single Process PIE, packet lag/jitter/loss/reorder를 자동화한다.
4. possessed autonomous proxy의 조종 input replay와 RPC 중복 경로를 검증한다.
5. full Gerstner, Water Body 기준 Z/attenuation/LWC, ripple authority 정책을 완성한다.

### 2026-07-13 — M11~M14 late-join warm-start 경계 실험

목표:

- late join client가 레벨에 저장된 초기 transform에서 수 초 전부터 움직여 온 서버 배로 시작하며 만드는 큰 첫 correction을 줄인다.
- 별도 actor snapshot으로 Network Physics history를 안전하게 warm-start할 수 있는지 검증한다.

#### M11 — GT one-shot teleport + simulation barrier — 기각

변경:

- 진단용 `FShipReplicatedState`에 위치·회전·선속도·각속도·`ServerFrame`을 넣고 RepNotify로 받았다.
- client gravity/custom simulation을 일시 중단한 뒤 `UPrimitiveComponent`의 teleport와 velocity setter로 한 번 시드했다.

관찰:

- initial snapshot은 `ServerFrame=137`이었지만 첫 Network Physics state는 `ServerFrame=311`이었다. client map loading 중 initial actor bunch가 약 174 frame 낡았다.
- GT 로그에서는 `Z=-616.627`로 시드했지만 첫 PT `ApplyState` 직전 particle은 레벨 transform `Z=800`이었다. GT setter가 rewind history의 대응 particle state를 초기화하지 못했다.
- M10 대비 첫 두 mismatch가 `855.82/5.46 cm → 11291.22/226.08 cm`, 안정 구간 평균이 `6.13 → 8.21 cm`로 악화됐다.

결론:

- 초기 actor property는 물리 history용 authoritative state가 아니며, client simulation을 멈추면 첫 history가 유효 particle state 없이 만들어질 수 있다.
- M11 변경은 제거했다.

로그:

- `Saved/Logs/Codex_M11_WarmStart_Server.log`
- `Saved/Logs/Codex_M11_WarmStart_Client.log`

#### M12 — stale snapshot reject + PT seed guard — 발동 불가, 기각

변경:

- 중력/시뮬레이션 barrier를 제거해 M10 동작을 유지했다.
- snapshot이 현재 Network Physics frame에 충분히 가까운 경우에만 async input으로 넘기고, Chaos 내부 particle의 `X/R/P/Q/V/W`를 PT에서 한 번 시드하도록 했다.
- `ApplyState_Internal`이 먼저 실행됐으면 seed가 덮어쓰지 못하게 guard했다.

관찰:

- 첫 Network Physics state 전 `GetUpcomingServerFrame_External`은 client에서 `1`을 반환해 usable server mapping이 아니었다.
- mapping 이후 actor transform update는 약 30 server frame 간격으로 보였고 도착 시점 나이는 약 9~30 frame이었다. 6-frame freshness 조건에서는 seed가 한 번도 적용되지 않았다.
- guard는 정상 작동해 Network Physics correction 뒤 상태를 덮어쓰지 않았다.
- 첫 두 mismatch `11318.66/12.60 cm`, 안정 구간 평균 `9.40 cm`, 최대 `21.04 cm`였다.

결론:

- 별도 actor replication의 cadence와 Network Physics frame mapping 준비 시점이 history warm-start 요구와 맞지 않는다.
- M12 변경은 제거했다.

로그:

- `Saved/Logs/Codex_M12_PTSeed_Server.log`
- `Saved/Logs/Codex_M12_PTSeed_Client.log`

#### M13 — replicated absolute clock 기반 PT seed — 시간축 불일치로 기각

변경:

- 첫 Network Physics packet 전에는 `ServerPhysicsTimeOrigin + SolverDt + GameState server time`으로 snapshot 나이를 추정했다.
- 최대 60-frame snapshot을 허용해 callback 시작 직후 PT seed가 실제 발동하도록 했다.

관찰:

- `SnapshotSF=420`, 당시 engine estimate `SF=452`, 즉 32-frame 과거 상태가 현재 particle에 적용됐다.
- 곧 첫 authoritative `SF=424 → LocalFrame=0` rewind가 실행되면서 current particle은 다시 레벨 transform `Z=800`인 history로 돌아갔다.
- 이후 `SF=502`에서 `140.47 cm`, `4149.51 cm/s`의 큰 2차 오차가 발생했다.
- 총 mismatch 79회, 안정 구간 평균 `17.43 cm`; M10보다 명백히 나빴다.

결론:

- snapshot이 신선한가보다 더 중요한 것은 `ServerFrame=N`의 상태를 대응 `LocalFrame` history slot에 쓰는 것이다.
- 과거 상태를 현재 frame particle에 적용하면 fast-forward가 없으므로 결정론적 초기화가 아니라 temporal corruption이다.
- M13 변경은 제거했다.

로그:

- `Saved/Logs/Codex_M13_ClockSeed_Server.log`
- `Saved/Logs/Codex_M13_ClockSeed_Client.log`

#### M14 — M10 계열 기준선 복귀 — 검증 완료

변경:

- M11~M13의 RepNotify, velocity/frame actor snapshot, gravity barrier, PT seed 필드와 적용 코드를 모두 제거했다.
- M10의 `X/R/P/Q/V/W`, 5 cm effective threshold, 60 Hz clock, 폰툰별 5M clamp만 유지했다.

검증:

- 전체 Editor rebuild 성공.
- 첫 실행은 코드 문제가 아니라 Windows Smart App Control 정책이 unsigned `UnrealEditor-WaterAndShip.dll`을 오류 4551로 차단해 무효였다. Code Integrity event 3077/3033과 정책 ID를 확인했다.
- 명시적으로 승인된 전체 rebuild 뒤 같은 M14 run을 다시 실행해 모듈 로드와 맵 진입을 확인했다.
- client `CreateDataHistory` 등록 1회, state Load 진단 8회, network/ensure/fatal/module-load error 0.
- 첫 두 mismatch `396.24 cm`, `364.33 cm`; 이후 안정 구간 최대 `10.89 cm`, 평균 `7.00 cm`.
- mismatch 37회, `ApplyState` 36회. 첫 mismatch는 즉시 Apply되지 않고 다음 권위 상태에서 correction됐으며, 이후 정상 rewind loop가 유지됐다.

로그:

- `Saved/Logs/Codex_M14_RevertedBaseline_Server.log`
- `Saved/Logs/Codex_M14_RevertedBaseline_Client.log`

M11~M14 종합 인사이트:

1. late join 초기 오차는 단순히 “서버 transform을 늦게 받음”이 아니라 callback/history 생성과 첫 server-frame mapping의 경계 문제다.
2. `FShipReplicatedState`의 빨강/초록 latest snapshot은 frame identity가 없고 낮은 빈도로 오므로 history seed로 사용할 수 없다.
3. GT component teleport는 Chaos rewind history를 초기화하지 않는다.
4. PT particle 직접 설정도 snapshot frame과 현재 local frame이 다르면 잘못된 초기화다.
5. 올바른 해법은 엔진의 첫 replicated physics state가 대응되는 local history slot을 확정한 뒤 그 동일 frame 상태를 기록하거나, 엔진이 지원하는 초기 state application 경계를 사용하는 것이다.
6. 다음 실험은 상태를 다시 주입하는 것이 아니라 first packet 수신과 history slot 생성 순서를 계측해야 한다.

### 2026-07-13 — M15/M16 startup history와 frame-offset readiness 계측

#### M15A/M15B — first packet/history 순서 계측 — 진단 완료

방법:

- 동작을 바꾸지 않고 첫 40 local physics step의 callback, particle, state build를 기록했다.
- client가 수신하는 모든 authority state frame을 일시적으로 기록했다.
- 같은 8초 late-join 조건을 두 번 반복했다.

공통 관찰:

- `CreateDataHistory` 뒤 실제 PT `OnPostInitialize_Internal`이 시작되기 전에 authority state collection이 대량 수신됐다.
- M15A는 callback 전 48개(`SF160~326`), M15B는 44개(`SF305~463`)가 로드됐다.
- 그럼에도 client history는 callback 시작 시 레벨 transform `X=11220, Y=1280, Z=800`에서 `Step=0, ServerFrame=0, LocalFrame=0`으로 만들어졌다.
- `BuildState_Internal`이 매번 `State.ServerFrame - State.LocalFrame`을 cache하므로 startup의 `0-0=0`을 실제 Network Physics offset으로 오인했다.
- 실제 late-join mapping은 M15A `LocalFrame=25, offset=333`, M15B `LocalFrame=28, offset=470`에서 뒤늦게 나타났다.
- 따라서 첫 25~28 step 동안 custom wave/force clock은 실제 서버 frame이 아니라 client `SF0~27`을 사용했다.

수치:

| Run | 첫 mismatch | 첫 mapping | callback 전 state | 안정 구간 최대 | 안정 구간 평균 | 오류 |
|---|---:|---:|---:|---:|---:|---:|
| M15A | `870.85 cm` | `LF25 / offset333` | 48 | `14.49 cm` | `8.13 cm` | 0 |
| M15B | `732.59 cm` | `LF28 / offset470` | 44 | `9.85 cm` | `6.29 cm` | 0 |

추가 관찰:

- M15B 첫 비교는 `AuthSF495/AuthLF25`와 `PredSF25/PredLF25`였다. LocalFrame slot은 맞지만 mapping 확정 전 만들어진 predicted payload의 `ServerFrame` label은 과거 local 값이었다.
- 첫 state backlog가 이미 도착했다는 사실만으로 PT history와 tick offset이 초기화되지는 않는다.

로그:

- `Saved/Logs/Codex_M15A_StartupOrder_Server.log`
- `Saved/Logs/Codex_M15A_StartupOrder_Client.log`
- `Saved/Logs/Codex_M15B_StartupOrder_Server.log`
- `Saved/Logs/Codex_M15B_StartupOrder_Client.log`

#### M16 — external upcoming frame 기반 zero-offset gate — 기각

변경:

- authority 여부와 GT의 `GetUpcomingServerFrame_External` 값을 PT로 전달했다.
- client에서는 non-zero candidate offset 또는 external frame으로 zero offset이 확인될 때까지 custom wave/force readiness를 막으려 했다.

결과:

- UE startup placeholder가 고정 `1`이 아니라 `2`, 이후 `26`처럼 local 진행에 따라 변했다.
- `ExternalSF=2`와 startup `State.ServerFrame=2`가 같아 zero offset이 실제로 확인된 것으로 잘못 판정됐고, Step 3부터 다시 `Ready=1`이 됐다.
- 실제 mapping은 `LocalFrame=28`, `offset=384`에서 나타났다.
- 첫 mismatch `860.61 cm`, mismatch 68회, Apply 67회, 안정 구간 최대 `25.50 cm`, 평균 `8.68 cm`, 오류 0으로 기준선보다 악화됐다.

결론:

- `GetUpcomingServerFrame_External`의 작은 양수만으로 “authority mapping 준비”를 판정할 수 없다.
- M16 gate와 M15 과다 계측 로그는 모두 제거했다.
- 최종 소스는 M14/M10 채택 상태로 복귀했다: 60 Hz, server-frame clock, 5 cm effective 위치 threshold, 폰툰별 5M clamp, `X/R/P/Q/V/W` correction.
- 미해결 핵심은 **수신 state backlog를 callback/history의 대응 local frame에 언제 어떻게 결합하는가**이다. 이번 작업에서는 추가 구현을 중단하고 증거와 실패 경로만 보존한다.

로그:

- `Saved/Logs/Codex_M16_FrameGate_Server.log`
- `Saved/Logs/Codex_M16_FrameGate_Client.log`

### 2026-07-13 — M17 정확한 Tick Offset 준비 플래그 및 에디터 튜닝 인계

#### M17 변경 상태

- M15/M16에서 확인한 startup placeholder 문제를 피하기 위해 `GetUpcomingServerFrame_External()`의 숫자를 준비 여부로 사용하지 않도록 변경했다.
- 클라이언트는 UE 5.7의 `APlayerController::GetNetworkPhysicsTickOffsetAssigned()`가 true가 된 뒤에만 실제 `NetworkPhysicsTickOffset`을 GT에서 PT로 전달한다.
- 서버는 offset 0을 즉시 유효한 값으로 취급한다.
- `BuildState_Internal()`이 `State.ServerFrame - State.LocalFrame`으로 offset을 추측하여 startup의 `0 - 0 = 0`을 확정하던 부작용을 제거했다.
- Editor Development 빌드는 성공했다. 엔진 API deprecation warning 외 컴파일 오류는 없었다.

#### M17 런타임 첨부 로그가 새로 보여 준 미해결점

- 사용자가 제공한 PIE 로그에서 authority state 자체는 `ServerFrame=986`, `LocalFrame=900`, 즉 실제 offset 86으로 올바르게 매핑되었다.
- 그러나 같은 과거 Step 900을 재시뮬레이션할 때 custom callback은 다음을 출력했다.

```text
[PHYSICS-PT-WAITING] Step=900 Pontoons=4 Waves=16 Clock=Ready Dt=0.016667 FrameOffset=Missing
```

- 이 경로에서 `bStaticDataReady=false`가 되어 그 resim step의 커스텀 파도·부력·조종 힘을 전부 건너뛴다.
- forward simulation에서 offset을 알고 있더라도 rewind된 callback 시점에는 custom readiness/cache가 동일하게 복원되지 않거나, resim 중에는 GT async input을 소비하지 않기 때문에 다시 채울 수 없는 것으로 보인다.
- 따라서 M17은 startup의 거짓 offset 0 판정을 제거했지만, **resim에서 필요한 결정론적 설정과 frame mapping을 history-safe하게 보존한다는 조건은 아직 충족하지 못했다.** M17은 빌드 성공 상태이지만 최종 채택 검증 완료 상태가 아니다.

첨부 로그의 추가 관찰:

- 위치 mismatch가 5~34 cm 범위로 연속 발생하며 5 cm threshold 때문에 반복 rollback한다.
- 순간 선속도 차이는 `1076.77 cm/s`, 각속도 차이는 `24.682 deg/s`까지 증가했다.
- 서버/클라이언트 상태의 rotation 차이는 대부분 5도 threshold 아래였고, 주 trigger는 위치 오차였다.
- GT 최신 snapshot 비교에서는 `LocDiff=92.39 cm`도 보였지만 이것은 동일-frame history 비교가 아니므로 Network Physics mismatch 수치와 분리해서 해석해야 한다.

#### KKH_Test 시작 충돌에 대한 해석

- 지금까지의 headless runtime 검증은 `/Game/New/Level/KKH_Test`에서 수행했다.
- 이 레벨에서는 시작 직후 Player가 배 위로 떨어져 배를 물속으로 강하게 누르는 이벤트가 있다.
- 그러므로 초기 수백 cm mismatch에는 late join/history 문제뿐 아니라 Player-Ship 충돌 시점과 충격량 차이가 섞였을 가능성이 높다.
- Network Physics 자체의 초기 안정성을 검증할 때는 Player가 배를 타격하지 않는 빈 배 조건을 별도 기준선으로 사용해야 한다.

#### 에디터 물성 튜닝 규칙

현재 custom PT 부력 코드에서 실제 사용하는 에디터 값:

- `BuoyancyRoot`의 Chaos Mass 및 관성
- 첫 번째 Pontoon의 Radius — 현재 이 하나의 반경을 모든 custom pontoon에 공통 사용
- `BuoyancyCoefficient`
- `BuoyancyDamp`
- `BuoyancyDamp2`
- `MaxBuoyantForce` — custom 코드에서는 각 pontoon마다 clamp
- Ship의 `LateralDragCoefficient`

주의:

- 내장 `UBuoyancyComponent`는 BeginPlay에서 비활성화되므로, 위에서 custom 코드가 읽는 값 이외의 내장 컴포넌트 옵션은 실제 부력에 반영되지 않을 수 있다.
- BP 기본값을 바꾸어 서버와 모든 클라이언트가 동일한 asset 값을 읽는 튜닝은 가능하다.
- 런타임에 한 피어에서만 값을 변경하면 static/config가 Network Physics history payload로 복제되지 않으므로 결정론이 깨질 수 있다.
- 권장 순서는 `Mass → BuoyancyCoefficient/MaxBuoyantForce → Damp/Damp2 → LateralDrag`이다.
- Player 타격이 없는 정지 조건에서 흘수와 복원력을 먼저 맞춘 다음 승선/낙하 충돌을 추가한다.

### 2026-07-13 — 바다 상호작용 승선 직후 Ship 대형 요동 원인 분석 (코드 변경 없음)

#### 승선 경로

`HandlePortSeaBoarding()`과 `HandleStarboardSeaBoarding()`은 서버에서 다음 순서로 실행한다.

1. CharacterMovement를 즉시 `MOVE_Walking`으로 변경한다.
2. Character Actor origin을 `Port/StarboardSeaBoardingDestination`의 월드 좌표로 그대로 `TeleportTo`한다.
3. 이 경로에서는 Player collision을 끄거나 Ship과의 충돌을 무시하지 않는다.

이것은 Ship을 possess하는 `AShip::Board()`와 다른 경로다. `Board()`는 collision과 CharacterMovement를 끄지만, 바다에서 갑판으로 올라오는 두 Handle 함수는 그렇지 않다.

#### 원인 후보 우선순위

1. **Teleport 목적지와 Character Capsule의 초기 관통 — 가장 먼저 검사**
   - 목적지 SceneComponent가 갑판 표면에 놓여 있고 Character origin 기준 높이 보정이 없다면, capsule 중심이 갑판 표면으로 이동하여 capsule 하부가 배 collision 안에 박힌다.
   - Walking 전환까지 먼저 수행하므로 첫 floor update/Chaos contact에서 큰 depenetration과 off-center torque가 발생할 수 있다.

2. **CharacterMovement의 StandingDownwardForce — 엔진 소스로 확인된 외력 경로**
   - `ABasePlayer`는 `bEnablePhysicsInteraction=false`, `InitialPushForceFactor=0`, `PushForceFactor=0`으로 설정한다.
   - 하지만 UE 5.7 `UCharacterMovementComponent::ApplyDownwardForce()`는 `bEnablePhysicsInteraction`을 확인하지 않는다.
   - `StandingDownwardForceScale != 0`이고 현재 floor가 물리 시뮬레이션 중이면 다음 힘을 floor impact point에 매 틱 가한다.

```text
Gravity × CharacterMovement.Mass × StandingDownwardForceScale
```

   - 이 힘은 갑판 중앙이 아닌 발 위치에 적용되어 roll/pitch torque를 만들 수 있다.
   - 서버 CharacterMovement와 클라이언트 예측 CharacterMovement의 접촉/floor 갱신은 Ship custom Network Physics input history에 들어 있지 않으므로, 같은 과거 프레임을 resim할 때 동일하게 재생된다는 보장이 없다.

3. **외부 충돌 뒤 rollback에서 custom 부력 누락 — 첨부 로그로 확인**
   - Player 접촉이 5 cm 초과 오차를 만들면 Ship은 rollback한다.
   - 첨부 로그에서는 rollback Step 900에서 `FrameOffset=Missing`으로 custom 부력이 빠진다.
   - 따라서 `Player 접촉/Teleport 충격 → mismatch → rollback → resim 부력 누락 → 새로운 mismatch`의 증폭 루프가 가능하다.

4. **현재 부력값이 작은 자세 변화에 큰 복원 토크를 만드는 증폭 조건**
   - 첨부 시점 파라미터는 Radius 300, Coefficient 0.04, Damp 1000, Damp2 1, pontoon별 MaxForce 5M이었다.
   - 로그의 총 부력은 약 6.7~6.9M, 부력 토크는 약 1.7B~3.4B 단위까지 나타났다.
   - 이 값 자체가 원인이라고 확정할 수는 없지만, capsule 관통이나 off-center downward force로 시작된 작은 roll/pitch를 매우 큰 반대 토크로 되받아 요동을 확대할 수 있다.

#### 코드 작성 전 원인 분리 실험 순서

1. `PortSeaBoardingDestination`/`StarboardSeaBoardingDestination`을 capsule half-height보다 충분히 높은 위치로 임시 이동한다. 요동이 사라지면 초기 관통이 주원인이다.
2. Player CharacterMovement의 `Standing Downward Force Scale`만 임시로 0으로 설정한다. 기존 `Enable Physics Interaction=false`만으로는 이 힘이 차단되지 않는다.
3. 빈 배 위에 Player를 처음부터 안전한 높이로 배치한 경우와 바다 상호작용 Teleport를 비교한다. 전자는 안정적이고 후자만 요동하면 승선 전환/관통 문제다.
4. Standalone 또는 서버 단독에서도 같은 요동인지 확인한다.
   - Standalone에서도 발생: 관통, 물성, 부력 복원 토크가 주원인.
   - 네트워크에서만 심함: Character 외력이 Ship history에 없고 rollback에서 부력이 누락되는 문제가 주원인.
5. 진단 목적으로만 위치 rollback threshold를 크게 올려 correction을 잠시 줄여 본다.
   - 물리 배 자체는 차분해지고 화면 요동만 사라지면 correction loop 증폭이다.
   - 배가 계속 실제로 크게 흔들리면 물리 접촉/부력 튜닝 문제다.
   - 이 실험값은 최종 설정으로 채택하지 않는다.
6. PIE에서 collision visualization으로 승선 직후 capsule이 갑판 mesh 내부에 생성되는지, Character의 Movement Base가 `BuoyancyRoot`인지 확인한다.

현재 가장 효율적인 첫 실험은 **Destination을 위로 올리는 것**과 **StandingDownwardForceScale을 0으로 만드는 것**을 각각 독립적으로 비교하는 것이다.

### 2026-07-13 — 승선 요동 2차 실험: 네트워크 전용 physics-base feedback으로 범위 축소

#### 사용자가 수행한 반증 실험

1. 승선 Destination은 갑판보다 위에 있으며, Player가 잠깐 낙하한 뒤 발이 닿는 순간부터 요동한다.
   - capsule이 처음부터 갑판 안에 생성되는 단순 teleport penetration 가설은 우선순위가 크게 낮아졌다.
   - 단, 낙하 접촉 자체의 서버/클라이언트 시점 차이는 여전히 남는다.
2. Player BP에서 `Standing Downward Force Scale=0`으로 만든 뒤 `Enable Physics Interaction`을 다시 비활성화했지만 네트워크 PIE 요동은 동일했다.
3. Standalone에서는 Player가 갑판 위에 올라가 걷거나 좌우 난간에 충돌해도 정상이다.
   - 난간 충돌 시 배가 물리적으로 조금 기울기는 하지만 폭주하지 않는다.
   - 동일한 Mass/부력/충돌 형상으로 Standalone이 안정적이므로, 일반 부력 튜닝이나 Player 접촉 자체가 단독 주원인일 가능성은 낮다.

#### 이전 분석 정정 — 기록은 유지하고 이 항목으로 대체

이전 항목에서는 UE 5.7 `ApplyDownwardForce()` 함수 내부가 `bEnablePhysicsInteraction`을 검사하지 않는다는 점을 근거로 StandingDownwardForce를 1순위 후보로 두었다. 함수 내부만 보면 사실이지만, 호출부를 추가 확인한 결과 실제 실행은 다음 상위 조건 안에 있다.

```cpp
if (bEnablePhysicsInteraction)
{
    ApplyDownwardForce(DeltaTime);
    ApplyRepulsionForce(DeltaTime);
}
```

따라서 현재 Player BP처럼 `Enable Physics Interaction=false`이면 StandingDownwardForce와 RepulsionForce는 호출되지 않는다. 사용자의 Scale 0 실험도 같은 결론을 실측으로 확인했다. **명시적 CharacterMovement physics force는 이번 현상의 주원인에서 제외한다.**

다만 `Enable Physics Interaction=false`는 CharacterMovement가 직접 호출하는 push/downward/repulsion force만 막는다. Character capsule과 `BuoyancyRoot`가 서로 Block인 상태에서 Chaos collision solver가 만드는 비관통 접촉과, 움직이는 base를 따라가기 위한 based movement sweep까지 제거하지는 않는다.

#### 첨부 Client PIE 로그 정량 분석

- same-frame Network Physics compare 73회를 추출했다.
- 전체 평균 위치 오차 `6.54 cm`, 최대 `17.06 cm`.
- `SF < 504`: 평균 `6.09 cm`, 최대 `8.63 cm`, 최대 각속도 차이 `2.139 deg/s`.
- `SF >= 504`: 평균 `8.45 cm`, 최대 `17.06 cm`, 최대 각속도 차이 `5.127 deg/s`.
- SF 504 부근부터 서버 Ship에 기존에 없던 수평 속도와 각운동 변화가 나타나고 이후 위치/회전 오차가 커진다. Player 접촉 시점일 가능성은 있지만, 제공 로그에는 성공한 `HandlePort/StarboardSeaBoarding` 또는 contact marker가 없으므로 정확한 발 접촉 프레임으로 단정하지 않는다.
- excerpt 안의 interaction 로그 두 건은 모두 `WaterBodyOcean_0`을 hit하고 실패한 요청이므로 승선 성공 표식으로 사용할 수 없다.
- Ship은 계속 `ROLE_SimulatedProxy`, `ReplicateMovement=FALSE`였다.
- resim 중 `FrameOffset=Missing`이 8회 반복됐다. Step 240/300/360/420 등에서 재현되어 일회성 startup 로그가 아니라 반복되는 rollback 경로 문제임을 보여 준다.

#### 현재 최우선 인과 모델

1. 서버에서는 서버 Character가 서버 Ship 위에 착지하여 physics base를 잡는다.
2. Client 1에서는 autonomous Character가 로컬 예측된 simulated-proxy Ship 위에 착지하여 별도의 시점과 위치에서 base를 잡는다.
3. CharacterMovement는 PrePhysics에서 움직이는 base의 변화를 따라 Character capsule을 이동시킨다.
4. 첨부 이미지에서 `Based Movement Ignore Physics Base`는 꺼져 있다. 따라서 physics base를 따라 capsule을 옮기는 sweep가 현재 base actor 자체와 충돌할 수 있다.
5. Character와 Ship 사이의 contact/depenetration 결과는 서버와 클라이언트에서 서로 다를 수 있지만, `FNetInputShip` history에는 Move/Steer만 있고 Player 접촉 상태나 접촉 impulse는 없다.
6. Ship state가 5 cm threshold를 넘으면 client가 rollback한다.
7. rollback 시 CharacterMovement는 Ship과 동일한 Network Physics history로 과거 프레임부터 재생되지 않으며, 현재 M17 callback은 일부 resim step에서 `FrameOffset=Missing`으로 custom 부력까지 건너뛴다.
8. 결과적으로 `서로 다른 Player contact → Ship mismatch → 불완전한 rollback → 새로운 mismatch`가 화면상 큰 요동으로 증폭될 수 있다.

Standalone에서는 서버/클라이언트 두 세계와 correction이 없으므로 동일 접촉이 단순한 한 번의 물리 반응으로 끝난다. 이것이 Standalone 정상 / Client PIE 폭주의 차이를 가장 잘 설명한다.

#### 다음 실험 — 코드 작성 없이 우선 수행

1. **Player BP의 `Based Movement Ignore Physics Base`를 true로 변경한다.**
   - UE 설명상 simulated physics base를 따라 `UpdateBasedMovement()`가 Character를 이동시킬 때 current base actor와의 충돌을 무시하는 전용 옵션이다.
   - 바닥 판정을 없애는 옵션이 아니라 base-follow 이동 중 자기 base와 다시 부딪히는 경로를 막는 옵션이므로 현재 현상에 가장 직접적인 실험이다.
   - 이 값 하나만 바꾸고 동일 Client PIE 승선을 반복한다.
2. 진단용으로 `BuoyancyRoot/PlayerShip`의 Pawn response를 Ignore로 바꿔 Player가 배를 통과하게 한다.
   - 통과 순간 배가 전혀 요동하지 않으면 Character-Ship blocking contact가 trigger임이 확정된다.
   - 이것은 진단용이며 최종 게임 설정은 아니다.
3. Simulated Proxy state rewind만 잠시 끄고 같은 승선을 시험하는 것은 좋은 분리 실험이지만, 현재는 코드 변경 없이 바로 수행할 수 없다.
   - `AShip::Tick()`이 Simulated Proxy에서 매 틱 `SetCompareStateToTriggerRewind(true, true)`를 호출하므로 `np2.Resim.CompareStateToTriggerRewind.IncludeSimProxies=0` CVar만 바꿔도 component override가 다시 true로 설정된다.
   - 추후 임시 진단 스위치를 허용할 때만 이 강제 호출을 통제하여 비교한다.
   - 이 상태에서 Client 화면 폭주가 사라지면 rollback feedback 증폭이 확정되지만, drift를 허용하므로 최종 해결책은 아니다.
4. Player capsule collision을 착지 직후 잠시 끄는 BP 실험을 한다.
   - 끄는 즉시 Ship 요동이 멎으면 based movement 또는 blocking contact 경로가 원인이다.
5. 다음 로그에는 승선 RPC 성공 시점, Player MovementBase 이름, Player 위치/속도와 Ship server frame을 같은 표식으로 남겨 실제 발 접촉 프레임을 compare spike와 결합한다. 현재 로그만으로는 SF 504 전환을 접촉 시점이라고 확정할 수 없다.

#### 해결 방향 후보

가장 실용적인 구조는 양방향 물리 접촉을 Ship rigid body에서 분리하는 것이다.

- 실제 물리 `BuoyancyRoot`는 Pawn을 Ignore한다.
- 갑판 보행은 Ship에 부착된 별도 `QueryOnly` deck collision이 Pawn을 Block하도록 한다.
- Character는 deck을 floor/base로 쿼리해 걸을 수 있지만, capsule이 Chaos Ship rigid body에 직접 contact impulse를 만들지 않는다.
- 탑승자 무게를 배에 반영해야 한다면 raw capsule contact에 의존하지 않고, 서버 frame별 탑승자 질량/상대 위치를 Ship Network Physics input으로 결정론적으로 전달하여 custom PT에서 force/torque로 적용한다.

근본적인 Network Physics 쪽 해결은 resim에서도 frame offset과 모든 custom static config가 항상 유효하고, Player가 만드는 외력이 같은 과거 frame에서 재현되도록 만드는 것이다. `Based Movement Ignore Physics Base=true`가 현상을 완화하더라도 `FrameOffset=Missing` resim은 별도 결함으로 계속 해결해야 한다.

### 2026-07-13 — 승선 요동 3차 실험: 에디터 Collision 변경이 BeginPlay에서 무효화됨

#### 사용자가 수행한 실험

1. Player BP의 `Based Movement Ignore Physics Base=true` — 변화 없음, 동일하게 극심한 요동.
2. 다시 false로 복원 후 Ship `BuoyancyRoot`를 Custom으로 바꾸고 Pawn response를 Ignore — Player가 여전히 배 위에 서며 동일하게 요동.
3. Custom 상태에서 `Physics Only`와 `Query Only`도 각각 시험 — 양쪽 모두 Player가 서고 동일하게 요동.

#### 접근 방식: 이 결과가 실제로 무엇을 반증하는가

처음 해석하면 “Pawn Ignore인데도 접촉 요동이므로 Character-Ship collision 가설이 틀렸다”고 보기 쉽다. 그러나 실제 런타임 collision 설정이 바뀌었는지를 먼저 증명해야 한다.

CharacterMovement의 floor sweep는 Query 경로다. 실제 floor component가 정말 `Physics Only`였다면 Character는 그 component를 바닥으로 찾을 수 없고 통과해야 한다. 그런데 Player가 계속 섰다. 이것은 다음 중 하나를 의미한다.

1. 에디터에서 바꾼 `BuoyancyRoot` 설정이 PIE BeginPlay에서 다시 덮어써졌다.
2. `BuoyancyRoot` 외의 다른 PrimitiveComponent가 실제 갑판 floor로 Pawn을 Block한다.

현재 소스는 1번을 직접 증명한다.

#### 코드 증거

`AShip` 생성자:

```cpp
BuoyancyRoot->SetCollisionProfileName(TEXT("PlayerShip"));
```

`AShip::BeginPlay()`에서도 다시 실행:

```cpp
if (BuoyancyRoot)
{
    BuoyancyRoot->SetCollisionProfileName(TEXT("PlayerShip"));
    BuoyancyRoot->SetSimulatePhysics(true);
}
```

UE 5.7 `UPrimitiveComponent::SetCollisionProfileName()` API 주석도 이 함수가 현재 Collision Setting을 overwrite한다고 명시한다. `PlayerShip` profile은 `DefaultEngine.ini`에서 다음과 같이 정의된다.

- `CollisionEnabled=QueryAndPhysics`
- ObjectType `WorldDynamic`
- Pawn response를 별도로 지정하지 않으므로 기본 Block response 유지

따라서 BP Class Defaults/컴포넌트 Details에서 설정한 Custom, Pawn Ignore, Physics Only, Query Only는 PIE가 시작되면 BeginPlay의 `SetCollisionProfileName("PlayerShip")`에 의해 다시 `PlayerShip + QueryAndPhysics + Pawn Block`으로 돌아간다.

이번 세 Collision 실험은 실제 contact를 제거한 실험으로 인정할 수 없다. Character가 계속 설 수 있었고 결과가 완전히 같았던 이유도 이 강제 재설정으로 설명된다.

`Based Movement Ignore Physics Base=true`가 효과 없었던 결과는 유효하다. 따라서 based-movement가 자기 base를 sweep하는 세부 경로는 주원인 우선순위가 낮아졌지만, Character-Ship blocking contact 자체는 아직 반증되지 않았다.

#### 다음 실험 — 소스 수정 없이 실제 런타임 값을 바꾸는 방법

1. PIE 시작 후 Eject하여 **실행 중인 Ship 인스턴스**의 `BuoyancyRoot`를 선택한다.
2. 다음 런타임 값을 확인한다.
   - Collision Profile Name
   - Collision Enabled
   - Pawn response
3. 예상값은 `PlayerShip / QueryAndPhysics / Pawn Block`이다. 이 값이면 BeginPlay overwrite가 실측 확인된다.
4. BP 진단 그래프를 사용할 경우 Event BeginPlay 즉시가 아니라 짧은 `Delay` 뒤 `BuoyancyRoot`에 다음 중 하나를 적용한다.
   - `Set Collision Enabled: NoCollision`, 또는
   - `Set Collision Response To Channel: Pawn → Ignore`
   C++ `AShip::BeginPlay()`의 강제 profile 설정이 끝난 뒤 실행되도록 해야 한다.
5. 가장 명확한 진단은 Delay 뒤 `NoCollision`이다.
   - Player가 배를 통과하고 요동도 사라짐: 실제 Ship blocking contact가 trigger.
   - Player가 계속 섬: 다른 component가 floor를 제공함.
   - Player는 통과하지만 Ship은 계속 요동: contact와 별개로 Network Physics resim 결함이 단독으로 폭주함.
6. Player CharacterMovement의 `GetMovementBase()`를 Print String하여 실제 component 이름을 확인한다.
   - `BuoyancyRoot`이면 위 delayed runtime 변경 대상으로 확정한다.
   - 다른 mesh/collision component이면 그 component의 runtime Collision Enabled/Pawn response를 검사해야 한다.

#### 해결 방향에 미치는 영향

- 아직 Character-Ship contact 가설은 폐기하지 않는다. 이번 Custom/Pawn Ignore 테스트는 런타임에 적용되지 않았을 가능성이 코드로 증명됐다.
- 런타임 `NoCollision` 분리 실험이 먼저다.
- contact가 trigger로 확정되면 `BuoyancyRoot Pawn Ignore + 별도 QueryOnly deck floor` 구조를 검토한다.
- contact를 완전히 제거해도 요동하면 우선순위를 M17의 반복 `FrameOffset=Missing` resim 결함으로 이동한다.

### 2026-07-13 — 승선 요동 4차 실험: Pawn Blocking Contact가 trigger로 확정

#### 확정 실험

- Project Settings의 `PlayerShip` collision profile 자체에서 Pawn response를 Ignore로 변경했다.
- 이번에는 C++ BeginPlay가 다시 `PlayerShip`을 적용해도 수정된 profile 내용이 사용되므로 실제 런타임 변경이다.
- 결과:
  - Player는 Ship을 그대로 통과했다.
  - Ship은 아무 요동 없이 정상적으로 물 위에 떠 있었다.

판정:

- `BuoyancyRoot`와 Player Capsule 사이의 Pawn Blocking Contact가 극심한 네트워크 PIE 요동의 trigger임이 실험으로 확정됐다.
- `Based Movement Ignore Physics Base`, StandingDownwardForce, 초기 teleport penetration, 일반 부력값은 단독 주원인이 아니다.
- Network Physics의 불완전한 resim은 contact가 만든 작은 서버/클라이언트 차이를 큰 correction loop로 증폭하는 2차 원인이다.

#### Physics Interaction을 껐는데도 힘이 전달되는 이유

`CharacterMovement.bEnablePhysicsInteraction=false`가 차단하는 것은 CharacterMovement가 명시적으로 호출하는 다음 힘들이다.

- PushForce
- TouchForce
- StandingDownwardForce
- RepulsionForce

그러나 Capsule의 Pawn channel과 Ship의 collision response가 Block이면 Chaos collision solver는 두 shape가 서로 관통하지 않도록 contact constraint를 해결해야 한다.

Character Capsule은 일반적인 dynamic rigid body가 아니라 CharacterMovement가 위치를 직접 갱신하는 kinematic 성격의 collider다. 움직이는 kinematic collider가 dynamic Ship을 침범하거나 밀면 solver는 Capsule을 질량이 매우 큰/위치가 강제된 물체처럼 취급하고, 움직일 수 있는 Ship 쪽을 밀어내 contact를 해결할 수 있다. 이것은 CharacterMovement의 PushForce 옵션과 별개의 물리 충돌 반응이다.

네트워크에서는 더 심해진다.

1. 서버 Character Capsule과 autonomous client Capsule의 위치·착지 시점·based movement가 완전히 같지 않다.
2. 서버 Ship과 simulated-proxy client Ship이 서로 다른 kinematic contact impulse/depenetration을 받는다.
3. 이 Player contact는 `FNetInputShip` history에 기록되지 않는다.
4. server state가 도착해 Ship만 rewind해도 Character Capsule은 동일한 과거 frame contact로 함께 rewind/replay되지 않는다.
5. 현재 resim에는 `FrameOffset=Missing`으로 custom 부력이 빠지는 구간도 있다.
6. 결과적으로 contact가 만든 작은 차이가 5 cm rollback을 반복 호출하며 큰 화면 요동으로 증폭된다.

Standalone에서는 한 세계의 contact만 존재하고 server correction/rewind가 없으므로 동일한 blocking contact가 배를 약간 기울이는 일반 물리 반응으로 끝난다.

#### 권장 해결 구조: 물리 선체와 보행 충돌 분리

최종 구조의 목표는 Character가 갑판 위에 설 수는 있지만 Capsule이 Chaos Ship rigid body에 직접 contact impulse를 만들지 않게 하는 것이다.

1. 실제 물리 몸체인 `BuoyancyRoot`/`PlayerShip`은 Pawn Ignore를 유지한다.
2. Ship에 별도의 갑판/난간 collision component를 둔다.
   - `Collision Enabled = Query Only`
   - `Simulate Physics = false`
   - Pawn = Block
   - 필요 없는 다른 channel = Ignore
   - `Can Character Step Up On = Yes`
   - `BuoyancyRoot`에 attachment만 하고 physics weld는 하지 않는다.
3. CharacterMovement의 floor sweep와 이동 sweep는 QueryOnly deck/rail을 Block으로 인식하므로 Player는 갑판에 서고 난간에 막힌다.
4. QueryOnly component는 Chaos rigid-body contact에 참여하지 않으므로 Player Capsule이 실제 물리 Ship에 impulse를 전달하지 않는다.

BP에서 빠르게 구성하려면 갑판에는 얇은 Box Collision 여러 개, 난간에는 세로 Box Collision을 두는 방식이 가장 예측 가능하다. 복잡한 선체 mesh collision을 복제하는 것보다 보행 영역만 단순 shape로 만드는 편이 네트워크와 CharacterMovement 모두 안정적이다.

검증 체크리스트:

- PlayerShip profile의 Pawn Ignore 유지.
- QueryOnly deck 위에서 Player가 서고 걸을 수 있음.
- 난간 QueryOnly collision이 Player를 막음.
- Player 착지/걷기/점프/난간 충돌 후 Ship Network Physics mismatch와 화면 요동이 발생하지 않음.
- `GetMovementBase()`가 `BuoyancyRoot`가 아니라 새 QueryOnly deck component를 반환하는지 확인.

#### 탑승자 무게를 Ship에 반영하고 싶은 경우

QueryOnly deck 구조는 raw contact impulse를 제거하므로 Player 체중이 자동으로 Ship에 전달되지 않는다. 체중 효과가 필요하면 비결정적 capsule contact를 다시 켜지 말고 다음 정보를 Ship Network Physics input/history에 넣는다.

- 탑승자 질량
- Ship 기준 상대 위치
- 접지 여부
- 해당 server frame

그 뒤 custom PT에서 `Mass × Gravity` 힘과 상대 위치 기반 torque를 동일 server frame에 결정론적으로 적용한다. 이렇게 해야 server/client normal simulation과 rollback이 같은 승객 하중을 재생할 수 있다.

#### 별도 잔여 결함

Pawn contact를 분리하면 현재 사용자-visible 폭주는 해결될 가능성이 높지만, M17 resim의 반복 `FrameOffset=Missing`은 독립된 Network Physics 결함으로 남는다. Player 외의 외부 충돌이나 큰 correction에서도 문제가 재발할 수 있으므로 이후 별도로 수정·검증해야 한다.

### 2026-07-13 — 승선 요동 5차 실험: QueryOnly 보행 충돌로 증상 해결

#### 사용자가 적용한 최종 에디터 설정

- 실제 물리 몸체 `BuoyancyRoot`/`PlayerShip` profile: Pawn Ignore 유지.
- 별도 갑판 보행 collision:
  - `Collision Enabled = Query Only`
  - `Simulate Physics = false`
  - Pawn = Block
  - 불필요한 나머지 channel = Ignore
  - `Can Character Step Up On = Yes`
  - `BuoyancyRoot`에 attach
  - `Auto Weld = false`

실측 결과 Player는 배 위에 정상적으로 설 수 있고, 발을 딛거나 움직여도 이전의 극심한 Ship 진동이 사라졌다.

판정:

- 4차 실험에서 확정한 `Character Capsule ↔ dynamic BuoyancyRoot`의 Chaos blocking contact가 직접 trigger였다는 인과관계가 다시 확인됐다.
- QueryOnly deck은 CharacterMovement의 floor/movement query에는 Block으로 응답하지만 rigid-body contact constraint에는 참여하지 않는다. 따라서 Player가 설 수 있으면서도 dynamic Ship에 비결정적인 contact impulse를 주지 않는다.
- `Auto Weld=false`도 맞는 설정이다. 보행용 query shape를 물리 body에 합치지 않고 transform만 따라가게 한다.
- 이 구성은 현재 승선 요동 문제의 실용적인 해결책으로 채택한다.

#### 첨부 Client PIE 로그 정량 감사

분석 구간은 `AuthServerFrame 1107~1458`, same-frame compare 56회다.

- 첫 compare 한 건: 위치 `310.27 cm`, 회전 `2.473°`, 선속도 `594.23 cm/s` 차이. 바로 뒤 Player가 Swimming 상태에 진입한다. 로그가 중간부터 시작했으므로 이 한 건을 KKH_Test 초기 낙하나 승선 접촉 중 어느 하나로 확정할 수는 없으며, 큰 교란/초기 history 불일치 1회로 분리해서 본다.
- 위 1회를 제외한 55회:
  - MATCH 5회, MISMATCH 50회
  - 평균 위치 오차 `5.95 cm`
  - 최소 `0.55 cm`, 최대 `8.96 cm`
  - 최대 회전 오차 `0.368°`
  - 최대 선속도 차이 `126.21 cm/s`
  - 최대 각속도 차이 `3.947 deg/s`
- `SF >= 1200`의 안정 구간 43회도 평균 `5.89 cm`, 최대 `8.96 cm`로 비슷하다. 즉 처음 한 번의 310 cm spike가 사라진 뒤 발산하지 않고 작은 범위에 머문다.
- 이전 Pawn contact 로그의 후반 구간은 평균 `8.45 cm`, 최대 `17.06 cm`, 최대 각속도 차이 `5.127 deg/s`였다. 이번에는 특히 최대 위치/회전 편차가 줄었고 사용자 화면에서도 폭주가 사라졌으므로 contact 분리 효과가 수치와 육안 양쪽에서 일치한다.

#### 로그에 아직 남은 문제

화면상 승선 진동은 해결됐지만 Network Physics가 완전히 수렴한 로그는 아니다.

1. 위치 임계치가 `5.00 cm`라서 steady 구간 평균 `5.95 cm`도 계속 threshold를 넘는다. 그 결과 51회 trigger, 50회 `ApplyState`가 기록됐다. 육안으로 티가 안 나더라도 correction churn이 지속된다.
2. resim 경로에서 `[PHYSICS-PT-WAITING] ... FrameOffset=Missing`이 12회 반복됐다. 모든 WAITING 로그가 이 원인이었으며 일회성 startup이 아니다.
3. 이때 custom callback은 `Control-only rewind input` 상태로 진행한다. 즉 정상 frame과 동일한 시간/부력 계산을 완전하게 replay하지 못하는 기존 M17 잔여 결함이 그대로다.
4. `[SHIP-SYNC] LocDiff 40~60 cm`는 서로 다른 시점의 현재 Actor transform과 최근 replicated state를 비교하는 보조 지표이므로, same-frame 오차 `5~9 cm`와 동일한 값으로 해석하지 않는다.

따라서 현재 상태는 다음처럼 구분한다.

- **승선 collision 버그:** 해결됨.
- **일반 플레이 체감 안정성:** 이번 짧은 구간에서는 정상.
- **Network Physics 완전 동기화/rollback 재현성:** 미완료. 5 cm 주변의 반복 보정과 `FrameOffset=Missing`을 별도로 해결해야 함.

#### 다음 검증 및 우선순위

1. 현재 QueryOnly deck 구조는 유지한다. Pawn blocking contact를 `BuoyancyRoot`에 다시 켜지 않는다.
2. PIE에서 `GetMovementBase()`가 새 QueryOnly deck component인지 확인한다.
3. Client 승선 후 걷기, 점프, 난간 충돌, 장시간 대기, late join을 반복해 visible 진동이 재발하지 않는지 확인한다.
4. 다음 코드 작업의 우선순위는 collision tuning이 아니라 resim 중 exact tick offset이 사라지는 경로를 고쳐 `FrameOffset=Missing=0`으로 만드는 것이다.
5. 그 뒤 같은 테스트에서 MISMATCH/ApplyState 빈도와 steady-state same-frame 오차를 다시 측정한다. 임계치를 단순히 올리는 것은 결함을 가릴 수 있으므로 offset/replay 경로를 먼저 수정한다.

### 2026-07-13 — M18~M20: 간헐 제자리 진동의 근본 원인 수정

#### 사용자 관찰과 새 PIE 로그

QueryOnly 갑판을 적용한 뒤 승선 시 폭발적인 요동은 사라졌지만, Ship이 정상적으로 떠 있다가 간헐적으로 제자리에서 짧게 덜덜거린 뒤 다시 안정되는 현상이 관찰됐다.

첨부 PIE 로그 전체 1013줄, `AuthServerFrame 104~1146`을 분석했다.

- same-frame compare 157회
- MATCH 9회, MISMATCH 148회
- `ApplyState` 148회
- 위치 오차 평균 `21.71 cm`, 최대 `1219.44 cm`
- 초기 SF 104/106의 `1192.67/1219.44 cm` 두 건을 제외하면 대부분 `5~10 cm`, 최대 `12.70 cm`
- `FrameOffset=Missing` 진단 38회
- 로그 출력 조건이 `CurrentPhysicsStep % 60 == 0`이므로 Step 60, 120, 180…에서만 보이지만, 실제 `bHasNetworkPhysicsTickOffset=false` 상태에서는 그 사이의 모든 Client PT step도 custom 부력 계산 전에 return한다.

초기 12m 오차 두 건은 Client history가 아직 서버의 과거 frame을 갖지 못한 접속 초기 수렴 문제다. 그러나 그 뒤에도 거의 모든 authoritative state가 5 cm threshold를 넘어 rollback을 발생시키는 것은 별도 지속 결함이다.

#### M18 — 별도 Dedicated Server + Client 기준선 재현

실행 조건:

- Map: `/Game/New/Level/KKH_Test`
- Dedicated Server 1개를 먼저 시작
- 8초 뒤 Client 1개 접속
- `UnrealEditor-Cmd`, `-server`/`-game`, `-NullRHI`, 30초 수집
- 로그:
  - `Saved/Logs/Codex_M18_QueryDeck_Server.log`
  - `Saved/Logs/Codex_M18_QueryDeck_Client.log`

Client 결과:

- compare 162회: MATCH 3, MISMATCH 159
- 접속 초기 100 cm 이상 오차 2회
- 초기 두 건 제외 steady 평균 `7.41 cm`, 최대 `12.92 cm`
- 최대 회전 오차 `0.440°`
- `ApplyState` 158회
- `FrameOffset=Missing` 44회
- Client `[NETPHYS-OFFSET] PT accepted` 0회
- Server는 authority offset 0을 정상 수신
- 네트워크 에러 0회

PIE와 별도 프로세스에서 동일하게 재현됐으므로 Player 착지, PIE single-process 특성, QueryOnly deck 접촉은 간헐 진동의 원인에서 제외했다.

#### 근본 원인 추출

기존 `AShip::Tick()`은 Client offset 전달 전에 다음 조건을 사용했다.

```cpp
if (!HasAuthority() && NetworkPhysicsComponent
    && NetworkPhysicsComponent->IsNetworkPhysicsTickOffsetAssigned())
```

UE 5.7 엔진 구현을 확인한 결과:

1. `UNetworkPhysicsComponent::IsNetworkPhysicsTickOffsetAssigned()`는 내부에서 `GetPlayerController()`를 호출한다.
2. `GetPlayerController()`는 NetworkPhysicsComponent owner인 Ship의 Controller를 찾는다.
3. 이 Ship은 Client에서 `ROLE_SimulatedProxy`, `Owner=None`, Controller 없음이다.
4. 따라서 Component readiness 함수는 PlayerController의 실제 handshake 완료 여부와 무관하게 항상 false를 반환한다.
5. 반면 UE의 `UNetworkPhysicsComponent::UpdateAsyncComponent()`는 SimProxy를 위해 `World->GetFirstPlayerController()->GetNetworkPhysicsTickOffset()`을 직접 사용한다.

Dedicated M18 초기 compare도 이를 보여 준다.

- authoritative: `ServerFrame=529`, `LocalFrame=25`
- predicted: 처음에는 `ServerFrame=25`, `LocalFrame=25`
- 이후 UE internal NetworkPhysics history는 server/local mapping을 회복하지만, custom `FShipPhysicsAsync` 캐시는 offset을 끝내 받지 못했다.

결과적으로 서버는 server-frame 시간의 파도/부력을 정상 계산하지만 Client custom PT는 `bStaticDataReady=false`로 부력 계산을 계속 건너뛴다. Client Ship은 authoritative correction으로만 끌려가며 5~13 cm 차이가 반복되고, 이 correction burst가 화면의 간헐 제자리 진동으로 보였다.

#### M19 수정

`AShip::Tick()`에서 Component의 readiness gate를 제거하고, UE internal SimProxy 경로와 동일하게 World의 첫 PlayerController를 사용했다.

```cpp
if (!HasAuthority())
{
    if (const APlayerController* PlayerController = World->GetFirstPlayerController())
    {
        if (PlayerController->GetNetworkPhysicsTickOffsetAssigned())
        {
            NetworkPhysicsTickOffset = PlayerController->GetNetworkPhysicsTickOffset();
            bNetworkPhysicsTickOffsetAssigned = true;
        }
    }
}
```

수정 범위는 Simulated Proxy custom PT로 exact offset을 전달하는 조건 하나뿐이다. 서버 authority 경로는 기존처럼 offset 0을 사용한다.

빌드:

- `ArtisticSW2026Editor Win64 Development`
- 결과 `Succeeded`
- 기존 UE 5.7 deprecation/toolchain warning 외 새 compile error 없음

M19 동일 30초 Dedicated 재검증:

- Client PT가 `LocalStep=27 Offset=454` 수신
- `FrameOffset=Missing`: `44 → 0`
- compare: 12회, MATCH 8, MISMATCH 4
- 접속 초기 2회 제외 steady 평균: `7.41 → 1.16 cm`
- steady 최대: `12.92 → 6.02 cm`
- 최대 회전 오차: `0.440 → 0.048°`
- `ApplyState`: `158 → 3`
- 네트워크 에러 0회

#### M20 60초 장기 검증

동일 조건에서 60초간 추가 검증했다.

- Client PT: `LocalStep=28 Offset=506` 수신
- `FrameOffset=Missing=0`
- compare 23회: MATCH 20, MISMATCH 3
- 100 cm 이상 startup mismatch 1회
- startup 제외 mismatch는 SF 549의 `5.32 cm`, SF 572의 `6.10 cm` 두 번뿐
- startup 제외 전체 compare 평균 `0.579 cm`
- 최대 회전 오차 `0.073°`
- SF 1200~3360의 주기 compare는 위치 오차가 모두 `0 cm`
- 네트워크 에러 0회

동일 server frame의 `[NETPHYS-DET]`도 대조했다.

- 공통 frame 102개
- offset 수신 직후 정착 중인 SF 540/570 두 개만 차이 존재
- SF 600 이후 100개 공통 frame에서 Ship PZ, 첫 폰툰 Wave, Depth, Pontoon Force, 총 부력과 torque가 모두 로그 정밀도 기준 정확히 일치

#### 판정

- **승선 폭발 요동:** QueryOnly deck으로 해결 유지.
- **간헐 제자리 진동:** Simulated Proxy custom PT offset readiness gate가 근본 원인이었고 M19 수정으로 해결된 것으로 판정.
- **파도/부력 결정론:** offset 정착 후 서버/클라이언트 공통 100개 frame에서 정확히 일치함을 확인.
- **접속 초기 mismatch:** history가 없는 첫 수십 frame에 1~2회 남는다. 이후 장시간 발산이나 correction churn은 없다.

현재 5 cm threshold를 완화할 이유는 없다. 원인 수정 뒤 threshold를 유지한 상태에서도 steady compare가 대부분 0 cm로 수렴했다.

#### 다음 수동 확인

1. Editor를 재시작하거나 새 DLL이 로드된 상태에서 Client PIE를 실행한다.
2. KKH_Test 초기 침수/부상 후 최소 60초 동안 정지 관찰한다.
3. Client 로그에서 `[NETPHYS-OFFSET] ... Offset=<nonzero>` 1회와 `FrameOffset=Missing=0`을 확인한다.
4. Player 걷기/점프/난간 충돌/승선 후에도 QueryOnly deck 구성과 안정성이 유지되는지 확인한다.
5. 초기 접속 1~2회 correction까지 제거할지는 별도 startup-history 정책으로 다룬다. 현재 장시간 시뮬레이션 안정성과는 분리한다.

### 2026-07-13 — 현재 단계 최종 종합: 궁극 목표 체크리스트와 남은 작업

이 절은 앞선 실험 기록을 삭제하거나 덮어쓰지 않고, M20까지 확보한 최신 증거로 프로젝트 전체 상태를 다시 판정한 요약이다. 앞부분의 M1~M17 당시 “현재 위험”, “다음 실험”은 해당 시점의 역사 기록이며, 서로 모순될 경우 이 절과 M18~M20 결과를 최신 판정으로 사용한다.

#### 궁극적 목표

`AShip`의 서버와 모든 Client가 Unreal Engine Network Physics history를 사용해 동일한 서버 physics frame을 식별하고, 같은 과거 상태·입력·설정·파도 시간을 재생함으로써 다음을 만족하는 것이 최종 목표다.

- 서버는 Ship 물리의 최종 권위를 가진다.
- Client는 서버 패킷 사이를 자체 예측하여 부드럽게 표시한다.
- 권위 상태가 도착하면 동일 과거 frame의 Client history와 비교한다.
- 허용 오차를 넘을 때만 과거 상태를 적용하고 현재까지 재시뮬레이션한다.
- normal simulation과 resimulation이 동일한 입력, timestep, 파도, 부력, 외력을 사용한다.
- late join, 다중 Client, 실제 조종, 충돌, 지연·손실 환경에서도 발산하거나 correction storm이 발생하지 않는다.
- 렌더링 수면과 gameplay/physics 수면의 관계가 명시적으로 정의되고 검증된다.

#### 최종 체크리스트

표기: `[x]` 달성 및 런타임 검증, `[~]` 구조/일부 시나리오 달성이나 최종 수용 미완료, `[ ]` 미검증 또는 미구현.

##### A. 시간과 frame identity

- [x] 서버가 `ServerPhysicsTimeOrigin`과 실제 solver fixed dt를 생성하고 복제한다.
- [x] Client local physics frame과 server physics frame의 정확한 tick offset을 PlayerController handshake에서 얻는다.
- [x] `SimTime = Origin + ServerFrame × SolverDt`를 normal PT와 resim PT 모두 사용한다.
- [x] `FNetInputShip`과 `FNetStatePhysicsShip`이 상속 payload의 `ServerFrame`을 명시적으로 직렬화한다.
- [x] 8초 뒤 접속한 Dedicated Client에서 nonzero offset을 받고 matching local history frame을 비교한다.
- [~] 일반 gameplay query와 Water 렌더는 GameState 서버 시간축을 사용하지만, 이는 복제·스무딩 시간이라 physics frame과 비트 단위로 동일한 시계는 아니다.
- [ ] 60초 이상 late join, tick-offset 재조정, 여러 PIE world와 여러 Ship에서 instance 간 clock 독립성을 최종 검증한다.

##### B. 파도 결정론과 수면 정의

- [x] Ship PT는 각 peer의 임의 현재 월드 시간이 아니라 authoritative `ServerFrame` 기반 시간을 사용한다.
- [x] 동일 server frame에서 prediction particle의 `X/R`로 동일 폰툰 좌표를 구성한다.
- [x] M20에서 offset 정착 후 공통 100개 frame의 Ship PZ, 첫 폰툰 wave/depth/force, 총 부력/torque가 로그 정밀도 기준 server/client 완전 일치했다.
- [x] 수영 query, Ripple wrapper, WaterSubsystem/MPC에 GameState 서버 시간 주입 경로가 존재한다.
- [~] custom Ship wave는 base Gerstner 파라미터의 결정론적 cosine 합이지만 UE Water의 full Gerstner query, Water Body base Z, attenuation, LWC 처리를 모두 복제하지는 않는다.
- [~] 렌더 수면과 Ship physics 수면은 시간축은 공유하지만 수식 전체가 동일하다는 검증은 아직 없다.
- [ ] ripple을 cosmetic으로 제한할지 authoritative gameplay wave로 복제할지 정책을 확정한다.

##### C. Network Physics history, correction, resimulation

- [x] `UNetworkPhysicsComponent`, History Capture, Resimulation mode, input/state history가 실제 런타임에서 작동한다.
- [x] matching past frame에서 `CompareData`가 호출되고, threshold 초과 시 `ApplyState`와 rewind/resim이 실행된다.
- [x] 상태 복원 시 `X/R/V/W`뿐 아니라 Chaos next transform인 `P/Q`도 함께 복원한다.
- [x] 5 cm 위치 및 5° 회전 correction 정책을 실제 payload state에 저장하여 비교한다.
- [x] 정적 파도/폰툰/튜닝 cache를 조종 input history에서 분리해 resim input load가 기본값으로 오염시키지 않는다.
- [x] GT→PT 데이터 전달을 async producer input으로 한정하고 GT의 PT cache 직접 mutation 경로를 제거했다.
- [x] Simulated Proxy가 Controller를 소유하지 않아 custom PT offset readiness가 영원히 false였던 결함을 M19에서 수정했다.
- [x] M20 60초 run에서 `FrameOffset=Missing=0`, startup 이후 주기 same-frame 위치 오차 0 cm, 장시간 correction storm 없음.
- [~] 접속 직후 local history가 아직 없는 1~2개 authoritative packet은 수 m startup mismatch와 one-shot correction을 만든다. 이후에는 수렴하지만 완전한 warm start는 아니다.
- [~] runtime 중 wave asset이나 부력 계수가 바뀔 경우 과거 frame에서 당시 config version을 복원하는 history 정책은 없다.

##### D. Ship 물리 모델과 Player 상호작용

- [x] Chaos built-in gravity와 custom manual gravity의 이중 적용을 제거했다.
- [x] 폰툰별 최대 부력 상한과 60 Hz async fixed step을 적용했다.
- [x] Character Capsule과 dynamic `BuoyancyRoot`의 blocking contact가 네트워크 correction loop를 폭주시킨다는 것을 분리 실험으로 확정했다.
- [x] `BuoyancyRoot Pawn Ignore + 별도 QueryOnly deck/rail Pawn Block + no weld` 구조로 Player가 서면서 Ship에 raw Chaos contact impulse를 전달하지 않게 했다.
- [~] 현재 질량, pontoon 반경, coefficient, damping은 안정적으로 동작하지만 최종 게임 감각의 흘수·복원력·마찰 튜닝은 사용자의 에디터 조정 영역이다.
- [~] QueryOnly deck은 승객 체중을 자동으로 Ship에 전달하지 않는다. 체중 효과가 필요하면 passenger mass/relative position/grounded frame을 결정론적 Network Physics input으로 모델링해야 한다.
- [ ] 외부 dynamic rigid body 충돌을 rollback에서 결정론적으로 재현하는 정책을 설계하고 검증한다.

##### E. 조종, 소유권과 최종 수용 행렬

- [~] `MovementInput/SteeringInput`은 frame-tagged `FNetInputShip` history에 저장·직렬화·적용되는 구조가 구현돼 있다.
- [~] Ship possession 및 input relay 관련 코드가 존재하지만 RPC 조종 경로와 Network Physics input 경로가 중복돼 책임이 완전히 정리되지 않았다.
- [ ] 실제 possessed/autonomous Ship을 지속 조종하면서 correction 뒤 input replay가 동일하게 재현되는지 검증한다.
- [ ] Dedicated 2개 이상 Client, Listen Server, 60초 late join, 여러 Ship 인스턴스를 검증한다.
- [ ] packet lag, jitter, loss, reorder 환경에서 correction 예산과 조종 품질을 검증한다.
- [ ] 장시간 운항, 큰 좌표/LWC 또는 world-origin 경계를 검증한다.

#### 달성된 핵심 결과

1. Network Physics가 “서버 transform을 최신 값으로 복제하는 시스템”이 아니라 과거 frame history를 비교하고 되감는 폐루프로 실제 동작하게 만들었다.
2. server/client가 동일한 physics frame과 시간으로 custom 파도와 부력을 계산하도록 만들었다.
3. Dedicated late-join 기준으로 offset 정착 후 server/client force와 state가 정확히 일치함을 실측했다.
4. Client 화면이 서버 correction에만 끌려가던 `FrameOffset=Missing` 결함을 제거했고 correction storm을 없앴다.
5. 승선 Character contact와 Network Physics가 만드는 비결정적 feedback을 QueryOnly 보행 collision으로 분리했다.
6. 잘못된 timestep, 이중 중력, 무상한 부력 피크, 불완전 transform 복원, frame serializer 누락, resim cache 오염 등 주요 구조 결함을 수정했다.

#### 중간까지 진행했지만 멈춘 지점

1. **startup warm start**
   - 첫 authoritative state가 도착할 때 해당 Client local history slot이 생성되기 전이면 큰 mismatch가 발생한다.
   - M11 GT teleport, M12 seed guard, M13 absolute-clock PT seed를 시험했지만 서로 다른 frame의 snapshot을 현재 particle/history에 넣어 오히려 약 113 m 오염 또는 correction storm을 만들었다.
   - 모두 제거했다. 현재는 첫 authoritative correction을 허용하고 수십 frame 안에 정확히 수렴하는 안전한 기준선을 채택한다.
   - 완전 해결하려면 엔진 history 생성 시점에 **같은 local frame**의 authoritative initial state를 기록하는 방식이 필요하다.
2. **full water equivalence**
   - physics peer 간 custom wave는 일치한다.
   - 그러나 physics 함수와 렌더/UE Water full Gerstner 표면이 완전히 같은 함수라는 증거는 없다.
   - Water Body별 기준면, attenuation, LWC와 ripple authority를 먼저 결정해야 다음 구현을 확정할 수 있다.
3. **조종 예측**
   - payload와 history 구조는 준비됐다.
   - 자동 Dedicated 테스트의 Ship은 비소유 Simulated Proxy였고 Move/Steer가 0이어서 실제 조종 replay는 검증하지 못했다.
4. **다중 Client 검증**
   - M21 서버 1 + Client 2 staggered-join 테스트를 시작했지만 사용자 중지 요청으로 프로세스를 종료했다.
   - 해당 run은 완료된 수용 증거로 계산하지 않는다.

#### 남은 작업의 권장 순서

1. 새 빌드가 로드된 Editor의 KKH_Test Client PIE에서 60초 정지, 승선, 걷기, 점프, 난간 충돌을 수동 확인한다.
2. 실제 Ship possession 시나리오를 고정하고 Move/Steer input history와 correction 후 replay를 계측한다.
3. Dedicated 2 Client와 60초 late join을 완료한다.
4. packet impairment 행렬을 실행하고 위치/회전/속도 오차와 correction 횟수의 수용 예산을 정한다.
5. 렌더 수면과 physics 수면의 목표 관계 및 ripple 권위 정책을 결정한다.
6. 필요할 경우에만 engine-history-aware startup seed를 설계한다. 현재의 안정된 post-startup 동작을 해치는 임의 teleport 방식은 재도입하지 않는다.
7. 위 행렬을 통과한 뒤 여러 Ship/LWC/장시간 성능을 검증하고 최종 완료 판정을 내린다.

#### 현재 종합 판정

- **정지한 단일 AShip, Dedicated Server + 1 late-join Client의 core Network Physics와 base-wave 부력 동기화:** 달성.
- **Player가 Ship 위에 서는 네트워크 충돌 안정성:** 달성.
- **모든 gameplay 시나리오를 포함한 “완벽한 Network Physics” 최종 목표:** 아직 미완료.
- 현재 남은 문제는 core loop의 비작동이 아니라 startup 초기화 품질, 실제 조종/외력의 history 모델, full water 정의, 다중 Client와 불량 네트워크 수용 검증이다.

### 2026-07-14 M22 Enemy Ship Network Physics 입력 통합 및 비점유 Player Ship 탐지

#### 증상과 원인

- Test Level 시작 직후 Player가 배를 조종하지 않으면 근처 Enemy Ship 3척이 계속 Idle 상태에 머물렀다.
- 기존 `FindPlayerShip`은 PlayerController가 직접 `AShip`을 Possess하거나 Player Pawn이 Ship에 Attach된 경우만 탐색했다. 따라서 아무도 타지 않은 Player Ship은 AI 표적 후보에 들어가지 않았다.
- 기본 `AShip`이 `Player` 태그를 추가하고 `AEnemyShip`이 이를 상속한 뒤 `Enemy` 태그만 추가해, Enemy Ship도 `Player`와 `Enemy` 태그를 동시에 가질 수 있었다.
- Player Ship은 Network Physics 적용 후 physics thread의 `ParticleHandle->AddTorque`를 사용했지만 Enemy BT는 game thread의 `AddTorqueInDegrees`를 계속 사용했다. 같은 수치라도 degrees-to-radians 변환 때문에 Enemy 회전 토크가 약 57.3배 작게 적용됐다.
- BT는 선수 정렬값 `HeadingDot > 0`일 때만 전진력을 주므로, 약한 회전 때문에 측면이나 후방의 표적을 향하지 못하면 전진 입력도 계속 0에 가까워졌다.
- Enemy BT의 직접 Force/Torque는 `FNetInputShip` history에 기록되지 않아 Client Simulated Proxy가 AI 추진을 예측·재시뮬레이션할 수 없었다.

#### 수정한 구조

- `AEnemyShip`은 생성 및 BeginPlay 시 `Player` 태그를 제거하고 `Enemy` 태그를 보장한다.
- `FindPlayerShip`은 PlayerController 소유 여부 대신 World의 모든 `AShip`을 순회한다. `Player` 태그가 있고 `Enemy` 태그가 없는 Ship을 표적으로 선택하므로 비점유 Player Ship도 즉시 인식한다.
- `AShip::SetAIControlInput`을 추가했다. 서버 권한에서만 Move/Turn을 `[-1, 1]`로 clamp하여 Player와 동일한 `CurrentMoveInput`/`CurrentTurnInput` 경로에 기록한다.
- `BTTask_NavalDrive`는 직접 `AddForce`/`AddTorqueInDegrees`를 호출하지 않는다. BT가 계산한 정규화 입력을 `SetAIControlInput`으로 넘기고, `AShip::Tick`이 이를 `FShipPhysicsAsync`로 전달한다.
- BT 시작·중단 및 Enemy 사망 시 AI 입력을 0으로 초기화해 이전 추진 입력이 history에 남지 않도록 했다.
- 결과적으로 AI 판단은 서버 전용이지만, 서버가 작성한 Enemy Move/Turn은 Network Physics input history로 Client에 전달된다. Client의 Enemy Ship은 단순 transform 추종만 하는 것이 아니라 해당 입력으로 물리를 extrapolation하고, 서버 state와 비교해 필요 시 rewind/resimulation한다.

#### 기본 물리값 정렬

- Player Ship을 기준으로 Enemy Ship 계열의 기본값을 동일하게 맞췄다: Mass `10000`, Linear Damping `0.8`, Angular Damping `3.0`, Forward Force `2,000,000`, Turn Torque `6,000,000,000`, Lateral Drag `200`.
- Test Level이 사용하는 `BP_TestShip_SingeMesh`와 `BP_TestShip_SingleMesh`의 관련 값이 동일함도 확인했다.
- 향후 감각 튜닝은 각 BP의 에디터 값으로 분리 조정할 수 있다.

#### Dedicated Server + Client 검증

- `/Game/New/Level/Test_Level`에서 서버 1 + Client 1을 25초간 실행했다.
- Player가 배를 조종하지 않은 상태에서 3척 모두 `Idle -> Approach`로 전환하고 `BP_TestShip_SingeMesh`를 표적으로 선택했다.
- 서버에서 Enemy별 nonzero Move/Turn 입력이 Network Physics history에 저장됐고 Client에서 같은 입력을 load하는 것을 확인했다.
- Client physics thread에서 nonzero 전진력과 회전 토크가 적용됐고, 약 5 cm 정책에 따른 state 비교 및 resimulation이 작동했다.
- Fatal, Assertion, Ensure, NaN은 발생하지 않았다.

#### 로그 정리

- 검증에 사용한 `[GT]`, `[CLIENT-GT]`, `[SHIP-SYNC]`, `[GS-*]`, `[NETPHYS-*]`, `[PHYSICS-PT*]`, `[PT-RESIM]`, `[RESIM-COMPARE-TRIGGER]`, `[NAVAL-AI-STATE]` 진단 로그는 호출 코드를 삭제하지 않고 주석 처리했다.
- 탑승, 전투, 드롭, 설정 오류 등 기존 gameplay 로그는 유지했다.
- 필요 시 해당 주석만 해제하면 동일한 frame/input/state/force 계측을 다시 사용할 수 있다.
