# 프로젝트 시체 스킨 보정·여성 재질 복구·테스트 맵

> ShipAI 병합 후의 최신 동작·기존 에셋 문제·변경된 F11 성별 전환 키는 [최신 통합 안내](ShipAI_Corpse_Integration_2026-10-31.md)를 따른다.

이번 변경은 **실제 프로젝트에 통합**했다. 남녀 기존 운영 메시와 PA를 갱신하고, `SWPlayerRagdollComponent::StartRagdoll`에서 `SWCorpsePoseComponent`를 자동 설치한다. 별도 플러그인·검토 패키지·복사 프로젝트를 적용할 필요가 없다. 과거 독립 환경 실험 자료는 로컬에 보존하고 배포 대상에서 제외했다.

## 적용 내용

- 팔·다리 twist 보조 본이 사망 전 애니메이션의 로컬 위치에 남아 물리 관절 선에서 벗어나던 스킨 꼬임을 보정한다. post-physics의 최종 관절 위치와 축 회전을 사용하고 척추·목·쇄골 연결부 및 종속 보조 본도 처리한다.
- 물리 강체·ASC·생명 소유권을 변경하지 않는 시체용 visual pose를 사용한다. 동일한 메시와 재질을 사용하고, 원래 물리 메시의 중복 렌더를 끈다. reset 즉시 원래 렌더 상태를 복원하며 살아 있는 동안 보정 tick은 꺼진다. 전용 서버에는 visual을 생성하지 않는다.
- `PA_ChMan_UE5`, `PA_ChWoman_UE5`에 남녀·좌우 동일하게 어깨 twist ±25°, 팔꿈치 목표 굽힘 0–40°, 무릎 0–50°를 저장했다. 팔꿈치/무릎은 중심 편향 때문에 PA swing 반각이 20°/25°다. 나머지 제한도 `SourceArt/Characters/Physics` 제작 JSON·생성기에 일치시켰다.
- 여성 `SKM_ChWoman_UE5`의 비어 있던 재질 슬롯 7개를 기존 `M_ChGirl*`에 연결했다. 원인은 Python의 Unreal 배열 요소 복사본만 수정한 것이며, 임포트 스크립트도 인덱스 재할당으로 수정해 재발을 막는다.
- 기존 BP/ABP와 운영 GameMode의 5초 정책은 유지한다. 테스트용 10초 규칙은 아래 전용 맵에만 적용한다.

## 바로 테스트

브랜치 **JYU_merge**를 받은 뒤 `git lfs pull`을 실행하고 Development Editor를 빌드한다. 빌드는 기존 에디터·Live Coding·빌드·ShaderCompileWorker가 모두 끝난 상태에서 엔진의 직접 UnrealBuildTool.exe를 사용한다.

프로젝트에서 **`/Game/Developers/PlayerRagdoll/L_PlayerRagdollTest`**를 연다. Content Browser의 개발자 콘텐츠 표시가 필요할 수 있다. 또는 `Scripts/Editor/ChManRigValidation/Open_PlayerRagdollTest.cmd`를 실행한다(엔진이 기본 UE_5.7 위치에 설치된 경우). 로컬 바탕화면 바로가기는 **남녀 시체 테스트 (프로젝트)**다.

네트워크 모드는 Standalone, 플레이어 수 1명으로 **Selected Viewport PIE**를 사용한다(별도 실행 파일을 띄우는 Standalone Game 항목과 다르다). **Play → 플레이 화면 클릭** 후 평소 이동·시점·점프 키를 사용한다.

| 키 | 동작 |
|---|---|
| **Ctrl+Alt+Shift+F10** | 사망 → **10초 자동 부활** |
| **Ctrl+Alt+Shift+F7** | 남녀 전환 |

입력은 테스트 GameMode에만 바인딩한다. 기존 EnhancedInput context 14개에서 F7/F10 충돌이 없음을 검사했고 수정 키 조합과 입력 소비를 지정했다. 테스트 모드는 ClassFeatureEditor에 있어 **PIE에서 사용**한다. Standalone Game이나 패키징을 위한 사망/부활 관리자를 추가한 것이 아니다. 새 생명 초기화/진행도 복원은 기존 editor 검증 helper와 운영 플레이어 경로를 사용한다.

## 검증과 한계

실제 프로젝트 재검증 수치는 [요약 JSON](validation/Player_Ragdoll_PoseFix_20261031.json)을 따른다. 이전 독립 환경의 8개/1,896프레임 검사나 이전 리깅 23개 회귀를 이번 프로젝트 실행 결과와 혼동하지 않는다. 원시 자료와 변경 전 에셋 사본은 `Saved/PlayerRagdollPoseFix`에 보존한다.

프로젝트 반영 후 새 프로세스에서 **남녀 8개 시나리오 / 1,896프레임**을 다시 실행했다. 별도 수동 설치 없이 사망 경로에서 visual이 생성됐고, 물리 원본과 visual의 대응 본 오차는 최대 약 **0.000228cm**, 팔·다리 twist의 관절 선 이탈은 최대 약 **1.57e-13cm**였다. 재질 7개 일치·유효성, reset 후 애니메이션/렌더 복원과 이동 재개도 검사했다. native 회귀 **23개 통과**(기존 경고 포함 6개), 프로젝트 맵의 남녀 **10초 자동 부활 및 성별 전환 통과**를 확인했다. 바인딩된 chord delegate를 직접 실행했으며 Windows에 키를 주입하지 않았다.

충격 순간 최대 angular limit 초과는 약 **24.33°**, 정착 후 최대는 약 **0.335°**였다. 요청 각도가 모든 충격에서 오차 없이 고정된다는 결과는 아니다.

실제 프로젝트의 수정 후 UE 캡처:

![남성 시체](images/PlayerRagdollPoseFix/Man_idle_settled_angled.png)

![여성 시체와 복구한 재질](images/PlayerRagdollPoseFix/Woman_idle_settled_angled.png)

보정은 시체의 스킨 평가 단계에 대한 구조적 대응이다. 물리 강체를 화면에서 임의로 꺾어 solver 오차를 숨기지 않는다. 순간 충격의 제한 초과, 옷·피부·신발·긴 머리카락/가방 관통이 모두 없어졌다는 결과는 아니다. 실제 배/물속, 네트워크 지연, 다수 시체 성능, 모든 LOD/모프/커스텀 재질 데이터, cook/packaging은 별도 검증이 필요하다.
