# Sprint Stop의 정지 직전 Gait 보존

## 원인

`ABasePlayer::StopMoveInput()`은 입력을 지운 뒤 `RefreshSprintFromInput()`으로 실제 Sprint 상태를 해제한다. 기존 State Controller는 그 이후의 `bIsSprinting`으로 Stop Chooser의 Gait를 설정해서 Sprint 중 정지해도 Run Stop을 선택했다. 재생 중에도 Jump만 Gait를 고정해서 Stop의 표시 값은 속도와 입력에 따라 Run/Walk로 바뀌었다.

## 수정 구조

- `ULocomotionAnimStateComponent::bLastGroundMoveWasSprinting`은 소유 클라이언트와 서버에서 이동 입력이 있는 지상 프레임의 Gait를 기록한다. 입력이 사라진 프레임에는 직전 값을 유지한다. W/Shift의 개별 해제 콜백에서 기록하지 않으므로 같은 프레임의 해제 순서에 영향을 받지 않는다.
- Stop 요청을 만들 때 `bStopWasSprinting`으로 확정한다. Sprint를 종료한 뒤 실제 Run 이동 프레임을 거쳤다면 Run Stop을 사용한다. 이동 에피소드 전체에서 Sprint를 한 적이 있는지로 판정하지 않는다.
- State Controller는 Stop 요청의 Gait를 사용하고 선택한 Stop 재생 동안 유지한다. 요청이 없는 감속 fallback에는 마지막 지상 Gait를 사용한다. 실제 `bIsSprinting`, 이동 속도와 Sprint 입력/RPC 동작은 기존 정책을 따른다.
- 입력 재개는 기존 정책대로 이전 Stop 요청을 취소하고 새 이동을 시작한다. 낙하/수영 등 지상 이동이 아닌 상태와 공격 등의 액션 전환은 오래된 지상 기록을 제거한다. Land → Stop에는 `bLandWasSprinting`을 사용해서 착지 직전 문맥을 보존한다.
- 원격 캐릭터는 기존 `FReplicatedLocomotionState`에 포함된 `bLastGroundMoveWasSprinting`을 적용한다. 스냅샷 동등성 비교에도 포함해서 변경을 전달한다. 입력/Sprint 해제가 하나의 스냅샷으로 합쳐져도 서버의 직전 지상 Gait를 사용할 수 있다. 새 RPC, 매 프레임 Chooser 평가, 충돌 쿼리는 추가하지 않는다. 전용 서버도 Gait 기록만 갱신하며 애니메이션 요청/평가 생략을 유지한다.
- 기존 `a.StopDebug` 요청 로그에 `StopSprint`와 `LiveSprint`를 추가한다. 정상 플레이에서 기본값 0이며 별도 프레임 로그는 추가하지 않는다.

## Chooser와 ABP

`CHT_Player_Stop_Relaxed`의 Sprint Forward L/R 행은 `M_Relaxed_Sprint_Stop_F_Lfoot/Rfoot`, Start Time 0.7을 사용한다. 앞의 결과가 빈 두 행은 현재 `EvaluateObjectChooserBase()` 호출에서 null 반환 후 다음 일치 행을 계속 평가하므로 선택을 차단하지 않는다. 기존 표와 남녀 ABP는 이 수정에서 변경하지 않는다. 공통 C++ 부모에 적용된다.

## 검증

`ArtisticSW.Animation.Stop.CapturedGait`는 실제 Stop Chooser와 임시 월드의 플레이어/컴포넌트를 사용한다. 30/60/120fps에서 두 가지 같은 프레임 입력 해제 순서, Sprint/Run 에셋과 Start Time, 요청 소비 이후의 Gait/에셋 유지, 입력 재개, 실제 Run 프레임 이후 정지, 원격 스냅샷 합쳐짐, 스냅샷 동등성, Land → Stop, 낙하와 공격 전환을 검사한다.

실행 명령: `Automation RunTests ArtisticSW.Animation.Stop`.

직접 `UnrealBuildTool.exe`를 사용한 `ArtisticSW2026Editor Win64 Development` 최종 빌드가 성공했다. Stop 검사도 성공(오류 0)했으며 보고서는 `Saved/Automation/StopGait/index.json`, 로그는 `Saved/Logs/StopGaitAutomation.log`이다. 기존 Stop 에셋의 Foley Notify/실험용 Pose Search 참조 누락 경고 17건은 남아 있다. 기존 `ArtisticSW.Animation.JumpAir.AuthoredChooserContinuity`도 성공(오류 0)했다. Jump 에셋의 기존 실험용 Pose Search 참조 누락 경고 10건은 남아 있으며 보고서는 `Saved/Automation/StopGaitJumpRegression/index.json`이다. 보고서/로그는 제품 소스에 포함하지 않는다.

2026-10-04 사용자가 수정 적용 후 Sprint Stop이 정상 동작한다고 확인했다. 확인한 캐릭터·네트워크 역할·입력 조합 전체가 특정되지는 않았으므로 모든 조합의 화면 검증 완료로 기록하지 않는다.

추가 화면 검증은 남녀 캐릭터에서 W+Shift → W 해제, W/Shift 동시 해제, Shift 해제 후 Run 이동 → W 해제, Stop 도중 재출발, Sprint 착지 직후 정지 순서로 비교한다. 멀티플레이에서는 소유/원격 캐릭터의 선택 에셋도 비교한다. 자동화의 원격 스냅샷 검사는 실제 네트워크 지연·패킷 손실 PIE 검증을 대신하지 않는다.
