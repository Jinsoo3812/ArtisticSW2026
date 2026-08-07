# FacilityHub 제작·배 강화 통합 가이드

> 기준: 2026-07-23 `master`의 제작 UI 병합 후  
> 목표: 하나의 공용 FacilityHub 창과 Context 아래에 제작과 배 강화를 독립 자식 패널로 둔다.

## 1. 최종 구조

```text
BP_FacilityHub
└─ AFacilityHubActor
   └─ 상호작용 서버 검증
      └─ ABasePlayerController::OpenFacilityHubFromServer
         └─ ClientOpenFacilityHub
            └─ WBP_WorkspaceScreen
               └─ UFacilityHubWidget 공용 창/Context
                  ├─ index 0: WBP_ShipUpgradeScreen
                  ├─ index 1: WBP_CraftingPanel
                  └─ index 2: Skill Upgrade placeholder
```

다음 원칙은 코드에 반영되어 있다.

- `FacilityHubWidget`만 최상위 창과 Context를 소유한다.
- `WBP_WorkspaceScreen`의 기존 배 강화 디자인은 유지한다.
- 동료의 `FacilityHubActor`, `CraftingComponent`, `WBP_CraftingPanel`과 제작 행 위젯을 유지한다.
- `ShipUpgradeWorkspaceWidget`은 별도 창이 아니라 `FacilityHubWidget`의 얇은 호환 자식이다.
- 배 강화와 제작은 서로의 내부 로직을 모르는 독립 자식 패널이다.
- 제작 탭은 서버가 `CraftingAccessComponent`와 사용 거리를 승인한 뒤에만 열린다.
- 배 강화 탭은 기존 `UShipUpgradeScreenWidget` C++ 흐름을 그대로 사용한다.
- 인벤토리, 상태창, 창고, FacilityHub의 입력 모드와 닫기는 PlayerController 한 곳에서 관리한다.
- `BP_WorkTable`, `CrafterComponent`, StarForce 코드는 삭제하지 않았지만 현재 경로에는 참여하지 않는다.

## 2. 병합 후 보존된 자산

배 강화:

```text
/Game/Blueprints/02_UI/UI_WorkTable/WBP_WorkspaceScreen
/Game/Blueprints/02_UI/UI_WorkTable/UI_ShipUpgrade/WBP_ShipUpgradeScreen
```

제작과 Facility:

```text
/Game/Blueprints/02_UI/UI_FacilityHub/WBP_FacilityHub
/Game/Blueprints/02_UI/UI_FacilityHub/Crafting/WBP_CraftingPanel
/Game/Blueprints/02_UI/UI_FacilityHub/Crafting/WBP_CraftingRecipeEntry
/Game/Blueprints/02_UI/UI_FacilityHub/Crafting/WBP_CraftingIngredientEntry
/Game/Blueprints/03_WorldObject/03_FacilityHub/BP_FacilityHub
```

`WBP_FacilityHub`는 동료 작업 보존 및 참고용으로 남아 있다. 현재 게임의 공용 창 Class에는 배 강화 디자인을 살린 `WBP_WorkspaceScreen`을 지정한다. 두 WBP를 각각 별도 창으로 열면 안 된다.

## 3. 에디터에서 해야 할 최소 작업

### 3.1 에디터 재시작

1. Unreal Editor가 열려 있으면 완전히 종료한다.
2. 프로젝트를 다시 연다.
3. Live Coding만으로 부모 클래스 변경을 반영하려 하지 않는다.

### 3.2 PlayerController에 공용 창 지정

1. `/Game/Blueprints/Player/BP_BasePlayerController`를 연다.
2. `Class Defaults`를 누른다.
3. 검색창에 `Facility Hub Widget Class`를 입력한다.
4. 값을 `WBP_WorkspaceScreen`으로 지정한다.
5. Compile, Save한다.

지정하지 않아도 C++가 `WBP_WorkspaceScreen`을 먼저 찾는 안전장치가 있지만, 에셋 의존성을 명시하기 위해 직접 지정하는 것을 권장한다.

