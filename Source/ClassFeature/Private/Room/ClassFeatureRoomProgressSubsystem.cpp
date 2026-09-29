#include "Room/ClassFeatureRoomProgressSubsystem.h"

#include "BasePlayer.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "MultiGameMode.h"
#include "Room/SWRoomProgressSubsystem.h"
#include "Room/SWRoomSaveGame.h"
#include "Room/SWRoomSnapshotSubsystem.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "Network/SWNetworkLog.h"
#include "Room/SWLevelEntryPoint.h"
#include "KelvinShip.h"
#include "Cannon.h"
#include "WaterSurfaceQueryLibrary.h"
#include "WaterBodyActor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/OverlapResult.h"
#include "Storage/SharedStorageChest.h"
#include "Storage/StorageComponent.h"
#include "Inventory/InventoryComponent.h"
#include "StorySubsystem.h"
#include "StorySaveGame.h"
#include "Upgrade/SharedShipUpgradeState.h"
#include "Upgrade/ShipUpgradeComponent.h"
#include "Containers/Ticker.h"

namespace
{
USWRoomProgressSubsystem* GetRoom(UGameInstance* GameInstance)
{
	USWRoomProgressSubsystem* Room = GameInstance ? GameInstance->GetSubsystem<USWRoomProgressSubsystem>() : nullptr;
	return Room && Room->IsHostedRoom() && !Room->HasStartupError() ? Room : nullptr;
}

FString StorageKey(const FGuid& Id, const FString& Namespace)
{
	return Id.ToString(EGuidFormats::DigitsWithHyphens) + TEXT("|") + Namespace;
}

bool IsShipPartBlocked(UWorld* World, AKelvinShip* Ship, UPrimitiveComponent* Part)
{
	if (!Part || Part->GetCollisionEnabled() == ECollisionEnabled::NoCollision) return false;
	TArray<FOverlapResult> Overlaps;
	FComponentQueryParams Params(TEXT("SWRoomShipPlacement"), Ship);
	Part->ComponentOverlapMulti(Overlaps, World, Part->GetComponentLocation(), Part->GetComponentQuat(), ECC_WorldStatic, Params);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		const UPrimitiveComponent* Other = Overlap.GetComponent();
		if (Overlap.GetActor() && Overlap.GetActor()->IsA<AWaterBody>()) continue;
		if (Overlap.bBlockingHit && Other && Other->GetCollisionObjectType() == ECC_WorldStatic)
		{
			UE_LOG(LogSWRoom, Warning, TEXT("Room ship static overlap with %s"), *GetNameSafe(Overlap.GetActor()));
			return true;
		}
	}
	return false;
}

bool PlaceShipSafely(UWorld* World, AKelvinShip* Ship, ASWLevelEntryPoint* Entry, bool bUseEntry, FString& OutReason)
{
	if (!World || !Ship || !Entry) { OutReason = TEXT("Ship or entry marker missing"); return false; }
	if (!bUseEntry)
	{
		// The saved transform is authoritative on Continue. Collision is diagnostic only.
		Ship->SetShipRuntimePhysicsEnabled(true);
		const bool bBlocked = IsShipPartBlocked(World, Ship, Cast<UPrimitiveComponent>(Ship->GetRootComponent()))
			|| IsShipPartBlocked(World, Ship, Cast<UPrimitiveComponent>(Ship->GetDeckMeshSimple()))
			|| IsShipPartBlocked(World, Ship, Cast<UPrimitiveComponent>(Ship->GetDeckMeshComplex()));
		if (bBlocked) UE_LOG(LogSWRoom, Warning, TEXT("Flow=ShipPlacement SavedTransformOverlap Ship=%s"), *Ship->GetPathName());
		OutReason = TEXT("Saved ship transform");
		return true;
	}
	const FTransform Saved = Ship->GetActorTransform();
	const FTransform Candidates[3] = {Saved, FTransform(FRotator(0.0, Saved.Rotator().Yaw, 0.0), Saved.GetLocation()), Entry->GetActorTransform()};
	Ship->SetShipRuntimePhysicsEnabled(false);
	for (int32 Index = bUseEntry ? 2 : 0; Index < 3; ++Index)
	{
		FTransform Candidate = Candidates[Index];
		if (Index == 0 && (FMath::Abs(Candidate.Rotator().Pitch) > 45.0 || FMath::Abs(Candidate.Rotator().Roll) > 45.0)) continue;
		float WaterZ = 0.0f;
		if (!FWaterSurfaceQueryLibrary::QueryWaterSurface(World, Candidate.GetLocation(), WaterZ))
		{
			UE_LOG(LogSWRoom, Warning, TEXT("Room ship candidate %d: water query unavailable"), Index);
			if (Index != 2) continue;
		}
		if (Index == 1)
		{
			FVector Position = Candidate.GetLocation();
			Position.Z = FMath::Max(Position.Z, WaterZ);
			Candidate.SetLocation(Position);
		}
		Ship->SetActorTransform(Candidate, false, nullptr, ETeleportType::TeleportPhysics);
		UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Ship->GetRootComponent());
		if (!Root || (WaterZ != 0.0f && Root->GetComponentLocation().Z < WaterZ - Root->Bounds.BoxExtent.Z))
		{ UE_LOG(LogSWRoom, Warning, TEXT("Room ship candidate %d: root below safe surface"), Index); continue; }
		bool bBlocked = IsShipPartBlocked(World, Ship, Root);
		for (UPrimitiveComponent* Deck : {Cast<UPrimitiveComponent>(Ship->GetDeckMeshSimple()), Cast<UPrimitiveComponent>(Ship->GetDeckMeshComplex())})
			bBlocked |= IsShipPartBlocked(World, Ship, Deck);
		if (bBlocked) { UE_LOG(LogSWRoom, Warning, TEXT("Room ship candidate %d: static blocking overlap"), Index); continue; }
		OutReason = Index == 0 ? TEXT("Saved ship transform") : (Index == 1 ? TEXT("Level ship transform") : TEXT("Ship entry marker fallback"));
		Ship->SetShipRuntimePhysicsEnabled(true);
		return true;
	}
	OutReason = TEXT("No safe ship restore transform");
	return false;
}

