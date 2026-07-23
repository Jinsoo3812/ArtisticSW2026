# 공용 작업대 및 배 강화 UI 에디터 Step-by-Step 가이드

> 기준: 2026-07-23 네이티브 UMG 및 모듈 분리 이후  
> 대상: Unreal Widget Blueprint를 처음 사용하는 작업자  
> 목표: 게임 로직은 C++, 화면 배치와 디자인만 Widget Blueprint에서 담당한다.

네이티브 WBP 부모는 `ShipUpgradeUI` Runtime 모듈, 강화 데이터와 규칙은 `WaterAndShip`, 작업대·PlayerController·인벤토리 연결은 `ClassFeature`에 있다.

## 0. WBP에서 하지 않을 것

다음은 이미 C++에 있으므로 WBP에서 다시 만들지 않는다.

- `Event Construct`에서 강화 컴포넌트 검색
- `Async Load Asset`
- `Async Load Class Asset`
- `For Each Loop`로 노드, 연결선, 능력치 행, 재료 행 생성
- `ApplyNodeView`
- 활성화 서버 호출
- 활성화 성공 후 재료 직접 차감
- 노드 상태 직접 계산
- Tick으로 프리뷰 회전
- 탭 버튼 클릭 연결

정상적으로 구성된 WBP는 Event Graph가 비어 있어도 다음 기능이 작동한다.

- 강화 컴포넌트 탐색과 초기화 재시도
- 데이터 이벤트 바인딩
- 노드와 연결선 생성
- 노드 선택과 상태 표현
- 아이콘과 프리뷰 Actor 비동기 로드
- 상세 패널과 재료/능력치 행 생성
- 활성화 가능 여부와 요청 중 상태
- 서버 활성화 요청과 결과 처리
- 현재 배 능력치 표시
- 그래프 줌
- 3D 프리뷰 드래그 회전
- 작업대 탭 전환과 닫기

Event Graph는 색 전환 애니메이션 같은 선택적 연출에만 사용한다.

## 1. 에디터 준비

C++ 모듈 위치가 바뀌었으므로 에디터가 열려 있다면 완전히 종료한 뒤 다시 실행한다. Live Coding만으로 새 부모 클래스 목록이 안정적으로 갱신되지 않을 수 있다.

Content Browser에서 다음 폴더를 만든다.

```text
/Game/Blueprints/02_UI/UI_WorkTable
├─ UI_ShipUpgrade
│  ├─ Nodes
│  ├─ Details
│  ├─ Materials
│  └─ Preview
└─ UI_Workspace
```

기존에 만든 `WBP_ShipUpgradeNode`가 있다면 삭제할 필요는 없다.

1. 해당 WBP를 연다.
2. `File > Reparent Blueprint`를 선택한다.
3. 부모를 `ShipUpgradeNodeWidget`으로 변경한다.
4. 기존 `ApplyNodeView`, `Async Load Asset` 로직을 삭제한다.
5. 이 문서에서 설명하는 위젯 이름만 정확히 맞춘다.

## 2. WBP 부모 클래스 선택 방법

일반 WBP를 만들 때마다 다음 순서를 사용한다.

1. Content Browser의 원하는 폴더에서 우클릭한다.
2. `User Interface > Widget Blueprint`를 선택한다.
3. 부모 선택 화면에서 `All Classes`를 선택한다.
4. 원하는 C++ 부모를 검색한다.
5. 생성 후 아래 표의 에셋 이름으로 변경한다.

| 에셋 이름 | 검색할 부모 |
|---|---|
| `WBP_ShipUpgradeNode` | `ShipUpgradeNodeWidget` |
| `WBP_ShipUpgradeConnection` | `ShipUpgradeConnectionWidget` |
| `WBP_ShipUpgradeStatChangeRow` | `ShipUpgradeStatChangeRowWidget` |
| `WBP_ShipUpgradeMaterialRow` | `ShipUpgradeMaterialRowWidget` |
| `WBP_ShipUpgradeDetails` | `ShipUpgradeDetailsWidget` |
| `WBP_ShipUpgradeGraph` | `ShipUpgradeGraphWidget` |
| `WBP_ShipUpgradeScreen` | `ShipUpgradeScreenWidget` |
| `WBP_WorkspaceScreen` | `ShipUpgradeWorkspaceWidget` |

