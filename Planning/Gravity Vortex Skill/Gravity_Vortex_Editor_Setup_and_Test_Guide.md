# 중력 소용돌이 스킬 에디터 설정 및 테스트 가이드

## 1. 현재 상태

C++ 기본 클래스만으로 즉시 테스트할 수 있다. 별도 Input Action이나 Input Mapping Context를 만들 필요는 없다.

- 키보드 `3`: 테스트 스킬 조준 시작
- `3`을 누른 채 마우스 왼클릭: 투척
- 투척 전에 `3` 해제: 취소
- 테스트 입력이 켜진 동안 기존 Item Slot 3 장착 동작은 의도적으로 차단됨
- 투사체 Mesh가 없어도 노란 디버그 구체로 위치를 확인할 수 있음
- Field VFX가 없어도 청록색 범위 원과 보라색 중심 Dead Zone으로 확인 가능

기본 동작 값:

| 항목 | 기본값 |
|---|---:|
| 투사체 속도 | 2200 cm/s |
| 투사체 수명 | 10초 |
| 범위 | 5000 cm |
| Pull Acceleration | 900 cm/s² |
| Max Pull Acceleration | 1800 cm/s² |
| 지속시간 | 6초 |
| 중심 Dead Zone | 250 cm |
| 적 함선 추진 억제 | 켜짐 |

## 2. 선택적으로 만들 Blueprint

시각 에셋과 에디터 튜닝을 적용하려면 아래 3개 Blueprint를 만드는 것을 권장한다.

### 2.1 BP_GravityVortexField

1. Content Browser에서 Blueprint Class 생성
2. 부모 클래스로 `GravityVortexField` 선택
3. 예시 위치: `/Game/Blueprints/Skills/GravityVortex/BP_GravityVortexField`
4. Class Defaults의 `Gravity Vortex` 카테고리에서 범위, 힘, 지속시간 등을 조정
5. VFX를 붙이려면 Niagara/Particle/Audio Component를 Blueprint에 추가

주요 튜닝 항목:

- `Pull Radius`
- `Pull Acceleration`
- `Max Pull Acceleration`
- `Inner Dead Zone Radius`
- `Pull Falloff Curve`
- `Radial Damping`
- `Max Inward Speed`
- `Duration`
- `Target Refresh Interval`
- `Suppress Enemy Propulsion`

### 2.2 BP_GravityVortexProjectile

1. 부모 클래스로 `GravityVortexProjectile` 선택
2. 예시 위치: `/Game/Blueprints/Skills/GravityVortex/BP_GravityVortexProjectile`
3. `Visual Mesh`에 원하는 Mesh와 Material 지정
4. Class Defaults의 `Field Class`를 위에서 만든 `BP_GravityVortexField`로 지정
5. 필요하면 Trail Niagara Component 추가
6. `Projectile Movement`에서 Gravity Scale과 회전 설정 조정

투사체는 Collision을 사용하지 않고 Water Surface Query로 발동한다. Blueprint에서 Collision을 Block으로 바꾸지 않는다.

### 2.3 GA_GravityVortexThrow Blueprint

1. 부모 클래스로 `GA_GravityVortexThrow` 선택
2. 예시 위치: `/Game/Blueprints/Skills/GravityVortex/GA_GravityVortexThrow_BP`
3. `Projectile Class`를 위에서 만든 `BP_GravityVortexProjectile`로 지정
4. Throw Speed, Upward Aim Bias, Spawn Offset, Socket Name 등을 조정
5. `BP_BasePlayerTest`의 Class Defaults에서 다음 값을 확인
   - `Enable Gravity Vortex Test Input`: 켜짐
   - `Gravity Vortex Test Ability Class`: `GA_GravityVortexThrow_BP`

Blueprint를 만들지 않으면 `Gravity Vortex Test Ability Class`의 기본 C++ GA가 사용된다.

## 3. Test_Level 1인 테스트

테스트 맵: `/Game/New/Level/Test_Level`

헤드리스 확인 결과 이 맵에는 `BP_EnemyShip` 9척이 배치되어 있다.

1. `Test_Level`을 연다.
2. Play Mode를 `Selected Viewport`, Players를 1로 두고 PIE를 시작한다.
3. 캐릭터가 바다와 적 함선을 향하도록 시점을 조정한다.
4. 키보드 `3`을 누르고 유지한다.
5. 다음을 확인한다.
   - 카메라가 기존 `State.Aiming` 상태로 전환됨
   - 투척 예상 궤적 Debug가 표시됨
