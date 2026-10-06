#include "Room/ClassFeatureVoyageTransition.h"
#include "Room/ClassFeatureRoomProgressSubsystem.h"
#include "Room/SWVoyageResetSubsystem.h"
#include "Room/SWVoyageResetAnchor.h"
#include "Room/SWVoyageResetProfile.h"
#include "Room/SWRoomProgressSubsystem.h"
#include "Room/SWRoomSnapshotSubsystem.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "Room/SWRoomReadyState.h"
#include "Room/SWLevelEntryPoint.h"
#include "Room/SWFinalEncounterShipEntryPoint.h"
#include "BasePlayerController.h"
#include "BasePlayer.h"
#include "MultiGameMode.h"
#include "KelvinShip.h"
#include "PlayerRespawnPointComponent.h"
#include "Storage/SharedStorageChest.h"
#include "Storage/StorageComponent.h"
#include "Inventory/InventoryComponent.h"
#include "StoryFacadeSubsystem.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/LevelStreaming.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"

UClassFeatureRoomProgressSubsystem* UClassFeatureVoyageTransition::Owner() const
{
	return CastChecked<UClassFeatureRoomProgressSubsystem>(GetOuter());
}

void UClassFeatureVoyageTransition::Shutdown()
{
	if (TickerHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	TickerHandle.Reset(); Participants.Reset(); ActiveWorld.Reset(); Profile = nullptr;
}

bool UClassFeatureVoyageTransition::InitializeAttempt(UWorld* World, bool bBootstrap, FString& OutError)
{
	USWRoomProgressSubsystem* Room = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>() : nullptr;
	USWVoyageResetSubsystem* Core = World ? World->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	if (!Room || !Room->IsHostedRoom() || !Room->GetActiveRoom() || !Core || World->GetNetMode() == NM_Client
		|| AttemptSerial == MAX_int64 || (!bBootstrap && Room->GetRestoreGeneration() == MAX_int32))
	{ OutError = TEXT("VoyageAttemptPrerequisitesInvalid"); return false; }
	ActiveWorld = World; Participants.Reset();
	Context.AttemptId = ++AttemptSerial;
	Context.Generation = Room->GetRestoreGeneration() + (bBootstrap ? 0 : 1);
	Context.bAuthority = true; Context.bBootstrap = bBootstrap;
	if (!Core->ConfigureFromAnchor(OutError) || !Core->ValidateVoyageContracts(OutError)) return false;
	int32 EntryCounts[3] = {0, 0, 0};
	for (TActorIterator<ASWLevelEntryPoint> It(World); It; ++It)
	{
		const int32 Role = static_cast<int32>(It->EntryRole);
		if (Role < 0 || Role > 2 || It->GetLevel() != World->PersistentLevel || It->GetActorTransform().ContainsNaN())
		{ OutError = TEXT("VoyageDestinationEntryInvalid"); return false; }
		++EntryCounts[Role];
	}
	if (EntryCounts[0] != 1 || EntryCounts[1] != 1 || EntryCounts[2] != 1)
	{ OutError = TEXT("VoyageDestinationEntryMissingOrDuplicate"); return false; }
	if (Context.Reason == ESWVoyageReason::FinalDeparture)
	{
		int32 FinalEntries = 0;
		for (TActorIterator<ASWFinalEncounterShipEntryPoint> It(World); It; ++It)
		{
			if (It->GetLevel() != World->PersistentLevel || It->GetActorTransform().ContainsNaN())
			{ OutError = TEXT("VoyageFinalDestinationInvalid"); return false; }
			++FinalEntries;
		}
		if (FinalEntries != 1) { OutError = TEXT("VoyageFinalDestinationMissingOrDuplicate"); return false; }
	}
	Profile = nullptr;
	for (TActorIterator<ASWVoyageResetAnchor> It(World); It; ++It) Profile = It->Profile;
	if (!Profile || !Core->GetGameplayStreamingLevel()) { OutError = TEXT("VoyageProfileMissing"); return false; }
	Context.GameplayPackage = Core->GetGameplayStreamingLevel()->GetWorldAssetPackageFName();
	Context.bAuthority = true; Context.bBootstrap = bBootstrap;
	Context.RestoreStage = ESWVoyageRestoreStage::AuthoredActors;
	TotalDeadline = FPlatformTime::Seconds() + Profile->TotalTimeoutSeconds;
	bSharedApplied = false; bShipPlaced = false; bSnapshotCompleted = false;
	bSaveSucceeded = false; FailureMessage.Reset();
	return true;
}

bool UClassFeatureVoyageTransition::Start(UWorld* World, ESWVoyageReason Reason, bool bDevelopment, FString& OutError)
{
	if (IsBusy()) { OutError = TEXT("VoyageAlreadyBusy"); return false; }
	Context.Reason = Reason; Context.bContinue = false;
	if (!InitializeAttempt(World, false, OutError)) return false;
	USWRoomProgressSubsystem* Room = World->GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>();
	USWRoomSaveGame* Save = Room->GetMutableActiveRoom();
	AMultiGameMode* Mode = World->GetAuthGameMode<AMultiGameMode>();
	if (!Mode || !Mode->BeginVoyageReset(Context.Generation, Reason == ESWVoyageReason::GameOverRetry, OutError)) return false;
	RollbackHost = Save->HostProgress; RollbackGuests = Save->Guests; RollbackShared = Save->SharedProgress;
	bRollbackFinalCompleted = Save->bFinalDepartureCompleted;
	bCommitted = false; bRecoveryUsed = false; bRecoveryPending = false;
	bRecoveryBootstrap = false; bRecoveryContinue = false; bInitialSaveAttempted = true;
	bDevelopmentDeparture = bDevelopment;
	bStoryCommitAttempted = false;
	if (!Enter(ESWVoyagePhase::Presentation, OutError)) { Fail(OutError); return false; }
	// Shared reservations are returned before private interaction cleanup.
	for (TActorIterator<AStorageChest> It(World); It; ++It)
		if (UStorageComponent* Storage = It->GetStorageComponent()) Storage->ReturnAllReservedCursors();
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		ABasePlayerController* Controller = Cast<ABasePlayerController>(It->Get());
		if (!Controller || !Controller->CleanupLifeInteraction()
			|| !Owner()->CaptureControllerProgress(Controller, !Controller->IsLifeCharacterAlive(), OutError))
		{ if (OutError.IsEmpty()) OutError = TEXT("VoyagePlayerCaptureFailed"); Fail(OutError); return false; }
		FParticipant& Participant = Participants.AddDefaulted_GetRef(); Participant.Controller = Controller;
		Participant.Pawn = Controller->GetLifeCharacter();
	}
	if (Participants.IsEmpty() || (bDevelopment && Participants.Num() != 2) || !Owner()->CaptureSharedWorld(World))
	{ OutError = TEXT("VoyageTargetCaptureFailed"); Fail(OutError); return false; }
	// Rollback uses the secured live records, including Story changes since the last checkpoint.
	RollbackHost = Save->HostProgress; RollbackGuests = Save->Guests; RollbackShared = Save->SharedProgress;
	TargetHost = RollbackHost; TargetGuests = RollbackGuests; TargetShared = RollbackShared;
	Owner()->NormalizePlayerForVoyage(TargetHost);
	for (FSWRoomGuestProgress& Guest : TargetGuests) Owner()->NormalizePlayerForVoyage(Guest.Progress);
	if (Reason == ESWVoyageReason::FinalDeparture)
	{
		bStoryCommitAttempted = true;
		UStoryFacadeSubsystem* Story = World->GetGameInstance()->GetSubsystem<UStoryFacadeSubsystem>();
		if (!Story || (!Story->IsStoryNodeReached(EStoryNode::UldolmokBattleQuestAccepted)
			&& !(bDevelopment ? Story->ActivateDevelopmentFinalBattle() : Story->CompleteStoryNode(EStoryNode::UldolmokBattleQuestAccepted)))
			|| !Owner()->CaptureSharedWorld(World))
		{ OutError = TEXT("VoyageFinalStoryCommitFailed"); Fail(OutError); return false; }
	}
	TargetShared = Save->SharedProgress;
	TSet<FString> GuestKeys;
	for (FSWRoomGuestProgress& Guest : TargetGuests)
	{
		if (Guest.DisplayName.IsEmpty() || GuestKeys.Contains(Guest.DisplayName)) { OutError = TEXT("VoyageGuestIdentityInvalidOrDuplicate"); Fail(OutError); return false; }
		GuestKeys.Add(Guest.DisplayName);
	}
	TSet<FString> StorageKeys;
	for (const FSWRoomStorageProgress& Storage : TargetShared.Storage)
	{
		const FString Key = Storage.ChestId.ToString(EGuidFormats::DigitsWithHyphens) + TEXT("|") + Storage.SaveNamespace;
		if (!Storage.ChestId.IsValid() || StorageKeys.Contains(Key) || Storage.SlotsPerTab <= 0
			|| Storage.SlotsPerTab > MAX_int32 / 4 || Storage.Slots.Num() != Storage.SlotsPerTab * 4)
		{ OutError = TEXT("VoyageTargetStorageContractInvalid:") + Key; Fail(OutError); return false; }
		StorageKeys.Add(Key);
	}
	if (Reason == ESWVoyageReason::GameOverRetry)
		for (FSWRoomStorageProgress& Storage : TargetShared.Storage)
		{
			if (!Storage.ChestId.IsValid() || Storage.SlotsPerTab <= 0 || Storage.Slots.Num() != Storage.SlotsPerTab * 4)
			{ OutError = TEXT("VoyageRetryStorageContractInvalid"); Fail(OutError); return false; }
			Storage.Slots.Reset(); Storage.Slots.SetNum(Storage.SlotsPerTab * 4);
		}
	StoreTargets(); Save->bFinalDepartureCompleted = false;
	for (FParticipant& Participant : Participants)
	{
		if (!Owner()->GetStoredControllerProgress(Participant.Controller.Get(), Participant.Progress))
		{ OutError = TEXT("VoyageCapturedIdentityMissing"); Fail(OutError); return false; }
		Participant.Controller->ClientBeginVoyage(GetReplicatedState());
	}
	if (!TickerHandle.IsValid()) TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UClassFeatureVoyageTransition::Tick));
	return true;
}

