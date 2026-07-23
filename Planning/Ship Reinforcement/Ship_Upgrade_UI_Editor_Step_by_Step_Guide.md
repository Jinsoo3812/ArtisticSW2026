# 배 강화 UI 에디터 제작 순서

## 1. 이 가이드의 완료 목표

이 문서를 위에서 아래로 따라가면 다음 흐름이 완성된다.

```text
BP_WorkTable 상호작용
    -> 소유 클라이언트에 WBP_WorkspaceScreen 표시
    -> 첫 탭으로 배 강화 화면 표시
    -> DA_ShipUpgradeTree로 노드와 연결선 생성
    -> 노드 선택 시 전후 스탯과 재료 표시
    -> 활성화 요청 후 서버 결과 수신
    -> 재료 차감과 후속 노드 갱신
    -> ESC/닫기 버튼으로 게임 입력 복귀
```

먼저 2D 기능 화면을 완성한 뒤 3D 배/대포 프리뷰를 붙인다. 3D부터 시작하면 데이터 문제와 렌더 문제를 동시에 디버깅하게 되므로 순서를 바꾸지 않는 것을 권장한다.

현재 C++ 쪽에는 다음 준비가 끝나 있다.

- `ABasePlayerController.OpenShipUpgradeWorkspace()`
- `ABasePlayerController.CloseShipUpgradeWorkspace()`
- 작업대 상호작용 후 소유 클라이언트 화면 오픈
- 노드 View, 재료 View, 전후 스탯, 비동기 활성화 결과
- 2D/대포 3D/배 3D 프리뷰 타입과 Actor Class 필드
- 인벤토리 재료 원자 차감

---

## 2. 에디터를 열기 전

1. 실행 중인 Unreal Editor를 닫는다.
2. `ArtisticSW2026Editor Win64 Development` 빌드가 성공한 상태인지 확인한다.
3. 에디터를 다시 연다.
4. 기존 `DA_ShipUpgradeTree`를 열었을 때 각 Node 아래에 다음 항목이 보이는지 확인한다.

```text
Preview Type
Preview Actor Class
Activated Ship Actor Class
Activated Cannon Actor Class
Visual Priority
Camera Preset
```

보이지 않으면 이전 DLL을 로드한 상태이므로 에디터를 완전히 종료한 뒤 다시 빌드하고 연다.

---

## 3. 콘텐츠 폴더와 생성할 자산

Content Browser에서 다음 폴더를 만든다.

```text
/Game/Blueprints/02_UI/UI_WorkTable/UI_ShipUpgrade
/Game/Blueprints/02_UI/UI_WorkTable/UI_ShipUpgrade/Nodes
/Game/Blueprints/02_UI/UI_WorkTable/UI_ShipUpgrade/Details
/Game/Blueprints/02_UI/UI_WorkTable/UI_ShipUpgrade/Preview
/Game/Blueprints/02_UI/UI_WorkTable/UI_ShipUpgrade/Materials
```

다음 Widget Blueprint를 만든다. 모두 부모 클래스는 우선 `UserWidget`이면 된다.

| 자산 | 역할 |
| --- | --- |
| `WBP_WorkspaceScreen` | 작업대 전체 화면, 왼쪽 메뉴와 닫기 버튼 소유 |
| `WBP_ShipUpgradeScreen` | 배 강화 탭 본체, 컴포넌트와 이벤트 소유 |
| `WBP_ShipUpgradeNode` | 노드 하나의 이미지와 상태 표현 |
| `WBP_ShipUpgradeConnection` | 두 노드 사이 실선/점선 |
| `WBP_ShipUpgradeDetails` | 선택 노드의 이름, 수치, 재료, 활성화 버튼 |
| `WBP_ShipUpgradeMaterialRow` | 재료 한 종류의 아이콘과 보유량/필요량 |

3D 단계에서 다음 자산을 추가한다.

