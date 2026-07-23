# 배 강화 UI 에디터 Step-by-Step 가이드

> 기준: 2026-07-23 네이티브 UMG 리팩터링 이후  
> 목표: 로직은 C++, 배치와 아트는 Widget Blueprint에서 담당한다.

네이티브 WBP 부모는 `ShipUpgradeUI` Runtime 모듈, 강화 데이터와 규칙은 `WaterAndShip`, 작업대·PlayerController·인벤토리 연결은 `ClassFeature`에 있다.

## 0. 가장 중요한 원칙

이번 리팩터링은 UI 구현 위치만 Blueprint에서 C++로 옮긴 것이다. 아래 기능은 바꾸지 않는다.

- 노드 상태는 `Locked / Available / Active` 그대로다.
- 선행 노드, 재료 비용, 능력치 변화, 활성화 조건은 기존 데이터를 그대로 사용한다.
- 활성화 요청은 기존 서버 권한 경로를 그대로 탄다.
- 요청 중 표시, 성공/실패 결과, 재료 갱신, 저장, 복제, 실제 배 능력치 적용은 그대로다.
- 작업대와 상호작용하면 통합 작업 화면의 배 강화 탭이 먼저 열린다.
- 2D 아이콘 노드, 3D 대포 노드, 3D 배 노드 구분도 그대로다.
- 강화된 배/대포 외형은 `VisualPriority`가 가장 높은 활성 노드를 기준으로 선택한다.

따라서 아래 가이드를 따라 WBP를 다시 구성해도 게임 규칙이나 데이터는 수정하면 안 된다.

## 1. C++와 WBP의 책임

| C++가 자동으로 처리하는 것 | WBP에서 손으로 하는 것 |
|---|---|
| 강화 컴포넌트 탐색과 초기화 재시도 | 패널, 버튼, 이미지, 텍스트 배치 |
| 데이터/능력치/활성화 결과 델리게이트 바인딩 | 폰트, 색, 브러시, 여백, 애니메이션 |
| 노드와 연결선 동적 생성 및 배치 | 지정된 이름으로 위젯 생성 |
| 노드 선택, 상세 패널 갱신 | 자식 WBP Class 지정 |
| 아이콘과 Actor Soft Reference 비동기 로드 | Render Target 및 프리뷰 조명 구성 |
| 재료/능력치 행 생성 | 필요할 때만 선택적 스타일 이벤트 구현 |
| 활성화 가능 여부와 요청 중 상태 | 데이터 에셋에 실제 아이콘/프리뷰 클래스 지정 |
| 현재 배 능력치 표시 | 최종 해상도에 맞춘 시각 조정 |
| 그래프 줌, 프리뷰 드래그 회전 |  |
| 작업 화면 탭 전환과 닫기 |  |

`Async Load Asset`, `For Each Loop`, `Create Widget`, `Bind Event`, `ApplyNodeView` 같은 핵심 Event Graph는 이제 만들지 않는다.

## 2. 에디터를 다시 연다

C++ 부모 클래스가 추가되었으므로 에디터가 열려 있었다면 종료한 뒤 다시 연다. Live Coding만으로 새 `UCLASS`가 목록에 안정적으로 나타나지 않을 수 있다.

콘텐츠 폴더 권장 위치:

```text
/Game/Blueprints/02_UI/UI_WorkTable/UI_ShipUpgrade
├─ Nodes
├─ Details
├─ Preview
└─ Materials
```

모든 Widget Blueprint는 `UserWidget`으로 먼저 만든 뒤 Reparent하는 방식보다, 생성 창의 `All Classes`에서 아래 C++ 부모를 직접 고르는 편이 안전하다.

## 3. 만들 Blueprint 목록

| 에셋 | 부모 클래스 |
|---|---|
| `WBP_ShipUpgradeNode` | `ShipUpgradeNodeWidget` |
| `WBP_ShipUpgradeConnection` | `ShipUpgradeConnectionWidget` |
| `WBP_ShipUpgradeStatChangeRow` | `ShipUpgradeStatChangeRowWidget` |
| `WBP_ShipUpgradeMaterialRow` | `ShipUpgradeMaterialRowWidget` |
| `WBP_ShipUpgradeDetails` | `ShipUpgradeDetailsWidget` |
| `WBP_ShipUpgradeGraph` | `ShipUpgradeGraphWidget` |
| `WBP_ShipUpgradeScreen` | `ShipUpgradeScreenWidget` |
| `WBP_WorkspaceScreen` | `ShipUpgradeWorkspaceWidget` |
| `BP_ShipUpgradePreviewStage` | `ShipUpgradePreviewStage` Actor |

