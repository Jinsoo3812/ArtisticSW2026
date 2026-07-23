# 배 강화 UI Blueprint 사용 가이드

> 2026-07-23 기준: 이 문서의 API 설명은 디버깅과 확장용 참고 자료다. 실제 Widget Blueprint는 `UShipUpgrade...Widget` 네이티브 부모를 사용하며, 초기화·이벤트 바인딩·동적 위젯 생성·비동기 로드·활성화 흐름을 Event Graph에서 다시 만들지 않는다. 실제 에디터 제작 순서는 `Ship_Upgrade_UI_Editor_Step_by_Step_Guide.md`를 따른다.

## 1. UI가 알아야 하는 객체

UI는 `UShipUpgradeComponent` 하나만 사용한다.

다음 구현 세부사항에는 직접 접근하지 않는다.

- 강화 Tree Data Asset 직접 검색
- Ship Data Table 행 검색
- GAS Attribute 직접 읽기/쓰기
- SaveGame 직접 로드/저장
- Server RPC 직접 호출

네이티브 화면 위젯이 `NativeConstruct`에서 다음 API에 해당하는 작업을 자동 수행한다. 커스텀 화면을 새로 만드는 경우에만 직접 호출한다.

```text
Get Local Ship Upgrade Component
```

반환값이 유효하지 않으면 PlayerState가 아직 준비되지 않은 것이므로 짧게 기다린 뒤 다시 시도하거나 화면을 비활성 상태로 둔다.

기본 강화 트리는 다음 경로에서 자동으로 로드된다.

```text
/Game/New/Ship/Upgrade/DA_ShipUpgradeTree
```

PlayerState의 `ShipUpgradeComponent`에 다른 Tree를 지정하면 그 자산이 우선한다.

---

## 2. 화면 최초 구성

### 2.1 이벤트 바인딩

컴포넌트를 얻은 직후 다음 Delegate에 바인딩한다.

| 이벤트 | UI 처리 |
| --- | --- |
| `OnUpgradeDataReady` | 전체 노드 다시 생성 |
| `OnNodeStateChanged` | 해당 NodeId의 버튼 상태와 연결선 갱신 |
| `OnNodeActivationResult` | 로딩 표시 종료, 성공/실패 메시지 표시 |
| `OnShipStatsChanged` | 현재 총 스탯 패널 갱신 |
| `OnUpgradeDataChanged` | 재료 보유량 또는 강화 상태가 변했으므로 현재 View 재조회 |

위젯을 닫을 때 Delegate 바인딩을 해제한다.

### 2.2 전체 노드 생성

```text
UpgradeComponent
    → Get All Node Views
    → For Each Loop
        → Node Widget 생성
        → NodeId 저장
        → GraphPosition으로 Canvas Slot 위치 지정
        → DisplayName / Icon / State 적용
```

`GetAllNodeViews()`가 반환하는 배열 순서를 UI 의미로 사용하지 않는다. 영구 식별자는 항상 `NodeId`다.

### 2.3 연결선 생성

각 `FShipUpgradeNodeView`의 `PrerequisiteNodeIds`를 읽는다.

한 노드에 상위 노드를 N개 지정할 수 있다. N개는 모두 충족해야 하는 AND 조건이며, UI는 각 상위 노드에서 현재 노드로 연결선을 각각 그린다.

```text
현재 노드의 PrerequisiteNodeIds
    → 선행 NodeId 위젯 검색
    → 선행 위젯 중심에서 현재 위젯 중심까지 연결선 생성
```

노드 상태는 다음 세 종류다.

| 상태 | 권장 표현 |
| --- | --- |
| `Locked` | 어둡게, 활성화 버튼 비활성 |
| `Available` | 선택/활성화 가능 강조 |
| `Active` | 완료 색상과 체크 표시 |

---

## 3. 노드 선택 상세 패널

노드 버튼을 클릭하면 저장해 둔 `NodeId`로 다음 함수를 호출한다.

```text
Get Node View(NodeId)
```

