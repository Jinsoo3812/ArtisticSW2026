// Fill out your copyright notice in the Description page of Project Settings.


#include "BasePlayerController.h"
#include "BasePlayer.h"
#include "UI/PlayerHUDWidget.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "TimerManager.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "BaseGameplayTags.h"
#include "Attacker/AttackerComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Storage/StorageChest.h"
#include "Storage/StorageComponent.h"
#include "UI/StorageWindowWidget.h"
#include "UI/FacilityHubWidget.h"
#include "Facility/FacilityHubActor.h"
#include "UI/StatusWindowWidget.h"
#include "WaterSubsystem.h"
#include "GameFramework/GameStateBase.h"


void ABasePlayerController::OpenFacilityHubFromServer(AActor* ContextActor)
{
	if (!HasAuthority() || !IsValid(Cast<AFacilityHubActor>(ContextActor)))
	{
		return;
	}

	ClientOpenFacilityHub(ContextActor);
}

void ABasePlayerController::ClientOpenFacilityHub_Implementation(AActor* ContextActor)
{
	if (!IsLocalController() || !IsValid(ContextActor))
	{
		return;
	}

	CloseFacilityHub();

	if (StatusWindowWidget && StatusWindowWidget->IsStatusVisible())
	{
		StatusWindowWidget->SetStatusVisible(false);
		SetStatusCharacterInputLocked(false);
		if (PlayerHUDWidget)
		{
			PlayerHUDWidget->SetVisibility(PlayerHUDVisibilityBeforeStatus);
		}
	}
	if (IsStorageOpen())
	{
		CloseStorage();
	}
	if (PlayerHUDWidget)
	{
		PlayerHUDWidget->SetInventoryVisible(false);
		PlayerHUDVisibilityBeforeFacilityHub = PlayerHUDWidget->GetVisibility();
		PlayerHUDWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	TSubclassOf<UFacilityHubWidget> WidgetClass = FacilityHubWidgetClass;
	if (!WidgetClass)
	{
		WidgetClass = LoadClass<UFacilityHubWidget>(
			nullptr,
			TEXT("/Game/Blueprints/02_UI/UI_WorkTable/WBP_WorkspaceScreen.WBP_WorkspaceScreen_C"));
	}
	if (!WidgetClass)
	{
		WidgetClass = LoadClass<UFacilityHubWidget>(
			nullptr,
			TEXT("/Game/Blueprints/02_UI/UI_FacilityHub/WBP_FacilityHub.WBP_FacilityHub_C"));
	}

	if (!WidgetClass)
	{
		if (PlayerHUDWidget)
		{
			PlayerHUDWidget->SetVisibility(PlayerHUDVisibilityBeforeFacilityHub);
		}
		ApplyInventoryInputMode(false);
		return;
	}

	FacilityHubWidget = CreateWidget<UFacilityHubWidget>(this, WidgetClass);
	if (!FacilityHubWidget)
	{
		if (PlayerHUDWidget)
		{
			PlayerHUDWidget->SetVisibility(PlayerHUDVisibilityBeforeFacilityHub);
		}
		ApplyInventoryInputMode(false);
		return;
	}

	FacilityHubWidget->InitializeForContext(ContextActor);
	FacilityHubWidget->AddToViewport(100);
	ApplyInventoryInputMode(true);
	FacilityHubWidget->SetUserFocus(this);
}

void ABasePlayerController::CloseFacilityHub()
{
	if (!IsLocalController() || !FacilityHubWidget)
	{
		return;
	}

	FacilityHubWidget->PrepareToClose();
	FacilityHubWidget->RemoveFromParent();
	FacilityHubWidget = nullptr;
	if (PlayerHUDWidget)
	{
		PlayerHUDWidget->SetVisibility(PlayerHUDVisibilityBeforeFacilityHub);
	}
	ApplyInventoryInputMode(false);
}

bool ABasePlayerController::IsFacilityHubOpen() const
{
	return FacilityHubWidget && FacilityHubWidget->IsInViewport();
}


void ABasePlayerController::BeginPlay()
{
	Super::BeginPlay();

	// [클라/로컬]
	if (IsLocalPlayerController())
	{
		// UI IMC 등록
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* Context : UIIMC)
			{
				Subsystem->AddMappingContext(Context, UIIMCPriority);
			}
		}
	}

	// PlayerController에서 HUD 설정 ..
	// TODO: HUD로 바꾸기?
	if (IsLocalController() && PlayerHUDWidgetClass)
	{
		PlayerHUDWidget = CreateWidget<UPlayerHUDWidget>(this, PlayerHUDWidgetClass);
		if (PlayerHUDWidget)
		{
			PlayerHUDWidget->AddToViewport();
			PlayerHUDWidget->SetInventoryVisible(false);
			BindHUDToCurrentPlayer();
		}
	}

	if (IsLocalController() && StatusWindowWidgetClass)
	{
		StatusWindowWidget = CreateWidget<UStatusWindowWidget>(this, StatusWindowWidgetClass);
		if (StatusWindowWidget)
		{
			StatusWindowWidget->AddToViewport(10);
			StatusWindowWidget->SetStatusVisible(false);
			BindHUDToCurrentPlayer();
		}
	}
}

void ABasePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent))
	{
		if (UIInputConfig)
		{
			for (const FKeyInputAction& Action : UIInputConfig->KeyInputActions)
			{
				if (Action.InputAction && Action.KeyTag.IsValid())
				{
					EnhancedInput->BindAction(Action.InputAction, ETriggerEvent::Started, this, &ABasePlayerController::OnUIInputPressed, Action.KeyTag);
				}
			}
		}
	}

	InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &ABasePlayerController::ToggleStatus);
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ABasePlayerController::HandleMenuEscape);
}

void ABasePlayerController::OnUIInputPressed(FGameplayTag InputTag)
{
	// [클라/로컬]
	if (!IsLocalController() || !PlayerHUDWidget)
	{
		return;
	}

	if (InputTag.MatchesTagExact(Key_UI_I))
	{		
		ToggleInventory();
	}
}

void ABasePlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	BindHUDToCurrentPlayer();
}

void ABasePlayerController::OnRep_Pawn()
{
	Super::OnRep_Pawn();
	BindHUDToCurrentPlayer();
}

void ABasePlayerController::BindHUDToCurrentPlayer()
{
	// [클라/로컬] HUD 위젯은 로컬 플레이어에게만
	if (!IsLocalController())
	{
		return;
	}

	if (ABasePlayer* BasePlayer = Cast<ABasePlayer>(GetPawn()))
	{
		if (PlayerHUDWidget)
		{
			PlayerHUDWidget->InitializeForPlayer(BasePlayer);
		}
		if (StatusWindowWidget)
		{
			StatusWindowWidget->InitializeForPlayer(BasePlayer);
		}
	}
}

void ABasePlayerController::ToggleInventory()
{
	if (!IsLocalController() || !PlayerHUDWidget)
	{
		return;
	}

	if (StatusWindowWidget && StatusWindowWidget->IsStatusVisible())
	{
		return;
	}

	if (IsFacilityHubOpen())
	{
		CloseFacilityHub();
	}

	// 상자 UI가 열려 있으면, 상자를 닫고 인벤토리만 열기
	if (IsStorageOpen())
	{
		CloseStorage();
		PlayerHUDWidget->SetInventoryVisible(false);
		ApplyInventoryInputMode(false);
		return;
	}

	// 인벤토리가 열려 있지 않으면 인벤토리 열기
	const bool bOpen = !PlayerHUDWidget->IsInventoryVisible();
	PlayerHUDWidget->SetInventoryVisible(bOpen);
	ApplyInventoryInputMode(bOpen);
}

