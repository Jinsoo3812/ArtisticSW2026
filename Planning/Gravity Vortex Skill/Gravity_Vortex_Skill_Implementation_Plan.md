# 중력 소용돌이 투척 스킬 구현 계획

## 1. 문서 목적

플레이어가 조준 후 바다에 투사체를 던져, 일정 시간 동안 주변 적 함선을 중심으로 끌어당기는 스킬의 구현 계획을 정의한다.

현재 퀵슬롯과 정식 사용 조건은 미정이다. 최초 테스트 버전에서는 키보드 `3`을 누르고 있는 동안 조준하고, 조준 중 마우스 왼쪽 버튼을 누르면 투척한다.

이 문서는 구현 방향을 확정하기 위한 계획서이며 실제 C++/Blueprint/에셋 변경은 포함하지 않는다.

---

## 2. 결론 요약

### 2.1 Gameplay Ability로 구현한다

이 스킬은 다음 상태와 수명 주기를 갖기 때문에 GA가 적합하다.

- 키보드 `3` 입력으로 활성화 시도
- 입력을 누르는 동안 `State.Aiming` 유지
- 조준 중 왼클릭 Gameplay Event 수신
- 서버 권한으로 투사체 생성
- 투척 전 `3`을 놓으면 취소
- 쿨다운, 비용, 사용 불가 상태, 애니메이션, 퀵슬롯 연동을 추후 GAS 방식으로 확장 가능

GA는 입력과 조준/투척 절차만 책임진다. 비행은 투사체 Actor, 지속 효과와 대상 관리는 소용돌이 Field Actor, 함선에 대한 실제 힘 적용은 기존 Network Physics 경로가 각각 담당한다.

### 2.2 권장 책임 분리

```text
GA_GravityVortexThrow
  ├─ 조준 상태, 입력 대기, 투척 요청, 비용/쿨다운
  └─ 서버에서 Projectile 생성

GravityVortexProjectile
  ├─ 투사체 비행
  ├─ 배/플레이어/적/일반 Actor 충돌 무시
  └─ 바다 표면 도달 시 서버에서 Field 생성 후 소멸

GravityVortexField
  ├─ 범위/지속시간/감쇠 계산
  ├─ 범위 안 AEnemyShip만 추적
  ├─ 함선별 외부 가속도 Source 등록/갱신/해제
  └─ 적 함선 AI 추진 억제 Source 등록/해제

AShip + FShipPhysicsAsync
  ├─ 여러 외부 가속도 Source 합산
  ├─ FNetInputShip 입력 히스토리에 외부 가속도 기록
  └─ Physics Thread에서 질량 × 가속도를 AddForce
```

### 2.3 함선을 직접 이동시키지 않는다

다음 방식은 사용하지 않는다.

- `SetActorLocation`, `TeleportTo`로 함선을 당김
- Field Actor의 게임 스레드 Tick에서 `BuoyancyRoot->AddForce` 직접 호출
- 서버에서만 힘을 적용하고 클라이언트가 위치 복제만 따라가도록 처리
- AI가 계산한 기존 전진 입력에 반대 방향 입력을 섞어 당김 효과를 흉내 냄

이 방식들은 Network Physics의 예측/입력 히스토리/재시뮬레이션과 충돌하거나 부력 계산을 불안정하게 만들 수 있다.

기준안은 소용돌이가 계산한 **수평 외부 가속도**를 해당 물리 프레임의 `FNetInputShip`에 기록하고, `FShipPhysicsAsync::ProcessInputs_Internal`에서 Chaos Particle에 힘을 적용하는 것이다.

---

## 3. 현재 프로젝트 구조에서 확인된 사항

### 3.1 입력과 GAS