void UClassFeatureVoyageTransition::StoreTargets()
{
	if (UWorld* World = ActiveWorld.Get())
		if (USWRoomProgressSubsystem* Room = World->GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>())
			if (USWRoomSaveGame* Save = Room->GetMutableActiveRoom())
			{ Save->HostProgress = TargetHost; Save->Guests = TargetGuests; Save->SharedProgress = TargetShared; }
}

FSWVoyageReplicatedState UClassFeatureVoyageTransition::GetReplicatedState() const
{
	FSWVoyageReplicatedState State;
	State.AttemptId = Context.AttemptId; State.Generation = Context.Generation; State.Reason = Context.Reason;
	State.Phase = Context.Phase; State.GameplayPackage = Context.GameplayPackage;
	State.bBootstrap = Context.bBootstrap; State.bContinue = Context.bContinue; return State;
}

void UClassFeatureVoyageTransition::Publish()
{
	if (UWorld* World = ActiveWorld.Get())
	{
		const FSWVoyageReplicatedState State = GetReplicatedState();
		for (TActorIterator<ASWRoomReadyState> It(World); It; ++It) It->PublishVoyageState(State);
		for (FParticipant& Participant : Participants) if (Participant.Controller.IsValid()) Participant.Controller->ClientSetVoyagePhase(State);
	}
}