성공하면 다음 값을 상세 패널에 적용한다.

- `DisplayName`
- `Description`
- `Icon`
- `State`
- `StatChanges`
- `MaterialCosts`
- `bHasEnoughMaterials`
- `UnavailableReason`

`StatChanges`의 `FormattedText`는 바로 TextBlock에 넣어도 되는 완성 문구다.

예:

```text
대포 공격력: 20 → 30 (+10)
다음 발사까지의 시간: 2초 → 1.8초 (-0.2초)
```

색상을 직접 정하려면 `bImprovesStat`을 사용한다. 쿨다운은 숫자가 감소할 때 이득이므로 Delta 부호만 보고 색을 정하면 안 된다.

### 3.1 재료 비용 표시

`MaterialCosts`의 각 행에는 다음 값이 이미 가공되어 있다.

| 필드 | 사용법 |
| --- | --- |
| `DisplayName`, `Icon` | 재료 이름과 아이콘 |
| `OwnedQuantity` | 현재 인벤토리 보유량 |
| `RequiredQuantity` | 노드 활성화 필요량 |
| `bEnough` | 해당 재료 충족 여부와 색상 |

배열이 비어 있으면 무료 노드다. `bHasEnoughMaterials`는 모든 재료 행의 충족 여부를 합친 값이다. 인벤토리가 변하면 `OnUpgradeDataChanged`가 오므로 선택 중인 노드에 `GetNodeView(NodeId)`를 다시 호출한다.

---

## 4. 활성화 버튼

버튼 활성 여부는 반드시 다음 함수를 사용한다. `State == Available`은 선행 노드 조건만 충족했다는 뜻이며 재료 부족일 수 있다.

```text
Can Activate Node(NodeId, OutReason)
```

버튼 클릭 흐름:

```text
활성화 버튼 클릭
    → 버튼 임시 비활성화
    → 로딩 표시
    → Request Activate Node(NodeId)
    → 결과를 기다림
```

`RequestActivateNode`의 실행 직후에는 성공으로 간주하지 않는다. 실제 판정은 서버가 하며 결과는 `OnNodeActivationResult`로 돌아온다.

결과 처리:

| Result | UI 처리 예시 |
| --- | --- |
| `Success` | 성공 효과 재생, 노드와 연결선 갱신 |
| `AlreadyActive` | 현재 상태 다시 조회 |
| `UnknownNode` | 데이터 오류 메시지 및 로그 |
| `MissingPrerequisite` | 반환 Message 표시 |
| `NotAuthority` | 네트워크 오류 메시지 |
| `SaveFailed` | 저장 실패 메시지, 활성화되지 않은 상태 유지 |
| `NotConfigured` | 시스템 설정 오류 표시 |
| `MissingMaterials` | 반환 Message 표시, 재료 행 강조 |
| `InvalidCost` | 기획 데이터 오류 표시 및 로그 |

더블 클릭이나 같은 요청의 재전송이 발생해도 서버는 한 노드를 두 번 적용하지 않는다.

---

## 5. 활성화 전 미리보기

선택한 노드가 활성화 가능할 때 다음을 호출한다.

```text
Get Stats After Activating(NodeId)
```

반환되는 `FShipStatSnapshot`은 해당 노드까지 활성화했다고 가정한 전체 최종 스탯이다.

현재 값은 다음 함수로 얻는다.

```text
Get Current Ship Stats
```

두 Snapshot을 나란히 표시할 수도 있고, 일반적인 상세 패널에서는 `GetNodeStatChanges()`의 결과만 표시해도 된다.

Snapshot 필드:

| 필드 | UI 표시 |
| --- | --- |
| `CannonDamage` | 대포 공격력 |
| `CannonFireCooldownSeconds` | 다음 발사까지의 시간 |
| `CannonballSpeed` | 대포 발사속도 |
| `MaxHealth` | 배 체력 |
| `ForwardPropulsionMultiplier` | 배 WS 추진속도 |
| `TurnTorqueMultiplier` | 배 AD 회전속도 |

