# 통합 시설 허브 UI와 제작 Blueprint 연결 가이드

## 1. 최종 Widget 구조 예시

```text
WBP_FacilityHub
├─ TopTabBar
│  ├─ MapTabButton
│  ├─ UpgradeTabButton
│  └─ CraftingTabButton
└─ MainWidgetSwitcher
   ├─ WBP_MapPanel
   ├─ WBP_UpgradePanel
   └─ WBP_CraftingPanel
      ├─ RecipeList
      ├─ ResultDetail
      ├─ IngredientList
      ├─ CraftCountSelector
      └─ CraftButton
```

제작 시스템은 `WBP_FacilityHub`를 생성하지 않는다. 기존 Interaction 시스템 또는 통합 시설 BP가 허브 UI를 연다.

## 2. 통합 시설 BP 준비

지도·강화·제작을 모두 제공하는 기존 통합 시설 BP에 `CraftingAccessComponent`를 추가한다.

설정값:

- `UseDistance`: 허브에서 제작 기능을 사용할 수 있는 거리
- `AllowedExternalReceivers`: 인벤토리 외 출력이 필요할 때만 등록

제작 전용 별도 액터는 만들 필요가 없다.

통합 시설의 기존 Interaction 처리에서는:

1. `WBP_FacilityHub`를 생성한다.
2. 생성 시 자신을 `ContextActor`로 전달한다.
3. 아직 `OpenCraftingScreen`은 호출하지 않는다.
4. 허브 기본 탭이나 이전 탭을 표시한다.

## 3. 허브 UI가 보관할 참조

`WBP_FacilityHub` 변수:

- `ContextActor`: 상호작용한 통합 시설 액터
- `CraftingComponent`: Owning Player의 `UCraftingComponent`
- `CurrentTab`

초기화 시:

1. `Get Owning Player Pawn`
2. `Cast to BasePlayer`
3. `Get Crafting Component`
4. 결과를 `CraftingComponent` 변수에 저장
5. 통합 시설 BP가 전달한 액터를 `ContextActor`에 저장

## 4. 제작 이벤트 바인딩

허브 UI Construct 또는 명시적 Initialize 함수에서 Crafting Component에 바인딩한다.

- `OnCraftingScreenOpened`
- `OnCraftingScreenClosed`
- `OnCraftingResult`
- `OnCraftingDataChanged`

`OnCraftingScreenOpened`는 서버가 Context와 거리를 승인했다는 뜻이다. 제작 탭 화면 전환은 이 이벤트에서 수행한다.

## 5. 제작 탭 버튼

`CraftingTabButton.OnClicked`:

1. Crafting Component와 ContextActor가 유효한지 검사한다.
2. `OpenCraftingScreen(ContextActor)`를 호출한다.
3. 즉시 WidgetSwitcher를 변경하지 않는다.
4. `OnCraftingScreenOpened`를 기다린다.

`OnCraftingScreenOpened(ApprovedContext)`:

1. `ApprovedContext == ContextActor`인지 확인한다.
2. `MainWidgetSwitcher`를 `WBP_CraftingPanel`로 변경한다.
3. `GetCraftableList`를 호출해 목록을 채운다.

서버가 거부하면 이벤트가 오지 않으므로 기존 탭을 유지한다.

## 6. 지도·강화 탭으로 이동

제작 탭이 열린 상태에서 Map 또는 Upgrade 탭을 누르면 먼저:

`CraftingComponent.CloseCraftingScreen()`

그 다음 WidgetSwitcher를 대상 탭으로 변경한다.

제작 탭이 아니었던 경우에는 Close 호출을 생략해도 된다.

`OnCraftingScreenClosed`에서는 제작 Panel의 선택 항목과 대기 상태를 정리한다.

## 7. 허브 UI 전체 닫기

ESC 또는 Close 버튼:

1. 제작 탭이 활성 상태면 `CloseCraftingScreen()` 호출
2. 허브 Widget 제거
3. Input Mode와 Mouse Cursor 복원
4. 이벤트 바인딩 정리

Interaction Context가 사라졌거나 플레이어가 멀어지면 다음 제작 요청이 `NoActiveContext` 또는 `OutOfRange`로 거부된다.

## 8. 제작 목록

제작 화면 진입 승인 후 호출:

`GetCraftableList(Query)`

권장 Query:

- `SearchText = ""`
- `bIncludeLocked = true`
- `bIncludeDisabled = false`

각 Entry 표시:

- `DisplayName`
- `Icon`
- `ResultQuantity`
- `RarityTag`
- `CategoryTag`
- `Availability`

목록 위젯에는 반드시 `RecipeId`를 저장한다.