부모를 잘못 만든 기존 WBP가 있다면:

1. WBP를 연다.
2. `File > Reparent Blueprint`를 누른다.
3. 위 표의 부모를 지정한다.
4. 아래의 필수 위젯 이름을 정확히 맞춘다.
5. Compile한다.

## 4. `WBP_ShipUpgradeNode`

### 4.1 필수 배치

루트 크기는 우선 `160 x 130`으로 잡는다.

```text
Button  Button_Node                 [필수]
└─ Overlay
   ├─ Border Border_StateGlow       [선택]
   ├─ Image  Image_Icon             [선택]
   ├─ Image  Image_Lock             [선택]
   ├─ Image  Image_Check            [선택]
   └─ Text   Text_Name              [선택]
```

이름은 대소문자까지 정확해야 한다. `Button_Node`만 컴파일 필수이며 나머지는 디자인에 필요할 때 배치한다.

### 4.2 Class Defaults

`Ship Upgrade | Style`에서 다음을 조정할 수 있다.

- 잠김/활성 가능 아이콘 투명도
- 잠김/활성 가능/활성/선택 Glow 색
- Hover/Selected 배율

### 4.3 Event Graph

비워 둬도 된다. C++가 다음을 모두 수행한다.

- `FShipUpgradeNodeView` 보관
- 이름 표시
- 아이콘 비동기 로드
- Lock/Check 표시
- 상태 색과 투명도
- Hover/선택 크기
- 클릭 전달

더 복잡한 애니메이션이 필요할 때만 `BP On Visual State Applied`, `BP On Hover Changed`를 구현한다. 이 이벤트에는 게임 규칙을 넣지 않는다.

## 5. `WBP_ShipUpgradeConnection`

### 5.1 배치

```text
Image Image_Line                    [선택]
```

`Image_Line`을 배치하면 C++가 실선/점선 Texture와 Tint를 자동 적용한다. 아무것도 배치하지 않으면 연결선이 보이지 않으므로 실제 UI에서는 추가하는 것을 권장한다.

### 5.2 Class Defaults

- `Solid Line Texture`
- `Dashed Line Texture`
- `Solid Line Color`
- `Dashed Line Color`

투명 배경의 가로 선 Texture를 사용한다. C++가 두 노드 사이 길이와 각도를 계산한다.

Event Graph는 필요 없다. 특별한 머티리얼 전환만 `BP On Connection Style Applied`에서 표현한다.

## 6. 행 위젯 두 개

### 6.1 `WBP_ShipUpgradeStatChangeRow`

```text
Text Text_Change                    [선택]
```

C++가 능력치 이름과 `이전 → 이후` 값을 만들고, 이로운 변화와 불리한 변화의 색을 구분한다. `Improvement Color`, `Worsening Color`만 Class Defaults에서 조정한다.

### 6.2 `WBP_ShipUpgradeMaterialRow`

```text
HorizontalBox
├─ Image Image_ItemIcon             [선택]
├─ Text  Text_ItemName              [선택]
└─ Text  Text_Count                 [선택]
```

C++가 아이템 아이콘을 비동기 로드하고 `보유량 / 필요량`을 표시한다. 부족 여부에 따른 색은 `Enough Color`, `Insufficient Color`에서 조정한다.

두 WBP 모두 Event Graph는 비워 둔다.

## 7. `WBP_ShipUpgradeDetails`

### 7.1 배치

```text
VerticalBox
├─ Text        Text_NodeName                  [선택]
├─ Text        Text_Description               [선택]
├─ Overlay
│  ├─ Image    Image_2DPreview                [선택]
│  └─ Panel    Panel_3DPreview                [선택]
├─ VerticalBox VerticalBox_StatChanges        [선택]
├─ VerticalBox VerticalBox_MaterialCosts      [선택]
├─ Text        Text_UnavailableReason         [선택]
├─ Button      Button_Activate                [필수]
│  └─ Text     Text_Activate                  [선택]
└─ Throbber    Throbber_Requesting             [선택]
```

`Panel_3DPreview`는 `Overlay`, `Border`, `SizeBox` 등 원하는 단일 패널 위젯을 사용할 수 있다. 이름만 정확하면 된다.

### 7.2 Class Defaults