추진과 회전 값은 현재 물리 힘/토크 배율이다. 퍼센트로 표시하려면 `1.0 = 100%`로 변환한다.

---

## 6. 네이티브 위젯 책임 분리

### `WBP_ShipUpgradeScreen`

- 부모: `UShipUpgradeScreenWidget`
- WBP 책임: `GraphWidget`, `DetailsWidget` 배치와 화면 디자인
- C++ 책임: UpgradeComponent 획득, Delegate 바인딩/해제, 전체 조회, 선택, 요청 중 상태, 현재 스탯, 줌, 3D 프리뷰

### `WBP_ShipUpgradeNode`

- 부모: `UShipUpgradeNodeWidget`
- WBP 책임: `Button_Node`와 선택적 이미지/텍스트 배치
- C++ 책임: Node View 보관, 아이콘 비동기 로드, 상태 표시, 클릭/호버/선택 처리

### `WBP_ShipUpgradeDetails`

- 부모: `UShipUpgradeDetailsWidget`
- WBP 책임: `Button_Activate`와 선택적 상세 위젯 배치, Row Class 지정
- C++ 책임: 선택 View 표시, Stat/Material 행 생성, 버튼 상태, 활성화 요청, 요청 중 표시

노드 WBP가 SaveGame이나 PlayerState를 개별 조회하지 않는다. 네이티브 Screen이 컴포넌트 하나를 소유하고 하위 네이티브 위젯에는 View 데이터만 전달한다.

---

## 7. 전체 유즈케이스

아래 흐름은 기능 이해용이다. 이 흐름은 현재 C++ 네이티브 위젯 계층에 구현되어 있으므로 WBP Event Graph로 옮기지 않는다.

```text
[화면 열기]
    GetLocalShipUpgradeComponent
    Bind Events
    GetAllNodeViews
    Create Nodes and Connections

[Hull_I 선택]
    GetNodeView(Hull_I)
    Details에 체력 증가량 표시
    State가 Available이므로 버튼 활성

[활성화 클릭]
    RequestActivateNode(Hull_I)
    서버에서 모든 상위 노드와 재료 재검증
    Inventory 재료 원자적 차감
    SaveGame 저장
    ActiveNodeIds 복제
    OnNodeActivationResult(Success)
    OnNodeStateChanged(Hull_I, Active)
    OnShipStatsChanged

[후속 Cannon_I]
    선행 Hull_I가 Active이므로 Available로 변경
    MaterialCosts에 보유량/필요량 표시
    재료가 부족하면 버튼 비활성, 재료 획득 시 OnUpgradeDataChanged로 재조회
    활성화 후 대포 공격력 미리보기와 현재 수치 갱신

[전투 맵 진입]
    PlayerState가 저장된 ActiveNodeIds 복원
    플레이어 Ship이 기본 DT 행을 읽음
    ApplyPlayerUpgrades(PlayerState)
    동일 계산기로 최종 Snapshot 생성
    Ship GAS Attribute와 WS/AD 물리 배율에 적용
```

---

## 8. UI 디버깅 체크리스트

- 컴포넌트를 얻지 못함: 현재 PlayerController에 PlayerState가 복제됐는지 확인
- 노드가 0개: `DA_ShipUpgradeTree`가 설정/로드됐는지 확인
- 자식 노드가 계속 Locked: `UnavailableReason`을 화면 또는 로그에 출력
- 상위 노드가 여러 개인 자식이 안 열림: `PrerequisiteNodeIds`의 모든 노드가 Active인지 확인
- Available인데 버튼이 꺼짐: `bHasEnoughMaterials`와 `UnavailableReason` 확인
- 재료 숫자가 갱신되지 않음: `OnUpgradeDataChanged` 바인딩 확인 후 `GetNodeView` 재호출
- 클릭 후 변화가 없음: `OnNodeActivationResult` 바인딩 여부 확인
- 미리보기와 실제 전투 수치가 다름: UI가 직접 계산하지 않았는지 확인
- 쿨다운 개선이 나쁜 색으로 표시됨: Delta 부호 대신 `bImprovesStat` 사용
- 맵 이동 후 초기화됨: PlayerState/SaveGame 로드 완료 전에 UI를 그리지 않았는지 확인