bool UClassFeatureVoyageTransition::Enter(ESWVoyagePhase Phase, FString& OutError)
{
	UWorld* World = ActiveWorld.Get();
	USWVoyageResetSubsystem* Core = World ? World->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	Context.Phase = Phase;
	if (!Core || !Core->BeginLocalPhase(Context, OutError))
	{ if (Phase == ESWVoyagePhase::Failed) Publish(); return false; }
	const double Now = FPlatformTime::Seconds();
	switch (Phase)
	{
	case ESWVoyagePhase::Presentation: Deadline = Now + Profile->PresentationTimeoutSeconds; break;
	case ESWVoyagePhase::Quiesce: Deadline = TotalDeadline; break;
	case ESWVoyagePhase::Unload:
	case ESWVoyagePhase::Load: Deadline = Now + Profile->StreamingTimeoutSeconds; break;
	case ESWVoyagePhase::Restore: Deadline = Now + Profile->RestoreTimeoutSeconds; break;
	case ESWVoyagePhase::ClientReady: Deadline = Now + Profile->ClientReadyTimeoutSeconds; break;
	default: break;
	}
	Publish(); return true;
}

bool UClassFeatureVoyageTransition::HaveAck(ESWVoyageAck Ack) const
{
	for (const FParticipant& Participant : Participants)
		if (Participant.Controller.IsValid() && !Participant.Acks.Contains(Ack)
			&& !(Participant.bLateJoin && Ack != ESWVoyageAck::Ready)) return false;
	return true;
}

bool UClassFeatureVoyageTransition::Matches(ABasePlayerController* Controller, int64 AttemptId, int32 Generation) const
{
	return Controller && Controller->HasAuthority() && Controller->GetWorld() == ActiveWorld.Get()
		&& AttemptId == Context.AttemptId && Generation == Context.Generation && IsBusy()
		&& Participants.ContainsByPredicate([Controller](const FParticipant& P) { return P.Controller.Get() == Controller; });
}

void UClassFeatureVoyageTransition::ReceiveAck(ABasePlayerController* Controller, int64 AttemptId, int32 Generation, ESWVoyageAck Ack)
{
	if (!Matches(Controller, AttemptId, Generation)) return;
	const bool bAllowed = (Ack == ESWVoyageAck::Presentation && Context.Phase == ESWVoyagePhase::Presentation)
		|| (Ack == ESWVoyageAck::Unloaded && Context.Phase >= ESWVoyagePhase::Unload && Context.Phase <= ESWVoyagePhase::ClientReady)
		|| (Ack == ESWVoyageAck::Loaded && Context.Phase >= ESWVoyagePhase::Load && Context.Phase <= ESWVoyagePhase::ClientReady)
		|| (Ack == ESWVoyageAck::Ready && Context.Phase == ESWVoyagePhase::ClientReady);
	if (!bAllowed) return;
	for (FParticipant& Participant : Participants) if (Participant.Controller.Get() == Controller)
	{
		if (Ack == ESWVoyageAck::Ready)
		{
			const USWVoyageResetSubsystem* Core = ActiveWorld->GetSubsystem<USWVoyageResetSubsystem>();
			APawn* Pawn = Participant.Pawn.Get();
			if (!Participant.bPlaced || !Core || !Pawn || Controller->GetPawn() != Pawn || Pawn->GetController() != Controller
				|| Core->GetActorGeneration(Pawn) != Context.Generation) return;
		}
		Participant.Acks.Add(Ack);
	}
}

void UClassFeatureVoyageTransition::HandleLogout(ABasePlayerController* Controller)
{
	Participants.RemoveAll([Controller](const FParticipant& P) { return P.Controller.Get() == Controller; });
}