- `Stat Change Row Class` = `WBP_ShipUpgradeStatChangeRow`
- `Material Row Class` = `WBP_ShipUpgradeMaterialRow`

### 7.3 자동 처리

C++가 선택된 노드를 받으면 다음을 갱신한다.

- 제목과 설명
- 2D/3D 프리뷰 영역
- 능력치 변화 행
- 재료 행
- 잠김 사유
- 활성화 버튼 활성/비활성
- 요청 중 Throbber와 중복 클릭 방지
- 활성화 요청 전달

Event Graph는 필요 없다. `BP On Details State Applied`는 선택적 애니메이션 전용이다.

## 8. `WBP_ShipUpgradeGraph`

### 8.1 필수 배치

```text
Canvas Panel CanvasPanel_Graph      [필수]
```

Canvas는 콘텐츠 전체를 채우게 한다.

### 8.2 Class Defaults

- `Node Widget Class` = `WBP_ShipUpgradeNode`
- `Connection Widget Class` = `WBP_ShipUpgradeConnection`
- `Node Widget Size` = 우선 `(160, 130)`
- `Graph Origin Offset` = 우선 `(300, 150)`
- `Connection Thickness` = 우선 `4`

C++가 데이터의 `GraphPosition`과 `PrerequisiteNodeIds`를 읽어 노드와 선을 생성한다. 노드 수가 늘어나도 Event Graph를 수정하지 않는다.

## 9. `BP_ShipUpgradePreviewStage`

부모는 반드시 Actor `ShipUpgradePreviewStage`다. `Actor`를 직접 부모로 만든 뒤 로직을 복사하지 않는다.

### 9.1 Render Target

1. `Preview` 폴더에서 `Materials & Textures > Render Target`을 만든다.
2. 이름을 `RT_ShipUpgradePreview`로 한다.
3. 해상도는 우선 `1024 x 1024`로 둔다.
4. 투명 배경이 필요하면 Render Target과 Scene Capture의 Alpha 설정을 프로젝트 표현에 맞춘다.

### 9.2 Preview Stage 구성

부모로부터 다음 컴포넌트를 상속받는다.

```text
SceneRoot
├─ PreviewAnchor
└─ SceneCapture
```

1. `SceneCapture`의 `Texture Target`에 `RT_ShipUpgradePreview`를 지정한다.
2. 카메라가 `PreviewAnchor` 원점을 바라보도록 위치와 회전을 잡는다.
3. Directional Light, Sky Light 또는 Point Light를 자식 BP에 추가한다.
4. 필요하면 단색 배경 Plane이나 Cyclorama Mesh를 추가한다.
5. Tick 로직, Spawn Actor, Async Load Class Asset 노드는 만들지 않는다.

C++가 Soft Class를 비동기 로드하고 이전 프리뷰 Actor를 제거한 뒤 새 Actor를 `PreviewAnchor`에 붙인다. 마우스 드래그 회전도 C++가 처리한다.

### 9.3 프리뷰용 Actor

실제 플레이 배/대포 클래스를 그대로 띄우면 이동·충돌·게임플레이 컴포넌트가 작동할 수 있다. UI 전용 Actor Blueprint를 권장한다.

- `BP_UIShipPreview_Base`
- `BP_UICannonPreview_Base`

프리뷰 Actor는 다음처럼 구성한다.

- Replicates 끔
- Collision 끔
- Simulate Physics 끔
- 이동/공격/AI 로직 없음
- Mesh와 Material만 소유

강화 단계별 자식 BP는 Mesh 또는 Material만 바꾼다.

## 10. `WBP_ShipUpgradeScreen`

### 10.1 필수 자식 WBP 배치

```text
WBP_ShipUpgradeGraph   GraphWidget          [필수]
WBP_ShipUpgradeDetails DetailsWidget        [필수]
```

둘 다 변수 이름을 정확히 `GraphWidget`, `DetailsWidget`으로 지정한다. 타입이 맞지 않거나 이름이 틀리면 컴파일에 실패한다.

### 10.2 권장 전체 구조