---

## 9. 프로그래머에게 요청해야 하는 경우

다음 요구는 UI Blueprint에서 우회 구현하지 말고 API 확장을 요청한다.

- 새로운 배 스탯 종류 추가
- 새로운 비용 종류 또는 인벤토리 외 결제 수단 추가
- 여러 강화 트리 전환
- 노드 초기화/환불
- 전투 중 실시간 강화 적용 정책
- 온라인 계정 프로필 저장

UI에서 별도의 계산식이나 임시 SaveGame을 만들면 서버의 실제 전투 수치와 화면이 달라질 수 있다.

---

## 10. 기획자의 노드 설정 절차

`DA_ShipUpgradeTree`에서 노드마다 다음을 자유롭게 설정한다.

1. `PrerequisiteNodeIds`: 상위 노드 0~N개. 모두 활성화되어야 해금된다.
2. `ActivationCosts`: 필요한 `ItemTag`와 `Quantity` 0~N개. 모두 보유해야 하며 성공 시 전부 차감된다.
3. `StatModifiers`: 활성화 후 플레이어 배에 적용할 스탯 변경 1~N개.
4. `GraphPosition`: 하향식 트리 UI에서 사용할 좌표.

잘못된 상위 ID, 순환 참조, 중복 상위 ID, 잘못된 재료 태그/수량은 Data Asset Validation에서 검출된다. 실제 활성화 요청 때도 서버가 다시 검증하므로 UI 표시를 우회해 요청해도 재료 없이 해금되지 않는다.

---

## 11. 2026-07-22 UI 착수 전 구현 상태 분석

### 11.1 결론

**2026-07-22 후속 구현으로 UI 착수를 막던 C++ 선행 작업은 완료되었다. 이제 UI 담당자가 에디터에서 기능 화면과 3D 프리뷰 자산을 제작할 수 있다.**

완료된 후속 작업:

1. 작업대 상호작용에서 통합 UI를 열고 닫는 PlayerController 진입점과 입력 모드 관리
2. 배/대포 3D 프리뷰 및 활성 외형 Actor Class를 노드 View로 전달하는 데이터 필드
3. 강화 재료 원자 차감의 `InventorySlots`/`InventoryPages` 불일치 수정
4. 에디터 타깃 컴파일 성공과 `ArtisticSW.ShipUpgrade` 자동화 테스트 3개 전체 통과

2026-07-23 네이티브 UMG 계층까지 구현되어, 남은 일은 필수 이름의 Widget Blueprint 배치, 아이콘, 프리뷰 Actor, RenderTarget, 노드 좌표와 스타일 제작이다. 구체적인 에디터 작업 순서는 `Ship_Upgrade_UI_Editor_Step_by_Step_Guide.md`를 따른다.

### 11.2 현재 준비된 범위

