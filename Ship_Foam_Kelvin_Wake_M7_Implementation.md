# Ship Foam Kelvin Wake M7 구현

## 결론

M7은 M4~M6의 상태 텍스처/CPU FFT를 폐기하고 Ripple과 같은 **불변 이벤트 큐**로 다시 만들었다. 한 장의 Fr=0.50 Golden Image를 CPU와 GPU가 함께 읽으며, 결과 높이는 기존 Gerstner/Ripple 높이에 합산되어 게임 스레드와 Async Physics 부력에도 동일하게 적용된다. Foam 통합은 이번 범위에서 제외했다.

파이프라인은 다음만 남는다.

`BP_PlayerShip_Kelvin Emitter → FastArray Event Queue → CPU/Async Evaluator + GPU Event Texture → SWShipWake.ush → M_Realistic_Water WPO Add`

## Golden Image

- 파일: `Content/New/Water/Realistic_Water/Kelvin/kelvin_wake_golden_fr050_fp16.bin`
- 형식: raw R16F, U 512 × V 256, 262,144 bytes
- 기준: Fr=0.50, peak 1.3871971383
- SHA-256: `46E39E39C5C55D94BDA65174C0937E2210C4BD9CE285AFFD34B4227BFE555BF3`
- 12-slice atlas/Froude 보간은 삭제했다. 속도는 모양을 교체하지 않고 발생 여부와 전파 시간에만 관여한다.

## 연속성과 Golden 중복 문제

Golden Image는 작은 충격 커널이 아니라 한 배의 완성된 Kelvin Wake다. 이를 이벤트마다 단순 가산하면 동일한 마루와 골이 중복되어 높이가 폭증하고 복잡한 간섭이 생긴다.

M7은 각 이벤트의 전파 가중치로 **정규화 overlap-add**를 수행한다.

```text
downstream = -dot(P - Origin, Forward)
lateral    =  dot(P - Origin, Right)
front      = PropagationSpeed * Age
weight     = FrontEnvelope * FadeIn
height     = sum(Amplitude * Golden(UV) * weight * Decay)
             / max(sum(weight), 1)
```

따라서 이벤트 밀도가 늘어나도 전체 높이가 배수로 증가하지 않는다. 기본 발생 간격 250 cm는 Golden의 대표 파장(`WakeLength / 10 = 1,600 cm`)보다 충분히 작아 인접 마루/골을 부드럽게 보간한다.

## 회전

Houdini Kelvin Wakes Deformer의 경로/접선 개념을 적용했다.

- 각 이벤트는 생성 순간의 `Origin`과 궤적 접선 `Forward`를 영구 보존한다.
- 과거 Wake를 배의 현재 회전으로 다시 돌리지 않는다.
- 이동 거리와 회전각을 동시에 검사하고, 한 프레임에 건너뛴 위치/방향/시간을 최대 8개 중간 이벤트로 재샘플링한다.
- 기본 회전 분할은 8도다. 급선회에서도 V자 축이 궤적을 따라 점진적으로 휘어진다.

Godot의 세 장 히스토리에서 참고한 것은 “과거 소스를 보존하고 혼합한다”는 원리다. M7에서는 별도 히스토리 렌더 타깃 대신 시간 불변 이벤트 큐가 그 역할을 하므로 텍스처 회전에 따른 재샘플 손실이 없다.

## 이벤트와 네트워크

이벤트는 `Origin`, `Forward`, `Start/ExpireServerTime`, `Amplitude`, `PropagationSpeed`, `Decay`, `WakeLength`, `WakeHalfWidth`, `EnvelopeWidth`, `FadeIn`만 가진다.

- FastArray 복제, 최대 64개
- 서버가 양수 EventId를 발급
- 로컬 클라이언트는 음수 ID로 즉시 예측
- 권위 이벤트 도착 시 위치·시간·방향이 가장 가까운 예측 이벤트와 교체하되 시각 시작 시각은 보존
- 만료 뒤 2초 동안 CPU 물리 재시뮬레이션용 히스토리를 유지
- GPU 이벤트 텍스처는 큐 revision이 바뀔 때만 업로드하고, 시간 scalar만 매 프레임 갱신