- `ABasePlayer`는 Gameplay Tag를 CRC 기반 InputID로 변환하여 ASC의 `AbilityLocalInputPressed/Released`로 전달한다.
- 마우스 입력은 GAS 입력 전달 외에도 Gameplay Event로 활성 GA에 전달된다.
- `Key.Default.Mouse.LeftClick` 및 Released 이벤트가 이미 존재한다.
- `UGA_BowAimFire`가 활성 상태에서 왼클릭 Gameplay Event를 기다리는 선행 패턴을 제공한다.
- `UGA_ThrowGrenade`가 조준 상태, 궤적, 서버 투사체 생성 패턴을 제공한다.
- `State.Aiming`은 기존 카메라 조준 전환에도 사용된다.

### 3.2 키보드 3 충돌 가능성

`3`은 현재 `Key.Item.3` 아이템 슬롯 입력으로 사용된다. 단순히 같은 키에 GA를 추가하면 아이템 장착과 스킬 조준이 동시에 시작될 가능성이 있다.

테스트 단계에서는 다음과 같이 분리한다.

- 신규 임시 Tag: `Key.Test.Skill.GravityVortex`
- 신규 테스트 Input Action: 예시 `IA_Test_GravityVortex`
- 신규 또는 테스트 전용 IMC에서 실제 키보드 `3`에 매핑
- 테스트 IMC는 Item IMC보다 높은 우선순위를 사용하고 입력을 Consume
- `DefaultAbilityMap` 또는 테스트 전용 grant 설정에서 위 Tag에 GA를 부여

즉, 실제 키는 `3`이지만 코드 의미를 `Key.Item.3`에 결합하지 않는다. 정식 퀵슬롯 정책이 확정되면 테스트 Tag/IMC만 제거하고 최종 Slot Tag로 교체한다.

### 3.3 함선 Network Physics

- `AShip`은 `UNetworkPhysicsComponent`와 `FShipPhysicsAsync`를 사용한다.
- 조종 입력은 `FNetInputShip`의 `MovementInput`, `SteeringInput`으로 입력 히스토리에 기록된다.
- 부력, 횡방향 Drag, 전진 힘, 회전 토크는 `FShipPhysicsAsync::ProcessInputs_Internal`에서 적용된다.
- AI 함선도 `UBTTask_NavalDrive`에서 `AShip::SetAIControlInput`을 호출하여 같은 Network Physics 입력 경로를 사용한다.
- 따라서 소용돌이 힘도 같은 Physics Thread 및 재시뮬레이션 입력 경로에 포함해야 한다.

---

## 4. 사용자 입력과 GA 상태 흐름

### 4.1 상태 흐름

```text
3 Started
  → GA 활성화/Commit
  → State.Aiming 추가
  → 로컬 조준 표시 및 투척 궤적 표시
  → 왼클릭 이벤트와 3 Released 대기

조준 중 Left Click Started
  → 중복 발사 방지
  → 클라이언트 조준 데이터 확보
  → 서버 검증
  → 투척 몽타주/Notify 또는 즉시 투사체 Spawn
  → State.Aiming 제거
  → GA 종료

투척 전 3 Released
  → 조준 취소
  → State.Aiming 제거
  → 궤적/VFX 정리
  → GA 취소 종료
```

### 4.2 GA 기본 정책

- 클래스 예시: `UGA_GravityVortexThrow : UBaseGameplayAbility`
- `InstancingPolicy`: `InstancedPerActor`
- `NetExecutionPolicy`: `LocalPredicted`
- 투사체 Spawn과 사용 결과 판정은 서버 권한
- `CommitAbility` 시점은 조준 진입 시점으로 시작하되, 실제 비용 소모 정책이 정해지면 투척 직전 Commit과 비교 검토
- 한 번의 활성화에서 투척은 1회만 허용
- 사망, 피격 강제 상태, 장비 전환, 다른 상호 배타적 공격 상태에서는 취소 또는 활성화 차단
- 우클릭 취소는 선택 사항이며 기존 투척 GA와 UX를 맞추려면 지원

### 4.3 조준 데이터

초기 버전은 기존 `GetBaseAimRotation()`과 플레이어/장착 지점의 Spawn 위치를 사용한다. 클라이언트가 보낸 방향은 서버에서 다음을 검증한다.

