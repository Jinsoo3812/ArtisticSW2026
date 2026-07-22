# 부력 컴포넌트 변경 및 운영 정리

> 기준일: 2026-07-22. 선박/강체 부력과 플레이어 수영을 혼동하지 않도록 현재 구현과 이번 수영 작업의 변경 범위를 기록한다.

## 결론

프로젝트에는 이름은 비슷하지만 실행 방식이 다른 두 시스템이 있다.

| 대상 | 시스템 | 물리 소유자 | 이번 수영 작업 영향 |
|---|---|---|---|
| 플레이어 캐릭터 | `USwimmingComponent` + `USWCharacterMovementComponent` | CMC Custom Movement | 변경됨 |
| 선박 | `USWBuoyancyComponent` + `FShipPhysicsAsync` | Network Physics / Chaos Physics Thread | 변경 없음 |
| 상자 등 일반 강체 | `USWBuoyancyComponent` | 서버 권한 Chaos | 변경 없음 |

따라서 플레이어가 잠수하거나 수면을 따라 움직이는 로직을 수정할 때 `USWBuoyancyComponent`의 Force Settings, Pontoon, Ship Physics 코드를 수정하면 안 된다.

## 1. `USWBuoyancyComponent`: 선박·강체용

위치:

- `Source/WaterAndShip/Public/Buoyancy/SWBuoyancyComponent.h`
- `Source/WaterAndShip/Private/Buoyancy/SWBuoyancyComponent.cpp`

### 역할

- Chaos Rigid Body의 각 폰툰(Pontoon)에 대해 WaterBody 수면 높이와 수면 속도를 조회한다.
- `FSWBuoyancyMath::SolvePontoon`으로 잠긴 구체 체적, 감쇠, 위쪽 부력을 계산한다.
- 계산된 힘을 `AddForceAtLocation`으로 강체에 적용한다.
- 여러 WaterBody가 겹치면 가장 높은 유효 수면을 선택한다.
- Water query에 `IncludeWaves`를 사용하므로 Gerstner 파도를 포함한다.

### 설정

| 설정 | 의미 |
|---|---|
| `Pontoons` | 로컬 위치, 반지름, 개별 Force Scale을 가진 부력 샘플점 |
| `ForceSettings` | 부력 계수, 선형/제곱 감쇠, 최대 부력, 깊은 물 회복 배율 |
| `ExecutionMode` | 힘을 직접 적용할 주체를 결정 |
| `bImportLegacyWaterBuoyancy` | 기존 UE Water `UBuoyancyComponent` 설정을 한 번 가져오는 마이그레이션 브리지 |

`DeepWaterBuoyancyMultiplier`는 수면 근처 평형을 바꾸지 않고, 완전히 잠긴 뒤의 회복 부력만 강화한다. 현재 상자 기본값은 `3.0`이고 일반 Ship 기본값은 `1.0`이다.

### 실행 모드와 네트워크

| 모드 | 사용 대상 | 힘 적용 |
|---|---|---|
| `ServerAuthority` | Storage 등 일반 강체 | 서버 Game Thread에서만 `USWBuoyancyComponent`가 적용, 클라이언트는 Replicated Movement 수신 |
| `ExternalNetworkPhysics` | Ship | 컴포넌트는 폰툰/Force Settings 원본만 제공, `FShipPhysicsAsync`가 Physics Thread에서 Network Physics prediction/resimulation 중 힘을 계산 |

Ship은 매 Game Thread 프레임에 컴포넌트의 Pontoon/Force Settings와 파도·Ripple 데이터를 Async Input으로 전달한다. `FShipPhysicsAsync`는 이 캐시를 사용해 Physics Thread에서 같은 부력 해석을 수행한다. 이중으로 `USWBuoyancyComponent::TickComponent`가 Ship에 힘을 가하지 않도록 Execution Mode가 분리되어 있다.

### 레거시 마이그레이션

기존 Blueprint의 Water Plugin `UBuoyancyComponent`를 즉시 제거하지 않아도 된다.

1. `bImportLegacyWaterBuoyancy`를 켜면 SW Pontoon 배열이 비어 있을 때만 기존 값을 가져온다.
2. 가져온 뒤 기존 컴포넌트는 Deactivate 및 Tick 비활성화된다.
3. SW Pontoon이 명시적으로 설정된 에셋은 SW 설정을 우선한다.

## 2. 플레이어 `USwimmingComponent`: CMC 수영용

위치:

- `Source/ClassFeature/Public/SwimmingComponent.h`
- `Source/ClassFeature/Private/SwimmingComponent.cpp`

플레이어는 Chaos 강체가 아니다. `USWCharacterMovementComponent::PhysCustom`이 `USwimmingComponent::UpdateSwimmingMovement`를 호출하고, `SafeMoveUpdatedComponent` sweep/slide로 이동을 결정한다.

### 이번 수영 작업에서 변경한 내용

- 수면 모드: 현재 파도 수면 높이를 목표로 하는 감쇠 스프링으로 높이를 추적한다.
- 잠수 모드: Ctrl 입력 후에는 위쪽 부력 없이 Z 속도를 감쇠해 선택한 깊이를 유지한다.
- 실제 수중 상태: 머리 위치와 수면 사이에 진입/이탈 히스테리시스를 적용해 파도에 따른 애님 깜빡임을 막는다.
- 애님 데이터: 수영 컴포넌트가 상태 스냅샷을 만들고 AnimInstance 프록시로 복사한다.
- 입력: Space/Ctrl의 수직 입력은 서버 RPC로 동기화한다.

### 이번 최적화 정리

수면 스프링/중성 잠수 모델로 바꾼 뒤에도 남아 있던 `FSWBuoyancyMath::SolvePontoon` 호출을 제거했다.

- 제거 대상: 캐릭터 수영 틱에서 결과가 사용되지 않던 질량, 중력, 잠긴 체적, 부력, 감쇠 계산과 관련 파라미터.
- 유지 대상: WaterBody 수면 높이 조회, 파도 추적, 수면/수중 판정, 얕은 물 Walking 전환, 폰툰 디버그 표시.
- 영향 없음: `USWBuoyancyComponent`, Ship Network Physics, Storage 강체 부력.

## 3. 수정 시 경계 규칙

1. 캐릭터의 수영 감각/잠수/애님을 수정한다면 `USwimmingComponent`만 검토한다.
2. Ship의 부력 안정성·회전·파도 동기화를 수정한다면 `USWBuoyancyComponent`, `AShip`, `FShipPhysicsAsync`를 함께 검토한다.
3. Storage 같은 단순 강체는 `ServerAuthority` 모드를 유지한다. 클라이언트에서 별도로 힘을 적용하지 않는다.
4. Ship을 `ServerAuthority`로 전환하거나 Ship에서 `USWBuoyancyComponent` Tick force를 켜면 Network Physics 힘과 중복될 수 있다.
5. Water query/폰툰 수를 늘린 뒤에는 서버 및 클라이언트 PIE에서 CPU/Physics 시간과 위치 보정을 함께 확인한다.

## 4. 검증 상태

- 2026-07-22: 플레이어의 사용되지 않는 부력 해석 제거 후 `ArtisticSW2026Editor Win64 Development` 빌드 성공.
- 이 변경은 코드 정리/최적화만 수행했으며 Ship/Storage 부력 튜닝값이나 네트워크 정책은 변경하지 않았다.