M5의 불연속 입력/빈 히스토리 텍스처 문제는 더 이상 존재하지 않는다.

## CPU 부력과 GPU WPO 일치

`FSWShipWakeEvaluator`가 Golden bilinear sample, 전파 전면, 감쇠, overlap 정규화를 계산한다.

- `USWRippleWaterWaves::GetWaveInfo/GetWaveHeightAtPosition`: Gerstner + Ripple + Kelvin
- `Ship.cpp`: 동일 이벤트 스냅샷을 Async 입력에 전달
- `ShipPhysicsAsync.cpp`: 동일 `FSWShipWakeEvaluator`로 부력 높이 계산
- `SWShipWake.ush`: 같은 식과 같은 Golden 데이터로 WPO 계산
- 별도 `Height * 5` 또는 `Height * 10` 배율은 없다.

## M_Realistic_Water Custom 노드

Custom 노드 설명은 `SW Kelvin Wake M7 Golden Event WPO`, Include File Path는 `/Project/SWShipWake.ush`다.

입력은 정확히 여섯 개다.

1. `WorldPosition`
2. `ShipWakeTex`
3. `ShipWakeGolden`
4. `ShipWakeServerTime`
5. `ShipWakeCount`
6. `ShipWakeEnable`

출력 `float3(0, 0, Height)`는 기존 Gerstner WPO와 `Add`되어 최종 SetMaterialAttributes WPO에 연결되어 있다. Lerp가 아니다.

## BP_PlayerShip_Kelvin 기본값

- Emission Distance: 250 cm
- Maximum Turn Angle: 8°
- Maximum Emission Interval: 0.20 s
- Maximum Catch-up Events: 8
- Minimum Speed: 250 cm/s
- Maximum Amplitude: 65 cm
- Propagation Speed: 1,200 cm/s
- Wake Length / Half Width: 16,000 / 6,000 cm
- Envelope Width: 2,500 cm
- Decay: 0.12

형상 조절은 `KelvinApexLocalOffset`, `KelvinDirectionYawDegrees`, `WakeLengthCm`, `WakeHalfWidthCm`으로 한다. 회전이 거칠면 `EmissionDistanceCm` 또는 `MaximumTurnAngleDegrees`를 낮춘다.

남은 CVar는 `sw.ShipWake.Enable`과 `sw.ShipWake.DebugLog`뿐이다.

## 제거한 M1~M6 경로

- `SWShipWakeField.ush`, `SWShipWakeHistory.ush`
- CPU spectral FFT/3-state height history/field-center interpolation
- trajectory texture와 12-slice Froude atlas
- `M_SWShipWakeFieldUpdate`
- Landscape Kelvin 테스트 물 머티리얼/인스턴스/파도 복사본
- M1~M6 변환·진단 Python 도구

실제 사용 에셋은 `M_Realistic_Water`, `M_Realistic_Water_Ocean`, 원래 파도 에셋, `BP_PlayerShip_Kelvin`이다.

## 검증

- `ArtisticSW2026Editor Win64 Development`: 성공
- `ArtisticSW.Water.ShipWake.M7GoldenPropagation`: 성공
- `ArtisticSW.Water.ShipWake.M7ImmutableRotation`: 성공
- D3D12 / PCD3D_SM6에서 `M_Realistic_Water` 재컴파일: 오류 0, shaders left 0
- M7 에셋 검증: Custom 입력 6개 연결, include/code, BP 기본값 모두 통과

## 범위와 주의점

- Foam은 아직 연결하지 않았다.
- 고정 Fr=0.50 Golden이므로 속도별 Kelvin 각도/파장 변화는 표현하지 않는다.
- 현재 overlap 정규화는 한 대의 활성 Kelvin 배를 목표로 한다. 여러 배를 동시에 운용할 때는 SourceId별로 독립 정규화한 뒤 배 사이 결과만 합산해야 한다.
- 최종 품질 판단은 `Realistic_Water` PIE에서 직진과 급선회를 각각 확인한다.
