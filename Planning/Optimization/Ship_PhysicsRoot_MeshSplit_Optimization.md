# Ship Physics Root / Mesh Split 최적화

작성일: 2026-07-16  
상태: 1차 구현 및 FHD 네트워크 검증 완료

## 1. 목표

기존 선박은 하나의 `BuoyancyRoot` Static Mesh Component가 다음 책임을 동시에 가졌다.

- Chaos Simulate Physics 및 Network Physics의 물리 Body
- 선박 외형 렌더링
- 대포알 피격 Overlap
- Pawn이 걷는 발판 충돌
- 과거 Water Plugin `UBuoyancyComponent`가 설정한 WaterBody Overlap

움직이는 복합 Collision Mesh에서 `GenerateOverlapEvents=true`가 유지되면 Chaos 결과를 Game Thread에 동기화할 때마다 `UpdateOverlaps`가 실행된다. `Test_Level`의 9척에서 이 경로가 `SyncBodies`의 대부분을 차지했다.

이번 변경의 목표는 물리 Body와 query/render 책임을 분리하고, 클라이언트가 권위 판정에 필요하지 않은 Overlap을 수행하지 않도록 만드는 것이다.

## 2. 최종 구조

기존 Blueprint 호환성을 위해 직렬화된 컴포넌트 이름 `BuoyancyRoot`는 유지했다. 에디터 표시 이름만 `Physics Root (BuoyancyRoot)`로 명확히 했다.

| 컴포넌트 | 역할 | 렌더 | Collision | Overlap | 현재 Mesh |
|---|---|---:|---|---:|---|
| `BuoyancyRoot` | Chaos Body, 질량/관성, Network Physics | 숨김 | 기존 PlayerShip/EnemyShip 기반, Pawn·양측 Cannon 채널 Ignore | OFF | 기존 Root Mesh |
| `ShipVisualMesh` | 선박 외형 | 표시 | NoCollision | OFF | Root Mesh 복사 |
| `ShipDamageMesh` | 대포 Sweep 대상 query | 숨김 | 서버만 QueryOnly, 상대 Cannon 채널만 Block | OFF | Root Mesh 복사 |
| `ShipDeckMesh` | Pawn 발판 | 숨김 | QueryOnly, Pawn만 Block | OFF | Root Mesh 복사 |
| `SWBuoyancyComponent` | Water/Wave query 및 부력 설정 | 해당 없음 | Collision 불필요 | 불필요 | Pontoon 기반 |

현재는 사용자가 요청한 검증 단계이므로 `/Game/New/Ship/Mesh/SM_TestShip.SM_TestShip`을 네 컴포넌트에 그대로 사용한다. `OnConstruction()`과 `BeginPlay()`가 Root의 Static Mesh 및 Material Override를 분리 컴포넌트에 복사한다.

이 구조에서 WaterBody와 물의 수면 높이는 Collision Mesh가 담당하지 않는다. `SWBuoyancyComponent`와 Network Physics 경로가 Water/Wave query를 직접 수행한다. 즉 선박의 모든 Static Mesh는 물을 몰라도 된다.

## 3. 구현 상세

### 3.1 Physics Root

- 실제 Chaos Simulate Physics는 계속 `BuoyancyRoot` 하나만 수행한다.
- 기존 Blueprint의 Mesh, 물리 재질, Mass, Inertia 및 Network Physics 참조를 보존한다.
- `GenerateOverlapEvents=false`를 생성자, Construction, BeginPlay 최종 설정에서 반복 보장한다.
- Pawn과 Player/Enemy Cannon 채널은 Ignore한다.
- 시각 Mesh가 별도로 렌더되므로 Root는 숨긴다.

### 3.2 Visual Mesh

- Root의 Mesh와 Material을 복사한다.
- `NoCollision`, `GenerateOverlapEvents=false`이다.
- 보이는 선박 외형만 담당한다.

### 3.3 Damage Mesh와 대포알 Sweep

