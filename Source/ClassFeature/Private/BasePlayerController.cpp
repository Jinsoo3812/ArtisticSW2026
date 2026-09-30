// Fill out your copyright notice in the Description page of Project Settings.


#include "BasePlayerController.h"
#include "UI/SWDeathFlowWidget.h"
#include "Room/SWRoomReadyState.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BaseHealthComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AbilitySystemComponent.h"
#include "PlayerDialogueComponent.h"
#include "Cannon.h"
#include "Net/UnrealNetwork.h"
#include "BasePlayer.h"
#include "BasePlayerState.h"
#include "UI/PlayerHUDWidget.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "TimerManager.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "BaseGameplayTags.h"
#include "Attacker/AttackerComponent.h"
#include "Inventory/InventoryComponent.h"
#include "Storage/StorageChest.h"
#include "Storage/StorageInteractionDiagnostics.h"
#include "Storage/SharedStorageChest.h"
#include "Storage/StorageComponent.h"
#include "UI/StorageWindowWidget.h"
#include "UI/FacilityHubWidget.h"
#include "Facility/FacilityHubActor.h"
#include "UI/StatusWindowWidget.h"
#include "UI/SWRoomMenuWidget.h"
#include "Room/ClassFeatureRoomProgressSubsystem.h"
#include "Room/SWRoomProgressSubsystem.h"
#include "Room/SWRoomSnapshotSubsystem.h"
#include "Network/SWNetworkLog.h"
#include "Network/SWConnectionSubsystem.h"
#include "MultiGameMode.h"
#include "InputAction.h"
#include "WaterSubsystem.h"
#include "GameFramework/GameStateBase.h"
#include "Upgrade/SharedShipUpgradeState.h"
#include "Upgrade/ShipUpgradeComponent.h"
#include "Upgrade/ShipUpgradeTreeDataAsset.h"
#include "Ship.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Network/Lobby/SWRoomSubsystem.h"
#include "HAL/PlatformMisc.h"

void ABasePlayerController::ClientBeginRoomReturn_Implementation()
{
	USWConnectionSubsystem* Connection = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWConnectionSubsystem>() : nullptr;
	if (Connection && Connection->BeginRoomReturnPresentation()) ServerConfirmRoomReturnPresentation();
}

void ABasePlayerController::ClientBeginFinalDeparture_Implementation(int32 AttemptId)
{
	USWConnectionSubsystem* Connection = GetGameInstance()
		? GetGameInstance()->GetSubsystem<USWConnectionSubsystem>() : nullptr;
	if (Connection && Connection->BeginRoomFinalDeparturePresentation(AttemptId))
		ServerConfirmRoomReturnPresentation();
}

void ABasePlayerController::ClientCancelRoomReturn_Implementation()
{
	if (USWConnectionSubsystem* Connection = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWConnectionSubsystem>() : nullptr)
		Connection->CancelRoomReturnPresentation();
}

void ABasePlayerController::ServerConfirmRoomReturnPresentation_Implementation()
{
	if (UClassFeatureRoomProgressSubsystem* Progress = GetGameInstance() ? GetGameInstance()->GetSubsystem<UClassFeatureRoomProgressSubsystem>() : nullptr)
		Progress->ConfirmReturnPresentation(this);
}


void ABasePlayerController::OpenFacilityHubFromServer(AActor* ContextActor)
{
	if (!CanMutateGameplay()) return;
	AFacilityHubActor* FacilityHub = Cast<AFacilityHubActor>(ContextActor);
	if (!HasAuthority() || !IsValid(FacilityHub) || !FacilityHub->TryAcquire(this))
	{
		/* UE_LOG(LogTemp, Warning,
			TEXT("[FacilityHubFlow][SERVER] Open rejected. Controller=%s Authority=%s Context=%s ContextClass=%s"),
			*GetNameSafe(this),
			HasAuthority() ? TEXT("YES") : TEXT("NO"),
			*GetNameSafe(ContextActor),
			*GetNameSafe(ContextActor ? ContextActor->GetClass() : nullptr)); */
		return;
	}
	ActiveFacilityHub = FacilityHub;

	// The shared upgrade state is session infrastructure, so opening the server-
	// authoritative facility must guarantee it exists. Ship BeginPlay remains an
	// early registration path, but is no longer the single point of failure.
	ASharedShipUpgradeState* SharedState = ASharedShipUpgradeState::Find(this);
	if (!SharedState)
	{
		SharedState = GetWorld()->SpawnActor<ASharedShipUpgradeState>();
		UE_LOG(LogTemp, Warning,
			TEXT("[ShipUpgradePipeline][FacilityEnsureState] Controller=%s Action=Spawn State=%s"),
			*GetNameSafe(this), *GetNameSafe(SharedState));
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ShipUpgradePipeline][FacilityEnsureState] Controller=%s Action=Reuse State=%s Ship=%s"),
			*GetNameSafe(this),
			*GetNameSafe(SharedState),
			*GetNameSafe(SharedState->GetCurrentPlayerShip()));
	}

	// Preserve the designer-authored tree configured on BP_BasePlayerState. The
	// shared component replicates that asset reference to both clients; its own
	// hard-coded load is only a last-resort fallback for unconfigured test worlds.
	if (SharedState)
	{
		UShipUpgradeComponent* SharedUpgrade = SharedState->GetUpgradeComponent();
		const ABasePlayer* RequestingPlayer = Cast<ABasePlayer>(GetPawn());
		const ABasePlayerState* RequestingState = GetPlayerState<ABasePlayerState>();
		const UShipUpgradeComponent* ConfiguredUpgrade = RequestingState
			? RequestingState->GetShipUpgradeComponent()
			: nullptr;
		if (SharedUpgrade && ConfiguredUpgrade && ConfiguredUpgrade->UpgradeTree)
		{
			SharedUpgrade->ConfigureForUseCase(
				ConfiguredUpgrade->UpgradeTree,
				SharedUpgrade->PreviewBaseStats,
				false);
			SharedState->ForceNetUpdate();
			UE_LOG(LogTemp, Warning,
				TEXT("[ShipUpgradePipeline][FacilityConfigureTree] State=%s Source=%s Tree=%s Nodes=%d"),
				*GetNameSafe(SharedState),
				*GetNameSafe(ConfiguredUpgrade),
				*GetNameSafe(ConfiguredUpgrade->UpgradeTree.Get()),
				ConfiguredUpgrade->UpgradeTree->Nodes.Num());
		}
		if (SharedUpgrade)
		{
			SharedUpgrade->SetIgnoreMaterialCostsForTesting(
				RequestingPlayer && RequestingPlayer->IsIgnoringShipUpgradeMaterialCostsForTest());
			SharedState->ForceNetUpdate();
		}
	}

	if (SharedState && !IsValid(SharedState->GetCurrentPlayerShip()))
	{
		AShip* FoundPlayerShip = nullptr;
		for (TActorIterator<AShip> It(GetWorld()); It; ++It)
		{
			AShip* Candidate = *It;
			if (IsValid(Candidate) && Candidate->GetIsReplicated() && !Candidate->IsEnemyShipForEffects())
			{
				FoundPlayerShip = Candidate;
				break;
			}
		}
		if (FoundPlayerShip)
		{
			SharedState->RegisterPlayerShip(FoundPlayerShip);
		}
		UE_LOG(LogTemp, Warning,
			TEXT("[ShipUpgradePipeline][FacilityEnsureShip] State=%s FoundShip=%s"),
			*GetNameSafe(SharedState), *GetNameSafe(FoundPlayerShip));
	}

	/* UE_LOG(LogTemp, Log,
		TEXT("[FacilityHubFlow][SERVER] Context validated; sending ClientOpenFacilityHub. Context=%s"),
		*GetNameSafe(ContextActor)); */
	ClientOpenFacilityHub(ContextActor);
}