bool HasPostPlacementBlock(UWorld* World, AKelvinShip* Ship)
{
	for (UPrimitiveComponent* Part : {Cast<UPrimitiveComponent>(Ship->GetRootComponent()),
		Cast<UPrimitiveComponent>(Ship->GetDeckMeshSimple()), Cast<UPrimitiveComponent>(Ship->GetDeckMeshComplex())})
	{
		if (IsShipPartBlocked(World, Ship, Part)) return true;
	}
	for (TActorIterator<ABasePlayer> It(World); It; ++It)
	{
		if (UCapsuleComponent* Capsule = It->GetCapsuleComponent())
		{
			FCollisionQueryParams PlayerParams(SCENE_QUERY_STAT(SWRoomPlayerPostPlacement), false, *It);
			if (World->OverlapBlockingTestByChannel(Capsule->GetComponentLocation(), Capsule->GetComponentQuat(),
				ECC_WorldStatic, Capsule->GetCollisionShape(), PlayerParams)) return true;
		}
	}
	return false;
}
}

void UClassFeatureRoomProgressSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PostLoadHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UClassFeatureRoomProgressSubsystem::HandlePostLoadMap);
}

void UClassFeatureRoomProgressSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadHandle);
	if (RestoreTickerHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(RestoreTickerHandle);
	Super::Deinitialize();
}

void UClassFeatureRoomProgressSubsystem::HandlePostLoadMap(UWorld* World)
{
	if (!GetRoom(GetGameInstance()) || !World || World->GetGameInstance() != GetGameInstance()) return;
	PendingWorld = World;
	RestoreDeadline = FPlatformTime::Seconds() + 10.0;
	bReturning = false;
	bWorldSnapshotRestored = false;
	bReturnShipPlaced = false;
	bShipSafetyFallbackUsed = false;
	ShipSafetyCheckAt = 0.0;
	if (RestoreTickerHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(RestoreTickerHandle);
	RestoreTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UClassFeatureRoomProgressSubsystem::TickRestore), 0.0f);
}