Authority Split 1차 단계에서는 서버 Damage Mesh만 Overlap을 유지했지만, 최종 단계에서 대포알의 `ProjectileMovementComponent` swept blocking hit로 전환했다.

- `ShipDamage` 전용 Object Channel: `ECC_GameTraceChannel5`
- Authority 서버 Damage Mesh: QueryOnly, `GenerateOverlapEvents=false`
- Enemy Damage Mesh: Player Cannon만 Block, Enemy Cannon은 Ignore
- Player Damage Mesh: Enemy Cannon만 Block, Player Cannon은 Ignore
- 비권위 클라이언트 Damage Mesh: NoCollision
- 대포알: `ShipDamage`만 Block, Pawn·Storage·일반 WorldDynamic은 Ignore
- GAS 적용 직전에도 발사 선박과 피격 선박의 Enemy Tag를 비교하여 동일 진영 피해를 다시 차단

따라서 적 대포알은 상대 선박 Hull에만 Sweep Hit를 만들고, 같은 진영 선박은 관통한다. 선박 Hull은 서버와 클라이언트 모두 지속 Overlap Pair를 만들지 않는다.

Damage Mesh는 현재 정확한 선체 모양의 query target으로 남아 있지만 Overlap source는 아니다. 추후 전용 저복잡도 Hull이나 별도 sweep query 구조가 준비되면 같은 API를 유지한 채 Mesh 복잡도만 낮출 수 있다.

### 3.4 Water Ripple과 대포알 Overlap

대포알의 Overlap 자체는 끄지 않았다. WorldStatic에 대해서만 Overlap을 유지하여 WaterBody의 서버 권위 `OnActorBeginOverlap`이 계속 호출된다.

- WaterBody: Overlap → `URippleSubsystem`이 서버에서 Ripple 생성/복제
- 일반 WorldStatic: Overlap callback은 발생할 수 있지만 Cannonball gameplay handler가 무시하고 계속 비행
- Pawn/Storage/일반 WorldDynamic: Collision Response Ignore
- ShipDamage: Blocking Sweep

즉 수면 Ripple 경로와 선박 피해 Sweep 경로가 서로 다른 Collision Response로 공존한다.

### 3.5 Deck Mesh

- Pawn만 Block하고 나머지는 Ignore한다.
- Overlap은 생성하지 않는다.
- 지금은 전체 선박 Mesh를 임시 사용하므로 실제 갑판 이외의 표면도 Pawn을 막을 수 있다.
- 최종 에셋 단계에서는 갑판과 승선 가능 영역만 본뜬 단순 Collision Mesh로 교체한다.

### 3.6 기존 Water Plugin Buoyancy

Ship의 `UBuoyancyComponent`는 더 이상 힘이나 설정의 원천이 아니다. BeginPlay에서 다음을 적용한다.

- Auto Activate OFF
- Component Tick OFF
- Deactivate

그 뒤 Split Collision 정책을 다시 적용하여 Legacy 초기화가 Root Overlap을 되살리지 못하게 한다.

## 4. 실패한 1차안과 교훈

1차 구현은 Root Overlap을 끄고, 같은 전체 선박 Mesh를 가진 `ShipDamageMesh`의 Overlap을 서버와 클라이언트 모두에서 켰다.

결과는 다음과 같았다.

| FHD Client | 원본 | Split v1 |
|---|---:|---:|
| Frame 평균 | 45.581ms | 44.271ms |
| Game Thread | 45.578ms | 44.269ms |
| SyncBodies | 41.820ms | 40.706ms |

병목이 거의 그대로였다. 문제는 컴포넌트 이름이나 WaterBody 전용 채널이 아니라, 움직이는 복합 선박 Hull에서 지속적인 Overlap 갱신 자체였다. Root에서 Damage Mesh로 같은 책임을 옮기는 것만으로는 최적화가 되지 않는다.