```text
Overlay
├─ Image Image_MainShipPreview                       [선택]
├─ HorizontalBox
│  ├─ ScrollBox
│  │  └─ SizeBox SizeBox_GraphExtent                 [선택]
│  │     └─ WBP_ShipUpgradeGraph GraphWidget         [필수]
│  └─ WBP_ShipUpgradeDetails DetailsWidget           [필수]
├─ VerticalBox CurrentStats
│  ├─ Text Text_CurrentHealth                        [선택]
│  ├─ Text Text_CurrentCannonDamage                  [선택]
│  ├─ Text Text_CurrentCooldown                      [선택]
│  ├─ Text Text_CurrentCannonballSpeed               [선택]
│  ├─ Text Text_CurrentPropulsion                    [선택]
│  └─ Text Text_CurrentTurn                          [선택]
└─ Text Text_ResultMessage                           [선택]
```

`ScrollBox`를 쓴다면 `Consume Mouse Wheel = Never`로 두어 화면 부모의 C++ 줌 입력이 전달되게 한다. 기본 구현은 휠로 줌하고, 프리뷰 이미지 위에서 왼쪽 버튼 드래그로 배를 회전한다.

### 10.3 Class Defaults

- `Preview Stage Class` = `BP_ShipUpgradePreviewStage`
- `Default Ship Preview Actor Class` = 기본 UI 배 Actor
- `Base Graph Extent` = 우선 `(2400, 1600)`
- `Zoom Min / Max / Step` = 기본값으로 시작
- `Preview Rotation Sensitivity` = 기본값으로 시작

`Image_MainShipPreview`에는 Render Target을 직접 지정하지 않아도 된다. C++가 Preview Stage의 Render Target을 가져와 Brush에 넣는다.

### 10.4 Event Graph

완전히 비워도 핵심 기능이 작동한다.

선택적으로 다음 이벤트만 꾸밈에 사용할 수 있다.

- `BP On Activation Result`: 결과 Toast 애니메이션
- `BP On Current Stats Changed`: 숫자 전환 애니메이션
- `BP On Active Visuals Changed`: 별도 장식 갱신
- `BP On Initialization Failed`: 오류 오버레이

활성화 요청, 서버 호출, 재료 차감, 상태 판정은 이 이벤트에서 다시 구현하지 않는다.

## 11. `WBP_WorkspaceScreen`

### 11.1 필수 구조

```text
HorizontalBox
├─ VerticalBox Menu
│  ├─ Button Button_ShipUpgrade            [선택]
│  ├─ Button Button_ItemCrafting            [선택]
│  └─ Button Button_SkillUpgrade            [선택]
└─ Overlay
   ├─ WidgetSwitcher WidgetSwitcher_Content [필수]
   │  ├─ WBP_ShipUpgradeScreen              [index 0]
   │  ├─ 아이템 제작 화면                   [index 1]
   │  └─ 스킬 강화 화면                     [index 2]
   └─ Button Button_Close                   [선택]
```

현재 없는 탭은 빈 Placeholder 위젯으로 채워도 된다. 인덱스는 기본적으로 배 강화 `0`, 아이템 제작 `1`, 스킬 강화 `2`다.

C++가 버튼 클릭, Switcher 전환, 초기 배 강화 탭 선택, 닫기 요청을 처리한다. Event Graph는 필요 없다.

`BP On Workspace Tab Changed`는 선택된 메뉴 버튼 색이나 탭 전환 애니메이션에만 쓴다.

## 12. PlayerController와 작업대 연결

### 12.1 PlayerController

1. `/Game/Blueprints/Player/BP_BasePlayerController`를 연다.
2. Class Defaults를 연다.
3. `Ship Upgrade Workspace Widget Class`에 `WBP_WorkspaceScreen`을 지정한다.
4. Compile/Save한다.

실제 GameMode가 다른 PlayerController를 사용한다면 그 클래스에도 같은 값을 지정하거나 `ABasePlayerController` 계열인지 확인한다.

### 12.2 WorkTable

1. `/Game/Blueprints/Interactable_Object/BP_WorkTable`을 연다.
2. `Open Integrated Workspace`를 켠다.
3. Compile/Save한다.

이 값이 켜져 있으면 작업대 상호작용 시 기존 인벤토리 화면처럼 PlayerController가 위젯을 한 번 생성해 보관하고, 이후에는 Visibility와 입력 모드만 전환한다.

## 13. 강화 데이터 에셋 설정

각 노드의 기존 게임 데이터는 유지하고 UI 필드만 채운다.

| 노드 종류 | `PreviewType` | 권장 필드 |
|---|---|---|
| 단순 능력치 | `Icon2D` | `Icon` |
| 대포 강화 | `Cannon3D` | `PreviewActorClass`, `ActivatedCannonActorClass` |
| 배 강화 | `Ship3D` | `PreviewActorClass`, `ActivatedShipActorClass` |