void ABasePlayerController::ToggleStatus()
{
	if (!IsLocalController() || !StatusWindowWidget)
	{
		return;
	}

	if (IsFacilityHubOpen())
	{
		CloseFacilityHub();
	}

	if (StatusWindowWidget->IsStatusVisible())
	{
		StatusWindowWidget->SetStatusVisible(false);
		SetStatusCharacterInputLocked(false);
		if (PlayerHUDWidget)
		{
			PlayerHUDWidget->SetVisibility(PlayerHUDVisibilityBeforeStatus);
		}
		ApplyInventoryInputMode(false);
		return;
	}

	// A storage window owns the inventory interaction while it is open.
	if (IsStorageOpen())
	{
		return;
	}

	// A standalone inventory yields to the full status window.
	if (PlayerHUDWidget && PlayerHUDWidget->IsInventoryVisible())
	{
		PlayerHUDWidget->SetInventoryVisible(false);
	}

	if (PlayerHUDWidget)
	{
		PlayerHUDVisibilityBeforeStatus = PlayerHUDWidget->GetVisibility();
		PlayerHUDWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	StatusWindowWidget->SetStatusVisible(true);
	ApplyInventoryInputMode(true);
	SetStatusCharacterInputLocked(true);
}

void ABasePlayerController::HandleMenuEscape()
{
	if (IsFacilityHubOpen())
	{
		CloseFacilityHub();
	}
}

void ABasePlayerController::OpenStorageFromServer(AStorageChest* StorageChest)
{
	if (!HasAuthority() || !StorageChest)
	{
		return;
	}

	// 이미 열려 있는 동일한 상자에는 중복 열기 요청을 보내지 않는다.
	if (ActiveStorageChest == StorageChest)
	{
		return;
	}

	ActiveStorageChest = StorageChest;
	StartStorageSearch(StorageChest);
	ClientOpenStorage(StorageChest);
}

void ABasePlayerController::ClientOpenStorage_Implementation(AStorageChest* StorageChest)
{
	OpenStorage(StorageChest);
}

void ABasePlayerController::ClientUpdateStorageRevealState_Implementation(AStorageChest* StorageChest, int32 RevealedSlotCount, int32 SearchingSlotIndex)
{
	if (!StorageChest)
	{
		return;
	}

	FStorageRevealState& RevealState = StorageRevealStates.FindOrAdd(StorageChest);
	RevealState.RevealedSlotCount = RevealedSlotCount;
	RevealState.SearchingSlotIndex = SearchingSlotIndex;

	if (StorageWindowWidget && ActiveStorageChest == StorageChest)
	{
		StorageWindowWidget->RefreshStorage();
	}
}

void ABasePlayerController::ServerTransferStorageSlot_Implementation(AStorageChest* StorageChest, int32 SlotIndex)
{
	ServerQuickMoveStorageSlotToInventory_Implementation(StorageChest, SlotIndex);
}

void ABasePlayerController::ServerHandleStorageLeftClick_Implementation(AStorageChest* StorageChest, int32 SlotIndex)
{
	// 좌클릭 했을 때 상호작용
	// 커서에 아이템이 붙어 있으면 인벤토리 -> storage
	// 커서에 아이템이 없으면 storage -> 인벤토리
	if (!StorageChest || ActiveStorageChest != StorageChest)
	{
		return;
	}

	ABasePlayer* BasePlayer = Cast<ABasePlayer>(GetPawn());
	if (!BasePlayer)
	{
		return;
	}

	UStorageComponent* StorageComponent = StorageChest->GetStorageComponent();
	UInventoryComponent* InventoryComponent = BasePlayer->GetInventoryComponent();
	if (!StorageComponent || !InventoryComponent)
	{
		return;
	}

	if (InventoryComponent->GetCursorItem().IsValid())
	{
		const TArray<FInventorySlot>& Slots = StorageComponent->GetSlots();
		if (Slots.IsValidIndex(SlotIndex) && !Slots[SlotIndex].IsEmpty() && !IsStorageSlotRevealed(StorageChest, SlotIndex))
		{
			return;
		}

		InventoryComponent->TransferCursorToStorageSlot(StorageComponent, SlotIndex);
		StartStorageSearch(StorageChest);
		return;
	}

	if (!IsStorageSlotRevealed(StorageChest, SlotIndex))
	{
		return;
	}

	StorageComponent->TransferSlotToInventory(SlotIndex, InventoryComponent);
	StartStorageSearch(StorageChest);
}

void ABasePlayerController::ServerQuickMoveInventorySlotToStorage_Implementation(int32 SlotIndex)
{
	// 인벤토리 -> storage
	if (!ActiveStorageChest)
	{
		return;
	}

	ABasePlayer* BasePlayer = Cast<ABasePlayer>(GetPawn());
	if (!BasePlayer)
	{
		return;
	}

	UInventoryComponent* InventoryComponent = BasePlayer->GetInventoryComponent();
	UStorageComponent* StorageComponent = ActiveStorageChest->GetStorageComponent();
	if (!InventoryComponent || !StorageComponent)
	{
		return;
	}

	if (InventoryComponent->GetCursorItem().IsValid())
	{
		InventoryComponent->ReturnCursorToOriginalSlot();
		return;
	}

	InventoryComponent->TransferSlotToStorage(SlotIndex, StorageComponent);
	StartStorageSearch(ActiveStorageChest);
}

void ABasePlayerController::ServerQuickMoveStorageSlotToInventory_Implementation(AStorageChest* StorageChest, int32 SlotIndex)
{
	//storage -> Inventory (우클릭)
	if (!StorageChest || ActiveStorageChest != StorageChest)
	{
		return;
	}

	ABasePlayer* BasePlayer = Cast<ABasePlayer>(GetPawn());
	if (!BasePlayer)
	{
		return;
	}

	UStorageComponent* StorageComponent = StorageChest->GetStorageComponent();
	UInventoryComponent* InventoryComponent = BasePlayer->GetInventoryComponent();
	if (!StorageComponent || !InventoryComponent)
	{
		return;
	}

	if (!IsStorageSlotRevealed(StorageChest, SlotIndex))
	{
		return;
	}

	InventoryComponent->ReturnCursorToOriginalSlot();
	StorageComponent->TransferSlotToInventory(SlotIndex, InventoryComponent);
	StartStorageSearch(StorageChest);
}

void ABasePlayerController::ServerCloseStorage_Implementation(AStorageChest* StorageChest)
{
	if (ActiveStorageChest == StorageChest)
	{
		ActiveStorageChest = nullptr;
		GetWorldTimerManager().ClearTimer(StorageSearchTimerHandle);
	}
}

bool ABasePlayerController::IsStorageSlotRevealed(AStorageChest* StorageChest, int32 SlotIndex) const
{
	if (!StorageChest || SlotIndex < 0)
	{
		return false;
	}

	const FStorageRevealState* RevealState = StorageRevealStates.Find(StorageChest);
	return RevealState && SlotIndex < RevealState->RevealedSlotCount;
}

bool ABasePlayerController::IsStorageSlotSearching(AStorageChest* StorageChest, int32 SlotIndex) const
{
	if (!StorageChest || SlotIndex < 0)
	{
		return false;
	}

	const FStorageRevealState* RevealState = StorageRevealStates.Find(StorageChest);
	return RevealState && RevealState->SearchingSlotIndex == SlotIndex;
}

void ABasePlayerController::OpenStorage(AStorageChest* StorageChest)
{
	// chest에 대한 storage UI열기
	if (!IsLocalController() || !StorageChest || !PlayerHUDWidget)
	{
		return;
	}

	if (IsFacilityHubOpen())
	{
		CloseFacilityHub();
	}

	// 동일한 상자 UI가 이미 열려 있으면 위젯과 입력 모드를 다시 생성하지 않는다.
	if (ActiveStorageChest == StorageChest && StorageWindowWidget)
	{
		return;
	}

	// 열려 있던 창 제거
	if (StorageWindowWidget)
	{
		PlayerHUDWidget->HideStorageWindow();
		StorageWindowWidget = nullptr;
	}

	ActiveStorageChest = StorageChest;

	PlayerHUDWidget->SetInventoryVisible(true);
	ApplyInventoryInputMode(true);

	StorageWindowWidget = PlayerHUDWidget->ShowStorageWindow(
		StorageChest,
		Cast<ABasePlayer>(GetPawn()),
		StorageWindowWidgetClass);
	if (!StorageWindowWidget)
	{
		return;
	}
}

void ABasePlayerController::CloseStorage(bool bNotifyServer)
{
	AStorageChest* ClosingStorageChest = ActiveStorageChest;

	if (StorageWindowWidget)
	{
		if (PlayerHUDWidget)
		{
			PlayerHUDWidget->HideStorageWindow();
		}
		StorageWindowWidget = nullptr;
	}

	ActiveStorageChest = nullptr;
	GetWorldTimerManager().ClearTimer(StorageSearchTimerHandle);

	if (!bNotifyServer || !ClosingStorageChest)
	{
		return;
	}

	if (HasAuthority())
	{
		ServerCloseStorage_Implementation(ClosingStorageChest);
	}
	else
	{
		ServerCloseStorage(ClosingStorageChest);
	}
}

bool ABasePlayerController::IsStorageOpen() const
{
	return StorageWindowWidget != nullptr && ActiveStorageChest != nullptr;
}

void ABasePlayerController::StartStorageSearch(AStorageChest* StorageChest)
{
	if (!HasAuthority() || !StorageChest)
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(StorageSearchTimerHandle);

	FStorageRevealState& RevealState = StorageRevealStates.FindOrAdd(StorageChest);

	if (const UStorageComponent* StorageComponent = StorageChest->GetStorageComponent())
	{
		const TArray<FInventorySlot>& Slots = StorageComponent->GetSlots();
		const int32 SlotCount = StorageComponent->GetSlotCount();

		while (RevealState.RevealedSlotCount < SlotCount)
		{
			if (Slots.IsValidIndex(RevealState.RevealedSlotCount) && !Slots[RevealState.RevealedSlotCount].IsEmpty())
			{
				break;
			}

			++RevealState.RevealedSlotCount;
		}
	}

	RevealState.SearchingSlotIndex = FindNextUnrevealedStorageSlot(StorageChest);
	NotifyStorageRevealState(StorageChest);

	if (RevealState.SearchingSlotIndex == INDEX_NONE)
	{
		return;
	}

	const float SearchTime = GetStorageSlotSearchTime(StorageChest, RevealState.SearchingSlotIndex);
	if (SearchTime <= 0.0f)
	{
		RevealCurrentStorageSlot();
		return;
	}

	GetWorldTimerManager().SetTimer(StorageSearchTimerHandle, this, &ABasePlayerController::RevealCurrentStorageSlot, SearchTime, false);
}

void ABasePlayerController::RevealCurrentStorageSlot()
{
	if (!HasAuthority() || !ActiveStorageChest)
	{
		return;
	}

	FStorageRevealState* RevealState = StorageRevealStates.Find(ActiveStorageChest);
	if (!RevealState || RevealState->SearchingSlotIndex == INDEX_NONE)
	{
		StartStorageSearch(ActiveStorageChest);
		return;
	}

	RevealState->RevealedSlotCount = FMath::Max(RevealState->RevealedSlotCount, RevealState->SearchingSlotIndex + 1);
	RevealState->SearchingSlotIndex = INDEX_NONE;
	NotifyStorageRevealState(ActiveStorageChest);

	StartStorageSearch(ActiveStorageChest);
}

void ABasePlayerController::NotifyStorageRevealState(AStorageChest* StorageChest)
{
	if (!StorageChest)
	{
		return;
	}

	const FStorageRevealState* RevealState = StorageRevealStates.Find(StorageChest);
	if (!RevealState)
	{
		return;
	}

	ClientUpdateStorageRevealState(StorageChest, RevealState->RevealedSlotCount, RevealState->SearchingSlotIndex);
}

int32 ABasePlayerController::FindNextUnrevealedStorageSlot(AStorageChest* StorageChest) const
{
	if (!StorageChest)
	{
		return INDEX_NONE;
	}

	const UStorageComponent* StorageComponent = StorageChest->GetStorageComponent();
	if (!StorageComponent)
	{
		return INDEX_NONE;
	}

	const FStorageRevealState* RevealState = StorageRevealStates.Find(StorageChest);
	const int32 RevealedSlotCount = RevealState ? RevealState->RevealedSlotCount : 0;
	const TArray<FInventorySlot>& Slots = StorageComponent->GetSlots();

	for (int32 Index = RevealedSlotCount; Index < Slots.Num(); ++Index)
	{
		if (!Slots[Index].IsEmpty())
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

float ABasePlayerController::GetStorageSlotSearchTime(AStorageChest* StorageChest, int32 SlotIndex) const
{
	if (!StorageChest)
	{
		return CommonSearchTime;
	}

	const UStorageComponent* StorageComponent = StorageChest->GetStorageComponent();
	if (!StorageComponent)
	{
		return CommonSearchTime;
	}

	const TArray<FInventorySlot>& Slots = StorageComponent->GetSlots();
	if (!Slots.IsValidIndex(SlotIndex) || Slots[SlotIndex].IsEmpty())
	{
		return CommonSearchTime;
	}

	switch (StorageComponent->GetItemRarityRank(Slots[SlotIndex].ItemTag))
	{
	case 1:
		return CommonSearchTime;
	case 2:
		return RelicSearchTime;
	case 3:
		return RareSearchTime;
	case 4:
		return EpicSearchTime;
	case 5:
		return LegendarySearchTime;
	default:
		return CommonSearchTime;
	}
}

void ABasePlayerController::ApplyInventoryInputMode(bool bOpen)
{
	bShowMouseCursor = bOpen;

	if (bOpen)
	{
		// 게임 입력, UI 입력 모두 받을 수 있는 InputMode
		FInputModeGameAndUI InputMode;
		// 클릭 및 드래그 할 때 커서 숨기지 않음
		InputMode.SetHideCursorDuringCapture(false);
		// 게임 화면 안에 마우스 가두지 않음
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		// playercontroller에 적용
		SetInputMode(InputMode);

		// 캐릭터나 카메라 회전 막기 true
		if (!bInventoryInputModeApplied)
		{
			SetIgnoreLookInput(true);
			bInventoryInputModeApplied = true;
		}
		// 이동 입력 가능하게 설정
	}
	else
	{
		FInputModeGameOnly InputMode;
		// Closing a UI must not consume the first mouse-down just to recapture the viewport.
		// Forward that click to gameplay as well (attack/interact inputs use mouse buttons).
		InputMode.SetConsumeCaptureMouseDown(false);
		SetInputMode(InputMode);
		UWidgetBlueprintLibrary::SetFocusToGameViewport();
		FlushPressedKeys();

		if (bInventoryInputModeApplied)
		{
			SetIgnoreLookInput(false);
			bInventoryInputModeApplied = false;
		}
	}
}

void ABasePlayerController::SetStatusCharacterInputLocked(bool bLocked)
{
	if (bLocked)
	{
		if (bStatusCharacterInputLocked)
		{
			return;
		}

		APawn* ControlledPawn = GetPawn();
		if (!ControlledPawn)
		{
			return;
		}

		StatusInputLockedPawn = ControlledPawn;
		bWasStatusPawnInputEnabled = ControlledPawn->InputEnabled();
		ControlledPawn->DisableInput(this);
		SetIgnoreMoveInput(true);
		bStatusCharacterInputLocked = true;
		return;
	}

	if (!bStatusCharacterInputLocked)
	{
		return;
	}

	if (StatusInputLockedPawn.IsValid() && bWasStatusPawnInputEnabled)
	{
		StatusInputLockedPawn->EnableInput(this);
	}
	SetIgnoreMoveInput(false);
	StatusInputLockedPawn.Reset();
	bStatusCharacterInputLocked = false;
}

void ABasePlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (GetWorld())
	{
		if (AGameStateBase* GameState = GetWorld()->GetGameState())
		{
			float CurrentServerTime = GameState->GetServerWorldTimeSeconds();
			if (UWaterSubsystem* WaterSubsystem = UWaterSubsystem::GetWaterSubsystem(GetWorld()))
			{
				WaterSubsystem->SetShouldOverrideSmoothedWorldTimeSeconds(true);
				WaterSubsystem->SetOverrideSmoothedWorldTimeSeconds(CurrentServerTime);
				WaterSubsystem->SetSmoothedWorldTimeSeconds(CurrentServerTime);
			}
		}
	}
}
