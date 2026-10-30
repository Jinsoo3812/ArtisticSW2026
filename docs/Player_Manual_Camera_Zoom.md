# 플레이어 수동 카메라 줌

보행·수영 카메라는 SpringArm의 자동 충돌 당김을 기본적으로 사용하지 않는다. 휠 위로 가까이, 아래로 멀리 이동하며 FOV 대신 카메라 거리를 조절한다. 벽·선체를 통과할 수 있는 카메라 정책이며, 캐릭터 이동·공격 충돌은 바꾸지 않는다. 배 조종 카메라의 기존 휠 줌은 유지한다.

## 설정

Woman/Man BP의 상속된 `CameraZoom` 컴포넌트에서 `Camera > Manual Zoom`을 조정한다.

| 항목 | 기본값 | 의미 |
| --- | --- | --- |
| Min Distance | 150cm | 사용자 기본 거리의 최솟값 |
| Max Distance | 800cm | 사용자 기본 거리의 최댓값 |
| Wheel Step | 50cm | 휠 입력 1단위당 거리 변화 |
| Interp Speed | 10 | 일반 줌 목표 거리 추종 속도 |
| Enable Camera Collision | false | 명시적으로 켜면 기존 장애물 회피 경로 사용 |

최소/최대가 뒤바뀌면 두 값을 정렬하여 사용하고, 음수/0은 10cm 이상으로 보정한다. 비유한 거리 입력은 저장하지 않는다. 복원 값과 질주 거리도 유효 범위에 제한한다. 휠 변화량과 간격은 과도한 입력을 제한한다. 조준/저격의 기존 고정 거리는 사용자 줌 범위와 별도로 유지한다.

`PreferredDistance`는 사용자가 선택한 기본 거리다. 실제 SpringArm 길이는 조준·질주·보간에 따라 달라질 수 있다. 매 프레임 설정 기본값 400cm로 덮어쓰지 않는다. 질주는 기존 `SprintTargetArmLength - DefaultTargetArmLength` 차이만 기본 거리에 더하므로 현재 기본값에서는 +50cm이며, 최댓값을 넘지 않는다.

## 입력 우선순위

- 로컬 플레이어가 자신의 캐릭터를 조종하고 보고 있을 때만 휠 줌을 받는다. 수영은 줌을 막지 않는다.
- 조준·저격 중 휠 입력은 폐기한다. 해제하면 저장한 기본 거리로 돌아온다.
- UI 창/커서, 시점 입력 차단, 대화/게임오버/로딩 등의 공통 UI 차단 조건에서는 줌을 받지 않는다.
- 배/대포 조종 등으로 Pawn 또는 ViewTarget이 바뀌면 캐릭터 줌을 받지 않는다.
- 집속 수류탄 자탄 선택 중에는 선택 능력이 휠을 예약한다. 선택 종료/취소 시 해제한다. 다른 휠 소비자도 `ReserveWheel(Consumer, true/false)`로 같은 경로를 사용할 수 있으며 여러 소비자의 예약이 중첩돼도 남은 예약이 있으면 줌하지 않는다.
- 차단 중 입력을 나중에 실행하지 않는다.

`USWPlayerCameraZoomComponent`는 자체 로컬 Enhanced Input 액션/컨텍스트를 생성하여 `MouseWheelAxis`에 연결한다. 우선순위 -10, 입력 비소비로 기존 높은 우선순위 매핑을 보존한다. 기존 자탄 선택용 WheelUp/WheelDown 이벤트는 그대로 전달한다. 재바인딩 시 자신의 이전 바인딩과 컨텍스트만 정리하고, EndPlay에서도 제거한다. 전역 매핑 초기화나 에셋 재저장은 필요 없다.

## 로컬 상태와 복원

줌 설정은 RPC/복제 대상이 아니다. 로컬 Controller의 transient 기본 거리로 같은 Controller에서의 캐릭터 교체·재소유 시 이어받는다. 기존 방 진행 CameraZoom 필드에는 실제 조준/질주 길이 대신 기본 거리를 기록하고, 복원할 때 이미 있는 로컬 선호값을 우선한다. 원격 플레이어의 로컬 선택을 서버에 동기화하지 않는다. 앱 재실행/재접속/새 Controller까지 유지하는 영구 사용자 설정은 이번 범위에 포함하지 않는다.

이전 `SWCameraSpringArmComponent` 및 베이크 프록시는 충돌 회피를 명시적으로 다시 켤 때 사용할 수 있도록 남아 있다. 기본 플레이어 카메라는 `BeginPlay`에서 새 설정을 적용하므로 기존 BP에 직렬화된 `bDoCollisionTest=true`도 기본적으로 꺼진다. 배 조종 카메라 설정은 변경하지 않았다.

## 검증

자동화: `ArtisticSW.Camera.ManualZoom.BoundsReservationAndPersistence`.
기본 거리/반복 입력 최소·최대/질주 오프셋/중첩 예약/UI 및 시점 차단/캐릭터 교체의 로컬 선호값/비소유 입력 차단/역순 경계를 검사한다.

2026-10-30 검증 결과:

- UE 5.7.4 직접 UBT `ArtisticSW2026Editor Win64 Development` 최종 빌드 성공(종료 코드 0). 최초 빌드의 조준 상태 검사 오류를 수정한 뒤 재빌드했다.
- 위 자동화 테스트 최종 성공. 조준/저격 태그 차단, 기본값 초기화 뒤 저장 복원, 기존 로컬 선호값 우선도 검사한다. 최초 테스트의 추상 UObject 임시 소비자 때문에 발생한 ensure는 구체적인 컴포넌트 객체로 바꿔 해결했다. 최종 로그: `Saved/CameraZoomValidation/AutomationFinal.log`.
- 실제 `Lvl_CY`, Woman/Man, 2인 단일 프로세스 listen server/client PIE에서 네이티브 줌 InputAction에 Enhanced Input 값을 주입했다. 선실 보행부터 수영 전환까지 250개 시점(양쪽 500개 관측) 검증. 안정 구간에서 호스트 400→350cm, 클라이언트 400→450cm로 독립 변경됐고, 반복 입력 최소 150/최대 800cm, UI 및 휠 예약 중 값 유지, 수영 중 양쪽 줌 동작을 확인했다. 양쪽 카메라의 `bDoCollisionTest`는 검사 내내 false였다.
- 실제 로드된 매핑 감사에서 도보 기본 컨텍스트는 MouseScrollUp/Down, 배는 MouseWheelAxis, 새 컨텍스트는 비소비 MouseWheelAxis로 연결된 것을 확인했다. 도보 기본 컨텍스트가 같은 축을 점유하는 충돌은 없었다.
- 실제 하드웨어 휠, 모든 UI 창별 상호작용, 실제 집속 수류탄 발동/배 조종 왕복 전체는 이번 스크립트가 구동하지 않았다. 예약 차단은 같은 공개 예약 API로 검증했고, 조준/질주/캐릭터 교체는 자동화로 검증했다. 별도 프로세스/패킷 손실과 영구 설정 저장은 검증 범위가 아니다.
- 테스트용 PIE/CPU/FPS 설정을 복구했고 에디터를 종료했다. 맵/입력 에셋은 수정하지 않았다.

정량 결과는 `validation/Player_Manual_Zoom_20261030.json`, 원본 표본은 `Saved/AudioIntegration/Live/1396/`에 있다. 테스트 스크립트는 `Scripts/Editor/CameraValidation/manual_zoom_network.py`, 정량 검증 및 요약은 `summarize_manual_zoom.py`에 있다.