UMG Property Binding으로 매 프레임 호출하지 않는다. 제작 탭 진입, 검색 변경, 데이터 변경 이벤트에서만 다시 호출한다.

## 9. 제작법 선택

Entry 클릭 시:

`GetCraftingDetails(RecipeId, CraftCount, OutDetails)`

처리:

1. false면 목록을 갱신하고 선택 해제
2. Header로 결과 영역 갱신
3. `MissingRecipe`이면 “레시피가 없습니다” 표시
4. `bIngredientsVisible == false`이면 재료 Panel 숨김
5. Ingredients 배열로 재료 Entry 생성
6. `OwnedQuantity / RequiredQuantity` 표시
7. `bEnough`에 따라 부족 색상 표시
8. `MaxCraftableCount`를 수량 선택기에 반영

잠긴 제작법은 C++에서 재료 배열 자체를 비워 반환한다.

## 10. 제작 수량 변경

수량 변경 시 현재 RecipeId로 `GetCraftingDetails`를 다시 호출한다.

권장 범위:

- 최소 1
- 최대 `Min(MaxCraftableCount, 99)`

서버는 UI 값과 관계없이 수량 및 재료를 다시 검증한다.

## 11. 제작 버튼

`FCraftingRequest` 작성:

- `RequestId`: 비워두면 자동 생성
- `RecipeId`: 선택 Entry 값
- `CraftCount`: 선택 수량
- `Output.Type = Inventory`
- `Output.ReceiverActor = None`

호출:

`RequestCraft(Request)`

응답 전까지 버튼을 비활성화하는 것을 권장한다. 클라이언트에서 재료를 직접 차감하거나 결과를 미리 추가하지 않는다.

## 12. 제작 결과

`OnCraftingResult(Result)`에서 처리한다.

| Reason | UI 처리 |
|---|---|
| `Success` | 성공 연출 후 상세 갱신 |
| `InvalidRecipe` | 목록 갱신 및 선택 해제 |
| `RecipeDisabled` | 현재 제작 불가 표시 |
| `MissingRecipe` | 레시피 없음 표시 |
| `NoActiveContext` | 제작 Panel 종료 또는 허브 닫기 |
| `OutOfRange` | 거리 메시지 후 제작 Panel 종료 |
| `InvalidQuantity` | 수량 복구 |
| `MissingIngredients` | 상세 수량 재조회 |
| `OutputUnavailable` | 인벤토리 공간/출력 대상 오류 |
| `OutputRejected` | 외부 수신 거부 표시 |
| `DuplicateRequest` | 현재 상태만 갱신 |
| `InternalError` | 일반 오류 표시 |

## 13. 인벤토리 변경

`OnCraftingDataChanged`를 받으면:

- 현재 선택 제작법의 `GetCraftingDetails` 재호출
- 잠금이나 목록 상태가 바뀔 수 있으면 `GetCraftableList` 재호출

기존 Inventory native delegate에 WBP가 직접 접근할 필요는 없다.

## 14. 외부 결과 출력

외부 수신 Actor가 `CraftingOutputReceiver` 인터페이스를 구현한다.

- `Can Receive Crafted Item`
- `Receive Crafted Item`

해당 Actor를 통합 시설의 `CraftingAccessComponent.AllowedExternalReceivers`에 등록한다.

요청:

- `Output.Type = ExternalReceiver`
- `Output.ReceiverActor = 수신 Actor`

## 15. 제작법 입력

`/Game/Blueprints/Item/DT_CraftingRecipes`에 Row를 추가한다.

1. 고유 RowName 지정
2. `ResultItemTag`, `ResultQuantity` 입력
3. 필요 시 `RequiredRecipeItemTag` 입력
4. 일회용일 때만 `bConsumeRecipeItem` 활성화
5. Ingredients 배열 입력
6. `bEnabled` 활성화
7. `SortOrder` 입력

태그는 `Item_Id_...`가 아니라 실제 `Item.Id....`를 입력한다.

## 16. 호출 요약

| 시점 | 호출/이벤트 |
|---|---|
| 통합 시설 Interaction | 허브 UI 생성, 제작 API 호출 없음 |
| 제작 탭 클릭 | `OpenCraftingScreen(ContextActor)` |
| 서버 진입 승인 | `OnCraftingScreenOpened` |
| 제작 Panel 표시 | `GetCraftableList` |
| 제작법 클릭 | `GetCraftingDetails` |
| 수량 변경 | `GetCraftingDetails` |
| 제작 버튼 | `RequestCraft` |
| 제작 응답 | `OnCraftingResult` |
| 인벤토리 변경 | `OnCraftingDataChanged` |
| 다른 탭 이동 | `CloseCraftingScreen` |
| 허브 전체 닫기 | 필요 시 `CloseCraftingScreen` 후 Widget 제거 |