| 자산 | 역할 |
| --- | --- |
| `RT_ShipUpgradePreview` | SceneCapture 결과를 받을 Render Target |
| `BP_ShipUpgradePreviewStage` | SceneCapture와 프리뷰 Actor 스폰 위치 소유 |
| `BP_UIShipPreview_Base` | 기본 배 프리뷰 Actor |
| `BP_UICannonPreview_Base` | 기본 대포 프리뷰 Actor |

---

## 4. `WBP_ShipUpgradeNode` 만들기

### 4.1 Designer 계층

권장 크기는 우선 `160 x 130`이다. 나중에 전체 그래프를 보며 조절한다.

```text
SizeBox_Root (160 x 130)
└─ Overlay
   ├─ Border_StateGlow
   ├─ Button_Node
   │  └─ Overlay
   │     ├─ Image_Icon
   │     ├─ Image_Lock
   │     └─ Image_Check
   └─ Text_Name
```

초기 Visibility:

- `Image_Lock`: Collapsed
- `Image_Check`: Collapsed
- `Text_Name`: Hit Test Invisible
- `Border_StateGlow`: Hit Test Invisible

### 4.2 변수

다음 변수를 만든다.

| 변수 | 타입 | 설정 |
| --- | --- | --- |
| `NodeId` | Name | Instance Editable, Expose on Spawn |
| `NodeView` | Ship Upgrade Node View | 내부 캐시 |
| `bSelected` | Boolean | 기본 false |

Event Dispatcher:

```text
OnNodeSelected(NodeId : Name)
```

### 4.3 함수 `ApplyNodeView`

입력은 `Ship Upgrade Node View` 하나다.

```text
Set NodeView
Set NodeId = View.NodeId
Set Text_Name = View.DisplayName
Async Load Asset(View.Icon)
    -> Set Brush From Texture(Image_Icon)

Switch on EShipUpgradeNodeState(View.State)
    Locked:
        Image_Lock Visible
        Image_Check Collapsed
        Icon opacity 0.20
        Glow 비활성 색
    Available:
        Image_Lock Collapsed
        Image_Check Collapsed
        Icon opacity 0.65 또는 1.0
        Glow 강조 색
    Active:
        Image_Lock Collapsed
        Image_Check Visible
        Icon opacity 1.0
        Glow 완료 색
```

아이콘 Soft Reference가 비어 있으면 임시 Placeholder Texture를 표시한다. 노드마다 `Load Synchronous`를 반복하기보다 `Async Load Asset`을 사용한다.

### 4.4 선택과 호버

`Button_Node.OnClicked`:

```text
OnNodeSelected.Broadcast(NodeId)
```

`OnHovered`와 `OnUnhovered`에서는 Render Transform Scale을 `1.0 -> 1.08` 정도로 변경한다. 선택 상태는 별도 함수 `SetSelected(bool)`에서 테두리 색과 Scale을 조정한다.

잠긴 노드의 상세 정보를 호버 전에는 숨기고 싶다면 Screen 쪽에서 선택 허용 여부를 정한다. API에는 잠긴 노드의 정보도 들어 있으므로 Widget 표시 정책만 바꾸면 된다.

---

## 5. `WBP_ShipUpgradeConnection` 만들기

### 5.1 Designer

```text
SizeBox_Root
└─ Image_Line
```

실선용 Brush와 점선용 Brush를 각각 준비한다. 점선 Texture는 가로 방향 Tiling이 가능한 얇은 흰색 점선이면 충분하다.

### 5.2 함수 `ConfigureConnection`

입력:

```text
Start : Vector2D
End : Vector2D
bDashed : Boolean
```

계산:

```text
Delta = End - Start
Length = Vector2D Length(Delta)
Angle = Degrees(Atan2(Delta.Y, Delta.X))
```

부모 `CanvasPanelSlot`에 다음을 적용한다.

```text
Position = Start
Size = (Length, LineThickness)
Alignment = (0.0, 0.5)
ZOrder = 0
```