void UClassFeatureVoyageTransition::HandleLogin(ABasePlayerController* Controller)
{
	if (Controller)
	{
		AMultiGameMode* Mode = Controller->GetWorld() ? Controller->GetWorld()->GetAuthGameMode<AMultiGameMode>() : nullptr;
		if (!Mode || Mode->GetPlayerIndex(Controller) < 0 || Mode->GetPlayerIndex(Controller) > 1) return;
	}
	if (Controller && Controller->HasAuthority() && Context.Phase == ESWVoyagePhase::Idle && !bInitialSaveAttempted)
	{
		UWorld* World = ActiveWorld.Get(); AMultiGameMode* Mode = World ? World->GetAuthGameMode<AMultiGameMode>() : nullptr;
		USWRoomProgressSubsystem* Room = World ? World->GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>() : nullptr;
		ABasePlayer* Player = Controller->GetLifeCharacter();
		if (Mode && Room && (Room->IsNewRoomPending() || bRecoveryBootstrap) && Mode->GetPlayerIndex(Controller) == 0
			&& Player && Player->GetController() == Controller && Player->HasCompletedInitialPossession())
		{
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				const ABasePlayerController* Other = Cast<ABasePlayerController>(It->Get());
				if (!Other || !Other->GetLifeCharacter() || !Other->GetLifeCharacter()->HasCompletedInitialPossession()) return;
			}
			bInitialSaveAttempted = true; FString Error;
			if (bRecoveryBootstrap && Context.Reason == ESWVoyageReason::FinalDeparture) Room->GetMutableActiveRoom()->bFinalDepartureCompleted = true;
			if (Context.bContinue) bSaveSucceeded = true;
			else { TGuardValue<bool> SaveScope(bSavingResult, true); bSaveSucceeded = Owner()->TrySave(World, Room->IsNewRoomPending() ? ESWRoomSaveKind::New : ESWRoomSaveKind::Return, Error); }
			if (!bSaveSucceeded && bRecoveryBootstrap && Context.Reason == ESWVoyageReason::FinalDeparture) Room->GetMutableActiveRoom()->bFinalDepartureCompleted = false;
			if (!bSaveSucceeded) for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
				if (APlayerController* Connected = It->Get()) Connected->ClientMessage(TEXT("항해 준비는 완료됐지만 저장에 실패했습니다. 수동 저장을 다시 시도하세요."));
			Room->ClearReturnTravelPending(); Room->ClearFinalDepartureTravelPending(); Room->ClearGameOverRetryTravelPending(); Room->ClearGameOverTravelPending();
			bRecoveryBootstrap = false;
		}
	}
	if (Controller && Controller->HasAuthority() && Context.Phase == ESWVoyagePhase::Failed)
	{
		if (!Participants.ContainsByPredicate([Controller](const FParticipant& P) { return P.Controller.Get() == Controller; }))
		{
			FParticipant& Participant = Participants.AddDefaulted_GetRef(); Participant.Controller = Controller;
			Controller->ClientBeginVoyage(GetReplicatedState());
			const AMultiGameMode* Mode = Controller->GetWorld()->GetAuthGameMode<AMultiGameMode>();
			Controller->ClientVoyageFailure(Context.AttemptId, Context.Generation, Mode && Mode->IsRoomHostController(Controller), FailureMessage);
		}
		return;
	}
	if (Context.bBootstrap) return;
	if (!Controller || !Controller->HasAuthority() || !IsBusy() || Context.Phase >= ESWVoyagePhase::Save
		|| Participants.ContainsByPredicate([Controller](const FParticipant& P) { return P.Controller.Get() == Controller; })) return;
	FParticipant& Participant = Participants.AddDefaulted_GetRef(); Participant.Controller = Controller;
	Participant.bLateJoin = Context.Phase > ESWVoyagePhase::Presentation;
	Participant.bFresh = !Owner()->GetStoredControllerProgress(Controller, Participant.Progress);
	// Late admission catches up locally, but does not re-open completed server barriers.
	Controller->ClientBeginVoyage(GetReplicatedState());
}

bool UClassFeatureVoyageTransition::ResolvePlacement(int32 Slot, FTransform& OutTransform, FString& OutError) const
{
	UWorld* World = ActiveWorld.Get();
	AMultiGameMode* Mode = World ? World->GetAuthGameMode<AMultiGameMode>() : nullptr;
	if (!Mode || Slot < 0 || Slot > 1) { OutError = TEXT("VoyagePlacementSlotInvalid"); return false; }
	int32 Count = 0;
	if (Context.Reason == ESWVoyageReason::FinalDeparture)
	{
		AShip* Ship = Cast<AShip>(Mode->GetPlayerRespawnShip());
		if (!Ship) { OutError = TEXT("VoyagePlacementShipMissing"); return false; }
		TArray<UPlayerRespawnPointComponent*> Points; Ship->GetComponents(Points);
		for (const UPlayerRespawnPointComponent* Point : Points)
			if (Point && Point->PlayerSlot == static_cast<ESWPlayerSlot>(Slot) && Point->IsRegistered())
			{ OutTransform = Point->GetComponentTransform(); ++Count; }
	}
	else for (TActorIterator<ASWLevelEntryPoint> It(World); It; ++It)
		if (It->EntryRole == (Slot == 0 ? ESWLevelEntryRole::Host : ESWLevelEntryRole::Guest))
		{ OutTransform = It->GetActorTransform(); ++Count; }
	if (Count != 1 || OutTransform.ContainsNaN()) { OutError = TEXT("VoyagePlacementMarkerInvalid"); return false; }
	return true;
}

bool UClassFeatureVoyageTransition::PlaceBootstrapPlayer(ABasePlayerController* Controller, FString& OutError)
{
	if (!bRecoveryBootstrap || Context.bContinue || !Controller || !ActiveWorld.IsValid()) return true;
	AMultiGameMode* Mode = ActiveWorld->GetAuthGameMode<AMultiGameMode>(); FTransform Target;
	ABasePlayer* Player = Controller->GetLifeCharacter();
	if (!Mode || !Player || !ResolvePlacement(Mode->GetPlayerIndex(Controller), Target, OutError)
		|| !Player->TeleportTo(Target.GetLocation(), Target.Rotator(), false, false))
	{ if (OutError.IsEmpty()) OutError = TEXT("VoyageBootstrapPlayerPlacementFailed"); return false; }
	Controller->SetControlRotation(FRotator(0, Target.Rotator().Yaw, 0)); return true;
}