- 정규화된 유효 방향인지
- 서버 측 Control Rotation과 허용 각도 이상 차이나지 않는지
- Spawn 위치가 플레이어 또는 지정 소켓에서 허용 거리 안인지
- GA가 실제로 활성 상태이며 아직 발사하지 않았는지

추후 정확한 화면 지점 투척이 필요해지면 GAS TargetData 기반으로 교체한다.

---

## 5. 투사체 설계

### 5.1 역할

- 서버에서 Spawn되는 복제 Actor
- `UProjectileMovementComponent` 또는 프로젝트 표준 투사체 이동 사용
- 다른 배, 플레이어, 적, 일반 Actor와의 충돌은 전부 Ignore
- 바다 표면에 처음 도달했을 때만 발동
- 발동 즉시 서버에서 소용돌이 Field를 생성하고 투사체 제거
- 최대 수명 내 바다에 닿지 못하면 조용히 제거

### 5.2 바다 표면 판정 기준안

WaterBody의 단순 Collision만 신뢰하지 않는다. Water Body 설정에 따라 표면 Collision이 없거나 수면 높이와 Collision 형상이 일치하지 않을 수 있기 때문이다.

기준안은 서버에서 투사체의 이전 위치와 현재 위치에 대해 Water Body 수면 높이를 조회하고, 아래 조건을 만족하는 순간을 수면 도달로 판정하는 것이다.

```text
이전 위치 Z > 이전 위치의 수면 Z
현재 위치 Z <= 현재 위치의 수면 Z
현재 XY가 유효한 Water Body 영역 내부
```

- 파도가 적용된 실제 표면 높이 조회 API를 우선 사용한다.
- 수면 교차 지점은 이전/현재 signed height의 비율로 보간한다.
- 투사체가 수면 아래 Spawn된 예외는 첫 유효 조회에서 즉시 발동한다.
- 같은 프레임 다중 판정을 막기 위해 `bActivated` 가드를 둔다.
- Dedicated Server에서도 동일하게 판정되어야 하며 VFX Collision에 의존하지 않는다.

구현 착수 시 현재 레벨의 Ocean/WaterBody 설정과 Water Plugin 5.7 API를 먼저 확인한다. 만약 프로젝트의 바다가 전용 Collision Channel을 안정적으로 제공한다면, 그 채널 Sweep을 빠른 1차 판정으로 사용하고 Water Surface Query를 최종 검증으로 사용한다.

### 5.3 투사체 에디터 파라미터

- `ProjectileSpeed`
- `ProjectileGravityScale`
- `MaxProjectileLifetime`
- `CollisionRadius` 또는 수면 교차 허용 오차
- `LaunchUpwardBias`
- `ProjectileClass`
- `FieldClass`
- 투사체 Mesh/Trail/VFX/SFX

투사체 속도는 GA와 Projectile 양쪽에 중복 저장하지 않는다. 기준 소유자를 하나로 정한다. 권장안은 GA의 설정 구조체가 Spawn 초기화 데이터로 투사체에 전달하는 방식이다.

---

## 6. 소용돌이 Field와 대상 선정

### 6.1 Field Actor

클래스 예시: `AGravityVortexField`

- 서버 권한으로 지속시간과 대상 관리를 수행
- 클라이언트에는 중심, 반경, 시작 서버 시간, 종료 서버 시간을 복제하여 VFX만 재생
- 전용 Collision Overlap에만 의존하지 않고 서버 주기 Query를 기준으로 대상 집합을 갱신
- Field 종료, 대상 이탈, 대상 Destroy 시 등록했던 효과를 반드시 해제

### 6.2 대상 필터

초기 버전의 유효 대상은 `AEnemyShip`만 허용한다.

- Player Ship 제외
- Player Character 제외
- 일반 Enemy Character 제외
- 다른 투사체 및 환경 Actor 제외
- 죽었거나 Physics Root가 유효하지 않은 적 함선 제외

단순 Actor Tag보다 `AEnemyShip` 타입 검사를 우선하여 Player Ship이 잘못 포함될 가능성을 제거한다. 향후 진영 시스템이 생기면 Team/Faction 인터페이스로 교체한다.

