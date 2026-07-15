# Buoyancy Integration 구현 체크리스트 및 테스트 기록

> 기준 계획서: `Buoyancy_Architectures_Comparison_and_Unification_Proposal.md`  
> 시작일: 2026-07-15  
> 테스트 맵: `/Game/New/Level/KKH_Test`

## 5가지 목표

- [x] 1. Ripple 생성과 상태의 서버 원천화
- [x] 2. GT/PT 파도 및 Ripple 쿼리의 공통 데이터·계산 경로 확보
- [x] 3. Player/Rigidbody가 동일한 순수 부력 계산 함수 사용
- [x] 4. Rigidbody 전용 `USWBuoyancyComponent` 제작
- [x] 5. Storage → Ship → Player 통합 회귀 검증 완료

## 구현 원칙

- [x] Player의 CMC prediction 및 custom swimming 구조를 유지한다.
- [x] Ship의 Network Physics input/state/history/resimulation 구조를 유지한다.
- [x] Storage의 서버 권위 + Replicated Movement 구조를 유지한다.
- [x] 정적 폰툰/wave 배열의 매 프레임 GT→PT 복사 최적화는 보류한다.
- [x] 사용자 수정 `Content/New/Level/KKH_Test.umap`과 이동된 Planning 문서를 보존한다.

## Phase 1 — 공통 기반

- [x] `FSWRippleEvent` 및 순수 `FSWRippleEvaluator` 추가
- [x] thread-safe `USWRippleStateSubsystem` 추가
- [x] `ASWRippleReplicator` + replicated Fast Array 추가
- [x] `FSWBuoyancyMath`와 공통 설정/폰툰 타입 추가
- [x] Core 단위 및 전체 Editor 빌드

## Phase 2 — 서버 인증 Ripple

- [x] Water overlap 감지를 서버/Standalone 전용으로 변경
- [x] `URippleSubsystem`을 인증 cache 기반 query/texture 경로로 변경
- [x] Dedicated Server에서도 CPU ripple query 활성화
- [x] 클라이언트의 로컬 gameplay ripple 생성 차단
- [x] Fast Array로 현재 활성 Ripple의 late join 초기 상태 지원
- [x] 만료 뒤 Network Physics rewind용 CPU history 보존

## Phase 3 — 파도 쿼리 일관성

- [x] `USWRippleWaterWaves` GT 경로가 공통 인증 cache/evaluator 결과 사용
- [x] Ship GT가 인증 Ripple snapshot을 PT input에 전달
- [x] Ship PT Gerstner 결과에 동일 `FSWRippleEvaluator` 적용
- [ ] 동일 위치·동일 서버 시각의 GT/PT 수치 비교 테스트

## Phase 4 — 부력 계산 및 컴포넌트

- [x] `USWBuoyancyComponent` ServerAuthority 모드 구현
- [x] `USWBuoyancyComponent` ExternalNetworkPhysics 모드 구현
- [x] 공통 Details 필드명/category/단위 적용
- [x] Storage를 새 컴포넌트로 전환
- [x] Ship 설정 소스를 새 컴포넌트로 전환
- [x] SwimmingComponent를 공통 `FSWBuoyancyMath`로 전환
- [x] 기존 Ship Blueprint의 Water Buoyancy 설정을 런타임 import하는 호환 브리지 추가

## Phase 5 — 빌드 및 테스트

- [x] `ArtisticSW2026Editor Win64 Development` 빌드
- [x] KKH_Test Dedicated Server 기동
- [x] KKH_Test Client 접속
- [x] Player 낙하/수영 진입 및 부력 회귀 확인
- [x] Ship 낙하/다중 폰툰 부력 및 Network Physics 회귀 확인
- [x] BP_Storage_Buoyancy 낙하/서버 부력/클라이언트 복제 회귀 확인
- [x] Ripple 서버 생성 및 client cache 수신 확인
- [ ] 서버/클라이언트의 동일 Ripple query 값 확인
- [x] Fatal/Assertion/Ensure 없음 확인

## 구현 구조 메모

- `ArtisticSWCore`: 순수 Ripple 이벤트/evaluator, thread-safe 상태 cache, Fast Array replicator, 순수 부력 solver.
- `ClassFeature`: 서버 Ripple 감지·렌더링 facade, Player Swimming/CMC 적용, Storage 서버 권위 적용.
- `WaterAndShip`: Rigidbody 설정 컴포넌트와 Ship Network Physics PT 적용.
- `ESWBuoyancyExecutionMode::ServerAuthority`: 컴포넌트가 authority에서 Chaos force를 적용한다.
- `ESWBuoyancyExecutionMode::ExternalNetworkPhysics`: 컴포넌트는 설정 원천이고 외부 PT callback이 force를 적용한다.
- 기존 Water Plugin `UBuoyancyComponent`는 Ship/Storage asset reference 파손 방지를 위해 비활성 호환 껍데기로만 남긴다.

## 테스트 기록

### 2026-07-15 — 컴파일 검증

- 명령: `Build.bat ArtisticSW2026Editor Win64 Development ...`
- 결과: 성공. UHT, ArtisticSWCore, WaterAndShip, ClassFeature 및 Editor target 링크 완료.
- 비고: UE 5.7 Network Physics/Chaos 헤더의 기존 deprecated 경고만 존재하며 신규 컴파일 오류 없음.