void ABasePlayerController::ClientOpenFacilityHub_Implementation(AActor* ContextActor)
{
	/* UE_LOG(LogTemp, Log,
		TEXT("[FacilityHubFlow][CLIENT] Open RPC received. Controller=%s Local=%s Context=%s"),
		*GetNameSafe(this),
		IsLocalController() ? TEXT("YES") : TEXT("NO"),
		*GetNameSafe(ContextActor)); */

	if (!IsLocalController() || !IsValid(ContextActor))
	{
		/* UE_LOG(LogTemp, Error,
			TEXT("[FacilityHubFlow][CLIENT] FAILED: Invalid local controller or context.")); */
		return;
	}
	ActiveFacilityHub = Cast<AFacilityHubActor>(ContextActor);

	// Interacting with a facility while its hub is already open is a toggle:
	// close the current hub and do not immediately construct a replacement.
	if (IsFacilityHubOpen())
	{
		CloseFacilityHub();
		return;
	}

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
		/* UE_LOG(LogTemp, Error,
			TEXT("[FacilityHubFlow][CLIENT] FAILED: Neither WBP_WorkspaceScreen nor WBP_FacilityHub could be loaded.")); */
		if (PlayerHUDWidget)
		{
			PlayerHUDWidget->SetVisibility(PlayerHUDVisibilityBeforeFacilityHub);
		}
		ApplyInventoryInputMode(false);
		if (ActiveFacilityHub)
		{
			ServerReleaseFacilityHub(ActiveFacilityHub);
			ActiveFacilityHub = nullptr;
		}
		return;
	}

	/* UE_LOG(LogTemp, Log,
		TEXT("[FacilityHubFlow][CLIENT] Widget class resolved. Class=%s"),
		*GetNameSafe(WidgetClass.Get())); */

	FacilityHubWidget = CreateWidget<UFacilityHubWidget>(this, WidgetClass);
	if (!FacilityHubWidget)
	{
		/* UE_LOG(LogTemp, Error,
			TEXT("[FacilityHubFlow][CLIENT] FAILED: CreateWidget returned null. Class=%s"),
			*GetNameSafe(WidgetClass.Get())); */
		if (PlayerHUDWidget)
		{
			PlayerHUDWidget->SetVisibility(PlayerHUDVisibilityBeforeFacilityHub);
		}
		ApplyInventoryInputMode(false);
		if (ActiveFacilityHub)
		{
			ServerReleaseFacilityHub(ActiveFacilityHub);
			ActiveFacilityHub = nullptr;
		}
		return;
	}

	FacilityHubWidget->InitializeForContext(ContextActor);
	FacilityHubWidget->AddToViewport(100);
	ApplyInventoryInputMode(true);
	FacilityHubWidget->SetUserFocus(this);
	/* UE_LOG(LogTemp, Log,
		TEXT("[FacilityHubFlow][CLIENT] SUCCESS: Common FacilityHub added to viewport. Widget=%s Context=%s"),
		*GetNameSafe(FacilityHubWidget),
		*GetNameSafe(ContextActor)); */
}

void ABasePlayerController::CloseFacilityHub()
{
	if (!IsLocalController() || !FacilityHubWidget)
	{
		return;
	}

	FacilityHubWidget->PrepareToClose();
	/* UE_LOG(LogTemp, Log,
		TEXT("[FacilityHubFlow][CLIENT] Closing FacilityHub. Widget=%s"),
		*GetNameSafe(FacilityHubWidget)); */
	FacilityHubWidget->RemoveFromParent();
	FacilityHubWidget = nullptr;
	if (ActiveFacilityHub)
	{
		ServerReleaseFacilityHub(ActiveFacilityHub);
		ActiveFacilityHub = nullptr;
	}
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

void ABasePlayerController::ServerReleaseFacilityHub_Implementation(AFacilityHubActor* FacilityHub)
{
	if (IsValid(FacilityHub))
	{
		FacilityHub->Release(this);
	}
	if (ActiveFacilityHub == FacilityHub)
	{
		ActiveFacilityHub = nullptr;
	}
}

void ABasePlayerController::ServerRequestActivateSharedShipUpgrade_Implementation(
	ASharedShipUpgradeState* SharedState,
	FName NodeId)
{
	if (!CanMutateGameplay()) return;
	UShipUpgradeComponent* SharedUpgrade = IsValid(SharedState)
		&& SharedState == ASharedShipUpgradeState::Find(this)
		&& IsValid(ActiveFacilityHub)
		&& ActiveFacilityHub->IsOccupiedBy(this)
		? SharedState->GetUpgradeComponent()
		: nullptr;
	ABasePlayer* RequestingPlayer = Cast<ABasePlayer>(GetPawn());
	UInventoryComponent* Inventory = RequestingPlayer
		? RequestingPlayer->GetInventoryComponent()
		: nullptr;

	EShipUpgradeActivationResult Result = EShipUpgradeActivationResult::NotAuthority;
	if (SharedUpgrade && Inventory)
	{
		const bool bIgnoreCosts = RequestingPlayer->IsIgnoringShipUpgradeMaterialCostsForTest();
		UE_LOG(LogTemp, Warning,
			TEXT("[ShipUpgradeTrace][SharedRequestOptions] Player=%s IgnoreMaterialCosts=%s"),
			*GetNameSafe(RequestingPlayer),
			bIgnoreCosts ? TEXT("true") : TEXT("false"));
		Result = SharedUpgrade->ActivateNodeWithInventoryProvider(NodeId, Inventory, bIgnoreCosts);
		SharedState->ForceNetUpdate();
		if (AShip* Ship = SharedState->GetCurrentPlayerShip())
		{
			Ship->ForceNetUpdate();
		}
	}

	const FText Message = SharedUpgrade
		? SharedUpgrade->GetActivationMessage(NodeId, Result)
		: NSLOCTEXT("ShipUpgrade", "SharedRequestRejected", "작업대 사용 권한 또는 공용 배 상태를 확인할 수 없습니다.");
	ClientReceiveSharedShipUpgradeResult(SharedState, NodeId, Result, Message);
}

void ABasePlayerController::ClientReceiveSharedShipUpgradeResult_Implementation(
	ASharedShipUpgradeState* SharedState,
	FName NodeId,
	EShipUpgradeActivationResult Result,
	const FText& Message)
{
	if (IsValid(SharedState) && SharedState->GetUpgradeComponent())
	{
		SharedState->GetUpgradeComponent()->NotifyActivationResult(NodeId, Result, Message);
	}
}


void ABasePlayerController::BeginPlay()
{
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	Super::BeginPlay();
	if (IsLocalController() && GetWorld() && GetWorld()->WorldType == EWorldType::Game)
	{
		if (UGameViewportClient* Viewport = GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr)
		{
			BoundRoomViewport = Viewport;
			PreviousWindowCloseRequested = Viewport->OnWindowCloseRequested();
			Viewport->OnWindowCloseRequested().BindUObject(this, &ABasePlayerController::HandleRoomWindowCloseRequested);
		}
	}

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

void ABasePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGameViewportClient* Viewport = BoundRoomViewport.Get())
	{
		if (Viewport->OnWindowCloseRequested().IsBoundToObject(this))
			Viewport->OnWindowCloseRequested() = PreviousWindowCloseRequested;
	}
	BoundRoomViewport.Reset();
	if (GetWorld()) GetWorldTimerManager().ClearTimer(RoomSaveTimeoutHandle);
	if (HasAuthority() && ActiveFacilityHub)
	{
		ActiveFacilityHub->Release(this);
		ActiveFacilityHub = nullptr;
	}
	if (DeathFlowWidget) { DeathFlowWidget->RemoveFromParent(); DeathFlowWidget = nullptr; }
 if (DeathCamera) { DeathCamera->Destroy(); DeathCamera = nullptr; }
 if (bDeathInputLocked) { SetIgnoreMoveInput(false); SetIgnoreLookInput(false); bDeathInputLocked = false; }
 bCameraPublishing = false; LifeCharacter.Reset();
 if (HasAuthority()) if (UClassFeatureRoomProgressSubsystem* Room = GetGameInstance()->GetSubsystem<UClassFeatureRoomProgressSubsystem>()) Room->HandleTransitionLogout(this);
 Super::EndPlay(EndPlayReason);
}

void ABasePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent))
	{
		RoomMenuAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_RoomMenu.IA_RoomMenu"));
		if (RoomMenuAction) EnhancedInput->BindAction(RoomMenuAction, ETriggerEvent::Started, this, &ABasePlayerController::HandleMenuEscape);
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

	InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &ABasePlayerController::ToggleInventory);
}

void ABasePlayerController::OnUIInputPressed(FGameplayTag InputTag)
{
	if (!CanMutateGameplay()) return;
	// [클라/로컬]
	if (!IsLocalController() || !PlayerHUDWidget)
	{
		return;
	}

	if (InputTag.MatchesTagExact(Key_UI_I))
	{		
		ToggleStatus();
	}
}

void ABasePlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (ABasePlayer* LifePawnCharacter = Cast<ABasePlayer>(InPawn)) LifeCharacter = LifePawnCharacter;
	if (HasAuthority() && ActiveFacilityHub)
	{
		ActiveFacilityHub->Release(this);
		ActiveFacilityHub = nullptr;
	}
	if (IsLocalController() && IsFacilityHubOpen())
	{
		CloseFacilityHub();
	}
	BindHUDToCurrentPlayer();
}

void ABasePlayerController::OnRep_Pawn()
{
	Super::OnRep_Pawn();
	if (IsFacilityHubOpen())
	{
		CloseFacilityHub();
	}
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
	if (!CanMutateGameplay()) return;
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
	if (!CanMutateGameplay()) return;
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
	if (!CanMutateGameplay()) return;
	if (IsFacilityHubOpen())
	{
		CloseFacilityHub();
		return;
	}
	if (IsStorageOpen()) { CloseStorage(); return; }
	if (StatusWindowWidget && StatusWindowWidget->IsStatusVisible()) { ToggleStatus(); return; }
	if (PlayerHUDWidget && PlayerHUDWidget->IsInventoryVisible()) { ToggleInventory(); return; }
	if (RoomMenuWidget) { CloseRoomMenu(); return; }
	if (!IsLocalController()) return;
	UClass* RoomMenuClass = LoadClass<USWRoomMenuWidget>(nullptr, TEXT("/Game/UI/Room/WBP_RoomMenu.WBP_RoomMenu_C"));
	RoomMenuWidget = CreateWidget<USWRoomMenuWidget>(this, RoomMenuClass ? RoomMenuClass : USWRoomMenuWidget::StaticClass());
	if (!RoomMenuWidget) return;
	bCursorVisibleBeforeRoomMenu = bShowMouseCursor;
	RoomMenuWidget->AddToViewport(50);
	ApplyInventoryInputMode(true);
}

void ABasePlayerController::CloseRoomMenu()
{
	if (!RoomMenuWidget) return;
	RoomMenuWidget->RemoveFromParent();
	RoomMenuWidget = nullptr;
	ApplyInventoryInputMode(false);
	bShowMouseCursor = bCursorVisibleBeforeRoomMenu;
}

void ABasePlayerController::RequestRoomSave()
{
	if (!IsLocalController() || bRoomSavePending) return;
	UE_LOG(LogSWRoom, Display, TEXT("Flow=ManualSave Side=Client Phase=ButtonPressed"));
	bRoomSavePending = true;
	PendingRoomSaveRequestId = ++NextRoomSaveRequestId;
	if (RoomMenuWidget) RoomMenuWidget->SetBusy(true);
	GetWorldTimerManager().SetTimer(RoomSaveTimeoutHandle, this, &ABasePlayerController::HandleRoomSaveTimeout, 30.f, false);
	ServerRequestRoomSave(PendingRoomSaveRequestId);
}

void ABasePlayerController::RequestRoomSaveAndExit()
{
	if (!IsLocalController()) return;
	bExitAfterRoomSave = true;
	if (!bRoomSavePending) RequestRoomSave();
}

bool ABasePlayerController::HandleRoomWindowCloseRequested()
{
	const USWRoomSubsystem* Room = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWRoomSubsystem>() : nullptr;
	if (Room && Room->GetRoomState() == ESWRoomState::Playing)
	{
		RequestRoomSaveAndExit();
		return false;
	}
	return !PreviousWindowCloseRequested.IsBound() || PreviousWindowCloseRequested.Execute();
}

void ABasePlayerController::HandleRoomSaveTimeout()
{
	if (!bRoomSavePending) return;
	UE_LOG(LogSWRoom, Error, TEXT("Flow=ManualSave Side=Client Result=Timeout RequestId=%llu"), PendingRoomSaveRequestId);
	bRoomSavePending = false;
	bExitAfterRoomSave = false;
	const FString Message = TEXT("방 저장 응답이 30초 안에 오지 않아 종료를 취소했습니다.");
	if (RoomMenuWidget) RoomMenuWidget->SetResult(Message);
	else ClientMessage(Message);
}