### 6.3 범위 판정

- XY 평면 거리 `Dist2D` 사용
- 중심과 함선 Physics Root 중심 사이 거리 기준
- `PullRadius` 밖에서는 효과 없음
- 범위 안으로 새로 들어온 적 함선도 남은 지속시간 동안 적용
- 범위 밖으로 나간 즉시 외부 힘 및 AI 추진 억제 Source 해제
- 여러 Field가 겹치면 Source별 가속도를 합산하되 최종 가속도 상한 적용

### 6.4 힘 곡선

기본 방향은 수평면에서 함선 중심에서 Field 중심으로 향한다.

```text
ToCenter = FieldCenter - ShipPhysicsPosition
ToCenter.Z = 0
DistanceAlpha = clamp(Distance / PullRadius, 0, 1)
Acceleration = Normalize(ToCenter) × PullAcceleration × Falloff(DistanceAlpha)
```

수직 힘은 기본적으로 0으로 고정한다. 수직으로 당기면 부력과 파고 계산을 방해해 함선이 가라앉거나 튀는 문제가 생길 수 있다.

중심 부근의 진동과 과속을 막기 위해 다음을 둔다.

- `InnerDeadZoneRadius`: 중심 극근처 힘 감소 또는 0
- `MaxPullAcceleration`: 겹친 Field 포함 최종 가속도 상한
- 선택적 `RadialDamping`: 중심 방향 속도가 너무 크면 감쇠
- 선택적 `MaxInwardSpeed`: 중심 방향 최대 접근 속도

단순 Force 값만 노출하면 배의 질량 차이에 따라 체감이 크게 달라진다. 디자이너가 다루는 주 파라미터는 `PullAcceleration`으로 하고, Physics Thread에서 `Mass × Acceleration`으로 Force를 계산한다. 필요하면 안전 상한인 `MaxPullForce`도 함께 둔다.

### 6.5 Field 에디터 파라미터

- `PullRadius`
- `PullAcceleration`
- `MaxPullAcceleration`
- `MaxPullForce`
- `Duration`
- `TargetRefreshInterval`
- `InnerDeadZoneRadius`
- `FalloffCurve` (`UCurveFloat`, 미설정 시 기본 선형/완만 감쇠)
- `RadialDamping`
- `MaxInwardSpeed`
- `bSuppressEnemyPropulsion`
- Field VFX/SFX/Decal 관련 설정
- 디버그 범위/힘 벡터 표시 여부

모든 밸런스 값은 C++ `EditDefaultsOnly`/`BlueprintReadOnly`로 노출하고 실제 튜닝은 GA/Projectile/Field Blueprint 기본값에서 수행한다. 서로 관련된 값은 `FGravityVortexSkillConfig` 구조체 또는 Primary Data Asset로 묶는 방안도 가능하다. 첫 구현은 Blueprint 기본값으로 시작하되 중복 저장은 금지한다.

---

## 7. Network Physics 적용 상세

### 7.1 기준안: 프레임별 외부 가속도를 Network Physics 입력에 포함

`FNetInputShip`에 소용돌이 및 향후 외부 힘을 위한 필드를 추가한다.

예시:

```cpp
FVector ExternalAcceleration;
```

실제 구현에서는 네트워크 대역폭을 줄이기 위해 범위가 제한된 Quantize 또는 축별 정수 직렬화를 사용한다.

변경 흐름은 다음과 같다.

1. `AGravityVortexField`가 서버에서 대상별 수평 가속도를 계산한다.
2. `AShip`의 Source Map에 Field 고유 ID별 가속도를 등록/갱신한다.
3. `AShip::Tick`에서 활성 Source를 합산하고 상한을 적용한다.
4. 합산값을 `FAsyncInputShip`에 전달한다.
5. `FShipPhysicsAsync::BuildInput_Internal`이 해당 값을 `FNetInputShip`에 기록한다.
6. 서버 입력이 Network Physics 히스토리와 클라이언트로 전달된다.
7. `ApplyInput_Internal`은 정상 시뮬레이션과 재시뮬레이션에서 그 프레임의 외부 가속도를 복원한다.
8. `ProcessInputs_Internal`이 Physics Thread의 현재 질량을 얻어 `Mass × ExternalAcceleration`을 `ParticleHandle->AddForce`로 적용한다.