| 요구 사항 | 상태 | 근거 및 사용 방법 |
| --- | --- | --- |
| 여섯 스탯 표시 | 준비됨 | 체력, 대포 공격력, 발사 주기, 포탄 속도, 전진 추진 배율, 선회 배율이 `FShipStatSnapshot`에 있음 |
| 노드 배치 | 준비됨 | `GetAllNodeViews()`의 `GraphPosition` 사용 |
| 다중 선행 노드와 연결선 | 준비됨 | `PrerequisiteNodeIds`가 0~N개이며 AND 조건으로 판정됨 |
| 잠김/활성화 가능/활성 상태 | 준비됨 | `Locked`, `Available`, `Active` 세 상태를 이미지, 자물쇠, 실선/점선 표현에 매핑 가능 |
| 선택/호버 상세 정보 | 준비됨 | 이름, 설명, 아이콘, 스탯 변화, 재료, 비활성 사유가 `FShipUpgradeNodeView`에 포함됨 |
| 전후 수치 표시 | 준비됨 | 일반 상세 패널은 `StatChanges.FormattedText`와 `bImprovesStat` 사용 |
| 재료 보유량/필요량 표시 | 준비됨 | `MaterialCosts`에 이름, 아이콘, 보유량, 필요량, 충족 여부가 포함되며 원자 차감 테스트 통과 |
| 활성화 요청/결과 | 준비됨 | `RequestActivateNode` 후 `OnNodeActivationResult`를 기다림 |
| 저장/복제/배 전투 수치 적용 | 준비됨 | PlayerState 컴포넌트 저장·복제 후 `AShip::PossessedBy()`에서 `ApplyPlayerUpgrades()` 호출 |
| UI 스크롤, 줌, 노드 확대, 토글 | 네이티브 UI 준비됨 | C++가 줌, 선택/호버 확대, 상태 전환을 처리. WBP는 스타일만 지정 |
| 3D 배 회전 프리뷰 | 네이티브 UI 준비됨 | C++가 Soft Class 로드, Actor 교체, 드래그 회전을 처리. SceneCapture/RenderTarget 자산은 에디터에서 제작 |
| 대포·배 강화 직후 외형 변경 | 데이터 준비됨 | `ActivatedShipActorClass`, `ActivatedCannonActorClass`, `VisualPriority` 제공 |
| 통합 작업대 화면 열기/닫기 | 준비됨 | Controller의 Open/Close 함수와 작업대 Client RPC 경로 구현. Widget Class 지정 필요 |

현재 콘텐츠에는 `/Game/New/Ship/Upgrade/DA_ShipUpgradeTree`가 있고 `Hull_I`, `CannonDamage_I`, `Reload_I`, `CannonballSpeed_I`, `Propulsion_I`, `Turn_I` 노드 식별자가 들어 있다. 반면 강화 화면 Widget Asset은 아직 없고, 현재 Data Asset 패키지에서는 외부 Texture 경로도 확인되지 않으므로 노드 `Icon`은 별도 지정이 필요하다.

### 11.3 기획 목업을 현재 상태 값에 매핑하는 방법

| 목업 표현 | 권장 판정 |
| --- | --- |
| 이미 활성화된 노드 | `State == Active` |
| 지금 해금 가능한 노드 | `State == Available`이며 `CanActivateNode == true` |
| 선행 노드는 충족했지만 재료가 부족한 노드 | `State == Available`, `bHasEnoughMaterials == false` |
| 아직 접근할 수 없는 노드 | `State == Locked` |
| 활성 노드의 다음 연결선 | 자식 View를 재조회하여 `Available`이면 강조 실선, `Locked`이면 점선 |
| 호버 전 정보 숨김 | 상태가 아니라 Widget의 Hover/Selected 로컬 상태로 처리 |
| 노드 클릭 시 확대/축소 | Widget Animation 또는 Render Transform으로 처리 |

`Locked` 노드도 API에는 이름과 스탯 정보가 들어 있다. 기획대로 잠김 상태에서 정보를 숨기는 것은 보안이나 API 문제가 아니라 UI 표시 정책이다.

전체 Snapshot 미리보기에 `GetStatsAfterActivating()`을 쓸 때는 주의한다. 이 함수는 재료가 부족하면 실패할 수 있다. 재료 부족 상태에서도 해당 노드의 전후 수치를 보여주려면 항상 제공되는 `GetNodeView().StatChanges` 또는 `GetNodeStatChanges()`를 사용한다.

### 11.4 인벤토리 방식을 참고한 화면 오픈 구조

현재 인벤토리는 `ABasePlayerController`가 `PlayerHUDWidget`을 한 번 만들고, `InventoryPanel`의 `Visible/Collapsed`를 전환하며, `ApplyInventoryInputMode()`에서 커서와 입력 모드를 중앙 관리한다. 강화 화면도 같은 소유 구조를 따르는 것이 안전하다.

구현된 구조와 UI가 이어서 구성할 부분은 다음과 같다.