`Image_Line.RenderTransform.Angle = Angle`로 설정한다. 노드 Widget의 ZOrder는 10 이상으로 둔다.

선의 시작/끝은 노드 좌상단이 아니라 노드 중심을 사용한다.

```text
NodeCenter = GraphPosition + (NodeWidth / 2, NodeHeight / 2)
```

자식 노드가 `Locked`이면 점선, `Available` 또는 `Active`이면 실선으로 표시하면 현재 기획 표현과 맞는다.

---

## 6. `WBP_ShipUpgradeMaterialRow` 만들기

Designer:

```text
HorizontalBox
├─ Image_ItemIcon
├─ Text_ItemName
├─ Spacer
└─ Text_Count
```

함수 `ApplyMaterialView(MaterialView)`:

```text
Async Load Asset(MaterialView.Icon)
Text_ItemName = MaterialView.DisplayName
Text_Count = Format "{OwnedQuantity} / {RequiredQuantity}"
bEnough == true  -> 정상/청록색
bEnough == false -> 빨강 또는 주황색
```

UI에서 OwnedQuantity를 임의로 빼지 않는다. 활성화 후 `OnUpgradeDataChanged`가 오면 View를 다시 받아 갱신한다.

---

## 7. `WBP_ShipUpgradeDetails` 만들기

### 7.1 Designer

```text
VerticalBox
├─ Text_NodeName
├─ Text_Description
├─ Image_2DPreview
├─ SizeBox_3DPreview
├─ VerticalBox_StatChanges
├─ VerticalBox_MaterialCosts
├─ Text_UnavailableReason
├─ Button_Activate
│  └─ Text_Activate
└─ Throbber_Requesting
```

초기 상태:

- 3D 프리뷰와 2D 프리뷰 중 하나만 보이게 한다.
- `Throbber_Requesting`은 Collapsed다.
- 선택 노드가 없으면 상세 패널 전체를 숨긴다.

### 7.2 변수와 Dispatcher

| 변수 | 타입 |
| --- | --- |
| `UpgradeComponent` | Ship Upgrade Component Object Reference |
| `SelectedView` | Ship Upgrade Node View |
| `bRequestPending` | Boolean |

Event Dispatcher:

```text
OnActivateRequested(NodeId : Name)
OnPreviewRequested(NodeView : Ship Upgrade Node View)
```

### 7.3 함수 `ShowNode`

1. `SelectedView`를 저장한다.
2. 이름과 설명을 채운다.
3. `StatChanges`를 순회해 TextBlock 또는 전용 Row를 생성한다.
4. 각 행의 Text는 `FormattedText`를 그대로 사용한다.
5. 색은 Delta 부호가 아니라 `bImprovesStat`으로 결정한다.
6. `MaterialCosts`를 순회해 `WBP_ShipUpgradeMaterialRow`를 생성한다.
7. `UnavailableReason`을 표시한다.
8. `Can Activate Node(NodeId, OutReason)` 결과로 버튼 활성 상태를 정한다.
9. `PreviewType`에 따라 2D Image 또는 3D Preview 영역을 보인다.
10. `OnPreviewRequested`를 Screen에 전달한다.

재료가 부족해도 스탯 변화는 `SelectedView.StatChanges`로 표시할 수 있다. 이때 전체 Snapshot용 `Get Stats After Activating` 실패를 UI 오류로 취급하지 않는다.

### 7.4 활성화 버튼

`Button_Activate.OnClicked`:

```text
Branch(!bRequestPending)
    -> bRequestPending = true
    -> Button 비활성
    -> Throbber Visible
    -> OnActivateRequested.Broadcast(SelectedView.NodeId)
```

함수 `FinishActivationRequest`에서 Pending을 해제한다. 성공 여부와 관계없이 서버 결과가 오면 호출한다.

---

## 8. `WBP_ShipUpgradeScreen` 만들기

