# Ship Foam / Kelvin Wake M6 구현서

작성일: 2026-08-16  
대상 프로젝트: `C:\Unreal Projects\ArtisticSW2026`  
범위: Persistent deep-water Kelvin displacement, GPU WPO/CPU buoyancy parity. Foam 통합은 제외한다.

## 1. M6 결론

M5의 문제는 Golden Image의 품질이 아니라 사용 방식이었다. M5는 매 fixed step마다 현재 선박 상태에 대응하는 완성된 정상상태 Kelvin 높이 `GoldenTarget`을 다시 만들고, 전체 기록 필드를 그 목표로 low-pass 이동시켰다.

```text
Next = Current + (GoldenTarget - Current) * ResponseAlpha
```

이 방식에서는 위치, 속도 또는 방향이 조금만 달라져도 완성 파면 전체의 위상과 파장이 바뀐다. 이전 crest와 새 trough가 필드 전체에서 교차하므로, 입력 타임라인을 연속화해도 20 Hz마다 수십 cm의 높이 변화와 주기적 진동이 남았다.

M6은 Golden target 추종을 제거했다. 선박은 완성된 wake를 생성하지 않고 이번 fixed step 동안 물에 가한 작은 Bow/Stern 압력원만 제공한다. 이미 생성된 파면은 별도 spectral state에 남아 심해 분산식으로 계속 전파된다.

```text
기존 spectral height/velocity
    -> deep-water dispersion으로 시간 전진
    + 이번 step의 Bow/Stern pressure footprint
    -> 새로운 persistent height field
```

## 2. 다섯 레퍼런스의 M6 반영

### Godot Compute Texture

- 반영: 이전 상태를 보존하고 새 disturbance만 주입하는 시간 상태 구조, 3중 높이 상태, 최신 두 상태의 렌더 보간.
- 미반영: Godot의 로컬 ripple 방정식. 이 식은 심해 중력파의 `omega^2 = g k` 분산 관계가 아니다.

### 논문 기반 Kelvin 시뮬레이션

- 반영: 움직이는 pressure field가 Kelvin wake를 만든다는 물리 모델, 심해 분산 관계, 일정 속도 장기 결과의 Golden 기준.
- 변경: 구운 steady Golden Image는 런타임 목표가 아니라 회귀·튜닝 기준으로만 사용한다.

### Houdini Kelvin Wakes Deformer

- 반영: steady wake는 일정 속도 조건에만 직접 적용할 수 있다는 제한, 회전 시 trajectory가 필요하다는 원칙.
- 변경: 가변 속도에서 서로 다른 steady wake를 매 step 교체하지 않는다.

### Sundog Triton

- 반영: Bow/Stern source 분리, Length/Beam/pressure size/source offset 입력, Kelvin displacement와 Foam/prop wash 분리.
- 변경: `SternOffsetCm`이 실제 replicated event와 solver 입력에 사용된다.

### Desmos Kelvin geometry

- 반영: 현재 배에 붙은 이미지가 아니라 과거에 방출된 파동들의 phase/group propagation이 V 포락선을 만든다는 원칙.
- 결과: 과거 파면은 생성 당시 세계공간 상태로 남고 현재 배의 속도·방향으로 다시 매핑되지 않는다.

## 3. M6 런타임 방정식

수면 높이를 `eta(x,y,t)`라 하면 M6의 각 Fourier mode는 다음 심해 선형식을 따른다.

```text
eta_ddot(k) + 2 beta eta_dot(k) + g |k| eta(k)
    = g |k| eta_equilibrium(k)

omega(k) = sqrt(g |k|)
```

- `eta`: persistent signed water height
- `eta_dot`: persistent vertical velocity
- `beta`: `sw.ShipWake.SpectralDamping`
- `eta_equilibrium`: 이번 step에서 선체가 가하는 국소 압력 footprint를 등가 정수압 깊이로 표현한 값
- `g = 980 cm/s^2`

각 mode는 한 fixed step 동안 pressure가 일정하다고 보고 cosine/sine 해로 전진한다.

```text
eta_next = eta cos(omega dt)
         + velocity sin(omega dt) / omega
         + eta_equilibrium (1 - cos(omega dt))

velocity_next = -eta omega sin(omega dt)
              + velocity cos(omega dt)
              + eta_equilibrium omega sin(omega dt)
```

그 후 두 상태에 지수 감쇠 `exp(-beta dt)`를 적용한다. 일반 explicit finite difference가 아니므로 해상도 끝의 높은 주파수에서도 `omega dt` CFL 폭발을 만들지 않는다.

## 4. Source injection

각 emitter는 완성 Kelvin V가 아니라 두 개의 타원형 Gaussian pressure footprint를 만든다.