이 방식의 장점은 재시뮬레이션 시 “현재 Field 상태”를 다시 조회하지 않고, 해당 과거 프레임에 실제로 기록된 외부 입력을 그대로 재생한다는 점이다.

### 7.2 필요한 Network Physics 변경

`FNetInputShip`

- `ExternalAcceleration` 추가
- `Reset`, `InterpolateData`, `MergeData`, `NetSerialize` 반영
- 서버 Validate에서 유한값 검사 및 최대 크기 Clamp
- 직렬화 버전 호환이 필요한지 확인

`FAsyncInputShip`

- GT → PT 전달용 `ExternalAcceleration` 추가
- `Reset` 반영

`FShipPhysicsAsync`

- 내부 캐시 `ExternalAcceleration_Internal` 추가
- `BuildInput_Internal`/`ApplyInput_Internal`에 포함
- `ProcessInputs_Internal`에서 수평 힘 적용
- 디버그 시 제어 힘과 외부 힘을 분리 출력

`AShip`

- Source ID 기반 외부 가속도 등록/해제 API
- 합산/상한 처리
- EndPlay 시 Source 정리
- NaN/Inf, 과도한 값, 오래 갱신되지 않은 Source 방어

### 7.3 서버 권한

- 대상 Query와 가속도 계산은 서버만 수행한다.
- 클라이언트가 Field 반경, 힘, 대상 목록을 결정하지 않는다.
- 클라이언트 Field Actor는 시각화만 담당한다.
- 투척 요청은 서버에서 GA 활성 상태와 조준 데이터를 검증한다.

### 7.4 향후 Player Ship까지 적용할 경우

현재 요구사항은 Player Ship 제외이므로 서버가 작성한 적 AI 함선 입력만 처리하면 된다.

향후 Autonomous Proxy인 Player Ship도 끌어당겨야 한다면 다음 단계가 추가로 필요하다.

- 로컬 예측에 사용할 복제된 Field Descriptor
- 시작/종료 Server Physics Frame 동기화
- 로컬과 서버가 동일한 거리 감쇠를 계산하도록 PT-safe 데이터 전달
- 입력 소유권과 서버 외부 입력 병합 정책

이 확장 전에는 현재 기준안을 Player Ship에 단순 적용하지 않는다.

---

## 8. 적 함선 AI 추진 제어

### 8.1 Behavior Tree 전체 Pause는 기본안에서 제외

AI Controller/Brain 전체를 Pause하면 다음 문제가 생길 수 있다.

- 사격, 타겟 갱신, 사망 처리까지 함께 멈출 가능성
- 여러 소용돌이가 겹칠 때 Resume 순서 문제
- Field 종료 전에 배가 죽거나 Controller가 교체되는 예외 처리 복잡도 증가
- Behavior Tree Task의 내부 상태와 Timer가 예상치 못하게 건너뛰는 문제

필요한 것은 “자체 추진력 중지”이므로 의사결정은 계속하게 두고 이동 입력만 차단한다.

### 8.2 Source 기반 추진 억제

`AShip` 또는 `AEnemyShip`에 다음 의미의 API를 둔다.

```text
AddPropulsionSuppression(SourceId)
RemovePropulsionSuppression(SourceId)
IsPropulsionSuppressed()
```

- 첫 Source가 들어오면 `CurrentMoveInput`, `CurrentTurnInput`을 즉시 0으로 설정
- 억제 중 `SetAIControlInput`은 입력을 저장하지 않거나 최종 출력에 반영하지 않음
- 마지막 Source가 제거되면 BT의 다음 Tick에서 새 입력을 받아 자연스럽게 재개
- bool 하나가 아닌 Source Set/Count를 사용하여 중첩 Field에 안전하게 대응
- Field 종료/대상 이탈/Field Destroy/Ship EndPlay 모든 경로에서 해제