`BP_ShipUpgradePreviewStage`만 Widget Blueprint가 아니라 일반 Actor Blueprint다.

위젯 이름은 대소문자까지 정확해야 한다. C++과 연결되는 위젯은 Details의 `Is Variable`도 켜는 것을 권장한다.

## 3. 가장 작은 위젯부터 만들기

### 3.1 `WBP_ShipUpgradeConnection`

부모: `ShipUpgradeConnectionWidget`

Designer에서 다음처럼 만든다.

```text
Image Image_Line
```

Image 하나를 루트로 사용해도 된다.

설정:

- 이름: `Image_Line`
- Anchors: 전체 채우기
- Visibility: `Hit Test Invisible` 권장
- Brush Draw As: `Image`

상단 `Class Defaults`를 누르고 `Ship Upgrade | Style`에서 다음 값을 지정한다.

- `Solid Line Texture`: 활성 또는 해금 연결선
- `Dashed Line Texture`: 아직 잠긴 연결선
- `Solid Line Color`
- `Dashed Line Color`

C++가 두 노드 사이 길이와 회전 각도를 자동 계산한다.

Event Graph는 비워 둔다.

### 3.2 `WBP_ShipUpgradeStatChangeRow`

부모: `ShipUpgradeStatChangeRowWidget`

구조:

```text
Border
└─ Text Text_Change
```

`Text_Change` 설정:

- `Is Variable`: 켬
- Auto Wrap Text: 필요하면 켬
- Justification: Left

Class Defaults:

- `Improvement Color`: 좋은 변화 색
- `Worsening Color`: 나쁜 변화 색

C++가 다음 형태의 텍스트를 넣는다.

```text
배 체력 100 → 120
발사 대기시간 3.0초 → 2.5초
```

Event Graph는 비워 둔다.

### 3.3 `WBP_ShipUpgradeMaterialRow`

부모: `ShipUpgradeMaterialRowWidget`

구조:

```text
Border
└─ HorizontalBox
   ├─ Image Image_ItemIcon
   ├─ Text  Text_ItemName
   ├─ Spacer
   └─ Text  Text_Count
```

정확한 이름:

- `Image_ItemIcon`
- `Text_ItemName`
- `Text_Count`

권장 설정:

- 아이콘 크기: `40 × 40`
- 아이템 이름 Slot Size: Fill
- Count Slot Size: Auto
- `Text_Count`: 오른쪽 정렬

Class Defaults:

- `Enough Color`: 흰색 또는 청록색
- `Insufficient Color`: 빨간색

C++가 아이콘을 비동기 로드하고 다음처럼 표시한다.

```text
나무 판자        12 / 5
철판              1 / 3
```

Event Graph는 비워 둔다.

## 4. 강화 노드 만들기

### 4.1 `WBP_ShipUpgradeNode`

부모: `ShipUpgradeNodeWidget`

권장 크기는 `160 × 130`이다. 실제 크기는 Graph의 Class Defaults에서 지정된다.

구조:

```text
Button Button_Node
└─ Overlay
   ├─ Border Border_StateGlow
   ├─ Image  Image_Icon
   ├─ Image  Image_Lock
   ├─ Image  Image_Check
   └─ Text   Text_Name
```

`Button_Node`는 반드시 있어야 한다.

배치 순서:

1. 루트에 Button을 추가한다.
2. 이름을 `Button_Node`로 변경한다.
3. Button 안에 Overlay를 추가한다.
4. Overlay에 상태 Glow용 Border를 추가한다.
5. 아이콘 Image를 추가한다.
6. 자물쇠 Image를 추가한다.
7. 완료 체크 Image를 추가한다.
8. 아래쪽에 이름 Text를 추가한다.

추천 위치:

- `Border_StateGlow`: 전체
- `Image_Icon`: 중앙
- `Image_Lock`: 오른쪽 위
- `Image_Check`: 오른쪽 위 또는 왼쪽 위
- `Text_Name`: 아래쪽 중앙

`Image_Lock`, `Image_Check`는 Designer에서 보이게 두어도 괜찮다. 실행하면 C++가 상태에 따라 자동으로 숨긴다.

Class Defaults의 `Ship Upgrade | Style`:

- `Locked Icon Opacity`
- `Available Icon Opacity`
- `Locked Glow Color`
- `Available Glow Color`
- `Active Glow Color`
- `Selected Glow Color`
- `Hover Scale`
- `Selected Scale`

Event Graph는 비워 둔다.

클릭, Hover, 선택 확대, 아이콘 로드, 자물쇠와 완료 표시는 전부 C++가 처리한다.

## 5. 상세 패널 만들기

### 5.1 `WBP_ShipUpgradeDetails`

부모: `ShipUpgradeDetailsWidget`

다음 구조를 권장한다.

```text
Border
└─ ScrollBox
   └─ VerticalBox
      ├─ Text Text_NodeName
      ├─ Text Text_Description
      ├─ Overlay
      │  ├─ Image Image_2DPreview
      │  └─ Border Panel_3DPreview
      ├─ Text "능력치 변화"
      ├─ VerticalBox VerticalBox_StatChanges
      ├─ Text "필요 재료"
      ├─ VerticalBox VerticalBox_MaterialCosts
      ├─ Text Text_UnavailableReason
      ├─ Button Button_Activate
      │  └─ Text Text_Activate
      └─ Throbber Throbber_Requesting
```

필수:

- `Button_Activate`

권장 선택 위젯:

- `Text_NodeName`
- `Text_Description`
- `Image_2DPreview`
- `Panel_3DPreview`
- `VerticalBox_StatChanges`
- `VerticalBox_MaterialCosts`
- `Text_UnavailableReason`
- `Text_Activate`
- `Throbber_Requesting`

`Panel_3DPreview`는 Border, Overlay, SizeBox 중 어떤 위젯이어도 된다. 이름만 정확하면 된다.

#### Class Defaults 연결

상단 `Class Defaults`에서:

- `Stat Change Row Class` = `WBP_ShipUpgradeStatChangeRow`
- `Material Row Class` = `WBP_ShipUpgradeMaterialRow`

이 연결을 빼먹으면 상세 화면은 열리지만 능력치와 재료 행이 표시되지 않는다.

Event Graph는 비워 둔다.

## 6. 노드 그래프 만들기

### 6.1 `WBP_ShipUpgradeGraph`

부모: `ShipUpgradeGraphWidget`

구조는 매우 단순하다.

```text
Canvas Panel CanvasPanel_Graph
```

Canvas 하나를 루트로 사용한다.

설정:

- 이름: `CanvasPanel_Graph`
- Anchors: 전체
- Offsets: 모두 0
- Visibility: Self Hit Test Invisible 권장

Class Defaults:

- `Node Widget Class` = `WBP_ShipUpgradeNode`
- `Connection Widget Class` = `WBP_ShipUpgradeConnection`
- `Node Widget Size` = `(160, 130)`
- `Graph Origin Offset` = `(300, 150)`
- `Connection Thickness` = `4`

C++가 `DA_ShipUpgradeTree`의 다음 데이터를 사용한다.

- `GraphPosition`
- `PrerequisiteNodeIds`
- `State`

따라서 노드를 하나 추가하더라도 Graph Event Graph를 수정할 필요가 없다.

## 7. 3D 프리뷰용 Render Target 만들기

`UI_ShipUpgrade/Preview` 폴더에서:

1. 우클릭한다.
2. `Materials & Textures > Render Target`을 선택한다.
3. 이름을 `RT_ShipUpgradePreview`로 지정한다.
4. 해상도를 우선 `1024 × 1024`로 설정한다.

성능이 부족하면 나중에 `512 × 512`로 낮출 수 있다.

## 8. 프리뷰 Stage 만들기