### 8.1 Designer 계층

```text
Overlay
├─ Border_Background
├─ Image_MainShipPreview
├─ ScrollBox_GraphScroll
│  └─ SizeBox_GraphExtent
│     └─ CanvasPanel_Graph
├─ WBP_ShipUpgradeDetails
├─ HorizontalBox_CurrentStats
└─ Text_ResultMessage
```

시작값 예시:

```text
GraphExtent = 2400 x 1600
NodeSize = 160 x 130
GraphOriginOffset = 300 x 150
Zoom = 1.0
ZoomMin = 0.6
ZoomMax = 1.8
```

`CanvasPanel_Graph`에는 연결선을 먼저 넣고 노드를 나중에 넣어야 한다.

### 8.2 변수

| 변수 | 타입 |
| --- | --- |
| `UpgradeComponent` | Ship Upgrade Component Object Reference |
| `NodeViews` | Ship Upgrade Node View Array |
| `NodeWidgetMap` | Name -> WBP_ShipUpgradeNode Reference Map |
| `SelectedNodeId` | Name |
| `PendingNodeIds` | Name Set 또는 Name Array |
| `Zoom` | Float |
| `InitRetryCount` | Integer |

### 8.3 `Event Construct`

```text
TryInitializeUpgrade
```

Custom Event `TryInitializeUpgrade`:

```text
Get Local Ship Upgrade Component(Self)
IsValid?
    false:
        InitRetryCount += 1
        InitRetryCount < 30 이면 0.1초 뒤 다시 호출
        아니면 "PlayerState/UpgradeComponent 준비 실패" 표시
    true:
        Set UpgradeComponent
        Bind Component Events
        Details.UpgradeComponent = UpgradeComponent
        UpgradeComponent.RefreshUpgradeData
        RefreshAll
```

`OnUpgradeDataReady`만 기다리지 않는다. 컴포넌트 BeginPlay에서 화면보다 먼저 발생했을 수 있다.

### 8.4 컴포넌트 이벤트 바인딩

다음 이벤트를 Screen의 Custom Event에 연결한다.

| 컴포넌트 이벤트 | Screen 처리 |
| --- | --- |
| `OnUpgradeDataReady` | `RefreshAll` |
| `OnUpgradeDataChanged` | `RefreshAll` |
| `OnNodeStateChanged` | 단독 최적화보다 `RefreshAll` 사용 권장 |
| `OnNodeActivationResult` | Pending 해제, 메시지 표시, `RefreshAll` |
| `OnShipStatsChanged` | 상단 현재 스탯 갱신 |

화면을 제거하지 않고 Collapsed로 재사용하므로 Construct에서 중복 바인딩되지 않게 한다. `bEventsBound` Boolean을 두거나 먼저 Unbind 후 Bind한다.

### 8.5 함수 `RefreshAll`

```text
Get All Node Views
    -> Set NodeViews
    -> RebuildGraph
    -> RefreshSelectedNode
    -> Get Current Ship Stats
    -> RefreshCurrentStats
    -> RefreshActiveVisuals
```

재료 변경과 부모 활성화는 여러 자식 노드에 영향을 준다. 따라서 `OnNodeStateChanged`로 한 개만 바꾸기보다 `OnUpgradeDataChanged`에서 전체 View를 다시 받는 편이 안전하다.

### 8.6 함수 `RebuildGraph`

1. `CanvasPanel_Graph.ClearChildren`.
2. `NodeWidgetMap.Clear`.
3. `NodeViews`를 한 번 순회해 노드 Widget을 전부 생성한다.
4. `GraphPosition + GraphOriginOffset`을 Canvas Slot Position으로 지정한다.
5. `ApplyNodeView` 호출.
6. `OnNodeSelected`를 Screen의 `HandleNodeSelected`에 바인딩한다.
7. `NodeWidgetMap.Add(NodeId, Widget)`.
8. 노드 생성이 끝나면 `NodeViews`를 다시 순회해 연결선을 만든다.
9. 각 View의 `PrerequisiteNodeIds`마다 부모 위치에서 현재 노드 위치로 선을 만든다.
10. 연결선 ZOrder 0, 노드 ZOrder 10.