이에 Damage Mesh를 서버 권위 전용으로 바꾼 뒤, 최종 단계에서 서버 Damage Mesh Overlap도 제거하고 projectile-driven Sweep으로 교체했다.

## 5. FHD 전후 프로파일

### 5.1 조건

- Map: `/Game/New/Level/Test_Level`
- 적 선박: 9척
- Dedicated Server + 별도 Client 프로세스
- Client 실제 Viewport: 1920×1080
- Development Editor target, D3D12
- Warmup 5초, CSV 600프레임, 선두 120프레임 제외 후 480프레임 통계

### 5.2 결과

| 지표 | 변경 전 | Authority Split | 변화 |
|---|---:|---:|---:|
| Frame 평균 | 45.581ms | 15.029ms | -67.0% |
| 환산 평균 FPS | 21.94 | 66.54 | +203.3% |
| Frame P95 | 50.340ms | 15.788ms | -68.6% |
| Game Thread | 45.578ms | 5.105ms | -88.8% |
| `SyncBodies` | 41.820ms | 1.985ms | -95.3% |
| GPU | 13.509ms | 13.748ms | +1.8% |

GPU가 약간 증가한 것은 GPU 회귀로 판단하지 않는다. 변경 전에는 Game Thread가 프레임을 충분히 공급하지 못했고, 변경 후에는 렌더 파이프라인이 정상적으로 더 많은 프레임을 처리한다. 최종 병목도 Game Thread에서 Render/GPU로 이동했다.

원시 자료:

- `Saved/Profiling/TestLevel/TestLevel_ActualFHD_Client_Baseline.csv`
- `Saved/Profiling/TestLevel/TestLevel_ActualFHD_Client_ShipSplit_v1.csv`
- `Saved/Profiling/TestLevel/TestLevel_ActualFHD_Client_ShipSplitAuthority.csv`
- `Saved/Logs/TestLevel_ShipSplitAuthority_FHD_Server.log`
- `Saved/Logs/TestLevel_ShipSplitAuthority_FHD_Client.log`

### 5.3 Sweep 전환 후 FHD 회귀 비교

디버그 렌더 최적화까지 적용된 직전 결과와 Sweep 전환 후 결과를 같은 FHD 600프레임 조건으로 비교했다.

| 지표 | Sweep 전 | Sweep 후 | 변화 |
|---|---:|---:|---:|
| Frame 평균 | 14.321ms | 14.368ms | +0.3% |
| Frame P95 | 15.137ms | 15.143ms | +0.04% |
| Game Thread | 4.656ms | 4.850ms | +4.2% |
| GPU | 12.988ms | 12.999ms | +0.09% |
| `SyncBodies` | 1.988ms | 1.999ms | +0.6% |
| 평균 Cannonball 수 | 19.96 | 21.56 | +8.0% |

Sweep 후 Cannonball 수가 약 8% 더 많았음에도 Frame/GPU/SyncBodies는 사실상 같은 범위였다. 따라서 클라이언트 성능 회귀는 관찰되지 않았다. Game Thread의 약 0.19ms 차이는 동적 Cannonball 수와 실행 편차가 함께 포함되므로 Sweep 자체의 순수 비용으로 해석하지 않는다.

이 비교는 클라이언트 회귀 검사다. 서버 Damage Hull Overlap 제거의 서버 측 이득은 별도의 대규모 선박 서버 A/B에서 측정해야 한다.

- `Saved/Profiling/TestLevel/TestLevel_RenderDebugGated_FHD_Client.csv`
- `Saved/Profiling/TestLevel/TestLevel_CannonSweep_FHD_Client.csv`
- `Saved/Logs/TestLevel_CannonSweep_FHD_Server.log`
- `Saved/Logs/TestLevel_CannonSweep_FHD_Client.log`

## 6. 기능 검증