초기 버전은 전진과 회전 입력을 모두 0으로 한다. 외부 소용돌이 힘과 기존 부력/Drag는 계속 동작한다.

사격 AI까지 멈출지는 별도 기획 옵션이다. 현재 요구사항에서는 함포 조준/사격은 유지하고 추진만 차단한다.

---

## 9. 예상 클래스와 파일 배치

실제 구현 시 모듈 의존성을 확인한 뒤 최종 배치를 확정한다.

### ClassFeature 모듈 후보

- `Public/Attacker/GA_GravityVortexThrow.h`
- `Private/Attacker/GA_GravityVortexThrow.cpp`
- `Public/Projectiles/GravityVortexProjectile.h`
- `Private/Projectiles/GravityVortexProjectile.cpp`

### WaterAndShip 모듈 후보

- `Public/Effects/GravityVortexField.h`
- `Private/Effects/GravityVortexField.cpp`
- 기존 `Ship.h/.cpp`
- 기존 `ShipPhysicsAsync.h/.cpp`

Field가 `AEnemyShip`에 직접 의존하면 `WaterAndShip → Enemy` 순환 의존이 생길 수 있다. 이를 피하기 위해 다음 중 하나를 선택한다.

1. **권장:** WaterAndShip에 `IShipExternalForceTarget` 또는 AShip의 `bIsEnemyShip`/Faction Query API를 두고 Field는 AShip만 의존한다.
2. Field를 Enemy 또는 상위 게임 모듈에 배치하여 `Enemy → WaterAndShip` 기존 방향을 유지한다.
3. 서버 Query는 상위 모듈의 Subsystem이 담당하고 WaterAndShip은 힘 입력 API만 제공한다.

구현 전 Build.cs 의존성 그래프를 확인하고 순환 의존이 없는 위치를 택한다. 단순히 `Cast<AEnemyShip>`를 위해 WaterAndShip에서 Enemy를 참조하지 않는다.

---

## 10. 구현 단계

### Phase 0. 에셋/환경 확인

- 테스트 레벨의 WaterBody/Ocean 종류와 수면 높이 조회 가능 여부 확인
- 키보드 3에 연결된 현재 IA/IMC와 Consume 정책 확인
- 플레이어의 투척 Spawn 소켓/장착 아이템 요구 여부 결정
- 적 함선 식별 방식과 `bEnemyShip` 접근 API 확인
- Dedicated Server에서 Water Query 동작 확인

완료 조건: 바다 표면 판정 API, 테스트 입력 충돌 회피 방식, 모듈 배치가 확정됨.

### Phase 1. Network Physics 외부 힘 경로

- `FNetInputShip`에 외부 가속도 입력 추가
- NetSerialize/보간/병합/검증 구현
- `FAsyncInputShip` 및 `FShipPhysicsAsync` 연결
- Physics Thread에서 질량 반영 수평 힘 적용
- `AShip` Source 기반 외부 가속도 API 구현
- 단일 서버 디버그 명령 또는 테스트 Actor로 일정 가속도 적용 검증

완료 조건: 서버와 클라이언트에서 같은 방향으로 움직이고, 외부 힘 적용 중 재시뮬레이션 후에도 큰 위치 발산이 없음.

### Phase 2. AI 추진 억제

- Source 기반 Propulsion Suppression 구현
- `SetAIControlInput` 최종 경계에서 차단
- 중첩 Source, 이탈, Destroy, 사망 예외 처리
- BT는 계속 Tick하지만 Network Move/Turn 입력은 0인지 검증

완료 조건: 끌리는 동안 자체 전진/회전 힘이 없고, 해제 후 다음 BT Tick에서 정상 주행 재개.

### Phase 3. Field Actor

- 서버 대상 Query와 AShip 필터 구현
- 거리 감쇠/Dead Zone/상한 구현
- 외부 가속도 Source 및 추진 억제 Source 수명 관리
- 중심/반경/시작·종료 시간 복제
- 디버그 Draw 및 최소 VFX Hook 추가