`GetAllNodeViews()` 배열 순서를 영구 ID나 계층 순서로 믿지 않는다. 부모 검색에는 `NodeWidgetMap` 또는 NodeId 기반 View 검색을 사용한다.

### 8.7 노드 선택

Custom Event `HandleNodeSelected(NodeId)`:

```text
SelectedNodeId = NodeId
Get Node View(NodeId)
    -> 모든 Node Widget SetSelected(false)
    -> 선택 Widget SetSelected(true)
    -> Details.ShowNode(View)
```

Details의 `OnActivateRequested`:

```text
PendingNodeIds에 NodeId가 없을 때만
    -> PendingNodeIds.Add(NodeId)
    -> UpgradeComponent.RequestActivateNode(NodeId)
```

`OnNodeActivationResult(NodeId, Result, Message)`:

```text
PendingNodeIds.Remove(NodeId)
Details.FinishActivationRequest
Text_ResultMessage = Message
Result == Success 이면 성공 애니메이션/사운드
RefreshAll
```

### 8.8 그래프 스크롤과 줌

첫 기능 버전에서는 ScrollBox의 세로/가로 Scrollbar를 모두 보이게 한다.

줌을 추가할 때 Screen의 `OnMouseWheel`을 Override한다.

```text
NewZoom = Clamp(Zoom + WheelDelta * 0.1, 0.6, 1.8)
CanvasPanel_Graph.RenderTransform.Scale = (NewZoom, NewZoom)
SizeBox_GraphExtent.WidthOverride = BaseWidth * NewZoom
SizeBox_GraphExtent.HeightOverride = BaseHeight * NewZoom
Zoom = NewZoom
Handled
```

Render Transform만 줄이고 SizeBox 크기를 바꾸지 않으면 Scroll 범위와 보이는 그래프 크기가 어긋날 수 있으므로 둘을 같이 갱신한다.

---

## 9. `WBP_WorkspaceScreen` 만들기

### 9.1 Designer

```text
CanvasPanel_Root
├─ Border_FullscreenBackground
├─ VerticalBox_LeftMenu
│  ├─ Text_WorkSpace
│  ├─ Button_ShipUpgrade
│  ├─ Button_ItemCrafting
│  └─ Button_SkillUpgrade
├─ WidgetSwitcher_Content
│  ├─ WBP_ShipUpgradeScreen
│  ├─ 제작 화면 Placeholder
│  └─ 스킬 강화 Placeholder
└─ Button_Close
```

`Event Construct` 또는 `OnInitialized`에서 `WidgetSwitcher_Content.ActiveWidgetIndex = 0`으로 설정한다. 작업대 상호작용 시 항상 배 강화로 시작한다는 요구가 이 한 줄로 충족된다.

버튼:

```text
Button_ShipUpgrade -> Index 0
Button_ItemCrafting -> Index 1
Button_SkillUpgrade -> Index 2
```

`Button_Close.OnClicked`:

```text
Get Owning Player
    -> Cast to BP_BasePlayerController 또는 BasePlayerController
    -> Close Ship Upgrade Workspace
```

전체 화면 Root의 Visibility는 Controller가 관리하므로 Widget 내부에서 `Remove From Parent`를 호출하지 않는다.

---

## 10. PlayerController에 화면 클래스 지정

1. `/Game/Blueprints/Player/BP_BasePlayerController`를 연다.
2. `Class Defaults`를 연다.
3. `UI > Workspace` 카테고리를 찾는다.
4. `Ship Upgrade Workspace Widget Class`에 `WBP_WorkspaceScreen`을 지정한다.
5. Compile, Save.
6. 테스트 레벨의 World Settings와 GameMode에서 실제 PlayerController Class가 `BP_BasePlayerController`인지 확인한다.