### 8.1 `BP_ShipUpgradePreviewStage`

이것은 WBP가 아니다.

1. Preview 폴더에서 우클릭한다.
2. `Blueprint Class`를 선택한다.
3. `All Classes`를 선택한다.
4. `ShipUpgradePreviewStage`를 검색한다.
5. 이름을 `BP_ShipUpgradePreviewStage`로 지정한다.

부모로부터 다음 컴포넌트가 보인다.

```text
SceneRoot
├─ PreviewAnchor
└─ SceneCapture
```

#### SceneCapture 설정

`SceneCapture`를 선택한 뒤:

- `Texture Target` = `RT_ShipUpgradePreview`
- Capture Every Frame = 켬
- Capture on Movement = 켬
- Projection Type = Perspective
- FOV Angle = 우선 35~50
- Capture Source = `Final Color (LDR) in RGB`부터 시작

카메라 위치 예시:

```text
Location: X=-800, Y=0, Z=250
Rotation: 배 원점을 바라보도록 설정
```

정확한 좌표는 배 Mesh의 정면 방향과 크기에 따라 달라진다.

#### 조명 추가

Components에서 다음을 추가한다.

- Directional Light
- Sky Light
- 필요하면 Point Light
- 배경용 Plane 또는 Cyclorama Mesh

Event Graph는 비워 둔다.

Actor 생성, 제거, Soft Class 로드, 회전은 C++가 처리한다.

## 9. UI 전용 배와 대포 Actor 만들기

일반 Actor Blueprint로 다음을 만든다.

```text
BP_UIShipPreview_Base
BP_UICannonPreview_Base
```

루트에 Static Mesh 또는 Skeletal Mesh만 추가한다.

설정:

- Replicates: 끔
- Collision: No Collision
- Simulate Physics: 끔
- Tick: 끔
- 이동, 공격, AI 로직 없음

강화 외형별 자식 Actor를 만든다.

```text
BP_UIShipPreview_Default
BP_UIShipPreview_Hull1
BP_UIShipPreview_Cannon1
BP_UICannonPreview_Default
BP_UICannonPreview_Cannon1
```

WBP 로직을 완전히 없애려면 배경 프리뷰용 배 Actor에 해당 단계의 대포 Mesh까지 포함하는 것을 권장한다.

예를 들어 대포 강화 노드에도 다음을 지정한다.

```text
ActivatedShipActorClass = BP_UIShipPreview_Cannon1
```

그러면 별도의 BP 조합 로직 없이 강화된 대포가 장착된 배 전체 프리뷰로 바뀐다.

`ActivatedCannonActorClass`만 별도 Actor로 조립하는 기능은 현재 단일 Preview Stage에서 자동 합성하지 않는다. 배와 대포가 조합된 프리뷰 Actor 변형을 사용하는 것이 가장 단순하고 WBP 로직이 필요 없다.

## 10. 배 강화 메인 화면 만들기

### 10.1 `WBP_ShipUpgradeScreen`

부모: `ShipUpgradeScreenWidget`

추천 구조:

```text
CanvasPanel
├─ Image Image_MainShipPreview
├─ Overlay GraphArea
│  └─ ScrollBox
│     └─ SizeBox SizeBox_GraphExtent
│        └─ WBP_ShipUpgradeGraph GraphWidget
├─ WBP_ShipUpgradeDetails DetailsWidget
├─ VerticalBox CurrentStats
│  ├─ HorizontalBox
│  │  ├─ Text "체력"
│  │  └─ Text Text_CurrentHealth
│  ├─ HorizontalBox
│  │  ├─ Text "대포 공격력"
│  │  └─ Text Text_CurrentCannonDamage
│  ├─ HorizontalBox
│  │  ├─ Text "발사 대기시간"
│  │  └─ Text Text_CurrentCooldown
│  ├─ HorizontalBox
│  │  ├─ Text "포탄 속도"
│  │  └─ Text Text_CurrentCannonballSpeed
│  ├─ HorizontalBox
│  │  ├─ Text "추진력"
│  │  └─ Text Text_CurrentPropulsion
│  └─ HorizontalBox
│     ├─ Text "선회력"
│     └─ Text Text_CurrentTurn
└─ Text Text_ResultMessage
```