### 2026-07-15 — KKH_Test 서버/클라이언트 1차 통합 실행

- 서버 로그: `Saved/Logs/Codex_BuoyancyIntegration_Server.log`
- 클라이언트 로그: `Saved/Logs/Codex_BuoyancyIntegration_Client.log`
- 실행: Dedicated Server 1 + Client 1, `-NullRHI -RippleDiagnostics`, 약 20초 gameplay 관찰.
- 서버 인식:
  - Ship: `ExternalNetworkPhysics`, 폰툰 4개, Authority.
  - Storage: `ServerAuthority`, 폰툰 1개, Authority.
- 클라이언트 인식:
  - Ship: `ExternalNetworkPhysics`, 폰툰 4개, Proxy.
  - Storage: `ServerAuthority`, 폰툰 1개, Proxy. 클라이언트 force 적용은 role guard로 차단.
- Ripple:
  - 서버 overlap에서 Event 3개 생성(Revision 1→3).
  - 클라이언트가 Event/Revision을 수신했고 활성 개수 1→2→3 및 만료 흐름을 재현.
  - 이후 서버/클라이언트 cache가 history retention에 따라 0까지 정리됨.
- 안정성: 양쪽 로그에 Fatal, Assertion, Ensure 없음.
- 제한: NullRHI이므로 client의 Ripple texture resource가 없는 것은 예상 결과이며 렌더링 자체는 미검증.

### 다음 검증

- 같은 Ripple event에 대한 서버/클라이언트 높이 수치 비교 진단 추가.

### 2026-07-15 — KKH_Test 위치·속도 회귀 실행

- 로그:
  - `Saved/Logs/Codex_BuoyancyState_Server.log`
  - `Saved/Logs/Codex_BuoyancyState_Client.log`
  - `Saved/Logs/Codex_BuoyancyEquilibrium_Server.log`
- 실행: `-BuoyancyDiagnostics`, Dedicated Server + Client 및 서버 장시간 평형 관찰.
- Player: 서버에서 `BP_Player_C_0 >>> Entered Swimming State` 확인. CMC 경로는 유지되고 공통 solver 호출로만 교체됨.
- Ship:
  - 시작 `Z=800cm`에서 낙하 후 4개 폰툰으로 파도 수면대(`Z` 대략 -60~-390cm)에서 운동 지속.
  - Authority와 Proxy 모두 External Network Physics 모드이며 비활성 Water Buoyancy가 중복 힘을 가하지 않음.
  - 클라이언트 proxy가 서버의 수면대 운동을 지속적으로 추종함.
- Storage:
  - 시작 `Z=10100cm`에서 서버 물리로 낙하.
  - 깊은 침수 후 상승으로 전환하고 수면대에서 `Z=-330~-147cm` 사이 파도 운동으로 전환됨.
  - 클라이언트는 자체 힘 없이 서버 Replicated Movement를 따라 동일한 상승 추세를 보임.
- 안정성: 모든 추가 실행에서 Fatal, Assertion, Ensure, NaN 없음.
- 해석: Storage의 긴 회복 시간은 10,100cm 낙하 관성과 기존 한 개 50cm 폰툰/단방향 damping 계수를 그대로 보존한 결과다. 발산은 관찰되지 않았다.

### 2026-07-15 — PIE 피드백 후 Storage/레거시 컴포넌트 정리

- 사용자 PIE 확인: client Ripple 렌더링 및 Storage Ripple에 대한 Player 반응 정상.
- Storage의 기존 native Water `UBuoyancyComponent`를 제거하여 편집 대상을 `SWBuoyancyComponent` 하나로 정리.
- 공통 `FSWBuoyancyForceSettings`에 `DeepWaterBuoyancyMultiplier` 추가.
  - 반 이상 잠긴 구간부터 선형으로 적용되고 완전 침수에서 최대가 된다.
  - 수면 근처 평형 계수는 바꾸지 않고 깊은 물에서의 회복만 가속한다.
  - Storage 기본값: `3.0`, 나머지 Player/Ship 기본값: `1.0`으로 기존 동작 보존.
- Ship legacy import를 SW Pontoons가 비어 있을 때만 수행하도록 변경.
  - 기존 Ship BP: `SettingsSource=LegacyFallback`.
  - SW Pontoons를 설정한 Ship BP: `SettingsSource=SWComponent`; 이후 legacy component 삭제 가능.
- 테스트 로그: `Saved/Logs/Codex_StorageDeepRecovery_Server.log`.
- 결과:
  - Storage 완전 침수 상승 속도 약 `100~175 cm/s`로 개선(기존 약 `27 cm/s`).
  - 수면대 도달 확인.
  - Storage asset 로드 시 제거된 native Buoyancy component 관련 오류/경고 없음.
  - Fatal, Assertion, Ensure, NaN 없음.
- 수면 근처 진동은 단일 폰툰이 Gerstner/Ripple 수면을 직접 추종하는 물리 반응과 server movement replication의 조합으로 판단한다. 별도 필터/다중 폰툰/보간 변경은 이번 범위에서 보류.