bool UClassFeatureRoomProgressSubsystem::TickRestore(float DeltaTime)
{
	UWorld* World = PendingWorld.Get();
	if (!World || World->GetGameInstance() != GetGameInstance()) return false;
	AMultiGameMode* Mode = World->GetAuthGameMode<AMultiGameMode>();
	if (!Mode || !World->HasBegunPlay())
	{
		if (FPlatformTime::Seconds() < RestoreDeadline) return true;
		UE_LOG(LogSWRoom, Error, TEXT("Hosted room world did not become ready"));
		FPlatformMisc::RequestExit(false);
		return false;
	}
	USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
	if (!Room || !Room->GetActiveRoom()) return false;
	if (!bWorldSnapshotRestored)
		UE_LOG(LogSWRoom, Display, TEXT("Flow=WorldRestore RoomId=%s Mode=%s Phase=Begin"),
			*Room->GetActiveRoom()->RoomId.ToString(), Room->IsNewRoomPending() ? TEXT("New")
			: Room->IsReturnTravelPending() ? TEXT("Return") : Room->IsGameOverTravelPending() ? TEXT("GameOver") : TEXT("Continue"));
	if ((Room->IsNewRoomPending() || Room->IsReturnTravelPending() || Room->IsGameOverTravelPending()) && !bWorldSnapshotRestored)
	{
		USWRoomSnapshotSubsystem* Snapshot = World->GetSubsystem<USWRoomSnapshotSubsystem>();
		FString Error;
		if (!Snapshot || !Snapshot->ValidateRegistration(Error))
		{
			UE_LOG(LogSWRoom, Error, TEXT("Hosted room registration validation failed: %s"), *Error);
			FPlatformMisc::RequestExit(false);
			return false;
		}
		bWorldSnapshotRestored = true;
		UE_LOG(LogSWRoom, Display, TEXT("Flow=WorldRestore Phase=RegistrationValidated"));
	}
	if (!bWorldSnapshotRestored)
	{
		USWRoomSnapshotSubsystem* Snapshot = World->GetSubsystem<USWRoomSnapshotSubsystem>();
		FString Error;
		for (TActorIterator<AKelvinShip> It(World); It; ++It) It->SetShipRuntimePhysicsEnabled(false);
		if (!Snapshot || !Snapshot->Restore(Room->GetActiveRoom()->WorldSnapshot, Error, false))
		{
			UE_LOG(LogSWRoom, Error, TEXT("Hosted room snapshot restore failed: %s"), *Error);
			FPlatformMisc::RequestExit(false);
			return false;
		}
		bWorldSnapshotRestored = true;
		UE_LOG(LogSWRoom, Display, TEXT("Flow=WorldRestore Phase=SnapshotApplied Actors=%d"), Room->GetActiveRoom()->WorldSnapshot.Actors.Num());
	}
	if (!bReturnShipPlaced)
	{
		AKelvinShip* Ship = nullptr;
		ASWLevelEntryPoint* ShipEntry = nullptr;
		for (TActorIterator<AKelvinShip> It(World); It; ++It) Ship = *It;
		for (TActorIterator<ASWLevelEntryPoint> It(World); It; ++It)
			if (It->EntryRole == ESWLevelEntryRole::Ship) ShipEntry = *It;
		if (!Ship || !ShipEntry) { UE_LOG(LogSWRoom, Error, TEXT("Return ship or entry missing")); return false; }
		FString Placement;
		if (!PlaceShipSafely(World, Ship, ShipEntry,
			Room->IsNewRoomPending() || Room->IsReturnTravelPending() || Room->IsGameOverTravelPending(), Placement))
		{ UE_LOG(LogSWRoom, Error, TEXT("Room ship safety failed: %s"), *Placement); FPlatformMisc::RequestExit(false); return false; }
		if (!Room->IsNewRoomPending() && !Room->IsReturnTravelPending() && !Room->IsGameOverTravelPending())
		{
			const USWRoomSnapshotComponent* Id = Ship->FindComponentByClass<USWRoomSnapshotComponent>();
			const FSWRoomActorRecord* SavedShip = Id ? Room->GetActiveRoom()->WorldSnapshot.Actors.FindByPredicate(
				[Id](const FSWRoomActorRecord& Record) { return Record.StableId == Id->StableId; }) : nullptr;
			if (SavedShip && SavedShip->MotionState.bHasMotion)
				if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Ship->GetRootComponent()))
				{
					Root->SetPhysicsLinearVelocity(SavedShip->MotionState.LinearVelocity);
					Root->SetPhysicsAngularVelocityInDegrees(SavedShip->MotionState.AngularVelocityDegrees);
				}
		}
		UE_LOG(LogSWRoom, Display, TEXT("Room ship placement: %s"), *Placement);
		UE_LOG(LogSWRoom, Display, TEXT("Flow=ShipPlacement Result=Placed Strategy=%s"), *Placement);
		bReturnShipPlaced = true;
		ShipSafetyCheckAt = FPlatformTime::Seconds() + 0.5;
		return true;
	}
	if (bReturnShipPlaced)
	{
		if (FPlatformTime::Seconds() < ShipSafetyCheckAt) return true;
		AKelvinShip* Ship = nullptr;
		ASWLevelEntryPoint* ShipEntry = nullptr;
		for (TActorIterator<AKelvinShip> It(World); It; ++It) Ship = *It;
		for (TActorIterator<ASWLevelEntryPoint> It(World); It; ++It)
			if (It->EntryRole == ESWLevelEntryRole::Ship) ShipEntry = *It;
		if (!Ship || !ShipEntry) return false;
		if (HasPostPlacementBlock(World, Ship))
		{
			if (!Room->IsNewRoomPending() && !Room->IsReturnTravelPending() && !Room->IsGameOverTravelPending())
			{
				UE_LOG(LogSWRoom, Warning, TEXT("Flow=ShipPlacement SavedTransformPostPhysicsOverlap Ship=%s"), *Ship->GetPathName());
			}
			else
			{
			if (bShipSafetyFallbackUsed)
			{ UE_LOG(LogSWRoom, Error, TEXT("Room ship post-placement overlap persisted")); FPlatformMisc::RequestExit(false); return false; }
			FString Reason;
			if (!PlaceShipSafely(World, Ship, ShipEntry, true, Reason))
			{ UE_LOG(LogSWRoom, Error, TEXT("Room ship fallback failed: %s"), *Reason); FPlatformMisc::RequestExit(false); return false; }
			bShipSafetyFallbackUsed = true;
			ShipSafetyCheckAt = FPlatformTime::Seconds() + 0.5;
			return true;
			}
		}
	}
	if (!RestoreSharedWorld(World))
	{
		if (FPlatformTime::Seconds() < RestoreDeadline) return true;
		UE_LOG(LogSWRoom, Error, TEXT("Hosted room shared progress restore failed"));
		FPlatformMisc::RequestExit(false);
		return false;
	}
	if (Room->IsReturnTravelPending())
	{
		bool bHostPresent = false;
		for (TActorIterator<ABasePlayer> It(World); It; ++It)
			if (It->GetController() && Mode->GetPlayerIndex(It->GetController()) == 0) bHostPresent = true;
		if (!bHostPresent) return true;
		FString Error;
		if (!TrySave(World, ESWRoomSaveKind::Return, Error))
		{
			UE_LOG(LogSWRoom, Error, TEXT("Return happened but room save failed: %s"), *Error);
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
				if (APlayerController* Controller = It->Get()) Controller->ClientMessage(TEXT("귀환은 되었지만 저장 실패. 수동 저장을 다시 시도하세요."));
			Room->ClearReturnTravelPending();
		}
	}
	Room->ClearGameOverTravelPending();
	if (USWRoomSnapshotSubsystem* Snapshot = World->GetSubsystem<USWRoomSnapshotSubsystem>())
	{
		FString FinalizeError;
		if (!Snapshot->CompleteRestore(FinalizeError))
		{
			UE_LOG(LogSWRoom, Error, TEXT("Hosted room final restore failed: %s"), *FinalizeError);
			return false;
		}
	}
	for (TActorIterator<ABasePlayer> It(World); It; ++It)
	{
		FString EffectsError;
		if (!It->FinalizeRoomProgressEffects(EffectsError))
		{
			UE_LOG(LogSWRoom, Error, TEXT("Player effects restore failed: %s"), *EffectsError);
			return false;
		}
	}
	if (!Room->IsNewRoomPending() && !Room->IsReturnTravelPending())
	{
		if (USWRoomSnapshotSubsystem* Snapshot = World->GetSubsystem<USWRoomSnapshotSubsystem>())
		{
			FSWRoomWorldSnapshot Observed;
			FString AuditError;
			TArray<FString> Differences;
			if (!Snapshot->Capture(Observed, ESWRoomSaveKind::Manual,
				Room->GetActiveRoom()->CaptureSequence, AuditError)
				|| !Snapshot->CompareRestored(Room->GetActiveRoom()->WorldSnapshot, Observed, Differences))
			{
				for (const FString& Difference : Differences)
					UE_LOG(LogSWRoom, Error, TEXT("Flow=RestoreAudit %s"), *Difference);
				UE_LOG(LogSWRoom, Error, TEXT("Flow=RestoreAudit Result=Failed Reason=%s Differences=%d"), *AuditError, Differences.Num());
				return false;
			}
		}
		const USWRoomSaveGame* SavedRoom = Room->GetActiveRoom();
		for (TActorIterator<ABasePlayer> It(World); It; ++It)
		{
			ABasePlayer* Player = *It;
			if (!Player->GetController()) continue;
			const FSWRoomPlayerProgress* Expected = nullptr;
			if (Mode->GetPlayerIndex(Player->GetController()) == 0) Expected = &SavedRoom->HostProgress;
			else if (const APlayerState* State = Player->GetPlayerState())
				if (const FSWRoomGuestProgress* Guest = SavedRoom->Guests.FindByPredicate(
					[State](const FSWRoomGuestProgress& Entry) { return Entry.DisplayName == State->GetPlayerName(); }))
					Expected = &Guest->Progress;
			if (!Expected) continue;
			FSWRoomPlayerProgress Actual;
			Player->CaptureRoomProgress(Actual);
			const bool bStatsMatch = FMath::IsNearlyEqual(Expected->CurrentHealth, Actual.CurrentHealth, 0.01f)
				&& FMath::IsNearlyEqual(Expected->MaximumHealth, Actual.MaximumHealth, 0.01f)
				&& FMath::IsNearlyEqual(Expected->BaseStrength, Actual.BaseStrength, 0.01f)
				&& FMath::IsNearlyEqual(Expected->BaseMoveSpeed, Actual.BaseMoveSpeed, 0.01f)
				&& FMath::IsNearlyEqual(Expected->BaseMoveSpeedMultiplier, Actual.BaseMoveSpeedMultiplier, 0.01f)
				&& FMath::IsNearlyEqual(Expected->BaseAttackSpeedMultiplier, Actual.BaseAttackSpeedMultiplier, 0.01f);
			bool bEffectsMatch = Expected->ActiveEffects.Num() == Actual.ActiveEffects.Num();
			const float TimerTolerance = FMath::Max(World->GetDeltaSeconds(), 1.f / 60.f);
			for (int32 Index = 0; bEffectsMatch && Index < Expected->ActiveEffects.Num(); ++Index)
			{
				const FSWRoomGameplayEffectState& Before = Expected->ActiveEffects[Index];
				const FSWRoomGameplayEffectState& After = Actual.ActiveEffects[Index];
				bEffectsMatch = Before.EffectClass == After.EffectClass && Before.StackCount == After.StackCount
					&& (Before.DurationRemaining < 0.f || FMath::Abs(Before.DurationRemaining - After.DurationRemaining) <= TimerTolerance)
					&& (Before.NextPeriodRemaining < 0.f || FMath::Abs(Before.NextPeriodRemaining - After.NextPeriodRemaining) <= TimerTolerance);
			}
			if (!bStatsMatch || !bEffectsMatch || Expected->EquippedItemTag != Actual.EquippedItemTag)
			{
				UE_LOG(LogSWRoom, Error,
					TEXT("Flow=RestoreAudit Result=Failed Player=%s Stats=%d Effects=%d EquippedExpected=%s EquippedActual=%s"),
					*Player->GetPathName(), bStatsMatch, bEffectsMatch,
					*Expected->EquippedItemTag.ToString(), *Actual.EquippedItemTag.ToString());
				return false;
			}
		}
	}
	if (!Room->IsNewRoomPending()) Mode->SetHostedRoomWorldReady();
	Mode->MarkHostedRoomWorldReady();
	UE_LOG(LogSWRoom, Display, TEXT("Flow=WorldRestore RoomId=%s Result=Ready ReturnPending=%d"),
		*Room->GetActiveRoom()->RoomId.ToString(), Room->IsReturnTravelPending());
	Mode->OnGameOverRequested.AddDynamic(this, &UClassFeatureRoomProgressSubsystem::HandleGameOverRestart);
	PendingWorld.Reset();
	RestoreTickerHandle.Reset();
	return false;
}

