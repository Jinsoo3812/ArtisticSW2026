# 플레이어 돛 색상

콘텐츠 브라우저에서 `/Game/Blueprints/Ship/Materials/Sail/MI_PlayerSail`을 연다.
`Sail` 그룹의 `SailTint`(색상)와 `TintStrength`(강도)를 변경하고 저장한다.

플레이어 `BP_PlayerShip_Kelvin`의 표시용 메시 슬롯 21, 원본 `MI_Mast01a`에만 색상을 적용한다. 원본 재질의 부모 체인을 복제하고 BaseColor에 색상 변조를 추가했다. 적 배에는 색상을 적용하지 않는다. 색상 자체를 갱신하는 Tick은 없다.

2026-10-31 가림 기능 원복 시 사용자 저장값 Linear RGB (0.171415, 0.223963, 1.0), TintStrength=1.0을 보존했다. 이후 사용자 요청으로 [CameraReveal](Camera_Reveal.md)을 재도입하여 현재 이 재질에도 Masked 가림 그래프가 적용된다. 색상 파라미터 경로는 유지하며 Nanite 강제 해제와 삼각형 정렬 변경은 없다.