필수:

- `GraphWidget`
- `DetailsWidget`

이름뿐 아니라 타입도 맞아야 한다.

- `GraphWidget`에는 반드시 `WBP_ShipUpgradeGraph`를 배치한다.
- `DetailsWidget`에는 반드시 `WBP_ShipUpgradeDetails`를 배치한다.

#### Graph ScrollBox

ScrollBox의 Details에서:

- `Consume Mouse Wheel` = `Never`

로 설정한다. 그래야 C++의 휠 줌이 작동한다.

그래프가 가로와 세로로 모두 크다면 ScrollBox를 두 개 중첩할 수 있다.

```text
Horizontal ScrollBox
└─ Vertical ScrollBox
   └─ SizeBox SizeBox_GraphExtent
      └─ WBP_ShipUpgradeGraph GraphWidget
```

두 ScrollBox 모두 `Consume Mouse Wheel = Never`로 설정하고 스크롤바를 직접 드래그해 이동한다.

#### Class Defaults

`WBP_ShipUpgradeScreen`의 Class Defaults:

- `Preview Stage Class` = `BP_ShipUpgradePreviewStage`
- `Default Ship Preview Actor Class` = `BP_UIShipPreview_Default`
- `Base Graph Extent` = `(2400, 1600)`
- `Zoom Min` = `0.6`
- `Zoom Max` = `1.8`
- `Zoom Step` = `0.1`
- `Preview Rotation Sensitivity` = `0.25`

`Image_MainShipPreview`의 Brush에는 Render Target을 직접 지정하지 않아도 된다. C++가 Stage의 Render Target을 자동 연결한다.

Event Graph는 비워 둔다.

## 11. 공용 작업대 화면 만들기

### 11.1 `WBP_WorkspaceScreen`

부모: `ShipUpgradeWorkspaceWidget`

추천 전체 구조:

```text
CanvasPanel
├─ Border Background
├─ HorizontalBox Main
│  ├─ SizeBox LeftMenu
│  │  └─ VerticalBox
│  │     ├─ Text "Work Space"
│  │     ├─ Button Button_ShipUpgrade
│  │     │  └─ Text "배 강화"
│  │     ├─ Button Button_ItemCrafting
│  │     │  └─ Text "아이템 제작"
│  │     └─ Button Button_SkillUpgrade
│  │        └─ Text "스킬 강화"
│  └─ Overlay Content
│     └─ WidgetSwitcher WidgetSwitcher_Content
│        ├─ WBP_ShipUpgradeScreen
│        ├─ 아이템 제작 화면 또는 Placeholder
│        └─ 스킬 강화 화면 또는 Placeholder
└─ Button Button_Close
   └─ Text "X"
```

필수:

- `WidgetSwitcher_Content`

선택적이지만 실제 UI에서는 권장:

- `Button_ShipUpgrade`
- `Button_ItemCrafting`
- `Button_SkillUpgrade`
- `Button_Close`

### 11.2 Switcher 인덱스

Hierarchy에서 `WidgetSwitcher_Content`의 자식 순서를 확인한다.

```text
index 0 = WBP_ShipUpgradeScreen
index 1 = 아이템 제작
index 2 = 스킬 강화
```

아직 아이템 제작 또는 스킬 강화 화면이 없다면 다음과 같은 간단한 Placeholder WBP를 넣어도 된다.

```text
Border
└─ Text "준비 중"
```

Class Defaults:

- `Ship Upgrade Tab Index` = `0`
- `Item Crafting Tab Index` = `1`
- `Skill Upgrade Tab Index` = `2`

버튼의 `OnClicked`를 Event Graph에서 연결하지 않는다. C++가 자동 연결한다.

## 12. PlayerController에 Workspace 지정

`BP_BasePlayerController`를 연다.

일반적인 경로:

```text
/Game/Blueprints/Player/BP_BasePlayerController
```