void UClassFeatureRoomProgressSubsystem::HandleGameOverRestart()
{
	UE_LOG(LogSWRoom, Display, TEXT("Flow=GameOver Phase=CapturePermanent NoDiskWrite=1"));
	if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		if (!GetRoom(GetGameInstance()) || !CaptureSharedWorld(World))
		{
			UE_LOG(LogSWRoom, Error, TEXT("Hosted room permanent progress capture failed during game-over restart"));
		}
	}
}

bool UClassFeatureRoomProgressSubsystem::RestoreSharedWorld(UWorld* World)
{
	USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
	const USWRoomSaveGame* Save = Room ? Room->GetActiveRoom() : nullptr;
	if (!World || !Save) return false;
	const FSWRoomSharedProgress& Shared = Save->SharedProgress;
	for (const FSWRoomCaptureIssue& Issue : Shared.CaptureIssues)
		UE_LOG(LogSWRoom, Warning, TEXT("Flow=SharedRestore Result=Partial Domain=%s Field=%s Reason=%s"),
			*Issue.Domain.ToString(), *Issue.FieldKey.ToString(), *Issue.Reason);
	ASharedShipUpgradeState* Ship = ASharedShipUpgradeState::Find(World);
	if (!Ship && !Shared.ShipUpgradeNodeIds.IsEmpty() && FPlatformTime::Seconds() < RestoreDeadline) return false;
	if (UStorySubsystem* Story = GetGameInstance()->GetSubsystem<UStorySubsystem>())
	{
		UStorySaveGame* StoryData = NewObject<UStorySaveGame>(this);
		StoryData->StoryDefinitionId = Shared.StoryDefinitionId;
		StoryData->Facts = Shared.Facts;
		StoryData->AppliedActionKeys = Shared.AppliedActionKeys;
		for (const FSWRoomCounterProgress& Counter : Shared.Counters)
		{
			FStoryCounterValue& Value = StoryData->Counters.AddDefaulted_GetRef();
			Value.CounterTag = Counter.Tag;
			Value.Value = Counter.Value;
		}
		if (Shared.StoryDefinitionId.IsValid() && !Story->ApplyRoomProgress(StoryData)) return false;
	}
	if (Ship)
		if (UShipUpgradeComponent* Upgrade = Ship->GetUpgradeComponent())
			Upgrade->RestoreActiveNodeIds(Shared.ShipUpgradeNodeIds);
	TSet<FString> Keys;
	for (TActorIterator<ASharedStorageChest> It(World); It; ++It)
	{
		ASharedStorageChest* Chest = *It;
		const FString Key = StorageKey(Chest->PersistentChestId, Chest->SaveNamespace);
		if (!Chest->PersistentChestId.IsValid() || Keys.Contains(Key)) return false;
		Keys.Add(Key);
		const FSWRoomStorageProgress* Stored = Shared.Storage.FindByPredicate([&Key](const FSWRoomStorageProgress& Entry)
		{
			return StorageKey(Entry.ChestId, Entry.SaveNamespace) == Key;
		});
		if (!Stored) continue;
		TArray<FInventorySlot> Slots;
		for (const FSWRoomStorageSlot& Entry : Stored->Slots)
		{
			FInventorySlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.ItemTag = Entry.ItemTag;
			Slot.Count = Entry.Count;
		}
		if (!Chest->GetStorageComponent()->ConfigureTabbedStorage(Chest->InitialSlotsPerTab, Slots, Stored->SlotsPerTab)) return false;
	}
	return true;
}