완료 조건: 레벨에 직접 배치한 Field가 적 함선만 지속적으로 끌어당기며 Player Ship은 무시.

### Phase 4. 투사체와 바다 발동

- 다른 Actor 충돌 Ignore 설정
- 서버 수면 교차 Query 구현
- 교차 지점에 Field Spawn
- 중복 발동 방지와 Max Lifetime 처리
- 클라이언트 투사체 및 발동 VFX 동기화

완료 조건: 배/캐릭터를 통과하고 바다 표면에서만 한 번 발동.

### Phase 5. GA 및 테스트 입력

- GA 조준/왼클릭 투척/3 Released 취소 구현
- `State.Aiming`과 카메라/궤적 표시 연결
- 서버 Spawn 검증
- `Key.Test.Skill.GravityVortex` + 테스트 IMC로 키보드 3 임시 연결
- BP 기본값에서 Projectile/Field 및 튜닝값 할당

완료 조건: `3` 홀드 → 조준, 왼클릭 → 투척, `3` 선해제 → 취소가 멀티플레이에서 동일하게 동작.

### Phase 6. 멀티플레이 및 회귀 테스트

- Dedicated Server + Client 1~2 환경
- 패킷 지연/손실 환경에서 투척 및 Field 수명 확인
- Network Physics resim 발생 시 위치/속도 발산 확인
- 다수 적 함선과 중첩 Field 성능 확인
- 기존 플레이어 배 조종, 적 AI 주행, 부력/파도 회귀 확인

완료 조건: 아래 수용 기준과 테스트 매트릭스를 통과.

---

## 11. 테스트 매트릭스

### 입력/GA

- `3` Started 시 한 번만 GA 활성화
- `3` 홀드 중 조준 상태 유지
- 조준 전/GA 비활성 상태의 왼클릭은 투척하지 않음
- 조준 중 왼클릭 연타에도 투사체 1개만 생성
- 투척 전 `3` Released 시 취소
- 사망/장비 전환/다른 배타적 상태에서 정리 누락 없음
- 키보드 3 입력이 기존 Item Slot 3을 동시에 실행하지 않음

### 투사체/바다

- Player Character 통과
- Enemy Character 통과
- Player Ship 통과
- Enemy Ship 통과
- 환경 Actor 통과 여부가 기획대로 Ignore
- 평수면과 파도 crest/trough에서 실제 표면 높이에 발동
- 바다 밖 지면에 떨어진 경우 발동하지 않고 수명 만료
- 서버와 각 클라이언트에서 Field 중심 오차가 허용 범위 이내

### 대상/AI

- 범위 안 AEnemyShip만 적용
- Player Ship 제외
- 범위 진입/이탈에 따라 적용/해제
- Field 종료 후 AI 추진 정상 복구
- 중첩 Field 하나가 끝나도 나머지 Field의 억제 유지
- 대상 Ship 또는 Field Destroy 시 Source 누수 없음
- 사격 AI 유지 여부가 현재 정책과 일치

### 물리/네트워크

- 힘 적용 위치가 Physics Thread임을 로그/Trace로 확인
- `FNetInputShip`의 외부 가속도가 정상/재시뮬레이션 프레임에 동일하게 복원
- Pull 중 부력 지속
- 수직 침강/비정상 Roll/Pitch 폭주 없음
- 중심 근처 진동과 과속이 허용 수준
- 5 cm Network Physics 보정 기준에서 과도한 연속 rollback이 발생하지 않음
- Dedicated Server와 Client의 위치/선속도 차이 기록
- Field 1개 × 적 함선 10/25/50척 성능 측정

---

## 12. 수용 기준