```text
작업대 상호작용 서버 검증
    -> 소유 클라이언트의 PlayerController에 OpenWorkspace 요청
    -> PlayerController가 열려 있는 Inventory / Status / Storage와 상호 배타 처리
    -> WBP_WorkspaceScreen을 최초 한 번만 생성하고 AddToViewport
    -> Ship Reinforcement 탭 선택 후 Visible
    -> GetLocalShipUpgradeComponent
    -> 이벤트 바인딩
    -> RefreshUpgradeData + GetAllNodeViews로 즉시 최초 화면 구성
    -> 공용 Menu Input Mode 적용

ESC 또는 닫기
    -> 활성화 요청 중 UI 정책 확인
    -> 이벤트 바인딩 해제 또는 화면 생존 기간에 맞게 유지
    -> Collapsed
    -> 커서 숨김, GameOnly 복원, 게임 뷰포트 포커스, FlushPressedKeys
    -> Look Input 잠금 해제
```

구현 위치:

- `ABasePlayerController`: `OpenShipUpgradeWorkspace`, `CloseShipUpgradeWorkspace`, 화면 인스턴스와 입력 모드 소유
- `UShipUpgradeWorkspaceWidget`: 탭 버튼과 Switcher, 닫기 요청 처리
- `UShipUpgradeScreenWidget`: `UShipUpgradeComponent` 바인딩과 노드/연결선/상세/프리뷰 갱신
- `UShipUpgradeNodeWidget`: 표현, 비동기 아이콘 로드, 클릭/호버/선택 처리
- `UShipUpgradeDetailsWidget`: 선택 노드 상세, 행 생성, 활성화 요청과 Pending 표시

`AWorkTable.bOpenIntegratedWorkspace`의 기본값은 true다. 따라서 기존 `Interaction.Craft` 서버 검증 뒤 `UCrafterComponent`가 소유 클라이언트의 `ClientOpenShipUpgradeWorkspace()`를 호출한다. false인 작업대만 기존 `InteractPopupUIClass`/스타포스 팝업 경로를 사용한다.

Controller는 Inventory, Status, Storage, Workspace를 상호 배타적으로 전환하고 Workspace를 닫을 때 게임 입력과 Look 입력을 복원한다. Widget 내부에서 별도로 Input Mode를 바꾸거나 `RemoveFromParent`를 호출하지 않는다.

### 11.5 이벤트 바인딩 시 주의점

- `UShipUpgradeScreenWidget`이 이벤트 바인딩 직후 `RefreshUpgradeData()`와 `GetAllNodeViews()`를 호출해 이미 지나간 `OnUpgradeDataReady`에도 대응한다.
- `GetLocalShipUpgradeComponent()`가 null이면 네이티브 Screen이 짧게 재시도한다.
- `OnUpgradeDataChanged`에서 모든 Node View를 다시 조회하므로 후속 노드의 `Locked -> Available`도 갱신된다.
- 네이티브 Screen이 `PendingNodeIds`를 관리해 중복 클릭을 막고 결과 수신 시 해제한다.
- 아이콘과 프리뷰 Actor Soft Reference는 C++ StreamableManager로 비동기 로드한다. WBP에 `Async Load Asset` 노드를 만들지 않는다.

### 11.6 자동화 테스트 결과와 수정 완료 사항

UE 5.7에서 `ArtisticSW.ShipUpgrade` 자동화 테스트 3개를 실행한 결과는 다음과 같다.

| 테스트 | 결과 |
| --- | --- |
| `CalculationAndValidation` | 성공 |
| `FullPipelineUseCase` | 성공 |
| `MaterialAndMultiParentPipeline` | 성공 |

최초 분석에서는 `RemoveItemsAtomically()`가 `InventorySlots`만 변경하고 `InventoryPages`를 갱신하지 않아 재료 테스트가 실패했다. 후속 구현에서 `TryApplyCraftingTransaction()`, `RemoveItemsAtomically()`, `AddItemsAtomically()`가 각 아이템의 실제 탭 Page 복사본에 작업하고, 모든 작업이 성공한 경우에만 `InventoryPages`와 현재 `InventorySlots`를 함께 커밋하도록 수정했다.