1. `Class Defaults`를 선택한다.
2. 검색창에 `Ship Upgrade Workspace`를 입력한다.
3. `Ship Upgrade Workspace Widget Class`를 찾는다.
4. `WBP_WorkspaceScreen`을 지정한다.
5. Compile한다.
6. Save한다.

실제 맵의 GameMode가 이 PlayerController를 사용하는지도 확인한다.

GameMode의 다음 값이 `BP_BasePlayerController` 또는 그 자식이어야 한다.

```text
Player Controller Class
```

## 13. 작업대 설정

`BP_WorkTable`을 연다.

일반적인 경로:

```text
/Game/Blueprints/Interactable_Object/BP_WorkTable
```

Class Defaults 또는 배치된 작업대 인스턴스에서 다음 값을 켠다.

```text
Open Integrated Workspace = true
```

이 값이 꺼져 있으면 통합 Workspace 대신 기존 StarForce 팝업 경로가 열린다.

## 14. 강화 데이터 에셋 설정

다음 에셋을 연다.

```text
/Game/New/Ship/Upgrade/DA_ShipUpgradeTree
```

기존 게임 기능을 유지하려면 다음 값은 함부로 변경하지 않는다.

- `NodeId`
- `PrerequisiteNodeIds`
- `ActivationCosts`
- `Modifiers`

UI 작업에서는 다음 필드만 채우는 것이 안전하다.

- `DisplayName`
- `Description`
- `Icon`
- `GraphPosition`
- `PreviewType`
- `PreviewActorClass`
- `ActivatedShipActorClass`
- `ActivatedCannonActorClass`
- `VisualPriority`

### 14.1 단순 능력치 노드

```text
PreviewType = Icon2D
Icon = 해당 아이콘
PreviewActorClass = None
```

### 14.2 대포 강화 노드

```text
PreviewType = Cannon3D
PreviewActorClass = BP_UICannonPreview_Cannon1
ActivatedShipActorClass = BP_UIShipPreview_Cannon1
ActivatedCannonActorClass = BP_UICannonPreview_Cannon1
VisualPriority = 10
```

### 14.3 배 강화 노드

```text
PreviewType = Ship3D
PreviewActorClass = BP_UIShipPreview_Hull1
ActivatedShipActorClass = BP_UIShipPreview_Hull1
VisualPriority = 20
```

`VisualPriority`가 높은 활성 노드의 배경 프리뷰가 우선된다.

## 15. 권장 실제 제작 순서

다음 순서대로 진행하면 누락을 찾기 쉽다.

1. 에디터를 완전히 재시작한다.
2. `WBP_ShipUpgradeConnection`을 만든다.
3. `WBP_ShipUpgradeStatChangeRow`를 만든다.
4. `WBP_ShipUpgradeMaterialRow`를 만든다.
5. `WBP_ShipUpgradeNode`를 만든다.
6. `WBP_ShipUpgradeDetails`를 만들고 두 Row Class를 연결한다.
7. `WBP_ShipUpgradeGraph`를 만들고 Node/Connection Class를 연결한다.
8. `RT_ShipUpgradePreview`를 만든다.
9. `BP_ShipUpgradePreviewStage`를 만들고 Render Target과 조명을 지정한다.
10. UI 전용 배와 대포 Actor를 만든다.
11. `WBP_ShipUpgradeScreen`을 만들고 Graph/Details를 배치한다.
12. Screen의 Preview Stage와 기본 배 Actor Class를 지정한다.
13. `WBP_WorkspaceScreen`을 만들고 Switcher index 0에 ShipUpgradeScreen을 넣는다.
14. PlayerController에 Workspace Widget Class를 지정한다.
15. WorkTable의 `Open Integrated Workspace`를 켠다.
16. 강화 Data Asset의 UI와 프리뷰 필드를 채운다.
17. Standalone PIE에서 아래 체크리스트를 확인한다.

## 16. 첫 실행 테스트

처음에는 Standalone PIE로 확인한다.

### 16.1 화면 열기