다른 PlayerController Blueprint를 사용하는 레벨이라면 그 Blueprint에도 같은 값을 지정하거나 부모를 `BP_BasePlayerController` 계열로 맞춰야 한다.

---

## 11. 작업대 설정

1. `/Game/Blueprints/Interactable_Object/BP_WorkTable`을 연다.
2. Class Defaults에서 `Open Integrated Workspace`가 체크되어 있는지 확인한다.
3. 작업대의 `InteractableComponent`가 기존 상호작용 경로를 유지하는지 확인한다.
4. Interaction Tag는 기존 제작 상호작용에 사용하던 `Interaction.Craft`를 유지한다.
5. Compile, Save.

`Open Integrated Workspace`가 true면 `InteractPopupUIClass`의 기존 `WBP_StarForce` 대신 통합 화면이 열린다. 옛 스타포스 팝업을 별도로 시험할 작업대만 이 값을 false로 둔다.

---

## 12. `DA_ShipUpgradeTree` 입력

자산 위치:

```text
/Game/New/Ship/Upgrade/DA_ShipUpgradeTree
```

각 노드에서 최소한 다음을 채운다.

```text
NodeId
DisplayName
Description
Icon
GraphPosition
PrerequisiteNodeIds
StatModifiers
ActivationCosts
PreviewType
```

현재 노드의 권장 PreviewType:

| NodeId | 권장 PreviewType |
| --- | --- |
| `Hull_I` | `Ship3D` |
| `CannonDamage_I` | `Cannon3D` |
| `Reload_I` | `Cannon3D` |
| `CannonballSpeed_I` | `Cannon3D` |
| `Propulsion_I` | `Ship3D` |
| `Turn_I` | `Ship3D` |

초기 2D 구현 중에는 Preview Actor Class를 비워 두어도 된다. 이 경우 `Icon`을 대신 표시한다.

GraphPosition 예시는 다음처럼 루트가 위, 자식이 아래로 내려가게 배치한다.

```text
Hull_I               (900, 100)
CannonDamage_I       (500, 420)
Propulsion_I         (1100, 420)
Reload_I             (300, 760)
CannonballSpeed_I    (700, 760)
Turn_I               (1300, 760)
```

실제 부모 관계는 기획에 맞게 입력한다. 위 좌표는 레이아웃 예시일 뿐 선행 관계를 자동으로 만들지 않는다.

저장 후 Content Browser에서 우클릭해 `Validate Assets`를 실행한다. 중복 NodeId, 없는 부모, 순환 참조, 잘못된 비용이 없어야 한다.

---

## 13. 3D 배/대포 프리뷰 붙이기

2D 기능 검증이 끝난 뒤 진행한다.

### 13.1 Render Target

1. Preview 폴더에서 `Materials & Textures > Render Target`을 만든다.
2. 이름은 `RT_ShipUpgradePreview`.
3. 크기는 먼저 `1024 x 1024`.
4. 필요하면 배경 UI와 어울리는 Clear Color를 지정한다.

### 13.2 프리뷰 Actor

`BP_UIShipPreview_Base`:

```text
SceneRoot
└─ StaticMeshComponent_Ship
```

- Mesh에 `/Game/New/Ship/Mesh/SM_TestShip` 또는 UI용 최적화 Mesh를 지정한다.
- Collision은 NoCollision.
- Replicates는 false.
- 물리 시뮬레이션은 끈다.
- Tick은 필요하지 않으면 끈다.

대포도 같은 방식으로 `BP_UICannonPreview_Base`를 만든다. 강화 외형 단계가 여러 개면 Actor Blueprint를 단계별 자식 클래스로 만들고 Mesh/Material만 바꾼다.

### 13.3 Preview Stage

`BP_ShipUpgradePreviewStage` 구성:

