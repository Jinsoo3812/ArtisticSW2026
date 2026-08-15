# NPC Dialogue 사용법

## 모듈 경계

- `Story`: 캠페인 전체가 공유하는 스토리 상태와 저장/복제
- `NPCDialogue`: NPC Pawn, 대화 규칙 선택, NPC 단독 예약, 카메라, UI 연결 지점
- `ClassFeature`: 플레이어 및 실제 인벤토리 구현

`NPCDialogue`는 `Story`와 `ArtisticSWCore`만 알고, 플레이어 구현은 `IDialogueInventoryProvider` 인터페이스로 연결된다. `ClassFeature`가 이 인터페이스를 구현한다.

## 테스트 NPC

- `/Game/New/NPC/Blueprints/BP_TestNPC`
- `/Game/New/NPC/Data/DA_TestNPCDialogue`

`BP_TestNPC`를 레벨에 배치한 뒤 Skeletal Mesh와 Animation Blueprint를 지정한다. NPC 블루프린트의 `DialogueCameraAnchor`를 옮기면 NPC별 카메라 구도를 바꿀 수 있다. FOV와 블렌드 시간은 Dialogue Data Asset에서 조정한다.

## 대화 규칙 작성

`DA_TestNPCDialogue` 또는 복제한 `NPCDialogueData`의 `Rules`를 편집한다. 조건을 만족하는 규칙 중 Priority가 가장 높은 규칙이 선택된다. 같은 Priority에서는 RuleId 오름차순으로 결정되어 네트워크에서도 선택이 안정적이다.

- 일상 대화: 스토리 완료 노드를 지정하지 않고 Priority 0 정도로 둔다.
- 퀘스트 제안/진행: Required Story Nodes와 Blocked Story Nodes를 지정하고 일상 대화보다 높은 Priority를 준다.
- 퀘스트 완료: Consumed Items, Reward Items, Complete Story Node를 설정한다. 마지막 줄을 넘길 때 서버가 아이템 변경과 스토리 완료를 커밋한다.
- 한 번만 보는 공유 대화: Complete Story Node와 Hide After Story Completion을 켠다. 한 플레이어가 끝내면 공유 Story 상태가 갱신되어 다른 플레이어에게도 더 이상 선택되지 않는다.
- 선택지: Line의 Replies에 Reply Id, Text, Next Line Id를 설정한다. Next Line Id가 비어 있으면 선택 즉시 결과를 커밋하고 종료한다. 값이 있으면 같은 Rule 안의 해당 Line Id로 이동한다.

아이템 변경과 스토리 완료는 마지막 줄 직전에 다시 검증된다. 같은 공유 노드를 두 플레이어가 거의 동시에 진행해도 먼저 완료한 한 명만 보상을 받을 수 있다.

## WBP Designer 편집

Designer 전용 UI는 `/Game/New/NPC/Blueprints/UI/WBP_NPCDialogue`와 `WBP_NPCDialogueReply`이다. `BP_Player`와 `BP_BasePlayerTest`의 `PlayerDialogueComponent.DialogueWidgetClass`에 연결되어 있다. 대화 시작 시 자동 생성되고 종료 시 제거된다.

WBP Graph와 프로퍼티 바인딩은 사용하지 않는다. 디자이너는 Designer 탭에서 하단 박스, 이름/대화문, 우측 상단 Reply의 위치·크기·폰트·색상만 조정한다. 네이티브 `BindWidget`에 사용하는 다음 이름은 변경하면 안 된다.

- `Text_NPCName`
- `Text_Dialogue`
- `VerticalBox_Replies`
- `Text_ContinueHint`
- Reply WBP의 `Button_Reply`
- Reply WBP의 `Text_Reply`

일반 대사는 화면 클릭, Enter, Space 또는 게임패드 확인으로 진행한다. Reply가 표시된 동안에는 일반 진행 입력이 차단되고 선택한 Reply Id만 서버로 전달된다. Escape는 대화를 취소한다. 초상화 필드는 의도적으로 포함하지 않았다.

## 멀티플레이 동작

- 예약은 서버 권한이며 NPC 한 명당 동시에 한 플레이어만 허용한다.
- 다른 플레이어는 Busy 실패 이벤트만 받고 대화로 끌려오지 않는다.
- 스토리 진행은 기존 Story 모듈의 공유 복제 상태를 사용한다.
- 전투 여부 제한은 넣지 않았다.