bool UClassFeatureRoomProgressSubsystem::CaptureSharedWorld(UWorld* World)
{
	USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
	USWRoomSaveGame* Save = Room ? Room->GetMutableActiveRoom() : nullptr;
	if (!World || !Save)
	{
		UE_LOG(LogSWRoomSave, Error, TEXT("Flow=SharedCapture Result=Failed Phase=Precondition World=%d Room=%d"), World != nullptr, Save != nullptr);
		return false;
	}
	FSWRoomSharedProgress& Shared = Save->SharedProgress;
	if (UStorySubsystem* Story = GetGameInstance()->GetSubsystem<UStorySubsystem>())
	{
		UStorySaveGame* StoryData = Story->BuildRoomProgress();
		if (!StoryData)
		{
			UE_LOG(LogSWRoomSave, Error, TEXT("Flow=SharedCapture Result=Failed RoomId=%s Sequence=%llu Object=Story Reason=BuildRoomProgressReturnedNull"),
				*Save->RoomId.ToString(), Save->CaptureSequence + 1);
			return false;
		}
		Shared.StoryDefinitionId = StoryData->StoryDefinitionId;
		Shared.Facts = StoryData->Facts;
		Shared.Counters.Reset();
		for (const FStoryCounterValue& Counter : StoryData->Counters)
		{
			FSWRoomCounterProgress& Value = Shared.Counters.AddDefaulted_GetRef();
			Value.Tag = Counter.CounterTag;
			Value.Value = Counter.Value;
		}
		Shared.AppliedActionKeys = StoryData->AppliedActionKeys;
		UE_LOG(LogSWRoomSave, Display,
			TEXT("Flow=SharedCapture Result=Success RoomId=%s Sequence=%llu Object=Story Definition=%s Facts=%d Counters=%d Actions=%d"),
			*Save->RoomId.ToString(), Save->CaptureSequence + 1, *Shared.StoryDefinitionId.ToString(),
			Shared.Facts.Num(), Shared.Counters.Num(), Shared.AppliedActionKeys.Num());
	}
	else UE_LOG(LogSWRoomSave, Warning, TEXT("Flow=SharedCapture Result=Skipped RoomId=%s Sequence=%llu Object=Story Reason=SubsystemUnavailable"),
		*Save->RoomId.ToString(), Save->CaptureSequence + 1);
	if (ASharedShipUpgradeState* Ship = ASharedShipUpgradeState::Find(World))
	{
		if (const UShipUpgradeComponent* Upgrade = Ship->GetUpgradeComponent())
		{
			Shared.ShipUpgradeNodeIds = Upgrade->GetActiveNodeIds();
			UE_LOG(LogSWRoomSave, Display, TEXT("Flow=SharedCapture Result=Success RoomId=%s Sequence=%llu Object=ShipUpgrade Actor=%s Nodes=%d"),
				*Save->RoomId.ToString(), Save->CaptureSequence + 1, *Ship->GetPathName(), Shared.ShipUpgradeNodeIds.Num());
		}
		else UE_LOG(LogSWRoomSave, Warning, TEXT("Flow=SharedCapture Result=Skipped RoomId=%s Sequence=%llu Object=ShipUpgrade Actor=%s Reason=MissingUpgradeComponent"),
			*Save->RoomId.ToString(), Save->CaptureSequence + 1, *Ship->GetPathName());
	}
	else UE_LOG(LogSWRoomSave, Warning, TEXT("Flow=SharedCapture Result=Skipped RoomId=%s Sequence=%llu Object=ShipUpgrade Reason=SharedStateActorUnavailable"),
		*Save->RoomId.ToString(), Save->CaptureSequence + 1);
	TSet<FString> Keys;
	for (TActorIterator<ASharedStorageChest> It(World); It; ++It)
	{
		ASharedStorageChest* Chest = *It;
		const FString Key = StorageKey(Chest->PersistentChestId, Chest->SaveNamespace);
		if (!Chest->PersistentChestId.IsValid() || Keys.Contains(Key))
		{
			UE_LOG(LogSWRoomSave, Error,
				TEXT("Flow=SharedCapture Result=Failed RoomId=%s Sequence=%llu Object=Storage Actor=%s ChestId=%s Namespace=%s Reason=%s"),
				*Save->RoomId.ToString(), Save->CaptureSequence + 1, *Chest->GetPathName(),
				*Chest->PersistentChestId.ToString(), *Chest->SaveNamespace,
				Chest->PersistentChestId.IsValid() ? TEXT("DuplicateStorageKey") : TEXT("InvalidChestId"));
			return false;
		}
		Keys.Add(Key);
		UStorageComponent* Storage = Chest->GetStorageComponent();
		Storage->ReturnAllReservedCursors();
		FSWRoomStorageProgress* Entry = Shared.Storage.FindByPredicate([&Key](const FSWRoomStorageProgress& Record)
		{
			return StorageKey(Record.ChestId, Record.SaveNamespace) == Key;
		});
		if (!Entry) Entry = &Shared.Storage.AddDefaulted_GetRef();
		Entry->ChestId = Chest->PersistentChestId;
		Entry->SaveNamespace = Chest->SaveNamespace;
		Entry->SlotsPerTab = Storage->GetSlotsPerTab();
		Entry->Slots.Reset();
		for (const FInventorySlot& Slot : Storage->GetPersistentSlots())
		{
			FSWRoomStorageSlot& SavedSlot = Entry->Slots.AddDefaulted_GetRef();
			SavedSlot.ItemTag = Slot.ItemTag;
			SavedSlot.Count = Slot.Count;
		}
		UE_LOG(LogSWRoomSave, Display,
			TEXT("Flow=SharedCapture Result=Success RoomId=%s Sequence=%llu Object=Storage Actor=%s ChestId=%s Namespace=%s SlotsPerTab=%d Slots=%d"),
			*Save->RoomId.ToString(), Save->CaptureSequence + 1, *Chest->GetPathName(),
			*Entry->ChestId.ToString(), *Entry->SaveNamespace, Entry->SlotsPerTab, Entry->Slots.Num());
	}
	Shared.ShipUpgradeNodeIds.Sort(FNameLexicalLess());
	Shared.Storage.Sort([](const FSWRoomStorageProgress& A, const FSWRoomStorageProgress& B)
	{
		return StorageKey(A.ChestId, A.SaveNamespace) < StorageKey(B.ChestId, B.SaveNamespace);
	});
	for (const FSWRoomCaptureIssue& Issue : Shared.CaptureIssues)
		UE_LOG(LogSWRoomSave, Warning, TEXT("Flow=SharedCaptureIssue RoomId=%s Sequence=%llu Domain=%s Field=%s Reason=%s"),
			*Save->RoomId.ToString(), Save->CaptureSequence + 1,
			*Issue.Domain.ToString(), *Issue.FieldKey.ToString(), *Issue.Reason);
	UE_LOG(LogSWRoomSave, Display, TEXT("Flow=SharedCapture Result=%s RoomId=%s Sequence=%llu Storage=%d Issues=%d"),
		Shared.CaptureIssues.IsEmpty() ? TEXT("Success") : TEXT("Partial"),
		*Save->RoomId.ToString(), Save->CaptureSequence + 1, Shared.Storage.Num(), Shared.CaptureIssues.Num());
	return true;
}