```text
SceneRoot
├─ SceneComponent_PreviewAnchor
├─ SceneCaptureComponent2D
├─ DirectionalLight 또는 PointLight
└─ 선택적 배경 Plane
```

SceneCapture의 Texture Target에 `RT_ShipUpgradePreview`를 지정한다.

변수:

```text
SpawnedPreviewActor : Actor Reference
CurrentPreviewClass : Actor Class Reference
```

함수 `SetPreviewActorClass(LoadedClass)`:

```text
IsValid(SpawnedPreviewActor) -> Destroy Actor
Spawn Actor From Class(LoadedClass, PreviewAnchor Transform)
Set SpawnedPreviewActor
Attach Actor To Component(PreviewAnchor, Snap To Target)
```

함수 `AddPreviewYaw(DeltaYaw)`:

```text
SpawnedPreviewActor.AddActorLocalRotation(0, DeltaYaw, 0)
```

프리뷰 Stage는 실제 전투 배를 사용하지 않는다. 레벨의 플레이 영역에서 충분히 떨어진 전용 위치에 한 개만 Spawn하거나 배치하고, 종료 시 정리한다.

### 13.4 Widget에 Render Target 표시

`WBP_ShipUpgradeScreen`의 `Image_MainShipPreview`에서 Brush Resource를 `RT_ShipUpgradePreview`로 지정한다.

Screen Construct 시 Preview Stage를 찾거나 Spawn하고 참조를 보관한다. 화면을 다시 열 때 Stage를 중복 Spawn하지 않는다.

선택 View의 `PreviewActorClass`는 Soft Class다.

```text
Async Load Class Asset(View.PreviewActorClass)
    -> PreviewStage.SetPreviewActorClass(LoadedClass)
```

### 13.5 활성화된 누적 외형 선택

`RefreshActiveVisuals`에서 모든 Node View를 순회한다.

```text
State == Active
ActivatedShipActorClass가 설정됨
VisualPriority가 현재 최고값보다 큼
    -> 현재 배 외형 후보 교체
```

대포도 `ActivatedCannonActorClass`로 같은 계산을 한다. 여러 노드가 외형을 제공할 때 가장 높은 `VisualPriority`가 이긴다.

Data Asset 예시:

```text
Hull_I:
    ActivatedShipActorClass = BP_UIShipPreview_Hull1
    VisualPriority = 100

Propulsion_I:
    ActivatedShipActorClass = BP_UIShipPreview_Engine1
    VisualPriority = 200
```

문자열 NodeId로 외형 클래스를 분기하지 않는다.

### 13.6 마우스로 배 회전

`Image_MainShipPreview`에서 다음을 처리한다.

```text
OnMouseButtonDown:
    Left Mouse면 bDraggingPreview = true
    Mouse Capture

OnMouseMove:
    bDraggingPreview이면
    DeltaYaw = CursorDelta.X * 0.25
    PreviewStage.AddPreviewYaw(DeltaYaw)

OnMouseButtonUp:
    bDraggingPreview = false
    Release Mouse Capture
```

회전은 Preview Actor에만 적용한다.

---

## 14. 단계별 PIE 검증

### 단계 A: 화면 수명 주기

1. Standalone PIE 실행.
2. `BP_WorkTable`에 상호작용.
3. 작업대 화면이 한 번만 생성되는지 확인.
4. 첫 탭이 배 강화인지 확인.
5. 마우스 커서가 보이고 카메라 Look이 잠기는지 확인.
6. ESC와 닫기 버튼 모두 화면을 닫는지 확인.
7. 닫은 직후 첫 클릭이 정상적으로 게임에 전달되는지 확인.

### 단계 B: 노드 그래프

1. 노드가 Data Asset의 GraphPosition에 나타나는지 확인.
2. 모든 부모-자식 연결선이 있는지 확인.
3. Locked/Available/Active 색과 자물쇠가 다른지 확인.
4. ScrollBox와 Zoom에서 선과 노드가 함께 움직이는지 확인.