void ABasePlayerController::ServerRequestRoomSave_Implementation(uint64 RequestId)
{
	UE_LOG(LogSWRoom, Display, TEXT("Flow=ManualSave Side=Server Phase=RpcReceived RequestId=%llu"), RequestId);
	if (RequestId == LastServerRoomSaveRequestId)
	{
		ClientRoomSaveResult(RequestId, bLastServerRoomSaveSuccess, LastServerRoomSaveMessage);
		return;
	}
	LastServerRoomSaveRequestId = RequestId;
	AMultiGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AMultiGameMode>() : nullptr;
	USWRoomProgressSubsystem* Room = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>() : nullptr;
	UClassFeatureRoomProgressSubsystem* Progress = GetGameInstance() ? GetGameInstance()->GetSubsystem<UClassFeatureRoomProgressSubsystem>() : nullptr;
	if (!Mode || !Room || !Room->IsHostedRoom() || Room->IsNewRoomPending()
		|| Room->IsReturnTravelPending() || Room->IsFinalDepartureTravelPending()
		|| !PlayerState || !Progress)
	{
		bLastServerRoomSaveSuccess = false;
		LastServerRoomSaveMessage = TEXT("방 저장을 요청할 수 없습니다.");
		UE_LOG(LogSWRoom, Warning, TEXT("Flow=ManualSave Side=Server Result=Rejected"));
		ClientRoomSaveResult(RequestId, false, LastServerRoomSaveMessage);
		return;
	}
	FString Error;
	const bool bSaved = Progress->TrySave(GetWorld(), ESWRoomSaveKind::Manual, Error);
	if (!bSaved)
	{
		bLastServerRoomSaveSuccess = false;
		LastServerRoomSaveMessage = Error;
		UE_LOG(LogSWRoom, Error, TEXT("Flow=ManualSave Side=Server Result=Failed Reason=%s"), *Error);
		ClientRoomSaveResult(RequestId, false, Error);
		return;
	}
	const USWRoomSnapshotSubsystem* Snapshot = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>();
	const int32 Unsupported = Snapshot ? Snapshot->GetUnsupportedCandidateCount() : 0;
	const int32 Missing = Progress->GetLastCaptureIssueCount();
	bLastServerRoomSaveSuccess = true;
	LastServerRoomSaveMessage = FString::Printf(TEXT("저장 완료 · 누락 %d건 · 지원 범위 밖 상태 %d건"), Missing, Unsupported);
	ClientRoomSaveResult(RequestId, true, LastServerRoomSaveMessage);
	UE_LOG(LogSWRoom, Display, TEXT("Flow=ManualSave Side=Server Result=Success Unsupported=%d"), Unsupported);
}

void ABasePlayerController::ClientRoomSaveResult_Implementation(uint64 RequestId, bool bSuccess, const FString& Message)
{
	if (!bRoomSavePending || RequestId != PendingRoomSaveRequestId) return;
	UE_LOG(LogSWRoom, Display, TEXT("Flow=ManualSave Side=Client Result=%s RequestId=%llu Message=%s"), bSuccess ? TEXT("Success") : TEXT("Failed"), RequestId, *Message);
	GetWorldTimerManager().ClearTimer(RoomSaveTimeoutHandle);
	bRoomSavePending = false;
	if (RoomMenuWidget) RoomMenuWidget->SetResult(Message);
	else ClientMessage(Message);
	if (!bSuccess) { bExitAfterRoomSave = false; return; }
	if (bExitAfterRoomSave)
	{
		bExitAfterRoomSave = false;
		if (USWRoomSubsystem* Room = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWRoomSubsystem>() : nullptr)
			Room->LeaveRoom();
		FPlatformMisc::RequestExit(false);
	}
}

void ABasePlayerController::OpenStorageFromServer(AStorageChest* StorageChest)
{
	if (!CanMutateGameplay()) return;
	const bool bLogInteraction = IsStorageInteractionLoggingEnabled();
	if (bLogInteraction)
	{
		UE_LOG(LogStorageInteraction, Warning,
			TEXT("[StorageServer] Open request. Controller=%s Authority=%d Chest=%s Valid=%d Locked=%d Access=%d Active=%s"),
			*GetNameSafe(this), HasAuthority(), *GetNameSafe(StorageChest), IsValid(StorageChest),
			IsValid(StorageChest) && StorageChest->IsLocked(), CanAccessStorage(StorageChest), *GetNameSafe(ActiveStorageChest));
	}
	if (!HasAuthority() || !CanAccessStorage(StorageChest))
	{
		if (bLogInteraction) UE_LOG(LogStorageInteraction, Warning, TEXT("[StorageServer] Rejected: authority or access check failed."));
		return;
	}

	// 이미 열려 있는 동일한 상자에는 중복 열기 요청을 보내지 않는다.
	if (ActiveStorageChest == StorageChest)
	{
		if (bLogInteraction) UE_LOG(LogStorageInteraction, Warning, TEXT("[StorageServer] Same chest already active; toggling closed."));
		CloseStorageFromServer(StorageChest);
		return;
	}

	if (ABasePlayer* StoragePlayer = Cast<ABasePlayer>(GetPawn()))
		if (UInventoryComponent* Inventory = StoragePlayer->GetInventoryComponent()) Inventory->ReturnCursorToOriginalSlot();
	ActiveStorageChest = StorageChest;
	StartStorageSearch(StorageChest);
	if (bLogInteraction) UE_LOG(LogStorageInteraction, Warning, TEXT("[StorageServer] Sending ClientOpenStorage. Chest=%s"), *GetNameSafe(StorageChest));
	ClientOpenStorage(StorageChest);
}

void ABasePlayerController::CloseStorageFromServer(AStorageChest* StorageChest)
{
	if (IsStorageInteractionLoggingEnabled())
	{
		UE_LOG(LogStorageInteraction, Warning,
			TEXT("[StorageServer] Close request. Controller=%s Chest=%s Active=%s Authority=%d"),
			*GetNameSafe(this), *GetNameSafe(StorageChest), *GetNameSafe(ActiveStorageChest), HasAuthority());
	}
	if (!HasAuthority() || !StorageChest || ActiveStorageChest != StorageChest)
	{
		return;
	}

	if (ABasePlayer* StoragePlayer = Cast<ABasePlayer>(GetPawn()))
		if (UInventoryComponent* Inventory = StoragePlayer->GetInventoryComponent()) Inventory->ReturnCursorToOriginalSlot();
	ClientCloseStorage(StorageChest);
	ActiveStorageChest = nullptr;
	GetWorldTimerManager().ClearTimer(StorageSearchTimerHandle);
}

void ABasePlayerController::ClientOpenStorage_Implementation(AStorageChest* StorageChest)
{
	if (IsStorageInteractionLoggingEnabled())
	{
		UE_LOG(LogStorageInteraction, Warning,
			TEXT("[StorageClient] Open RPC received. Chest=%s Valid=%d Locked=%d"),
			*GetNameSafe(StorageChest), IsValid(StorageChest), IsValid(StorageChest) && StorageChest->IsLocked());
	}
	if (StorageChest && !StorageChest->IsLocked())
	{
		OpenStorage(StorageChest);
	}
}

void ABasePlayerController::ClientCloseStorage_Implementation(AStorageChest* StorageChest)
{
	if (IsStorageInteractionLoggingEnabled())
	{
		UE_LOG(LogStorageInteraction, Warning, TEXT("[StorageClient] Close RPC received. Chest=%s Active=%s"),
			*GetNameSafe(StorageChest), *GetNameSafe(ActiveStorageChest));
	}
	if (ActiveStorageChest == StorageChest)
	{
		CloseStorage(false);
	}
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
	if (!CanMutateGameplay()) return;
	// 좌클릭 했을 때 상호작용
	// 커서에 아이템이 붙어 있으면 인벤토리 -> storage
	// 커서에 아이템이 없으면 storage -> 인벤토리
	if (!CanAccessStorage(StorageChest) || ActiveStorageChest != StorageChest)
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

	StorageComponent->PickUpSlotToCursor(SlotIndex, InventoryComponent);
	StartStorageSearch(StorageChest);
}