void UClassFeatureRoomProgressSubsystem::RestorePlayer(ABasePlayer* Player)
{
	USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
	const USWRoomSaveGame* Save = Room ? Room->GetActiveRoom() : nullptr;
	AMultiGameMode* Mode = Player && Player->GetWorld() ? Player->GetWorld()->GetAuthGameMode<AMultiGameMode>() : nullptr;
	if (!Save || !Mode || !Player->GetController()) return;
	auto ApplyProgress = [Player, Room](const FSWRoomPlayerProgress& Stored)
	{
		for (const FSWRoomCaptureIssue& Issue : Stored.CaptureIssues)
			UE_LOG(LogSWRoom, Warning, TEXT("Flow=PlayerRestore Result=Partial Player=%s Domain=%s Field=%s Reason=%s"),
				*Issue.PlayerKey, *Issue.Domain.ToString(), *Issue.FieldKey.ToString(), *Issue.Reason);
		FSWRoomPlayerProgress Progress = Stored;
		if (Room->IsReturnTravelPending())
		{
			Progress.bHasResumeTransform = false;
			Progress.bWasDead = false;
			Progress.CurrentHealth = Progress.MaximumHealth;
			Progress.bRestoreFullHealth = true;
			Progress.bHasMovement = false;
			Progress.bEffectsCaptured = false;
			Progress.ActiveEffects.Reset();
		}
		Player->RestoreRoomProgress(Progress);
	};
	if (Mode->GetPlayerIndex(Player->GetController()) == 0) ApplyProgress(Save->HostProgress);
	else if (const APlayerState* State = Player->GetPlayerState())
	{
		const FString Name = State->GetPlayerName();
		if (const FSWRoomGuestProgress* Guest = Save->Guests.FindByPredicate([&Name](const FSWRoomGuestProgress& Entry) { return Entry.DisplayName == Name; }))
			ApplyProgress(Guest->Progress);
		else ApplyProgress(FSWRoomPlayerProgress());
	}
	UE_LOG(LogSWRoom, Display, TEXT("Flow=PlayerRestore RoomId=%s PlayerIndex=%d Result=Applied Return=%d"),
		*Save->RoomId.ToString(), Mode->GetPlayerIndex(Player->GetController()), Room->IsReturnTravelPending());
	if (const USWRoomSnapshotSubsystem* Snapshot = Player->GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>();
		!Snapshot || !Snapshot->IsRestoringSnapshot())
	{
		FString EffectsError;
		if (!Player->FinalizeRoomProgressEffects(EffectsError))
			UE_LOG(LogSWRoom, Error, TEXT("Immediate player effects restore failed: %s"), *EffectsError);
	}
	if (Mode->GetPlayerIndex(Player->GetController()) == 0 && Room->IsNewRoomPending())
	{
		FString Error;
		if (!TrySave(Player->GetWorld(), ESWRoomSaveKind::New, Error)) UE_LOG(LogSWRoom, Error, TEXT("Hosted room initial save failed: %s"), *Error);
	}
}

