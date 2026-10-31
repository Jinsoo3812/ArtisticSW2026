# Nanite용 원형 가림 완화

`BP_BasePlayerController`의 `CameraReveal` 컴포넌트에서 조절한다. BP 기본값을 바꾼 경우 PIE를 다시 시작한다. 플레이 중 조절하려면 실제 로컬 PlayerController 인스턴스의 컴포넌트를 선택한다.

| 설정 | 기본값 | 입력 범위와 의미 |
| --- | --- | --- |
| bRequireOcclusion | true | 실제 시야 가림이 있을 때만 작동. false는 외관 조절용 상시 동작 |
| RequiredBlockedSamples | 2 | 머리·가슴·상부 골반 3개 검사 중 필요한 차단 수. 1~3 |
| TraceEndPadding | 15cm | 검사 끝을 몸 중심보다 카메라 쪽으로 당기는 거리. 0~100cm |
| OcclusionReleaseDelay | 0.1초 | 가림이 풀린 후 해제까지의 유지 시간. 0~2초 |
| VisibilityStrength | 0.97 | 0~1. **1이면 중심을 완전히 비운다.** 0이면 원래 표면 |
| RadiusScale | 0.65 | 0~1000. 플레이어 투영 크기에 곱하는 반경. 슬라이더는 0~10, 더 큰 수는 직접 입력 |
| FixedRadiusScreenHeight | 0 | 0~8. 0이면 자동 크기, 양수이면 화면 높이에 대한 고정 반경. 1이면 반경이 화면 높이 전체 |
| FeatherRatio | 0.1 | 0~1. 0이면 날카로운 경계, 1이면 중심부터 경계까지 전부 보간 |
| bLimitToPlayerDepth | true | false이면 원 안의 지원 표면을 거리 제한 없이 비움 |
| DepthOffset | 100cm | -100000~100000cm. 양수일수록 플레이어보다 더 뒤까지 비움 |
| DepthFeather | 5cm | 0~100000cm. 0이면 깊이 경계를 즉시 전환 |
| TransitionSeconds | 0.1초 | 0~30초. 0이면 활성화 스위치를 즉시 반영 |

**최대 효과 확인:** 외관 조절 중에만 bRequireOcclusion=false로 두고 VisibilityStrength=1, FixedRadiusScreenHeight=1, FeatherRatio=0, bLimitToPlayerDepth=false, TransitionSeconds=0으로 설정한다. 지원하는 배 표면이 화면의 큰 원 안에서 완전히 사라져야 한다. 확인 후 bRequireOcclusion=true로 복구하고 FixedRadiusScreenHeight=0과 FeatherRatio=0.1부터 외관을 조절한다. RadiusScale=0과 FixedRadiusScreenHeight=0이면 원 자체가 사라진다.

일반 플레이에서는 카메라에서 머리·가슴·상부 골반을 향해 Camera 채널의 복잡 충돌 선 검사를 최대 3회 수행한다. 로컬 캐릭터 자신은 무시하고, 첫 차단 물체가 배 또는 배에 부착된 물체인 경우만 센다. 기본적으로 2개 이상이 막혀야 활성화하므로 발 근처 갑판이나 낮은 난간만 원 안에 들어온 상황에서는 작동하지 않는다. 가림이 풀리면 기본 유지 시간 0.1초 후 기존 TransitionSeconds에 따라 복구한다. 완전히 즉시 해제하려면 두 시간을 모두 0으로 둔다. 런타임 컴포넌트의 BlockedSampleCount와 ViewOccluded로 실제 판정을 확인할 수 있다.

표시용 ShipVisual은 NoCollision이므로, 실제 가림 판정은 기존 배의 갑판/카메라 충돌 프록시를 사용한다. 렌더링에서 지워도 충돌은 유지되므로 효과 자체로 검출이 켜졌다 꺼지는 현상을 방지한다. Camera 채널에서 검출되지 않는 구조물이나 표시 형상과 프록시가 다른 부분은 정확히 판정하지 못할 수 있다. 월드의 모든 Actor를 순회하거나 이동/사격/카메라 충돌 설정을 바꾸지는 않는다.

기존 OccluderOpacity와 의미가 반대다. VisibilityStrength가 클수록 더 잘 보인다. 기본값은 중심에 표면 3%만 남기며, 부드러운 경계 폭은 반경의 10%다. 종횡비를 보정하여 해상도와 화면 비율이 바뀌어도 원형을 유지한다. 자동 반경은 줌에 따라 플레이어 크기와 함께 변하며 고정 반경 모드는 화면 높이에 대한 비율을 유지한다. 안전 상한은 화면 높이의 8배다.

배의 Masked 재질에서 연속적인 원/깊이 마스크를 DitherTemporalAA에 넣는다. 모든 재질에서 Random 입력을 0으로 통일하여 같은 화면 픽셀의 빈틈을 맞춘다. 중앙의 강도가 1이면 Dither 출력 0이 실제로 제거되도록 Mask Clip Value를 최소 0.001로 보장한다. 기존 컷오프가 양수이면 보존한다. 원래 OpacityMask 및 그림자 마스크는 유지한다.

플레이어 Kelvin BP와 적 배 BP의 표시용 StaticMesh 컴포넌트에 적용하며 유리(Window), 숨김 충돌 컴포넌트, 캐릭터, 아이템, 섬·지형은 페이드 대상에서 제외한다. 표시용 메시가 충돌이 없어도 페이드는 적용된다. Nanite 설정과 Sort Triangles는 변경하지 않고 Translucent 재질도 사용하지 않는다. 적 배 풀의 소스도 변경하지 않는다.

Masked programmable raster/shader 비용은 원래 Opaque보다 늘 수 있다. bEnabled=false 또는 VisibilityStrength=0은 효과를 끄지만 재질의 Masked 경로를 Opaque로 바꾸지는 않는다. 성능이 원본과 동일하다고 보장하지 않는다. 이 작업에서는 PIE/성능 측정을 실행하지 않았다.

로컬 3인칭 캐릭터를 대상으로 하며 사망·관전·1인칭 조타·대상 없음·화면 이탈 시 끈다. 분할 화면은 지원하지 않는다. 별도 프로세스 멀티플레이에서는 월드별 MPC에 로컬 값만 쓴다. 중심 깊이에 양의 여유를 둔 근사이므로 뒤에 있는 지형을 정확히 구분하는 기능은 아니다. 거리 제한을 끄면 플레이어 뒤의 지원 배 표면도 비워진다.

돛 색상은 기존 `/Game/Blueprints/Ship/Materials/Sail/MI_PlayerSail`에서 계속 조절한다. 기존 색상과 강도는 유지하며 해당 재질에도 Masked 가림 그래프를 추가한다. 가림 저작 보고서는 `Saved/CameraRevealAuthoring.json`, 빌드 로그는 `Saved/Logs/CameraReveal_Build.log`다.

검증: C++ 빌드, 재질 43개 셰이더 컴파일, 디스크 재로드 후 슬롯 70개/양수 컷오프/기본값/컴포넌트 렌더링 플래그 확인을 통과했다. D3D 저작 에디터는 저장·셰이더 완료 후 종료 코드 0xC0000005를 반환했으며, 이후 NullRHI 재로드 검증은 정상 종료했다. 기록은 `Saved/CameraRevealVerification.json`에 있다. PIE 외관과 프레임 성능은 미확인이다.