기존 `Ship Upgrade Workspace Widget Class` 항목은 제거된 레거시 경로다. 에디터에 오래된 항목이 남아 보이면 재시작 후 다시 확인하며, 새 작업에서는 사용하지 않는다.

### 3.3 WBP_WorkspaceScreen 계층 확인

1. `/Game/Blueprints/02_UI/UI_WorkTable/WBP_WorkspaceScreen`을 연다.
2. `Class Settings`에서 부모가 `ShipUpgradeWorkspaceWidget`인지 확인한다.
3. 다음 이름이 정확한지 확인한다.

```text
WidgetSwitcher_Content
Button_ShipUpgrade
Button_ItemCrafting
Button_SkillUpgrade
Button_Close
```

4. `WidgetSwitcher_Content`의 자식 순서를 아래처럼 맞춘다.

```text
index 0 = WBP_ShipUpgradeScreen
index 1 = WBP_CraftingPanel
index 2 = Skill Upgrade placeholder
```

5. index 1에 배치한 `WBP_CraftingPanel` 인스턴스 이름을 정확히 `CraftingPanelWidget`으로 바꾼다.
6. Compile, Save한다.

제작 패널을 아직 직접 넣지 않았으면 C++가 index 1이 비어 있을 때 동료의 `WBP_CraftingPanel`을 자동 생성한다. 그러나 최종 레이아웃 크기, Padding, Anchor를 눈으로 조절하려면 위처럼 직접 배치하는 편이 좋다.

버튼의 `OnClicked` Event Graph는 만들지 않는다. 탭 전환, 제작 승인 대기, 닫기 모두 C++가 연결한다.

### 3.4 WBP_CraftingPanel 확인

1. `/Game/Blueprints/02_UI/UI_FacilityHub/Crafting/WBP_CraftingPanel`을 연다.
2. 부모가 `CraftingPanelWidget`인지 확인한다.
3. 기존 동료의 레이아웃과 Event Graph는 삭제하지 않는다.
4. Compile, Save한다.

레시피 조회, 선택, 재료 표시, 제작 요청, 결과 처리는 이미 C++에 있다. 새 Blueprint 로직을 만들 필요가 없다.

기존 자산의 `Ingredientlist` 철자도 C++ 호환 코드가 처리한다. 나중에 정리하려면 인스턴스 이름을 `IngredientList`로 바꿔도 된다.

### 3.5 BP_FacilityHub 확인 및 배치

1. `/Game/Blueprints/03_WorldObject/03_FacilityHub/BP_FacilityHub`를 연다.
2. 부모가 `FacilityHubActor`인지 확인한다.
3. 다음 컴포넌트가 있는지 확인한다.

```text
Interactable
CraftingAccess
StaticMesh
```

4. `Interactable`의 Interaction Tag가 `Interaction.Craft`인지 확인한다.
5. `CraftingAccess`의 사용 거리와 허용 레시피 설정을 확인한다.
6. Compile, Save한다.
7. 테스트 레벨의 `BP_WorkTable` 대신 `BP_FacilityHub`를 배치한다.

`FacilityHubActor`가 `Interactable.OnInteracted`를 받아 PlayerController의 단일 FacilityHub 경로를 연다. `BP_WorkTable`은 이 경로의 진입점이 아니다.

### 3.6 BP_Player 확인

1. `/Game/Blueprints/Player/BP_Player`를 연다.
2. `CraftingComponent`가 있는지 확인한다.
3. 레거시 `CrafterComponent`가 없는지 확인한다.
4. Compile, Save한다.

`CraftingComponent`는 현재 제작 서비스다. `CrafterComponent`는 StarForce 기반 레거시이므로 현재 플레이어에 추가하지 않는다.

## 4. WBP에서 만들지 않는 로직

다음은 모두 C++ 책임이다.