bool UClassFeatureRoomProgressSubsystem::TrySave(UWorld* World, ESWRoomSaveKind Kind, FString& OutError)
{
	USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
	USWRoomSaveGame* Save = Room ? Room->GetMutableActiveRoom() : nullptr;
	UE_LOG(LogSWRoom, Display, TEXT("Flow=Save Kind=%s RoomId=%s Phase=Requested"), *UEnum::GetValueAsString(Kind),
		Save ? *Save->RoomId.ToString() : TEXT("None"));
	UE_LOG(LogSWRoomSave, Display, TEXT("Flow=SaveAttempt Phase=Requested RoomId=%s Sequence=%llu Kind=%s World=%s"),
		Save ? *Save->RoomId.ToString() : TEXT("None"), Save ? Save->CaptureSequence + 1 : 0,
		*UEnum::GetValueAsString(Kind), *GetNameSafe(World));
	if (!World || !Save || bSaving || World->GetNetMode() == NM_Client)
	{
		OutError = TEXT("Room save is unavailable");
		UE_LOG(LogSWRoomSave, Error, TEXT("Flow=SaveAttempt Result=Failed Phase=Precondition RoomId=%s Sequence=%llu World=%d Save=%d Busy=%d Client=%d Reason=%s"),
			Save ? *Save->RoomId.ToString() : TEXT("None"), Save ? Save->CaptureSequence + 1 : 0,
			World != nullptr, Save != nullptr, bSaving, World && World->GetNetMode() == NM_Client, *OutError);
		UE_LOG(LogSWRoom, Error, TEXT("Flow=Save Kind=%s Result=Failed Reason=%s"), *UEnum::GetValueAsString(Kind), *OutError);
		return false;
	}
	TGuardValue<bool> SavingGuard(bSaving, true);
	LastCaptureIssueCount = 0;
	for (TActorIterator<ABasePlayer> It(World); It; ++It)
	{
		if (UInventoryComponent* Inventory = It->GetInventoryComponent()) Inventory->ReturnCursorToOriginalSlot();
		CapturePlayer(*It);
	}
	if (!CaptureSharedWorld(World))
	{
		OutError = TEXT("Shared progress capture failed");
		UE_LOG(LogSWRoomSave, Error, TEXT("Flow=SaveAttempt Result=Failed Phase=SharedCapture RoomId=%s Sequence=%llu Reason=%s"),
			*Save->RoomId.ToString(), Save->CaptureSequence + 1, *OutError);
		UE_LOG(LogSWRoom, Error, TEXT("Flow=Save Kind=%s Result=Failed Reason=%s"), *UEnum::GetValueAsString(Kind), *OutError);
		return false;
	}
	USWRoomSnapshotSubsystem* Snapshot = World->GetSubsystem<USWRoomSnapshotSubsystem>();
	const uint64 Sequence = Save->CaptureSequence + 1;
	FSWRoomWorldSnapshot WorldData;
	if (!Snapshot || !Snapshot->Capture(WorldData, Kind, Sequence, OutError))
	{
		UE_LOG(LogSWRoomSave, Error, TEXT("Flow=SaveAttempt Result=Failed Phase=WorldCapture RoomId=%s Sequence=%llu HasSubsystem=%d Reason=%s"),
			*Save->RoomId.ToString(), Sequence, Snapshot != nullptr, *OutError);
		UE_LOG(LogSWRoom, Error, TEXT("Flow=Save Kind=%s Sequence=%llu Result=Failed Phase=WorldCapture Reason=%s"), *UEnum::GetValueAsString(Kind), Sequence, *OutError);
		return false;
	}
	TSet<FString> UniqueIssueKeys;
	auto DeduplicateIssues = [&UniqueIssueKeys](TArray<FSWRoomCaptureIssue>& Issues)
	{
		for (int32 Index = Issues.Num() - 1; Index >= 0; --Index)
		{
			const FSWRoomCaptureIssue& Issue = Issues[Index];
			const FString Owner = Issue.StableId.IsValid() ? Issue.StableId.ToString()
				: !Issue.PlayerKey.IsEmpty() ? Issue.PlayerKey : Issue.OwnerPath;
			const FString Key = FString::FromInt(static_cast<int32>(Issue.Scope)) + TEXT("|") + Owner
				+ TEXT("|") + Issue.Domain.ToString() + TEXT("|") + Issue.FieldKey.ToString();
			if (UniqueIssueKeys.Contains(Key)) Issues.RemoveAt(Index);
			else UniqueIssueKeys.Add(Key);
		}
	};
	DeduplicateIssues(WorldData.CaptureIssues);
	DeduplicateIssues(Save->HostProgress.CaptureIssues);
	DeduplicateIssues(Save->SharedProgress.CaptureIssues);
	for (FSWRoomGuestProgress& Guest : Save->Guests) DeduplicateIssues(Guest.Progress.CaptureIssues);
	LastCaptureIssueCount = UniqueIssueKeys.Num();
	UE_LOG(LogSWRoomSave, Display,
		TEXT("Flow=SaveAttempt Phase=Captured RoomId=%s Sequence=%llu Kind=%s Actors=%d Unloaded=%d Tombstones=%d Systems=%d Guests=%d Issues=%d Unsupported=%d"),
		*Save->RoomId.ToString(), Sequence, *UEnum::GetValueAsString(Kind), WorldData.Actors.Num(),
		WorldData.UnloadedActors.Num(), WorldData.DestroyedLevelActorIds.Num(), WorldData.Systems.Num(),
		Save->Guests.Num(), LastCaptureIssueCount, Snapshot->GetUnsupportedCandidateCount());
	UE_LOG(LogSWRoom, Display, TEXT("Flow=Save Kind=%s Sequence=%llu Phase=Captured Actors=%d Tombstones=%d Unsupported=%d"),
		*UEnum::GetValueAsString(Kind), Sequence, WorldData.Actors.Num(), WorldData.DestroyedLevelActorIds.Num(), Snapshot->GetUnsupportedCandidateCount());
	if (Kind == ESWRoomSaveKind::Return)
	{
		for (TActorIterator<ASWLevelEntryPoint> It(World); It; ++It)
		{
			if (It->EntryRole == ESWLevelEntryRole::Guest)
			{
				for (FSWRoomGuestProgress& Guest : Save->Guests)
				{
					Guest.Progress.bHasResumeTransform = true;
					Guest.Progress.ResumeWorldTransform = It->GetActorTransform();
					Guest.Progress.CurrentHealth = Guest.Progress.MaximumHealth;
					Guest.Progress.bWasDead = false;
				}
				break;
			}
		}
	}
	Save->SaveKind = Kind;
	Save->ContentContractVersion = USWRoomSaveGame::CurrentContentContractVersion;
	Save->CaptureSequence = Sequence;
	Save->SavedAtUtc = FDateTime::UtcNow();
	Save->MapPath = WorldData.MapPath;
	Save->WorldSnapshot = MoveTemp(WorldData);
	Save->bComplete = true;
	if (!Room->WriteCheckpoint())
	{
		OutError = TEXT("Room file transaction failed");
		UE_LOG(LogSWRoomSave, Error, TEXT("Flow=SaveAttempt Result=Failed Phase=Commit RoomId=%s Sequence=%llu Kind=%s Reason=%s"),
			*Save->RoomId.ToString(), Sequence, *UEnum::GetValueAsString(Kind), *OutError);
		UE_LOG(LogSWRoom, Error, TEXT("Flow=Save Kind=%s Sequence=%llu Result=Failed Phase=Commit"), *UEnum::GetValueAsString(Kind), Sequence);
		return false;
	}
	UE_LOG(LogSWRoomSave, Display, TEXT("Flow=SaveAttempt Result=%s RoomId=%s Sequence=%llu Kind=%s Issues=%d"),
		LastCaptureIssueCount ? TEXT("Partial") : TEXT("Committed"), *Save->RoomId.ToString(), Sequence,
		*UEnum::GetValueAsString(Kind), LastCaptureIssueCount);
	UE_LOG(LogSWRoom, Display, TEXT("Flow=Save Kind=%s RoomId=%s Sequence=%llu Result=Success Missing=%d"),
		*UEnum::GetValueAsString(Kind), *Save->RoomId.ToString(), Sequence, LastCaptureIssueCount);
	if (Kind == ESWRoomSaveKind::Return) Room->ClearReturnTravelPending();
	if (Kind == ESWRoomSaveKind::New)
		if (AMultiGameMode* Mode = World->GetAuthGameMode<AMultiGameMode>()) Mode->SetHostedRoomWorldReady();
	return true;
}