1. 테스트 키보드 `3`을 누르면 조준 상태에 진입한다.
2. `3`을 유지한 상태에서 왼클릭하면 서버 권한 투사체가 한 번 발사된다.
3. 투척 전 `3`을 놓으면 취소된다.
4. 투사체는 배, 플레이어, 일반 적 등과 충돌해 발동하지 않는다.
5. 투사체는 실제 바다 표면에 도달했을 때 한 번만 발동한다.
6. Field 범위 안의 적 함선만 중심으로 끌려오며 Player Ship은 영향을 받지 않는다.
7. 범위, 가속도/힘 상한, 투사체 속도, 지속시간, 감쇠 등 핵심 값은 Editor에서 조정 가능하다.
8. 끌리는 동안 적 함선의 자체 전진/회전 추진 입력은 0이며 부력과 외부 힘은 유지된다.
9. Field 종료 또는 이탈 후 AI 주행이 자동 복구된다.
10. 외부 힘은 `FShipPhysicsAsync`에서 적용되고 Network Physics 입력 히스토리에 포함된다.
11. 멀티플레이 재시뮬레이션 중 소용돌이 입력이 과거 프레임에 동일하게 재생된다.
12. 기존 함선 조종, 적 AI 주행, 부력 시스템에 회귀 문제가 없다.

---

## 13. 구현 전에 결정하면 좋은 기획 항목

아래 항목은 현재 구현을 막지는 않으며 문서의 기본값대로 시작할 수 있다.

- 조준 중 캐릭터 이동/회전/스프린트 허용 여부
- 투척 몽타주와 Notify 시점 사용 여부
- 소용돌이 범위에 나중에 진입한 적도 영향을 받는지 여부 — 현재 계획은 적용
- 중심 도달 후 고정/공전/통과 중 어떤 움직임이 좋은지 — 현재 계획은 Dead Zone + 감쇠
- 적 함선의 함포 사격도 중지할지 여부 — 현재 계획은 추진만 중지
- 여러 소용돌이의 힘이 합산되는지 여부 — 현재 계획은 합산 후 상한
- 쿨다운과 자원 비용
- 투사체가 바다 외 지형에 닿았을 때 소멸할지, 계속 통과할지 — 현재 계획은 Ignore 후 수명 만료

---

## 14. 주요 위험과 대응

| 위험 | 영향 | 대응 |
|---|---|---|
| 게임 스레드 직접 AddForce | 재시뮬레이션 불일치 | 프레임별 외부 가속도를 FNetInputShip에 기록하고 PT에서 적용 |
| 수직 힘이 부력과 충돌 | 침강, 튐, 회전 폭주 | 기본 Z=0, 수평 힘만 적용 |
| 키보드 3이 Item Slot 3과 중복 | 장착과 GA 동시 실행 | 별도 Test Tag/IA/고우선 IMC로 임시 분리 |
| WaterBody Collision 불일치 | 수면 위/아래 오발동 | Water Surface 높이 교차 Query를 권위 판정으로 사용 |
| AI Brain 전체 Pause | 사격/사망/복구 문제 | 최종 추진 입력만 Source 기반 억제 |
| Field 중첩 후 조기 Resume | 한 Field가 남았는데 AI 재가동 | Source ID Set/Count 사용 |
| 중심 근처 과속/진동 | 비정상 물리 움직임 | Dead Zone, 감쇠, 가속도/힘 상한 |
| 모듈 순환 의존 | 빌드 불가 | WaterAndShip은 Enemy 타입 직접 참조하지 않고 인터페이스/상위 모듈 사용 |
| 매 프레임 네트워크 입력 증가 | 대역폭 증가 | 외부 가속도 Quantize, 활성 시 플래그 기반 직렬화, 프로파일링 |

---

## 15. 최종 권장 구현 순서

시각 효과나 GA부터 만들기 전에 다음 순서를 지킨다.

1. Network Physics 외부 가속도 입력 경로
2. AI 추진 억제 API
3. 레벨 배치형 Field 테스트
4. 바다 표면 판정과 투사체
5. GA와 키보드 3 테스트 입력
6. 멀티플레이/재시뮬레이션 검증

가장 위험한 부분은 투척 UX가 아니라 함선 물리의 결정성과 복구이므로, Field를 레벨에 직접 배치해 물리 경로를 먼저 검증한 뒤 GA와 투사체를 연결한다.