ESWVoyageStepResult UClassFeatureVoyageTransition::PollPlayers(FString& OutError)
{
	UWorld* World = ActiveWorld.Get(); AMultiGameMode* Mode = World->GetAuthGameMode<AMultiGameMode>();
	bool bPending = false;
	for (FParticipant& Participant : Participants)
	{
		ABasePlayerController* Controller = Participant.Controller.Get(); if (!Controller) continue;
		if (Participant.bLateJoin && !Participant.Acks.Contains(ESWVoyageAck::Loaded)) { bPending = true; continue; }
		if (!Participant.bPrepared)
		{
			if (!(Participant.bFresh ? Controller->PrepareFreshVoyageAdmission(Context.Generation, OutError)
				: Controller->PrepareVoyageLife(Participant.Progress, Context.Generation, OutError))) return ESWVoyageStepResult::Failed;
			Participant.bPrepared = true;
		}
		if (!ResolvePlacement(Mode->GetPlayerIndex(Controller), Participant.Target, OutError)) return ESWVoyageStepResult::Failed;
		if (!Controller->GetPawn() && FPlatformTime::Seconds() < Participant.NextSpawnAt) { bPending = true; continue; }
		Participant.NextSpawnAt = FPlatformTime::Seconds() + 0.5;
		if (!Mode->SpawnVoyagePlayer(Controller, Participant.Target, OutError))
		{
			APawn* Pawn = Controller->GetPawn();
			if (!OutError.IsEmpty()) return ESWVoyageStepResult::Failed;
			if (Pawn)
			{
				const FSWLifeRestoreStatus Status = Controller->GetLifeRestoreStatus(Pawn, Context.Generation);
				if (!Status.bContractValid) { OutError = Status.Error; return ESWVoyageStepResult::Failed; }
				if (Status.Apply == ESWLifeRestoreStepState::Failed)
				{
					if (Participant.bFresh || Pawn->GetController() != Controller)
					{ OutError = TEXT("VoyageLifeRetryOwnerInvalid"); return ESWVoyageStepResult::Failed; }
					Controller->UnPossess();
					if (!Pawn->Destroy() || !Controller->PrepareVoyageLife(Participant.Progress, Context.Generation, OutError))
					{ if (OutError.IsEmpty()) OutError = TEXT("VoyageLifeRetryDestroyFailed"); return ESWVoyageStepResult::Failed; }
				}
			}
			bPending = true; continue;
		}
		Participant.Pawn = Controller->GetPawn();
		if (!Participant.bPlaced)
		{
			APawn* Pawn = Participant.Pawn.Get();
			if (!Pawn || !Pawn->TeleportTo(Participant.Target.GetLocation(), Participant.Target.Rotator(), false, false))
			{ OutError = TEXT("VoyagePlayerPlacementFailed"); return ESWVoyageStepResult::Failed; }
			Controller->SetControlRotation(FRotator(0, Participant.Target.Rotator().Yaw, 0));
			if (ABasePlayer* Player = Cast<ABasePlayer>(Pawn)) if (UCharacterMovementComponent* Movement = Player->GetCharacterMovement())
			{ Movement->StopMovementImmediately(); Movement->SetMovementMode(MOVE_Walking); }
			Controller->ClientSetVoyagePlacement(Context.AttemptId, Context.Generation, Pawn, Cast<AShip>(Mode->GetPlayerRespawnShip()), Participant.Target);
			Participant.NextPlacementAt = FPlatformTime::Seconds() + 0.5;
			if (Participant.bFresh)
			{
				ABasePlayer* Player = Cast<ABasePlayer>(Pawn);
				if (!Controller->PlayerState || !Player) { OutError = TEXT("VoyageFreshIdentityMissing"); return ESWVoyageStepResult::Failed; }
				Player->CaptureRoomProgress(Participant.Progress);
				const FString Name = Controller->PlayerState->GetPlayerName();
				if (Name.IsEmpty() || TargetGuests.ContainsByPredicate([&Name](const FSWRoomGuestProgress& Guest) { return Guest.DisplayName == Name; }))
				{ OutError = TEXT("VoyageFreshIdentityDuplicate"); return ESWVoyageStepResult::Failed; }
				FSWRoomGuestProgress& Guest = TargetGuests.AddDefaulted_GetRef(); Guest.DisplayName = Name; Guest.Progress = Participant.Progress;
				StoreTargets();
			}
			Participant.bPlaced = true;
		}
		else if (!Participant.Acks.Contains(ESWVoyageAck::Ready) && FPlatformTime::Seconds() >= Participant.NextPlacementAt)
		{
			Controller->ClientSetVoyagePlacement(Context.AttemptId, Context.Generation, Participant.Pawn.Get(), Cast<AShip>(Mode->GetPlayerRespawnShip()), Participant.Target);
			Participant.NextPlacementAt = FPlatformTime::Seconds() + 0.5;
		}
	}
	return bPending ? ESWVoyageStepResult::Pending : ESWVoyageStepResult::Succeeded;
}