### 단계 C: 상세 패널

1. 각 노드를 눌러 이름과 설명 확인.
2. 스탯 전후 값 확인.
3. 쿨다운 감소가 개선 색으로 표시되는지 확인.
4. 재료 보유량/필요량과 부족 색 확인.
5. 잠긴 노드에서 활성화 버튼이 비활성인지 확인.

### 단계 D: 활성화

1. 재료가 부족한 노드 요청이 거절되는지 확인.
2. 충분한 재료를 넣고 활성화.
3. 결과가 올 때까지 버튼이 Pending 상태인지 확인.
4. 성공 후 재료 수량이 정확히 감소하는지 확인.
5. 자식 노드가 즉시 `Locked -> Available`로 갱신되는지 확인.
6. 같은 노드를 연속 클릭해 중복 차감되지 않는지 확인.

### 단계 E: 저장과 실제 배

1. 노드 활성화 후 맵을 다시 연다.
2. Active 상태가 복원되는지 확인.
3. 플레이어가 배를 Possess한다.
4. 체력, 대포 공격력, 쿨다운, 포탄 속도, 추진, 선회 수치가 적용되는지 확인.

### 단계 F: 네트워크

Listen Server, 2 Players로 실행한다.

1. 각 플레이어가 자기 강화 상태와 재료만 보는지 확인.
2. 클라이언트의 작업대 상호작용이 해당 클라이언트 화면만 여는지 확인.
3. 클라이언트 활성화 결과가 서버 판정과 일치하는지 확인.
4. 다른 플레이어의 UI가 함께 갱신되지 않는지 확인.

---

## 15. 자주 막히는 지점

| 증상 | 확인할 것 |
| --- | --- |
| 작업대를 눌러도 화면이 없음 | 실제 GameMode가 `BP_BasePlayerController`를 쓰는지, Workspace Widget Class가 지정됐는지 확인 |
| 기존 스타포스 창이 열림 | `BP_WorkTable.Open Integrated Workspace` 확인 |
| 노드가 0개 | `GetLocalShipUpgradeComponent` 유효성, `DA_ShipUpgradeTree` 로드 확인 |
| 처음 열 때만 빈 화면 | DataReady만 기다리지 말고 컴포넌트 획득 직후 `RefreshUpgradeData`와 `RefreshAll` 호출 |
| 부모 활성화 후 자식이 그대로 | `OnUpgradeDataChanged`에서 전체 View 재조회 |
| 재료 수치가 안 바뀜 | View를 캐시한 채 Text만 쓰지 말고 `GetNodeView` 재호출 |
| 아이콘이 안 보임 | Soft Texture 비어 있음 또는 Async Load 완료 전 Brush 설정 여부 확인 |
| 선이 노드와 어긋남 | 노드 중심 좌표, GraphOriginOffset, Zoom Scale 중복 적용 확인 |
| 화면 닫은 뒤 카메라가 안 움직임 | Widget에서 임의 InputMode 변경 금지, Controller의 Close 함수만 사용 |
| 3D 모델이 검게 보임 | Preview Stage 조명, Capture Source, Render Target, Exposure 확인 |
| 여러 외형이 서로 덮음 | Active View 중 `VisualPriority` 최고값 하나만 선택 |

---

## 16. 첫 작업일 권장 범위

첫날에는 다음까지만 완료해도 된다.

1. `WBP_WorkspaceScreen` 생성과 Controller 지정
2. 작업대 상호작용으로 화면 열기/ESC 닫기
3. `WBP_ShipUpgradeScreen`에서 노드 6개 생성
4. 노드 클릭 시 이름, `FormattedText`, 재료 수량 표시
5. 활성화 버튼과 서버 결과 연결

이 다섯 가지가 안정적으로 동작한 뒤 연결선, 줌, 애니메이션, 3D 프리뷰 순서로 확장한다.