void ABasePlayerController::ServerQuickMoveInventorySlotToStorage_Implementation(int32 SlotIndex)
{
	if (!CanMutateGameplay()) return;
	// 인벤토리 -> storage
	if (!CanAccessStorage(ActiveStorageChest))
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
	if (!CanMutateGameplay()) return;
	//storage -> Inventory (우클릭)
	if (!CanAccessStorage(StorageChest) || ActiveStorageChest != StorageChest)
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

	if (InventoryComponent->GetCursorItem().IsValid()) { InventoryComponent->ReturnCursorToOriginalSlot(); return; }
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

	if (StorageChest->IsA<ASharedStorageChest>()) return StorageChest->GetStorageComponent()->GetSlots().IsValidIndex(SlotIndex);
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
	const bool bLogInteraction = IsStorageInteractionLoggingEnabled();
	if (bLogInteraction)
	{
		UE_LOG(LogStorageInteraction, Warning,
			TEXT("[StorageClient] OpenStorage. Local=%d Chest=%s Access=%d HUD=%s WidgetClass=%s Active=%s ExistingWidget=%s"),
			IsLocalController(), *GetNameSafe(StorageChest), CanAccessStorage(StorageChest),
			*GetNameSafe(PlayerHUDWidget), *GetNameSafe(StorageWindowWidgetClass.Get()),
			*GetNameSafe(ActiveStorageChest), *GetNameSafe(StorageWindowWidget));
	}
	// chest에 대한 storage UI열기
	if (!IsLocalController() || !CanAccessStorage(StorageChest) || !PlayerHUDWidget)
	{
		if (bLogInteraction) UE_LOG(LogStorageInteraction, Warning, TEXT("[StorageClient] Rejected: local controller, access, or HUD check failed."));
		return;
	}

	if (IsFacilityHubOpen())
	{
		CloseFacilityHub();
	}

	// 동일한 상자 UI가 이미 열려 있으면 위젯과 입력 모드를 다시 생성하지 않는다.
	if (ActiveStorageChest == StorageChest && StorageWindowWidget)
	{
		if (bLogInteraction) UE_LOG(LogStorageInteraction, Warning, TEXT("[StorageClient] Existing widget for same chest; skipped."));
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
		if (bLogInteraction) UE_LOG(LogStorageInteraction, Warning, TEXT("[StorageClient] ShowStorageWindow returned null. HUD=%s"), *GetNameSafe(PlayerHUDWidget));
		return;
	}
	if (bLogInteraction) UE_LOG(LogStorageInteraction, Warning, TEXT("[StorageClient] Storage widget opened. Widget=%s Visibility=%d"),
		*GetNameSafe(StorageWindowWidget), static_cast<int32>(StorageWindowWidget->GetVisibility()));
	UpdateInteractionMovementLock();
}