ESWVoyageStepResult UClassFeatureVoyageTransition::PollRestore(FString& OutError)
{
	UWorld* World = ActiveWorld.Get(); USWVoyageResetSubsystem* Core = World->GetSubsystem<USWVoyageResetSubsystem>();
	if (Context.RestoreStage == ESWVoyageRestoreStage::SharedState && !bSharedApplied)
	{
		const ESWVoyageStepResult Result = Owner()->RestoreSharedActors(World, TargetShared, OutError);
		if (Result != ESWVoyageStepResult::Succeeded) return Result;
		bSharedApplied = true;
	}
	if (Context.RestoreStage == ESWVoyageRestoreStage::Players)
	{
		if (!bShipPlaced)
		{
			if (!Owner()->PlaceVoyageShip(World, Context.Reason == ESWVoyageReason::FinalDeparture, Context.bContinue, OutError)) return ESWVoyageStepResult::Failed;
			bShipPlaced = true;
			FTransform GuestTransform;
			if (!Context.bContinue && !ResolvePlacement(1, GuestTransform, OutError)) return ESWVoyageStepResult::Failed;
			if (!Context.bContinue) for (FSWRoomGuestProgress& Guest : TargetGuests)
			{
				Guest.Progress.ResumeWorldTransform = GuestTransform; Guest.Progress.bHasResumeTransform = true;
				if (Context.Reason == ESWVoyageReason::FinalDeparture)
				{
					AActor* Ship = World->GetAuthGameMode<AMultiGameMode>()->GetPlayerRespawnShip();
					const USWRoomSnapshotComponent* Id = Ship ? Ship->FindComponentByClass<USWRoomSnapshotComponent>() : nullptr;
					if (!Id || !Id->StableId.IsValid()) { OutError = TEXT("VoyageNewShipIdentityMissing"); return ESWVoyageStepResult::Failed; }
					Guest.Progress.ShipStableId = Id->StableId; Guest.Progress.ShipRelativeTransform = GuestTransform.GetRelativeTransform(Ship->GetActorTransform());
				}
			}
			if (bRecoveryBootstrap)
			{
				FTransform HostTransform;
				if (!ResolvePlacement(0, HostTransform, OutError)) return ESWVoyageStepResult::Failed;
				TargetHost.ResumeWorldTransform = HostTransform; TargetHost.bHasResumeTransform = true;
			}
			StoreTargets();
		}
		if (!Context.bBootstrap)
		{
			const ESWVoyageStepResult Result = PollPlayers(OutError); if (Result != ESWVoyageStepResult::Succeeded) return Result;
		}
	}
	if (Context.RestoreStage == ESWVoyageRestoreStage::Readiness && Context.bContinue && !bSnapshotCompleted)
	{
		USWRoomSnapshotSubsystem* Snapshot = World->GetSubsystem<USWRoomSnapshotSubsystem>();
		if (!Snapshot || !Snapshot->CompleteRestore(OutError)) return ESWVoyageStepResult::Failed;
		bSnapshotCompleted = true;
	}
	Core->SetPreparationSpawnAllowed(true);
	const ESWVoyageStepResult Result = Core->PollRestoreStage(OutError);
	Core->SetPreparationSpawnAllowed(false);
	if (Result != ESWVoyageStepResult::Succeeded) return Result;
	if (Context.RestoreStage != ESWVoyageRestoreStage::Readiness)
	{
		Context.RestoreStage = static_cast<ESWVoyageRestoreStage>(static_cast<uint8>(Context.RestoreStage) + 1);
		if (!Core->BeginRestoreStage(Context.RestoreStage, OutError)) return ESWVoyageStepResult::Failed;
		return ESWVoyageStepResult::Pending;
	}
	return ESWVoyageStepResult::Succeeded;
}