void UClassFeatureRoomProgressSubsystem::CapturePlayer(ABasePlayer* Player)
{
	USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
	USWRoomSaveGame* Save = Room ? Room->GetMutableActiveRoom() : nullptr;
	AMultiGameMode* Mode = Player && Player->GetWorld() ? Player->GetWorld()->GetAuthGameMode<AMultiGameMode>() : nullptr;
	if (!Save || !Mode)
	{
		UE_LOG(LogSWRoomSave, Warning, TEXT("Flow=PlayerCapture Result=Skipped Player=%s Reason=MissingRoomOrGameMode"), *GetNameSafe(Player));
		return;
	}
	AController* Controller = Player->GetController();
	if (!Controller && Player->GetPlayerState()) Controller = Player->GetPlayerState()->GetOwningController();
	if (!Controller)
	{
		if (const AShip* Ship = Cast<AShip>(Player->GetAttachParentActor()); Ship && Ship->GetRidingPlayer() == Player)
			Controller = Ship->GetController();
		else if (const ACannon* Cannon = Cast<ACannon>(Player->GetAttachParentActor()); Cannon && Cannon->GetRidingPlayer() == Player)
			Controller = Cannon->GetController();
	}
	if (!Controller)
	{
		UE_LOG(LogSWRoomSave, Warning, TEXT("Flow=PlayerCapture Result=Skipped RoomId=%s Sequence=%llu Player=%s Reason=MissingController"),
			*Save->RoomId.ToString(), Save->CaptureSequence + 1, *Player->GetPathName());
		return;
	}
	auto TracePlayer = [Save, Player](const TCHAR* Role, const FString& PlayerKey, const FSWRoomPlayerProgress& Progress)
	{
		UE_LOG(LogSWRoomSave, Display,
			TEXT("Flow=PlayerCapture Result=%s RoomId=%s Sequence=%llu Role=%s Player=%s Key=%s InventorySlots=%d QuickSlots=%d Skills=%d Effects=%d Upgrades=%d Health=%g MaxHealth=%g Dead=%d Resume=%d Location=%s Movement=%d Issues=%d"),
			Progress.CaptureIssues.IsEmpty() ? TEXT("Success") : TEXT("Partial"), *Save->RoomId.ToString(),
			Save->CaptureSequence + 1, Role, *Player->GetPathName(), *PlayerKey,
			Progress.InventorySlots.Num(), Progress.QuickSlotItemTags.Num(), Progress.Skills.Num(),
			Progress.ActiveEffects.Num(), Progress.UpgradeNodeIds.Num(), Progress.CurrentHealth,
			Progress.MaximumHealth, Progress.bWasDead ? 1 : 0, Progress.bHasResumeTransform ? 1 : 0,
			*Progress.ResumeWorldTransform.GetLocation().ToString(), Progress.bHasMovement ? 1 : 0,
			Progress.CaptureIssues.Num());
		for (const FSWRoomCaptureIssue& Issue : Progress.CaptureIssues)
			UE_LOG(LogSWRoomSave, Warning,
				TEXT("Flow=PlayerCaptureIssue RoomId=%s Sequence=%llu Player=%s Key=%s Domain=%s Field=%s Reason=%s"),
				*Save->RoomId.ToString(), Save->CaptureSequence + 1, *Player->GetPathName(), *PlayerKey,
				*Issue.Domain.ToString(), *Issue.FieldKey.ToString(), *Issue.Reason);
	};
	const APlayerState* PlayerState = Player->GetPlayerState();
	if (!PlayerState)
		if (const APlayerController* PlayerController = Cast<APlayerController>(Controller)) PlayerState = PlayerController->PlayerState;
	if (Mode->GetPlayerIndex(Controller) == 0)
	{
		Player->CaptureRoomProgress(Save->HostProgress);
		TracePlayer(TEXT("Host"), Save->HostDisplayName, Save->HostProgress);
	}
	else if (const APlayerState* State = PlayerState)
	{
		const FString Name = State->GetPlayerName();
		FSWRoomGuestProgress* Guest = Save->Guests.FindByPredicate([&Name](const FSWRoomGuestProgress& Entry) { return Entry.DisplayName == Name; });
		if (!Guest) Guest = &Save->Guests.AddDefaulted_GetRef();
		Guest->DisplayName = Name;
		Player->CaptureRoomProgress(Guest->Progress);
		TracePlayer(TEXT("Guest"), Name, Guest->Progress);
		Save->Guests.Sort([](const FSWRoomGuestProgress& A, const FSWRoomGuestProgress& B) { return A.DisplayName < B.DisplayName; });
	}
	else
		UE_LOG(LogSWRoomSave, Warning, TEXT("Flow=PlayerCapture Result=Skipped RoomId=%s Sequence=%llu Player=%s Reason=MissingGuestPlayerState"),
			*Save->RoomId.ToString(), Save->CaptureSequence + 1, *Player->GetPathName());
}

bool UClassFeatureRoomProgressSubsystem::TryReturn(UWorld* World, ABasePlayer* Requester)
{
	UE_LOG(LogSWRoom, Display, TEXT("Flow=Return Phase=Requested"));
	USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
	AMultiGameMode* Mode = World ? World->GetAuthGameMode<AMultiGameMode>() : nullptr;
	if (!Room || !Mode || !Requester || bReturning)
	{ UE_LOG(LogSWRoom, Warning, TEXT("Flow=Return Result=Rejected")); return false; }
	for (TActorIterator<ABasePlayer> It(World); It; ++It)
	{
		if (UInventoryComponent* Inventory = It->GetInventoryComponent()) Inventory->ReturnCursorToOriginalSlot();
		CapturePlayer(*It);
	}
	if (!CaptureSharedWorld(World))
	{
		UE_LOG(LogSWRoom, Error, TEXT("Flow=Return Result=Failed Phase=PermanentCapture Reason=SharedProgress"));
		if (APlayerController* Controller = Cast<APlayerController>(Requester->GetController())) Controller->ClientMessage(TEXT("귀환 저장 실패"));
		return false;
	}
	bReturning = true;
	UE_LOG(LogSWRoom, Display, TEXT("Flow=Return RoomId=%s Phase=TravelRequested DiskWrite=0"), *Room->GetActiveRoom()->RoomId.ToString());
	Mode->RequestHostedRoomReturnTravel();
	return true;
}