6. `3`을 유지한 채 마우스 왼클릭한다.
7. 다음을 확인한다.
   - 노란 디버그 구체 또는 지정한 Mesh가 궤적을 따라 이동
   - 배, 플레이어, 적과 접촉해도 중간 발동하지 않음
   - 수면에 닿으면 투사체가 사라짐
   - 청록색 반경 원과 보라색 중심 원이 표시됨
   - 범위 안 적 함선이 중심으로 당겨짐
   - Player Ship은 당겨지지 않음
   - 약 6초 후 Field가 사라지고 적 함선 AI가 다시 주행

취소 테스트:

1. `3`을 누른다.
2. 왼클릭하지 않고 `3`을 놓는다.
3. 조준 상태가 종료되고 투사체가 생성되지 않는지 확인한다.

## 4. 힘 튜닝 순서

처음에는 Field Blueprint에서 아래 순서로 조정한다.

1. `Pull Radius`: 몇 척을 포함할지 결정
2. `Pull Acceleration`: 기본 당김 체감 조정
3. `Duration`: 목표 이동 거리 조정
4. `Max Inward Speed`: 중심으로 과도하게 가속되는 현상 제한
5. `Radial Damping`: 중심 진동 완화
6. `Inner Dead Zone Radius`: 중심에서 배들이 겹치거나 튀는 현상 완화
7. 필요하면 `Pull Falloff Curve`로 거리별 힘 수정

권장 첫 조정 범위:

- Pull Radius: 3000~7000 cm
- Pull Acceleration: 400~1400 cm/s²
- Duration: 3~8초
- Max Inward Speed: 1200~3000 cm/s
- Inner Dead Zone: 200~600 cm

함선이 침강하거나 튀는 문제가 보이면 Pull Acceleration을 바로 올리기보다 Max Inward Speed와 Radial Damping부터 조정한다. 소용돌이 힘의 Z는 코드에서 0으로 고정되어 있다.

## 5. 멀티플레이 테스트

1. PIE Players를 2로 설정한다.
2. 가능하면 `Run Dedicated Server`를 켠다.
3. Client 1에서 `3` 홀드 후 왼클릭으로 투척한다.
4. 양쪽 Client에서 다음을 비교한다.
   - 투사체 궤적과 수면 발동 위치
   - Field 중심과 지속시간
   - 적 함선의 이동 방향
   - Field 종료 후 AI 주행 복구 시점
5. 두 Client가 각각 Field를 던져 범위를 겹친다.
6. 한 Field만 먼저 종료되어도 나머지 Field의 힘과 추진 억제가 유지되는지 확인한다.

Network Emulation을 사용할 수 있다면 100~150 ms 지연과 소량의 Packet Loss에서도 반복한다.

## 6. 확인할 로그 및 이상 징후

정상:

- 빌드 성공
- Test_Level 로드 성공
- EnemyShip 9척 초기화
- Field 종료 후 AI가 다시 움직임

문제 징후:

- 수면에 닿아도 Field가 생성되지 않음
- Player Ship이 당겨짐
- Field 종료 뒤 적 함선이 계속 멈춰 있음
- 함선 위치가 순간이동하거나 지속적으로 큰 보정을 반복
- 중심에서 Roll/Pitch가 폭주
- Client마다 함선 당김 방향이 크게 다름

스모크 테스트 로그:

`Saved/Logs/Codex_GravityVortex_TestLevel_Smoke.log`

현재 로그에 보이는 일부 AnimNotify 누락 경고는 기존 콘텐츠 참조 경고이며 중력 소용돌이 구현에서 발생한 오류는 아니다.

## 7. 테스트 종료 후 정식 입력 전환

퀵슬롯 정책이 정해지면 `BP_BasePlayerTest`에서 `Enable Gravity Vortex Test Input`을 끈다. 그러면 Item Slot 3 차단도 해제된다.

그 뒤 최종 Slot Tag에 `GA_GravityVortexThrow_BP`를 부여하고, 프로젝트의 Enhanced Input/Data Asset 흐름으로 연결한다. `Key.Test.Skill.GravityVortex`는 테스트 전용이므로 정식 저장 데이터나 UI Slot ID로 사용하지 않는다.
