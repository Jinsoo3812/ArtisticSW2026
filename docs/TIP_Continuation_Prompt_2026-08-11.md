# 다른 채팅에 붙여 넣을 TIP 작업 프롬프트

```text
ArtisticSW2026의 Turn In Place(TIP) 구현을 이어서 작업해줘.

프로젝트 루트:
C:\Users\I\Documents\GitHub\ArtisticSW2026

먼저 반드시 이 문서를 처음부터 끝까지 읽어줘:
C:\Users\I\Documents\GitHub\ArtisticSW2026\docs\TIP_Handover_2026-08-11.md

참조 프로젝트:
C:\Users\I\Documents\GitHub\Project_J

중요한 전제:
- 이 프로젝트는 항상 Strafe다. Project_J의 비전투 OTM 구조는 복사하지 말고, Project_J 전투 Strafe의 방향/delta-yaw/Chooser 방식만 비교 참고해.
- 별도의 UE State Controller 노드/컴포넌트는 사용하지 않는다. 코드의 StateController 명칭은 C++ presentation-state 레이어의 레거시 이름이다.
- Idle/Locomotion loop는 Motion Matching PSD, Start/Stop/Pivot/Jump/FallOff/Land/TIP는 Chooser -> Blend Stack one-shot 구조다.
- Notify 기반 전환은 절대 추가하지 마.
- ABP_Player uasset의 현재 연결을 임의로 지우거나, 기존 사용자 변경을 reset/revert/checkout하지 마.
- TIP의 gameplay Actor yaw는 Offset Root Bone이 아니라 `ABasePlayer::ApplyCombatTurnInPlaceRotation()`의 직접 root-yaw 적용이 소유한다.
- Root Motion Mode를 Root Motion from Everything으로 바꾸지 마. 현재 직접 root yaw 적용과 중복될 위험이 있다.

현재 ABP 상태:
- Blend Stack의 Animation Asset/Animation Time/Loop/Blend Time은 Thread Safe getter에 연결돼 있다.
- Blend Poses by Bool의 Active Value는 `GetThreadSafeShouldOverrideMotionMatching()`이다.
- Blend Stack Details의 On Update는 `OnUpdate_0`이다.
- `OnUpdate_0`: Node -> Convert to Blend Stack Node -> Branch(`Get Thread Safe State Controller Force Blend Stack On Next Update`) -> True에서 `Force Blend On Next Update`.
- 에디터 pin watch에서 force bool이 계속 false로 보이는 것은 정상이다. 한 AnimGraph update만 유지되는 pulse다.
- 실제 로그 `[SC_TIP_FORCE] Emit=1 StateChanged=0 Previous=TurnInPlace Desired=TurnInPlace`가 이미 확인됐다. 즉 same-asset TIP replay/Force Blend 연결은 정상이다.

현재 핵심 버그:
- TIP animation은 재생/재시작되지만 실제 Actor가 기대한 90도까지 회전하지 않는다.
- 바로 수정하지 말고, 먼저 아래 진단 로그를 분석해 원인을 정확히 하나로 좁혀라.

필요 로그:
1) `a.StateControllerDebug 1` 활성화
2) 90도 TIP를 한 번 발동하고 마우스를 멈춘다.
3) 같은 시간대의 `[SC_TIP_ROOT]`, `[SC_TIP_NET]`, `[SC_TIP_RESELECT]`, `[SC_TIP_FORCE]`, `[SC_TIP]`를 분석한다.

`SC_TIP_ROOT`에서 반드시 확인할 값:
- Seq, Clock, Prev, Curr, Len, Start
- Index, Semantic, AuthoredTotal, Scale
- CumPrev/CumCurr, RawDelta/ScaledDelta
- FacingBefore, Clamped, ActorBefore/ActorAfter, Remaining, Cannot

원인 분리 기준:
- delta가 0이면 root track 또는 playback clock 문제.
- scaled delta는 있는데 clamp가 0이면 facing yaw/rotation policy 문제.
- ActorAfter가 바뀌지만 다음 ActorBefore가 되돌아가면 Controller/CharacterMovement/replication이 덮어쓰는 문제.
- actor yaw는 정상인데 화면만 이상하면 mesh/OffsetRootBone/Steering 시각 보정 문제.
- Force가 지나치게 연속이면 TIP reselect threshold 또는 clip-finished condition 문제.

작업 방식:
1. 현재 `git status`, 위 문서, 관련 C++ 함수를 읽고 분석 결과를 먼저 짧게 설명한다.
2. 로그가 없으면 필요한 로그만 요청한다. 추측으로 Root Motion Mode나 ABP 대구조를 바꾸지 않는다.
3. 원인이 확정되면 최소 수정으로 구현한다.
4. Unreal Editor/Live Coding/UBT/dotnet/MSBuild/ShaderCompileWorker가 실행 중이면 빌드를 겹쳐 실행하지 않는다.
5. 에디터가 종료돼 있을 때만 직접 UnrealBuildTool로 `ArtisticSW2026Editor Win64 Development`를 빌드한다.
6. C++만 수정하고 asset의 수동 연결이 필요하면, 정확한 노드/핀/값만 단계별로 안내한다.

최종적으로 90L, 90R, 180L, 180R, 같은 방향 추가 회전, 반대 방향 재회전, listen server/client에서 각각 검증 가능한 상태까지 진행해줘.
```