- [x] Editor build 성공
- [x] `Test_Level`의 Player Ship 1척 + Enemy Ship 9척에 Split 컴포넌트 생성 확인
- [x] 네 컴포넌트가 임시로 기존 `SM_TestShip`을 공유함을 Editor asset inspection으로 확인
- [x] Root/Visual/Deck의 Overlap OFF 확인
- [x] 실제 FHD 클라이언트의 `SyncBodies` 감소 확인
- [x] 서버 대포 피해 80→60→40→20→0 로그 확인
- [x] 클라이언트에서 Damage Mesh Collision이 비활성임을 코드 정책으로 보장
- [x] SW Buoyancy/Network Physics 실행 경로 유지
- [x] `Swept Hit Ship` 서버 로그로 Overlap이 아닌 Sweep 접촉 확인
- [x] 기존 GAS SetByCaller `Data.Damage` 경로 유지
- [x] Enemy Cannon → Player Ship 피해 확인
- [x] Enemy Damage Hull이 Enemy Cannon Ignore / Player Cannon Block인 피아식별 매트릭스 확인
- [x] Player Damage Hull이 Player Cannon Ignore / Enemy Cannon Block인 피아식별 매트릭스 확인
- [x] 대포알 Pawn·Storage·일반 WorldDynamic Ignore
- [x] 같은 실행에서 서버 Ripple Submit 및 Revision 증가 확인
- [x] Sweep/Ripple 실행 중 Collision/Profile 오류 없음
- [x] Sweep 전환 후 실제 FHD 600프레임 성능 회귀 없음

Sweep 회귀 로그:

- `Saved/Logs/TestLevel_CannonSweep_Server.log`
- `Saved/Logs/TestLevel_CannonSweep_Client.log`

30초 실행에서 `Swept Hit Ship` 피해가 24회 기록되었고, 첫 다섯 발이 Health 80→60→40→20→0으로 기존 GAS 결과와 동일했다. 같은 서버 로그에서 WaterBody Ripple Submit이 연속 승인되고 활성 이벤트가 최대 8개까지 증가했다.

## 7. 제한 및 후속 작업

1. 서버 Damage Mesh도 이제 Overlap을 만들지 않는다. 다만 query broadphase에는 존재하므로 선박 수가 크게 늘면 저복잡도 Damage Hull과 현재 전체 Mesh를 비교해야 한다.
2. Single-process PIE는 같은 프로세스 안에서 서버 World 비용도 함께 부담하므로 별도 프로세스 Client보다 느릴 수 있다.
3. `ShipDeckMesh`는 갑판 전용 저복잡도 Mesh로 교체해야 한다.
4. WaterBody가 WorldStatic이므로 대포알은 다른 WorldStatic과도 Overlap callback을 받을 수 있다. 현재 handler는 비수면 WorldStatic을 즉시 무시한다. 이것이 측정 가능한 비용이 되면 Water 전용 Object Channel을 분리한다.
5. 현재 Damage Mesh는 Root Mesh를 복사한다. 정확한 선체 모양이 필요하다는 요구에는 맞지만, 최종적으로 단순화한 피격 Hull을 쓰면 query 비용과 메모리를 더 줄일 수 있다.

## 8. 체크리스트

- [x] Root의 물리/렌더/query 책임 분리
- [x] Visual Mesh NoCollision
- [x] Physics Root Overlap OFF
- [x] Deck Pawn Block / Overlap OFF
- [x] Damage query 서버 권위화
- [x] 서버 Damage Mesh Overlap 완전 제거
- [x] 대포알 Blocking Sweep 전환
- [x] Player/Enemy 피아식별 이중 검증
- [x] Pawn·Storage·일반 WorldDynamic 무시
- [x] WaterBody Overlap 및 서버 Ripple 유지
- [x] Legacy Buoyancy 비활성화
- [x] 변경 전/실패안/최종안 FHD 비교
- [x] 대포 피해 회귀 검증
- [ ] 갑판 전용 단순 Mesh 제작 및 적용
- [ ] 서버 대규모 선박 수 Damage Sweep query 프로파일
- [ ] Root Mesh와 저복잡도 전용 Damage Hull 비교