수정 후 UE 5.7에서 세 테스트를 다시 실행했고 모두 성공했다. UI는 활성화 성공 후 임의로 수량을 빼지 말고 `OnUpgradeDataChanged`에서 View를 다시 조회한다.

### 11.7 구현된 3D 프리뷰 데이터

목업처럼 단순 스탯 노드는 2D 이미지, 대포 노드는 3D 대포, 배 노드는 3D 배를 보여주고 활성화 즉시 배경 모델까지 바꾸려면 `NodeId` 하드코딩 대신 별도 프레젠테이션 데이터를 둔다.

노드 정의와 Node View에 다음 필드가 구현되어 있다.

```text
PreviewType: Icon2D / Cannon3D / Ship3D
PreviewActorClass
ActivatedShipActorClass
ActivatedCannonActorClass
VisualPriority
CameraPreset
```

현재는 `FShipUpgradeNodeDefinition`에 직접 포함되어 `GetAllNodeViews()`와 `GetNodeView()`로 전달된다. 여러 Active 노드가 누적 외형을 제공하면 `VisualPriority`가 가장 큰 Actor Class를 사용한다. UI가 `Hull_I`, `CannonDamage_I` 같은 문자열로 외형을 분기하지 않는다.

SceneCapture2D와 RenderTarget으로 배를 보여주고, 프리뷰 Actor의 Yaw를 드래그 입력으로 변경하면 목업의 회전 요구를 구현할 수 있다. 이 프리뷰는 실제 전투 Ship Actor를 직접 옮기거나 회전시키지 않고 별도 프리뷰 월드/스테이지 Actor를 사용해야 한다.

### 11.8 UI 담당자 착수 순서

1. 각 WBP를 `UShipUpgrade...Widget` C++ 부모로 만들고 가이드의 필수 이름만 배치한다.
2. Graph/Details/Screen/Workspace의 Class Defaults에 자식 WBP Class를 지정한다.
3. Preview Actor, RenderTarget, `BP_ShipUpgradePreviewStage`를 만들고 프리뷰 필드에 연결한다.
4. `WBP_WorkspaceScreen`을 `BP_BasePlayerController`의 Workspace Widget Class에 지정한다.
5. 노드 Icon, GraphPosition, 설명, 비용을 실제 `DA_ShipUpgradeTree`에 채운다.
6. Standalone과 Listen Server에서 화면 재오픈, 재료 획득 중 갱신, 연속 클릭, 저장 후 재접속, 배 탑승 후 실제 스탯을 검증한다.

### 11.9 2026-07-23 네이티브 UMG 구현 현황

아래 클래스는 전용 Runtime 모듈 `ShipUpgradeUI`에 있다. 강화 규칙과 데이터는 `WaterAndShip`, PlayerController·작업대·인벤토리 연결은 `ClassFeature`에 유지한다.

추가된 네이티브 클래스:

```text
UShipUpgradeWorkspaceWidget
UShipUpgradeScreenWidget
UShipUpgradeGraphWidget
UShipUpgradeNodeWidget
UShipUpgradeConnectionWidget
UShipUpgradeDetailsWidget
UShipUpgradeStatChangeRowWidget
UShipUpgradeMaterialRowWidget
AShipUpgradePreviewStage
```

이 변경은 표현 계층의 구현 위치만 바꾼다. `UShipUpgradeComponent`, 강화 데이터, 서버 검증, 재료 원자 차감, 저장/복제, 실제 배 스탯 적용 경로는 변경하지 않는다. 기존 `/Script/ClassFeature.ShipUpgrade...` 부모 참조는 `CoreRedirects`로 `ShipUpgradeUI`에 연결된다. 2026-07-23 UE 5.7 Editor/Game Target 빌드와 `ArtisticSW` 자동화 테스트 17개가 모두 성공했다.