- Facility Context 저장과 검증
- FacilityHub 생성, 닫기, ESC 처리와 입력 모드 복구
- 인벤토리·상태창·창고와의 상호 배제
- 탭 버튼 바인딩과 WidgetSwitcher 전환
- 제작 탭 서버 승인과 제작 패널 활성화
- 레시피/재료 행 동적 생성
- 제작 요청과 결과 처리
- 배 강화 컴포넌트 탐색과 이벤트 바인딩
- 노드, 연결선, 상세 재료/스탯 행 생성
- 아이콘과 프리뷰 Actor 비동기 로드
- 배 강화 서버 요청과 결과 처리

WBP에서는 배치, 크기, 색, 폰트, 이미지, 애니메이션만 다룬다.

## 5. PIE 검증 순서

Standalone PIE에서 먼저 확인한다.

1. `BP_FacilityHub`에 접근하여 상호작용한다.
2. `WBP_WorkspaceScreen` 하나만 열리는지 확인한다.
3. 첫 화면이 기존 배 강화 화면인지 확인한다.
4. 배 강화 노드 선택, 상세 표시, 재료 갱신, 프리뷰 회전이 기존처럼 동작하는지 확인한다.
5. `아이템 제작` 버튼을 누른다.
6. 서버 승인 후 같은 창 안에서 `WBP_CraftingPanel`로 전환되는지 확인한다.
7. 레시피 선택, 재료 표시, 제작 버튼과 결과가 동작하는지 확인한다.
8. 다시 `배 강화` 버튼을 눌러 제작 Context가 닫히고 배 강화 패널로 돌아가는지 확인한다.
9. X와 ESC 각각으로 닫고 캐릭터 입력과 HUD가 복구되는지 확인한다.
10. 인벤토리, 상태창, 창고가 열린 상태에서 FacilityHub를 열어 창이 겹치지 않는지 확인한다.
11. Listen Server 2인 PIE에서 상호작용한 클라이언트에게만 창이 열리는지 확인한다.

## 6. 문제 해결

| 증상 | 확인할 것 |
|---|---|
| F를 눌러도 창이 안 열림 | 레벨에 `BP_FacilityHub`를 배치했는지, 부모가 `FacilityHubActor`인지, `Interactable`이 있는지 |
| 옛 StarForce UI가 열림 | `BP_Player`에 `CrafterComponent`가 남아 있거나 레벨에 `BP_WorkTable`을 사용 중인지 |
| 배 강화 디자인 대신 동료 Hub만 열림 | `BP_BasePlayerController.FacilityHubWidgetClass`를 `WBP_WorkspaceScreen`으로 지정했는지 |
| 제작 버튼을 눌러도 전환 안 됨 | Hub에 `CraftingAccess`, 플레이어에 `CraftingComponent`, 사용 거리 안인지 |
| 제작 탭이 비어 있음 | index 1에 `WBP_CraftingPanel`을 넣고 인스턴스 이름을 `CraftingPanelWidget`으로 지정 |
| Ingredient 행이 안 보임 | `IngredientList` 또는 기존 `Ingredientlist` 패널 존재, Ingredient Entry Class 지정 |
| 두 창이 겹쳐 열림 | 별도 Blueprint에서 `WBP_FacilityHub`나 `WBP_WorkspaceScreen`을 직접 Create Widget 하고 있지 않은지 |
| WBP 컴파일 오류 | 에디터 완전 재시작, C++ Editor 빌드 성공 여부, 필수 위젯 이름 확인 |

## 7. 통합 검증 결과

2026-07-23 기준:

- `master` 병합 충돌 해결 완료
- UE 5.7 Editor Target 빌드 성공
- `ArtisticSW` 자동화 테스트 17/17 성공
- 제작 `FullPipeline` 성공
- 배 강화 계산, 전체 파이프라인, 재료·다중 선행 노드 테스트 성공
- 전체 Blueprint 컴파일 0 errors, 0 warnings, 0 load failures

프로젝트 전체 검사에서 기존 Foley 및 EnemyDropData GameplayTag 경고는 별도로 출력되지만, FacilityHub·제작·배 강화 WBP 컴파일 결과에는 오류가 없다.