공통:

- `GraphPosition`: 그래프상의 위치
- `PrerequisiteNodeIds`: 연결선과 잠금 조건
- `VisualPriority`: 활성 외형이 여러 개일 때 우선순위
- `CameraPreset`: 프리뷰 카메라 확장용 값

배/대포 외형 노드는 활성화 즉시 UI 배경 프리뷰의 활성 외형 선택에도 반영되어야 하므로 `Activated...ActorClass`를 빠뜨리지 않는다.

## 14. 실제 제작 순서

다음 순서가 가장 오류를 찾기 쉽다.

1. 에디터 재시작 후 C++ 부모 클래스가 보이는지 확인한다.
2. Node, Connection, Stat Row, Material Row WBP를 만든다.
3. Details WBP를 만들고 두 Row Class를 지정한다.
4. Graph WBP를 만들고 Node/Connection Class를 지정한다.
5. Preview Stage와 Render Target, UI 전용 배 Actor를 만든다.
6. Screen WBP에 `GraphWidget`, `DetailsWidget`을 배치하고 Preview Class를 지정한다.
7. Workspace WBP의 Switcher index 0에 Screen을 넣는다.
8. PlayerController에 Workspace Class를 지정한다.
9. WorkTable의 `Open Integrated Workspace`를 켠다.
10. 기존 강화 Data Asset의 UI/프리뷰 필드를 채운다.
11. PIE에서 아래 체크리스트를 검증한다.

## 15. PIE 체크리스트

- 작업대 상호작용 시 통합 작업 화면이 열린다.
- 첫 탭이 배 강화다.
- 닫기 버튼과 ESC가 입력 모드를 정상 복구한다.
- 모든 노드와 연결선이 데이터 위치대로 표시된다.
- 잠긴 노드는 잠금 표시, 활성 노드는 완료 표시가 보인다.
- 노드 선택 시 이름, 설명, 수치, 재료가 갱신된다.
- 재료 부족 시 활성화 버튼이 비활성화된다.
- 활성화 클릭 후 중복 요청이 막히고 결과가 표시된다.
- 성공 후 재료, 노드 상태, 현재 능력치가 즉시 갱신된다.
- 화면을 닫았다 다시 열어도 상태가 유지된다.
- 저장 후 재접속해도 활성 노드가 유지된다.
- 2D 노드는 아이콘, 배/대포 노드는 3D 프리뷰가 표시된다.
- 프리뷰 드래그 회전과 그래프 휠 줌이 동작한다.
- UI 배경의 강화된 배/대포 프리뷰 외형이 `VisualPriority` 규칙대로 바뀐다.

## 16. 자주 생기는 문제

| 증상 | 확인할 것 |
|---|---|
| 부모 클래스가 목록에 없음 | 에디터 완전 재시작, Editor Target 빌드 성공 여부 |
| WBP 컴파일 시 BindWidget 오류 | 필수 위젯 이름과 타입, 대소문자 |
| 그래프가 비어 있음 | Graph의 Node/Connection Class 지정, 강화 컴포넌트/Tree Data |
| 상세 행이 없음 | Details의 두 Row Class와 VerticalBox 이름 |
| 아이콘이 없음 | Data Asset Soft Texture 값; BP에 Async Load 노드는 필요 없음 |
| 선이 없음 | `Image_Line`과 실선/점선 Texture 지정 |
| 3D 프리뷰가 없음 | Preview Stage Class, SceneCapture Texture Target, 프리뷰 Actor Class |
| 휠 줌이 안 됨 | ScrollBox `Consume Mouse Wheel = Never` |
| 작업대에서 옛 팝업이 열림 | `Open Integrated Workspace = true` |
| 화면 자체가 안 열림 | PlayerController의 Workspace Widget Class |

## 17. 완료 기준

정상 구성된 WBP의 Event Graph에는 게임 로직 노드가 없어도 된다. 허용되는 수작업은 다음 정도다.

- 필수 이름의 디자이너 위젯 배치
- Class Defaults의 자식 WBP/Texture/Actor Class 지정
- Render Target, 조명, 프리뷰 Mesh 구성
- 선택적인 색/애니메이션 이벤트

특히 `ApplyNodeView`, `Async Load Asset`, `Async Load Class Asset`, 노드/행 생성 루프, 활성화 서버 호출을 WBP에서 다시 만들지 않는다. 이 작업들은 모두 C++ 네이티브 부모가 담당한다.