```text
BowCenter   = Kelvin apex
SternCenter = BowCenter - Forward * SternOffsetCm

sigmaLong = PressureSizeCm * 0.35 * LongitudinalScale
sigmaLat  = BeamWidthCm    * 0.35 * LateralScale
```

fixed step 사이 이동거리가 큰 경우 이전 solver event와 현재 event 사이 선분을 최대 8개 지점으로 적분한다. 이것은 여러 완성 wake를 누적하는 도장이 아니라, 한 time step 동안 연속 이동한 pressure source의 시간 평균이다.

```text
eta_equilibrium -=
    Amplitude
    * SpectralSourceScale
    * SpectrumStrength
    * (BowGaussian + SternStrength * SternGaussian)
```

M6에서 `MaximumAmplitudeCm`은 최종 파고를 직접 지정하지 않는다. 움직이는 등가 압력원의 세기를 지정하며, 실제 wake 파고는 속도, source 크기, 감쇠와 간섭으로 결정된다.

`SternPhaseOffsetRadians`는 M4 Golden Atlas의 미술 보정값이었다. M6에서는 Bow/Stern의 실제 공간 거리로 위상차가 자연스럽게 발생하므로 spectral solver 경로에서 사용하지 않는다.

## 5. Moving field

800 m 유한 필드는 배를 따라 이동한다. 중심이 한 texel 이동할 때 기존 파면을 공간 resample하지 않고 Fourier shift theorem을 적용한다.

```text
F_new(k) = F_old(k) * exp(i k dot CenterDelta)
```

따라서 고정된 세계공간 crest가 필드 중심 이동 때문에 배와 함께 끌려가지 않는다. 필드가 크기의 45%보다 크게 순간 이동한 경우에는 다른 영역으로 teleport한 것으로 보고 spectral/spatial history를 초기화한다.

## 6. 렌더와 부력

M6도 기존 머티리얼 인터페이스를 유지한다.

- `ShipWakePreviousHeightField`
- `ShipWakeHeightField`
- 각 필드의 world center
- `ShipWakeFieldSizeCm`
- `ShipWakeHistoryAlpha`

IFFT로 얻은 동일한 CPU authoritative signed-height field를 다음 두 경로가 공유한다.

```text
Water material WPO -> Previous/Current texture sample + alpha
CPU/Async buoyancy -> Previous/Current CPU array sample + 같은 alpha
```

따라서 `M_Realistic_Water`의 기존 `SWKelvinWakeWPO` Custom 연결은 바꿀 필요가 없다. `.ush`도 높이 필드 소비자이므로 교체하지 않았다. 변경된 것은 필드 생산 방식이다.

## 7. 변경 파일

### `Source/WaterAndShip/Public/SWShipWakeTypes.h`

- replicated solver event에 `SternOffsetCm` 추가.

### `Source/WaterAndShip/Private/SWShipWakeEmitterComponent.cpp`

- component의 기존 `SternOffsetCm`을 event에 기록.

### `Source/WaterAndShip/Public/SWShipWakeSubsystem.h`

- persistent complex `SpectralHeight`, `SpectralVelocity` 추가.
- moving spectral center와 이전 fixed-step emitter 상태 추가.

### `Source/WaterAndShip/Private/SWShipWakeSubsystem.cpp`

- radix-2 CPU 2D FFT/IFFT 구현.
- `GoldenTarget -> HistoryResponse` 전체 제거.
- moving Bow/Stern Gaussian pressure source 생성.
- 정확한 per-mode deep-water cosine/sine 전진.
- Fourier field-center shift.
- 전역 안전 scale과 기존 3중 spatial field upload.
- `[M6Runtime] Solver=DeepWaterFFT` 로그.
- `M6SpectralTransform` 자동화 테스트.

## 8. M6 CVar

기존 CVar 중 다음은 계속 사용한다.

| CVar | 기본값 | 의미 |
|---|---:|---|
| `sw.ShipWake.Enable` | 1 | 렌더와 부력 Kelvin 동시 활성화 |
| `sw.ShipWake.FieldResolution` | 256 | spectral/spatial 한 축 해상도. 가장 가까운 상위 2의 거듭제곱 사용 |
| `sw.ShipWake.FieldWorldSizeCm` | 80000 | moving field 폭과 높이 |
| `sw.ShipWake.FieldSimulationHz` | 20 | solver fixed-step Hz |
| `sw.ShipWake.StateInterpolationDelay` | 0.10 | 네트워크 emitter 타임라인 보간 지연 |
| `sw.ShipWake.FieldMaximumHeightCm` | 200 | 전체 spectral state 안전 scale 기준 |
| `sw.ShipWake.FreezeHistory` | 0 | solver와 표시 alpha 동결 |
| `sw.ShipWake.DebugForceHistoryAlpha` | -1 | -1 자동, 0 Previous, 1 Current |
| `sw.ShipWake.DebugLockFieldCenter` | 0 | moving center 격리 시험 |