bool UClassFeatureVoyageTransition::Tick(float DeltaSeconds)
{
	if (Context.Phase == ESWVoyagePhase::Idle || Context.Phase == ESWVoyagePhase::Failed || Context.Phase == ESWVoyagePhase::RecoveryTravel)
	{ TickerHandle.Reset(); return false; }
	UWorld* World = ActiveWorld.Get();
	if (!World) { TickerHandle.Reset(); return false; }
	USWVoyageResetSubsystem* Core = World->GetSubsystem<USWVoyageResetSubsystem>();
	AMultiGameMode* Mode = World->GetAuthGameMode<AMultiGameMode>();
	USWRoomProgressSubsystem* Room = World->GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>();
	FString Error;
	if (!Core || !Mode || !Room) { Fail(TEXT("VoyageOwnerLost")); return true; }
	if (FPlatformTime::Seconds() > TotalDeadline || FPlatformTime::Seconds() > Deadline)
	{ Fail(TEXT("VoyagePhaseTimeout")); return true; }
	ESWVoyageStepResult Result = ESWVoyageStepResult::Succeeded;
	ESWVoyagePhase Next = Context.Phase;
	switch (Context.Phase)
	{
	case ESWVoyagePhase::Presentation: if (HaveAck(ESWVoyageAck::Presentation)) Next = ESWVoyagePhase::Quiesce; break;
	case ESWVoyagePhase::Quiesce:
		Result = Core->PollLocalPhase(Error);
		if (Result == ESWVoyageStepResult::Succeeded)
		{
			if (Room->GetRestoreGeneration() == MAX_int32 || Room->GetRestoreGeneration() + 1 != Context.Generation)
			{ Fail(TEXT("VoyageCommitGenerationChanged")); return true; }
			if (Room->AdvanceRestoreGeneration() != Context.Generation) { Fail(TEXT("VoyageCommitGenerationMismatch")); return true; }
			bCommitted = true;
			USWRoomSnapshotSubsystem* Snapshot = World->GetSubsystem<USWRoomSnapshotSubsystem>();
			if (!Snapshot || !Snapshot->BeginVoyageDiscard(Core->GetGameplayStreamingLevel()->GetLoadedLevel(), Context.Generation, Error)) { Fail(Error); return true; }
			Mode->ResetVoyageRuntimeState(Context.Generation);
			for (FParticipant& Participant : Participants)
				if (Participant.Controller.IsValid() && !Participant.bFresh)
				{
					if (!Participant.Controller->PrepareVoyageLife(Participant.Progress, Context.Generation, Error)) { Fail(Error); return true; }
					Participant.bPrepared = true;
				}
			if (Context.Reason != ESWVoyageReason::FinalDeparture) Room->ClearDevelopmentFinalEncounterWorld(World);
			if (bDevelopmentDeparture)
			{
				Room->SetDevelopmentFinalDeparturePending(World, true, true);
				if (!Room->CommitDevelopmentFinalDepartureInPlace(World)) { Fail(TEXT("VoyageDevelopmentPermissionCommitFailed")); return true; }
			}
			Next = ESWVoyagePhase::Unload;
		}
		break;
	case ESWVoyagePhase::Unload:
		Result = Core->PollLocalPhase(Error); if (Result == ESWVoyageStepResult::Succeeded) Next = ESWVoyagePhase::Purge; break;
	case ESWVoyagePhase::Purge:
		Result = Core->PollLocalPhase(Error);
		if (Result == ESWVoyageStepResult::Succeeded)
			for (const FParticipant& Participant : Participants) if (Participant.Pawn.IsValid()) { Result = ESWVoyageStepResult::Pending; break; }
		if (Result == ESWVoyageStepResult::Succeeded && HaveAck(ESWVoyageAck::Unloaded))
		{ if (!Owner()->RestoreStoryProgress(World, TargetShared, Error)) { Fail(Error); return true; } Next = ESWVoyagePhase::Load; }
		break;
	case ESWVoyagePhase::Load:
		Result = Core->PollLocalPhase(Error);
		if (Result == ESWVoyageStepResult::Succeeded && HaveAck(ESWVoyageAck::Loaded)) Next = ESWVoyagePhase::Restore;
		break;
	case ESWVoyagePhase::Restore:
		Result = PollRestore(Error); if (Result == ESWVoyageStepResult::Succeeded) Next = ESWVoyagePhase::ClientReady; break;
	case ESWVoyagePhase::ClientReady:
		Result = Core->PollLocalPhase(Error);
		if (Result == ESWVoyageStepResult::Succeeded)
		{
			if (Context.bBootstrap) { Next = ESWVoyagePhase::Release; break; }
			Result = PollPlayers(Error);
			if (Result == ESWVoyageStepResult::Succeeded && HaveAck(ESWVoyageAck::Ready)) Next = ESWVoyagePhase::Save;
		}
		break;
	case ESWVoyagePhase::Save:
	{
		USWRoomSnapshotSubsystem* Snapshot = World->GetSubsystem<USWRoomSnapshotSubsystem>();
		if (!Snapshot || !Snapshot->EndVoyageDiscard(Context.Generation, Error)) { Fail(Error); return true; }
		const bool bFinal = Context.Reason == ESWVoyageReason::FinalDeparture;
		if (Context.Reason == ESWVoyageReason::GameOverRetry && !Owner()->ValidateRetryStorage(World, Error)) { Fail(Error); return true; }
		if (bFinal) Room->GetMutableActiveRoom()->bFinalDepartureCompleted = true;
		{ TGuardValue<bool> SavingScope(bSavingResult, true); bSaveSucceeded = Owner()->TrySave(World, ESWRoomSaveKind::Return, Error); }
		if (!bSaveSucceeded && bFinal) Room->GetMutableActiveRoom()->bFinalDepartureCompleted = false;
		FailureMessage = bSaveSucceeded ? FString() : TEXT("항해 전환은 완료됐지만 저장에 실패했습니다. 수동 저장을 다시 시도하세요. 서버 종료 시 이전 저장으로 돌아갈 수 있습니다.");
		Next = ESWVoyagePhase::Release; break;
	}
	case ESWVoyagePhase::Release:
		if (Context.bBootstrap)
		{
			// The first host is admitted after world preparation, without waiting for a nonexistent connection.
			if (!Enter(ESWVoyagePhase::Idle, Error)) { Fail(Error); return true; }
			Mode->SetHostedRoomWorldReady(); Mode->MarkHostedRoomWorldReady();
			Mode->OnGameOverRequested.AddUniqueDynamic(Owner(), &UClassFeatureRoomProgressSubsystem::HandleGameOverRestart);
			return true;
		}
		Mode->CompleteVoyageReset(Context.Generation); Mode->SetHostedRoomWorldReady(); Mode->MarkHostedRoomWorldReady();
		Mode->OnGameOverRequested.AddUniqueDynamic(Owner(), &UClassFeatureRoomProgressSubsystem::HandleGameOverRestart);
		for (FParticipant& Participant : Participants) if (Participant.Controller.IsValid())
		{ Participant.Controller->ReleaseFrozenLifeProgress(); Participant.Controller->ClientFinishVoyage(Context.AttemptId, Context.Generation, bSaveSucceeded, FailureMessage); }
		Owner()->RetryRequester.Reset(); Owner()->RetryRequestId = 0;
		Next = ESWVoyagePhase::Idle; break;
	default: break;
	}
	if (Result == ESWVoyageStepResult::Failed) { Fail(Error); return true; }
	if (Next != Context.Phase && !Enter(Next, Error)) Fail(Error);
	return true;
}

void UClassFeatureVoyageTransition::Fail(const FString& Error)
{
	FailureMessage = Error.Left(512);
	UWorld* World = ActiveWorld.Get(); if (!World) return;
	USWRoomProgressSubsystem* Room = World->GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>();
	AMultiGameMode* Mode = World->GetAuthGameMode<AMultiGameMode>(); FString CancelError;
	if (!bCommitted && !Context.bBootstrap)
	{
		if (Room && Room->GetMutableActiveRoom())
		{
			USWRoomSaveGame* Save = Room->GetMutableActiveRoom(); Save->HostProgress = RollbackHost;
			Save->Guests = RollbackGuests; Save->SharedProgress = RollbackShared; Save->bFinalDepartureCompleted = bRollbackFinalCompleted;
			if (bStoryCommitAttempted && !Owner()->RestoreStoryProgress(World, RollbackShared, CancelError))
			{
				FailureMessage = (TEXT("VoyageOriginalStoryRollbackFailed:") + CancelError).Left(512);
				Enter(ESWVoyagePhase::Failed, CancelError);
				for (FParticipant& Participant : Participants) if (Participant.Controller.IsValid())
					Participant.Controller->ClientVoyageFailure(Context.AttemptId, Context.Generation, Mode && Mode->IsRoomHostController(Participant.Controller.Get()), FailureMessage);
				return;
			}
		}
		Enter(ESWVoyagePhase::Idle, CancelError);
		if (Mode) Mode->CancelVoyageResetPreparation();
		if (Context.Reason == ESWVoyageReason::GameOverRetry && Owner()->RetryRequester.IsValid())
			Owner()->RetryRequester->ReportGameOverRetryResult(Owner()->RetryRequestId, false, FailureMessage);
		Owner()->RetryRequester.Reset(); Owner()->RetryRequestId = 0;
		for (FParticipant& Participant : Participants) if (Participant.Controller.IsValid())
		{ Participant.Controller->ClientCancelVoyage(Context.AttemptId, Context.Generation); Participant.Controller->ClientMessage(FailureMessage); }
		return;
	}
	if ((!Context.bBootstrap || bRecoveryBootstrap) && !bRecoveryUsed && Recover()) return;
	Enter(ESWVoyagePhase::Failed, CancelError);
	for (FParticipant& Participant : Participants) if (Participant.Controller.IsValid()) Participant.Controller->ClientVoyageFailure(Context.AttemptId, Context.Generation, Mode && Mode->IsRoomHostController(Participant.Controller.Get()), FailureMessage);
}