void ABasePlayerController::CloseStorage(bool bNotifyServer)
{
	AStorageChest* ClosingStorageChest = ActiveStorageChest;
	if (ABasePlayer* StoragePlayer = Cast<ABasePlayer>(GetPawn()))
		if (UInventoryComponent* Inventory = StoragePlayer->GetInventoryComponent()) Inventory->ServerHandleRightClickInventory();

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

	if (PlayerHUDWidget)
	{
		PlayerHUDWidget->SetInventoryVisible(false);
	}
	ApplyInventoryInputMode(false);

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

bool ABasePlayerController::CloseActiveInteractionWindow()
{
	if (!IsLocalController()) return false;
	if (IsFacilityHubOpen())
	{
		CloseFacilityHub();
		return true;
	}
	if (IsStorageOpen())
	{
		CloseStorage();
		return true;
	}
	return false;
}

void ABasePlayerController::StartStorageSearch(AStorageChest* StorageChest)
{
	if (StorageChest && StorageChest->IsA<ASharedStorageChest>()) return;
	if (!HasAuthority() || !CanAccessStorage(StorageChest))
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
	if (!HasAuthority() || !CanAccessStorage(ActiveStorageChest))
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
	// A chest and the facility hub are modal; keep F/game input available for closing.
	UpdateInteractionMovementLock();

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

void ABasePlayerController::UpdateInteractionMovementLock()
{
	const bool bShouldLock = IsStorageOpen() || IsFacilityHubOpen();
	if (bInteractionMovementLocked != bShouldLock)
	{
		// SetIgnoreMoveInput is counted, so only change the count when this UI's lock changes.
		SetIgnoreMoveInput(bShouldLock);
		bInteractionMovementLocked = bShouldLock;
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
	TickDeathFlow(DeltaTime);
	if (HasAuthority() && ActiveStorageChest && !CanAccessStorage(ActiveStorageChest)) CloseStorageFromServer(ActiveStorageChest);

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

bool ABasePlayerController::CanAccessStorage(AStorageChest* Chest) const
{
	if (!CanMutateGameplay()) return false;
	if (!IsValid(Chest) || Chest->IsLocked()) return false;
	const ASharedStorageChest* Shared = Cast<ASharedStorageChest>(Chest);
	return !Shared || Shared->CanPlayerAccess(GetPawn());
}

void ABasePlayerController::ServerSharedStorageSlotAction_Implementation(AStorageChest* Chest, int32 Index,
	FGameplayTag ExpectedTag, int32 ExpectedCount, int32 ExpectedCapacity, bool bQuickMove)
{
	if (!CanMutateGameplay()) return;
	if (!CanAccessStorage(Chest) || ActiveStorageChest != Chest || !Chest->IsA<ASharedStorageChest>()) return;
	ABasePlayer* StoragePlayer = Cast<ABasePlayer>(GetPawn());
	UInventoryComponent* Inventory = StoragePlayer ? StoragePlayer->GetInventoryComponent() : nullptr;
	UStorageComponent* Storage = Chest->GetStorageComponent();
	if (bQuickMove && Inventory && Inventory->GetCursorItem().IsValid())
	{
		Inventory->ReturnCursorToOriginalSlot();
		return;
	}
	if (!Inventory || !Storage || ExpectedCapacity != Storage->GetSlotsPerTab() || !Storage->GetSlots().IsValidIndex(Index)) return;
	const FInventorySlot& Slot = Storage->GetSlots()[Index];
	// Reject stale views after another player modifies the same slot or capacity changes.
	if (Slot.ItemTag != ExpectedTag || Slot.Count != ExpectedCount) return;
	if (bQuickMove)
	{
		Storage->TransferSlotToInventory(Index, Inventory);
	}
	else if (Inventory->GetCursorItem().IsValid()) Inventory->TransferCursorToStorageSlot(Storage, Index);
	else Storage->PickUpSlotToCursor(Index, Inventory);
}

void ABasePlayerController::ServerQuickMoveInventorySlotInTab_Implementation(EInventoryTab Tab, int32 Index,
	FGameplayTag ExpectedTag, int32 ExpectedCount)
{
	if (!CanMutateGameplay()) return;
	if (!CanAccessStorage(ActiveStorageChest)) return;
	ABasePlayer* StoragePlayer = Cast<ABasePlayer>(GetPawn());
	UInventoryComponent* Inventory = StoragePlayer ? StoragePlayer->GetInventoryComponent() : nullptr;
	if (!Inventory || static_cast<uint8>(Tab) > static_cast<uint8>(EInventoryTab::Weapon)) return;
	const TArray<FInventorySlot>& Slots = Inventory->GetSlots(Tab);
	if (!Slots.IsValidIndex(Index) || Slots[Index].ItemTag != ExpectedTag || Slots[Index].Count != ExpectedCount) return;
	if (Inventory->GetCursorItem().IsValid()) { Inventory->ReturnCursorToOriginalSlot(); return; }
	Inventory->TransferSlotToStorageInTab(Tab, Index, ActiveStorageChest->GetStorageComponent());
	StartStorageSearch(ActiveStorageChest);
}

void ABasePlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
 Super::GetLifetimeReplicatedProps(OutLifetimeProps);
 DOREPLIFETIME_CONDITION(ABasePlayerController, DeathFlowState, COND_OwnerOnly);
}
ABasePlayer* ABasePlayerController::GetLifeCharacter() const
{
 if (ABasePlayer* LifePawnCharacter = Cast<ABasePlayer>(GetPawn())) return LifePawnCharacter;
 if (LifeCharacter.IsValid() && LifeCharacter->GetWorld() == GetWorld()) return LifeCharacter.Get();
 if (AShip* Ship = Cast<AShip>(GetPawn())) return Cast<ABasePlayer>(Ship->GetRidingPlayer());
 if (ACannon* Cannon = Cast<ACannon>(GetPawn())) return Cast<ABasePlayer>(Cannon->GetRidingPlayer());
 return nullptr;
}
bool ABasePlayerController::IsLifeCharacterAlive() const
{
 const ABasePlayer* LifePawnCharacter = GetLifeCharacter();
 return LifePawnCharacter && LifePawnCharacter->GetWorld() == GetWorld() && LifePawnCharacter->GetHealthComponent()
  && !LifePawnCharacter->GetHealthComponent()->IsDead() && DeathFlowState.Phase == ESWPersonalLifePhase::Alive
  && LocalSessionPhase != ESWSessionLifePhase::GameOver && LocalSessionPhase != ESWSessionLifePhase::ReturningAfterGameOver;
}
bool ABasePlayerController::CanMutateGameplay() const
{
 const AMultiGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AMultiGameMode>() : nullptr;
 if (Mode) return Mode->CanMutateGameplay(const_cast<ABasePlayerController*>(this));
 return DeathFlowState.Phase == ESWPersonalLifePhase::Alive && LocalSessionPhase != ESWSessionLifePhase::GameOver
  && LocalSessionPhase != ESWSessionLifePhase::ReturningAfterGameOver;
}
bool ABasePlayerController::CleanupLifeInteraction()
{
 if (ActiveStorageChest) CloseStorageFromServer(ActiveStorageChest);
 if (ActiveFacilityHub) { ActiveFacilityHub->Release(this); ActiveFacilityHub = nullptr; }
 ABasePlayer* LifePawnCharacter = GetLifeCharacter();
 if (LifePawnCharacter)
 {
  if (UPlayerDialogueComponent* Dialogue = LifePawnCharacter->FindComponentByClass<UPlayerDialogueComponent>()) Dialogue->CancelDialogue();
  if (UInventoryComponent* Inventory = LifePawnCharacter->GetInventoryComponent())
  {
   const FInventoryCursorItem Cursor = Inventory->GetCursorItem();
   if (Cursor.IsValid() && IsValid(Cursor.OriginalStorage))
    if (!Cursor.OriginalStorage->ReturnReservedCursor(Inventory)) return false;
   Inventory->ReturnCursorToOriginalSlot();
   if (Inventory->GetCursorItem().IsValid()) return false;
  }
 }
 return true;
}
bool ABasePlayerController::CaptureLatestLifeProgress(APawn* SourcePawn)
{
 if (!HasAuthority()) return false;
 if (bLifeProgressFrozen) return bHasLatestLifeProgress;
 ABasePlayer* LifePawnCharacter = Cast<ABasePlayer>(SourcePawn);
 if (!LifePawnCharacter) LifePawnCharacter = GetLifeCharacter();
 if (!LifePawnCharacter) return bHasLatestLifeProgress;
 LifeCharacter = LifePawnCharacter;
 if (!CleanupLifeInteraction()) return false;
 LifePawnCharacter->CaptureRoomProgress(LatestLifeProgress);
 bHasLatestLifeProgress = true;
 return true;
}
bool ABasePlayerController::GetLatestLifeProgress(FSWRoomPlayerProgress& OutProgress) const
{
 if (!bHasLatestLifeProgress) return false;
 OutProgress = LatestLifeProgress; return true;
}
bool ABasePlayerController::ApplyPendingLifeProgress(APawn* NewPawn)
{
 bPendingLifeProgressApplied = false; AppliedLifePawn.Reset();
 ABasePlayer* LifePawnCharacter = Cast<ABasePlayer>(NewPawn);
 FString Error;
 if (!HasAuthority() || !LifePawnCharacter || !bHasLatestLifeProgress || !LifePawnCharacter->RestoreProgressForNewLife(LatestLifeProgress, Error))
 {
  UE_LOG(LogSWRoom, Error, TEXT("RespawnSpawnFailed Controller=%s Reason=%s"), *GetName(), *Error); return false;
 }
 LifeCharacter = LifePawnCharacter; AppliedLifePawn = NewPawn; bPendingLifeProgressApplied = true;
 return true;
}
bool ABasePlayerController::WasLastLifeProgressApplySuccessful(APawn* NewPawn) const
{
 const ABasePlayer* LifePawnCharacter = Cast<ABasePlayer>(NewPawn);
 return bPendingLifeProgressApplied && IsValid(NewPawn) && AppliedLifePawn.Get() == NewPawn
  && LifePawnCharacter && LifePawnCharacter->GetHealthComponent() && !LifePawnCharacter->GetHealthComponent()->IsDead();
}
void ABasePlayerController::FreezeLifeProgressForGameOver()
{
 if (!HasAuthority()) return;
 if (!bLifeProgressFrozen && !bGameOverReconnect && !(DeathFlowState.Phase == ESWPersonalLifePhase::WaitingForRespawn && bHasLatestLifeProgress))
 {
  // Normal first defeat captures the live character, even without a previous value record.
  if (!CaptureLatestLifeProgress(GetPawn())) { bHasLatestLifeProgress = false; UE_LOG(LogSWRoom, Error, TEXT("RetryCaptureFailed: game-over life record Controller=%s"), *GetName()); }
 }
 bLifeProgressFrozen = bHasLatestLifeProgress;
 if (ABasePlayer* LifePawnCharacter = GetLifeCharacter())
 {
  if (!bGameOverCharacterProtected)
  {
   bLifeCharacterCouldBeDamaged = LifePawnCharacter->CanBeDamaged();
   bLifeCharacterWasInvulnerable = LifePawnCharacter->GetAbilitySystemComponent() && LifePawnCharacter->GetAbilitySystemComponent()->HasMatchingGameplayTag(State_Invulnerable);
   bGameOverCharacterProtected = true;
  }
  LifePawnCharacter->SetCanBeDamaged(false);
  if (UAbilitySystemComponent* ASC = LifePawnCharacter->GetAbilitySystemComponent()) { ASC->CancelAllAbilities(); if (!ASC->HasMatchingGameplayTag(State_Invulnerable)) ASC->AddLooseGameplayTag(State_Invulnerable); }
  if (UCharacterMovementComponent* Movement = LifePawnCharacter->GetCharacterMovement()) { Movement->StopMovementImmediately(); Movement->DisableMovement(); }
 }
}
void ABasePlayerController::ReleaseFrozenLifeProgress()
{
 bLifeProgressFrozen = false;
 if (bGameOverCharacterProtected)
  if (ABasePlayer* LifePawnCharacter = GetLifeCharacter())
  {
   LifePawnCharacter->SetCanBeDamaged(bLifeCharacterCouldBeDamaged);
   if (!bLifeCharacterWasInvulnerable) if (UAbilitySystemComponent* ASC = LifePawnCharacter->GetAbilitySystemComponent()) ASC->RemoveLooseGameplayTag(State_Invulnerable);
  }
 bGameOverCharacterProtected = false;
}
void ABasePlayerController::SetDeathFlowState(const FSWDeathFlowState& State)
{
 // Target and observation generation are maintained by the authoritative controller refresh.
 FSWDeathFlowState Next = State;
 Next.SpectatedPlayerState = DeathFlowState.SpectatedPlayerState;
 Next.ObservationGeneration = DeathFlowState.ObservationGeneration;
 DeathFlowState = Next;
 ForceNetUpdate();
 if (IsLocalController()) OnRep_DeathFlowState();
}
void ABasePlayerController::OnRep_DeathFlowState()
{
 if (LocalObservationGeneration != DeathFlowState.ObservationGeneration || LocalObservedPlayerState != DeathFlowState.SpectatedPlayerState)
 {
  LocalObservationGeneration = DeathFlowState.ObservationGeneration; LocalObservedPlayerState = DeathFlowState.SpectatedPlayerState;
  bHasObservedPOV = false; LastObservedSequence = 0; LastReceiveTime = -1;
 }
 ApplyLocalDeathFlow();
}
void ABasePlayerController::ApplyLocalDeathFlow()
{
 if (!IsLocalController() || GetNetMode() == NM_DedicatedServer) return;
 const bool bGameOver = LocalSessionPhase == ESWSessionLifePhase::GameOver || LocalSessionPhase == ESWSessionLifePhase::ReturningAfterGameOver;
 const bool bWaiting = DeathFlowState.Phase == ESWPersonalLifePhase::WaitingForRespawn;
 const bool bBlocked = bGameOver || bWaiting;
 if (bBlocked && !bDeathInputLocked)
 {
  FrozenOwnDeathPOV = bHasOwnPOV ? LastOwnAlivePOV : FMinimalViewInfo();
  if (!bHasOwnPOV && GetLifeCharacter())
  { FrozenOwnDeathPOV.Location = GetLifeCharacter()->GetActorLocation() + FVector(0, 0, 160); FrozenOwnDeathPOV.Rotation = GetControlRotation(); }
  bSavedAutoCamera = bAutoManageActiveCameraTarget; bAutoManageActiveCameraTarget = false;
  CloseFacilityHub(); CloseStorage(); CloseRoomMenu();
  if (StatusWindowWidget) StatusWindowWidget->SetVisibility(ESlateVisibility::Collapsed);
  if (PlayerHUDWidget) PlayerHUDWidget->SetVisibility(ESlateVisibility::Collapsed);
  SetIgnoreMoveInput(true); SetIgnoreLookInput(true); bDeathInputLocked = true;
 }
 if (bBlocked)
 {
  if (!DeathFlowWidget) { DeathFlowWidget = CreateWidget<USWDeathFlowWidget>(this, USWDeathFlowWidget::StaticClass()); DeathFlowWidget->AddToViewport(1000); }
  if (bGameOver)
  {
   bShowMouseCursor = true;
   FInputModeUIOnly Input; Input.SetWidgetToFocus(DeathFlowWidget->TakeWidget()); SetInputMode(Input);
   if (DeathFlowState.bHostMayRetry && !PendingRetryRequestId) DeathFlowWidget->FocusRetry();
  }
  else
  {
   bShowMouseCursor = false; SetInputMode(FInputModeGameOnly());
   if (!DeathCamera)
   {
    DeathCamera = GetWorld()->SpawnActor<ACameraActor>();
    if (DeathCamera) { DeathCamera->SetReplicates(false); DeathCamera->SetActorEnableCollision(false); DeathCamera->SetActorTickEnabled(false); }
   }
   if (DeathCamera)
   {
    if (!DeathFlowState.SpectatedPlayerState || !bHasObservedPOV)
    { DeathCamera->SetActorLocationAndRotation(FrozenOwnDeathPOV.Location, FrozenOwnDeathPOV.Rotation); DeathCamera->GetCameraComponent()->SetFieldOfView(FrozenOwnDeathPOV.FOV); }
    SetViewTargetWithBlend(DeathCamera, 0);
   }
  }
 }
 else if (bDeathInputLocked && Cast<ABasePlayer>(GetPawn()) && PlayerState
  && Cast<ABasePlayer>(GetPawn())->GetPlayerState() && Cast<ABasePlayer>(GetPawn())->GetAbilitySystemComponent())
 {
  SetIgnoreMoveInput(false); SetIgnoreLookInput(false); bDeathInputLocked = false;
  bAutoManageActiveCameraTarget = bSavedAutoCamera; SetViewTargetWithBlend(GetPawn(), 0);
  if (PlayerHUDWidget) PlayerHUDWidget->SetVisibility(ESlateVisibility::Visible);
  SetInputMode(FInputModeGameOnly()); bShowMouseCursor = false;
  if (DeathFlowWidget) { DeathFlowWidget->RemoveFromParent(); DeathFlowWidget = nullptr; }
  bHasObservedPOV = false;
 }
}
void ABasePlayerController::TickDeathFlow(float DeltaTime)
{
 if (!GetWorld()) return;
 for (TActorIterator<ASWRoomReadyState> It(GetWorld()); It; ++It)
 {
  if (LocalSessionPhase != It->SessionLifePhase)
  { LocalSessionPhase = It->SessionLifePhase; ApplyLocalDeathFlow(); }
  break;
 }
 const double Now = GetWorld()->GetTimeSeconds();
 if (HasAuthority() && Now - LastSpectatorRefreshTime >= .1)
 {
  LastSpectatorRefreshTime = Now;
  ABasePlayerController* Target = nullptr;
  if (DeathFlowState.Phase == ESWPersonalLifePhase::WaitingForRespawn && LocalSessionPhase != ESWSessionLifePhase::GameOver && LocalSessionPhase != ESWSessionLifePhase::ReturningAfterGameOver)
   for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    if (ABasePlayerController* Other = Cast<ABasePlayerController>(It->Get()); Other && Other != this && Other->IsLifeCharacterAlive()) { Target = Other; break; }
  APlayerState* TargetState = Target ? Target->PlayerState.Get() : nullptr;
  if (TargetState != DeathFlowState.SpectatedPlayerState)
  {
   for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    if (ABasePlayerController* Other = Cast<ABasePlayerController>(It->Get()); Other && Other->PlayerState == DeathFlowState.SpectatedPlayerState)
    { Other->bCameraPublishing = false; Other->ClientSetCameraPublishEnabled(false, DeathFlowState.RestoreGeneration, DeathFlowState.ObservationGeneration); }
   DeathFlowState.SpectatedPlayerState = TargetState; ++DeathFlowState.ObservationGeneration; bHasObservedPOV = false;
   ForceNetUpdate();
   if (Target)
   {
    Target->bCameraPublishing = true; Target->PublishRestoreGeneration = DeathFlowState.RestoreGeneration;
    Target->PublishObservationGeneration = DeathFlowState.ObservationGeneration; Target->LastAcceptedSequence = 0; Target->LastServerCameraTime = -1;
    Target->ClientSetCameraPublishEnabled(true, DeathFlowState.RestoreGeneration, DeathFlowState.ObservationGeneration);
   }
   if (IsLocalController()) OnRep_DeathFlowState();
  }
  if (LocalSessionPhase == ESWSessionLifePhase::GameOver && !bLifeProgressFrozen && !bGameOverCharacterProtected) FreezeLifeProgressForGameOver();
 }
 if (!IsLocalController()) return;

 if (!bDeathInputLocked && IsLifeCharacterAlive() && PlayerCameraManager)
 { LastOwnAlivePOV = PlayerCameraManager->GetCameraCacheView(); bHasOwnPOV = true; }
 if (bCameraPublishing && IsLifeCharacterAlive() && PlayerCameraManager && Now - LastPublishTime >= .05)
 {
  LastPublishTime = Now;
  const FMinimalViewInfo& POV = PlayerCameraManager->GetCameraCacheView();
  FSWObservedCameraFrame Frame; Frame.Location = POV.Location; Frame.Rotation = POV.Rotation; Frame.FOV = POV.FOV;
  Frame.RestoreGeneration = PublishRestoreGeneration; Frame.ObservationGeneration = PublishObservationGeneration; Frame.Sequence = ++CameraSequence;
  ServerPublishObservedCamera(Frame);
 }
 if (bDeathInputLocked || DeathFlowState.Phase == ESWPersonalLifePhase::WaitingForRespawn) ApplyLocalDeathFlow();
 if (DeathFlowWidget)
 {
  FString Waiting;
  if (LocalSessionPhase == ESWSessionLifePhase::ShipSinking) Waiting = TEXT("배가 침몰 중입니다");
  else if (const AGameStateBase* State = GetWorld()->GetGameState())
  {
   const int32 Remaining = FMath::CeilToInt(FMath::Max(0., DeathFlowState.RespawnEndServerTime - State->GetServerWorldTimeSeconds()));
   Waiting = Remaining > 0 ? FString::Printf(TEXT("부활까지 %d초"), Remaining) : TEXT("부활 위치를 준비 중입니다");
  }
  else Waiting = TEXT("부활 대기 중입니다");
  if (DeathFlowState.SpectatedPlayerState && Now - LastReceiveTime > .5) Waiting += TEXT("\n동료 시점을 기다리는 중입니다");
  DeathFlowWidget->UpdateFlow(LocalSessionPhase == ESWSessionLifePhase::GameOver || LocalSessionPhase == ESWSessionLifePhase::ReturningAfterGameOver,
   DeathFlowState.bHostMayRetry || PendingRetryRequestId != 0, PendingRetryRequestId != 0, Waiting, RetryStatus);
 }
}
void ABasePlayerController::ClientSetCameraPublishEnabled_Implementation(bool bEnabled, int32 RestoreGeneration, int32 ObservationGeneration)
{
 bCameraPublishing = bEnabled; PublishRestoreGeneration = RestoreGeneration; PublishObservationGeneration = ObservationGeneration;
 CameraSequence = 0; LastPublishTime = -1;
}
void ABasePlayerController::ServerPublishObservedCamera_Implementation(const FSWObservedCameraFrame& Frame)
{
 const double Now = GetWorld()->GetTimeSeconds();
 if (!bCameraPublishing || !IsLifeCharacterAlive() || Frame.RestoreGeneration != PublishRestoreGeneration
  || Frame.ObservationGeneration != PublishObservationGeneration || Frame.Sequence <= LastAcceptedSequence
  || Frame.Location.ContainsNaN() || Frame.Rotation.ContainsNaN() || !FMath::IsFinite(Frame.FOV) || Frame.FOV < 5 || Frame.FOV > 170
  || Now - LastServerCameraTime < 1. / 30.) return;
 LastAcceptedSequence = Frame.Sequence; LastServerCameraTime = Now;
 FSWObservedCameraFrame Validated = Frame; Validated.SourcePlayerState = PlayerState;
 for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
  if (ABasePlayerController* Observer = Cast<ABasePlayerController>(It->Get()); Observer
   && Observer->DeathFlowState.Phase == ESWPersonalLifePhase::WaitingForRespawn
   && Observer->DeathFlowState.SpectatedPlayerState == PlayerState
   && Observer->DeathFlowState.ObservationGeneration == Frame.ObservationGeneration) Observer->ClientReceiveObservedCamera(Validated);
}
void ABasePlayerController::ClientReceiveObservedCamera_Implementation(const FSWObservedCameraFrame& Frame)
{
 if (Frame.Sequence <= LastObservedSequence || Frame.RestoreGeneration != DeathFlowState.RestoreGeneration || Frame.ObservationGeneration != DeathFlowState.ObservationGeneration
  || Frame.SourcePlayerState != DeathFlowState.SpectatedPlayerState || DeathFlowState.Phase != ESWPersonalLifePhase::WaitingForRespawn) return;
 ApplyLocalDeathFlow();
 if (!DeathCamera) return;
 DeathCamera->SetActorLocationAndRotation(Frame.Location, Frame.Rotation); DeathCamera->GetCameraComponent()->SetFieldOfView(Frame.FOV);
 LastObservedSequence = Frame.Sequence; bHasObservedPOV = true; LastReceiveTime = GetWorld()->GetTimeSeconds();
}
void ABasePlayerController::RequestGameOverRetry()
{
 if (PendingRetryRequestId || !DeathFlowState.bHostMayRetry) return;
 PendingRetryRequestId = ++NextRetryRequestId; RetryStatus = TEXT("다시 시작하는 중입니다");
 ServerRequestGameOverRetry(DeathFlowState.RestoreGeneration, PendingRetryRequestId);
}
void ABasePlayerController::ServerRequestGameOverRetry_Implementation(int32 ExpectedRestoreGeneration, uint64 RequestId)
{
 if (RequestId == LastRetryRequestId && RequestId != 0) { ClientGameOverRetryResult(RequestId, bLastRetryAccepted, LastRetryMessage); return; }
 AMultiGameMode* Mode = GetWorld()->GetAuthGameMode<AMultiGameMode>();
 FString Error;
 bool bAccepted = false;
 if (!Mode || !Mode->IsRoomHostController(this)) Error = TEXT("RetryRejectedNotHost");
 else if (!Mode->CanHostRequestGameOverRetry(this)) Error = TEXT("다시하기 요청 처리 중이거나 게임 오버가 아닙니다");
 else if (ExpectedRestoreGeneration != DeathFlowState.RestoreGeneration) Error = TEXT("RetryRejectedWrongGeneration");
 else if (UClassFeatureRoomProgressSubsystem* Room = GetGameInstance()->GetSubsystem<UClassFeatureRoomProgressSubsystem>()) bAccepted = Room->TryGameOverRetry(GetWorld(), this, RequestId, Error);
 ReportGameOverRetryResult(RequestId, bAccepted, Error);
}
void ABasePlayerController::ClientGameOverRetryResult_Implementation(uint64 RequestId, bool bAccepted, const FString& Message)
{
 if (RequestId != PendingRetryRequestId) return;
 if (!bAccepted) { PendingRetryRequestId = 0; RetryStatus = TEXT("다시 시작하지 못했습니다: ") + Message; }
}

void ABasePlayerController::ReportGameOverRetryResult(uint64 RequestId, bool bAccepted, const FString& Message)
{
 LastRetryRequestId = RequestId; bLastRetryAccepted = bAccepted; LastRetryMessage = Message;
 ClientGameOverRetryResult(RequestId, bAccepted, Message);
}