신규 CVar:

| CVar | 기본값 | 의미 |
|---|---:|---|
| `sw.ShipWake.SpectralDamping` | 0.10 | persistent wave의 초당 지수 감쇠율 |
| `sw.ShipWake.SpectralSourceScale` | 0.35 | emitter amplitude를 pressure-equilibrium 깊이로 변환 |
| `sw.ShipWake.MinimumWavelengthCm` | 600 | 해상 불가능한 짧은 파장 source 차단 |

M5의 `HistoryResponseRate`와 `HistoryMomentum`은 더 이상 등록하거나 사용하지 않는다.

## 9. 초기 플레이 설정

먼저 과장값을 제거하고 다음으로 시작한다.

```text
Emitter.MaximumAmplitudeCm = 65
sw.ShipWake.SpectralSourceScale 0.35
sw.ShipWake.SpectralDamping 0.10
sw.ShipWake.MinimumWavelengthCm 600
sw.ShipWake.FieldMaximumHeightCm 200
sw.ShipWake.DebugLog 1
sw.ShipWake.DebugLogInterval 0.25
```

형상이 약하면 `MaximumAmplitudeCm`보다 먼저 `SpectralSourceScale`을 `0.5`, `0.75` 순서로 올린다. 파면이 너무 오래 남으면 `SpectralDamping`을 올리고, 너무 빨리 사라지면 내린다. `MaximumAmplitudeCm=300~10000`과 같은 포화 시험은 연속성 판정에 사용하지 않는다.

## 10. 런타임 합격 기준

새 로그 prefix는 `[M6Runtime]`이다.

1. `Events=1`, `StepEvents=1`이 이동 중 유지될 것.
2. `Source`는 Bow/Stern 주변의 음수 국소 footprint이며 전체 V 높이가 아닐 것.
3. 배가 정지하거나 source event가 만료된 뒤에도 `OutputRMS`가 즉시 0이 되지 않고 감쇠할 것.
4. 일정 속도 직진 시 V가 주행 시간에 따라 길어지고 내부 파면이 형성될 것.
5. 가속 후 과거의 긴/짧은 파면이 현재 속도 기준으로 동시에 재배치되지 않을 것.
6. `MaxDelta`가 일정한 20 Hz 펄스로 전체 필드에서 수십 cm 반복되지 않을 것.
7. `StepMs`를 서버와 클라이언트에서 측정할 것. M6은 step마다 source FFT 1회와 height IFFT 1회를 수행한다.
8. `FreezeHistory=1`에서 Previous와 Current 각각이 정적일 것.

## 11. 정적 검증 결과

- `ArtisticSW2026Editor Win64 Development`: 빌드 성공.
- `ArtisticSW.Water.ShipWake.M4AtlasShape`: Success.
- `ArtisticSW.Water.ShipWake.M5TimelineInterpolation`: Success.
- `ArtisticSW.Water.ShipWake.M6SpectralTransform`: Success.

M6 테스트는 signed 2D height의 FFT/IFFT round-trip 오차가 `1e-4` 미만인지 확인한다. 또한 field center가 한 texel 이동할 때 Fourier shift 이후 고정 세계공간 crest가 새 local index로 정확히 이동하는지 검증한다.

## 12. 현재 한계와 다음 단계

- 유한 spectral domain은 주기 경계다. 기본 field 크기와 damping으로 뒤쪽 wrap-around를 늦추지만, 장시간 항주에서 명시적인 absorbing border 또는 spectral clipmap이 필요할 수 있다.
- M6은 선형 심해파다. 쇄파, spray, 점성 near-field와 propeller wash는 포함하지 않는다.
- 클라이언트가 늦게 접속하거나 unreliable emitter sample을 장기간 잃으면 과거 spectral field를 서버와 똑같이 재구축할 수 없다. authoritative snapshot 동기화는 별도 네트워크 단계다.
- 여러 선박에서는 source field 작성 비용이 emitter 수에 비례한다.
- Golden Atlas와 M4 evaluator는 회귀/debug fallback artifact로 남아 있지만 M6 기본 height field 생성에는 사용되지 않는다.
- 실제 레벨에서의 wake 모양과 성능은 PIE 런타임 로그 및 화면 검증이 필요하다. 정적 테스트는 수치 변환과 빌드만 보장한다.
- Foam은 이번 범위에서 제외했다. 이후에는 별도 Kelvin mask를 붙이기보다 `Gerstner + Ripple + M6 Kelvin`의 최종 높이·gradient를 기존 FoamRule에 입력하는 방향을 유지한다.