bool UClassFeatureVoyageTransition::Recover()
{
	UWorld* World = ActiveWorld.Get(); AMultiGameMode* Mode = World ? World->GetAuthGameMode<AMultiGameMode>() : nullptr;
	if (!Mode) return false;
	bRecoveryUsed = true; bRecoveryPending = true; StoreTargets();
	bRecoveryContinue = Context.bContinue;
	if (bDevelopmentDeparture && Context.Reason == ESWVoyageReason::FinalDeparture)
		World->GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>()->SetDevelopmentFinalDeparturePending(World, true, true);
	FString Error;
	if (!Enter(ESWVoyagePhase::RecoveryTravel, Error)) { bRecoveryPending = false; return false; }
	const bool bAccepted = Context.Reason == ESWVoyageReason::FinalDeparture ? Mode->RequestHostedRoomFinalDepartureTravel()
		: Mode->RequestHostedRoomReturnTravel(Context.Reason == ESWVoyageReason::GameOverRetry);
	if (!bAccepted) bRecoveryPending = false;
	if (bAccepted && bRecoveryContinue)
	{
		USWRoomProgressSubsystem* Room = World->GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>();
		Room->ClearReturnTravelPending(); Room->ClearGameOverRetryTravelPending();
	}
	return bAccepted;
}

bool UClassFeatureVoyageTransition::RetryFailure(ABasePlayerController* Controller, int64 AttemptId, int32 Generation)
{
	AMultiGameMode* Mode = ActiveWorld.IsValid() ? ActiveWorld->GetAuthGameMode<AMultiGameMode>() : nullptr;
	if (!Matches(Controller, AttemptId, Generation) || !Mode || !Mode->IsRoomHostController(Controller)
		|| Context.Phase != ESWVoyagePhase::Failed || AttemptSerial == MAX_int64) return false;
	Context.AttemptId = ++AttemptSerial; bRecoveryUsed = false;
	if (Recover()) return true;
	Fail(FailureMessage.IsEmpty() ? TEXT("VoyageExplicitRecoveryTravelRejected") : FailureMessage);
	return false;
}

bool UClassFeatureVoyageTransition::BeginBootstrap(UWorld* World, FString& OutError)
{
	const bool bRecovery = bRecoveryPending;
	if (IsBusy() && !bRecovery) { OutError = TEXT("VoyageBootstrapAlreadyBusy"); return false; }
	USWRoomProgressSubsystem* Room = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>() : nullptr;
	if (!Room || !Room->GetActiveRoom()) { OutError = TEXT("VoyageBootstrapRoomMissing"); return false; }
	if (!bRecovery)
	{
		Context.Reason = ESWVoyageReason::Return; bDevelopmentDeparture = false; bRecoveryUsed = false;
		TargetHost = Room->GetActiveRoom()->HostProgress; TargetGuests = Room->GetActiveRoom()->Guests; TargetShared = Room->GetActiveRoom()->SharedProgress;
	}
	ActiveWorld = World; Participants.Reset();
	if (!InitializeAttempt(World, true, OutError)) return false;
	bRecoveryBootstrap = bRecovery; bInitialSaveAttempted = false;
	if (bRecovery && bDevelopmentDeparture && !Room->ConsumeDevelopmentFinalDeparturePending(World))
	{ OutError = TEXT("VoyageDevelopmentRecoveryPermissionInvalid"); return false; }
	Context.bContinue = bRecovery ? bRecoveryContinue : !Room->IsNewRoomPending() && !Room->IsGameOverTravelPending();
	bRecoveryPending = false; bCommitted = true;
	if (!Owner()->RestoreStoryProgress(World, TargetShared, OutError)) return false;
	USWRoomSnapshotSubsystem* Snapshot = World->GetSubsystem<USWRoomSnapshotSubsystem>();
	USWVoyageResetSubsystem* Core = World->GetSubsystem<USWVoyageResetSubsystem>();
	if (!Snapshot || !Core) { OutError = TEXT("VoyageBootstrapSubsystemMissing"); return false; }
	if (!Enter(ESWVoyagePhase::Restore, OutError)) return false;
	if (Context.bContinue) for (TActorIterator<AKelvinShip> It(World); It; ++It) It->SetShipRuntimePhysicsEnabled(false);
	Core->SetPreparationSpawnAllowed(true);
	const bool bSnapshotOK = Context.bContinue ? Snapshot->Restore(Room->GetActiveRoom()->WorldSnapshot, OutError, false) : Snapshot->ValidateRegistration(OutError);
	Core->SetPreparationSpawnAllowed(false);
	if (!bSnapshotOK) return false;
	if (TickerHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UClassFeatureVoyageTransition::Tick)); return true;
}