1. PIE를 시작한다.
2. 작업대에 접근한다.
3. 상호작용한다.
4. `WBP_WorkspaceScreen`이 열리는지 확인한다.
5. 첫 탭이 배 강화인지 확인한다.
6. 닫기 버튼과 ESC를 확인한다.

### 16.2 그래프

- 노드가 데이터 위치에 표시되는가
- 연결선이 표시되는가
- 잠긴 노드에 자물쇠가 보이는가
- 활성 노드에 완료 표시가 보이는가
- 노드를 클릭하면 선택 확대가 되는가
- 마우스 휠로 그래프가 확대 또는 축소되는가

### 16.3 상세 패널

- 이름과 설명이 표시되는가
- 능력치 변화가 표시되는가
- 재료 보유량과 필요량이 표시되는가
- 재료 부족 시 버튼이 비활성화되는가
- 잠김 사유가 표시되는가

### 16.4 활성화

- 클릭 직후 Throbber가 보이는가
- 연속 클릭이 차단되는가
- 성공 또는 실패 메시지가 나오는가
- 성공 후 재료와 능력치가 갱신되는가
- 자식 노드가 `Locked → Available`로 바뀌는가

### 16.5 3D 프리뷰

- 기본 배가 보이는가
- 배 또는 대포 노드 선택 시 해당 프리뷰가 보이는가
- 프리뷰 위에서 왼쪽 마우스 드래그 시 회전하는가
- 활성화 후 배경 배 모델이 바뀌는가

### 16.6 재오픈과 네트워크

- 화면을 닫았다 다시 열어도 상태가 유지되는가
- 저장 후 재접속해도 활성 노드가 유지되는가
- Listen Server에서 각 클라이언트가 자기 화면만 여는가
- 한 클라이언트의 활성화가 서버 검증 후 올바르게 복제되는가

## 17. 문제가 생겼을 때

| 증상 | 확인할 것 |
|---|---|
| 부모 클래스가 검색되지 않음 | 에디터 완전 재시작, Editor Target 빌드 성공 여부 |
| WBP 컴파일 시 BindWidget 오류 | 필수 이름, 타입, 대소문자, `Is Variable` |
| 노드가 없음 | Graph의 Node Widget Class |
| 선이 없음 | Connection Class, `Image_Line`, Texture |
| 상세 행이 없음 | Details의 두 Row Class |
| 배 프리뷰가 검정색 | SceneCapture Texture Target과 조명 |
| 배가 안 보임 | 프리뷰 Actor Mesh 위치와 카메라 방향 |
| 줌이 안 됨 | ScrollBox `Consume Mouse Wheel = Never` |
| 작업대에서 옛 UI가 열림 | `Open Integrated Workspace = true` |
| 화면이 안 열림 | Controller의 Workspace Widget Class와 실제 GameMode |
| 재료가 있는데 활성화 불가 | UI에서 수량을 고치지 말고 Inventory와 노드 조건 확인 |
| 기존 WBP 부모가 Invalid | Core Redirect 적용 후 에디터 재시작, 필요하면 새 부모로 Reparent |

## 18. 완료 기준

정상 구성된 WBP의 Event Graph에는 게임 로직 노드가 없어도 된다.

WBP에서 직접 하는 작업:

- 필수 이름의 Designer 위젯 배치
- 폰트, 색, 브러시, 여백 조정
- Class Defaults의 자식 WBP, Texture, Actor Class 지정
- Render Target, SceneCapture, 조명 구성
- 선택적 스타일 애니메이션

WBP에서 다시 만들지 않는 작업:

- 컴포넌트 검색
- 데이터 갱신 이벤트 바인딩
- Soft Asset 비동기 로드
- 노드와 연결선 생성
- 능력치와 재료 행 생성
- 활성화 조건 판정
- 서버 활성화 요청
- 재료 차감
- 저장과 복제
- 프리뷰 Actor 교체와 회전

이 기준을 지키면 WBP는 화면 디자인과 에셋 연결만 담당하고, 기능 변경은 C++와 강화 데이터 계층에서 관리할 수 있다.
