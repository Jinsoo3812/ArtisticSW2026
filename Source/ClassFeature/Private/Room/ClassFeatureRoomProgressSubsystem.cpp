#include "Room/ClassFeatureRoomProgressSubsystem.h"
#include "PlayerRespawnPointComponent.h"
#include "Room/SWRoomReadyState.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "BasePlayer.h"
#include "BasePlayerController.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "MultiGameMode.h"
#include "Room/SWRoomProgressSubsystem.h"
#include "Room/SWRoomSaveGame.h"
#include "Room/SWRoomSnapshotSubsystem.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "Network/SWNetworkLog.h"
#include "Network/SWRoomLoadDiagnostics.h"
#include "Room/SWLevelEntryPoint.h"
#include "Room/SWFinalEncounterShipEntryPoint.h"
#include "StoryFacadeSubsystem.h"
#include "Network/SWFinalEncounterDiagnostics.h"
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
#include "TimerManager.h"
#include "Room/SWVoyageResetSubsystem.h"
#include "Room/ClassFeatureVoyageTransition.h"

void UClassFeatureRoomProgressSubsystem::NormalizePlayerForVoyage(FSWRoomPlayerProgress& Progress)
{
	Progress.ResumeWorldTransform = FTransform::Identity;
	Progress.ShipRelativeTransform = FTransform::Identity;
	Progress.bHasResumeTransform = false;
	Progress.ShipStableId.Invalidate(); Progress.MountedDeviceId.Invalidate();
	Progress.ControlRotation = FRotator::ZeroRotator; Progress.CameraMode = NAME_None; Progress.CameraZoom = 0.f;
	Progress.bWasDead = false; Progress.bWasSwimming = false; Progress.bWasMounted = false; Progress.bHasMovement = false;
	Progress.WorldVelocity = FVector::ZeroVector; Progress.MovementMode = MOVE_Walking; Progress.CustomMovementMode = 0;
	Progress.bEffectsCaptured = false; Progress.ActiveEffects.Reset();
	Progress.CurrentHealth = Progress.MaximumHealth; Progress.bRestoreFullHealth = true;
}

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
			const USWRoomProgressSubsystem* Room = World && World->GetGameInstance()
				? World->GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>() : nullptr;
			if (Room && Room->IsFinalDepartureTravelPending())
				FSWFinalEncounterDiagnostics::Write(TEXT("FinalRestore"), TEXT("PlacementBlocked"),
					FString::Printf(TEXT("Part=%s Obstacle=%s Component=%s"),
						*Part->GetPathName(), *Overlap.GetActor()->GetPathName(), *Other->GetPathName()));
			const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Other);
			UE_LOG(LogSWRoom, Warning,
				TEXT("Flow=ShipPlacement Result=Blocked ShipPart=%s PartLocation=%s PartExtent=%s Obstacle=%s ObstacleComponent=%s Mesh=%s ObstacleLocation=%s ObstacleExtent=%s"),
				*Part->GetPathName(), *Part->Bounds.Origin.ToCompactString(), *Part->Bounds.BoxExtent.ToCompactString(),
				*Overlap.GetActor()->GetPathName(), *Other->GetPathName(),
				*GetNameSafe(StaticMeshComponent ? StaticMeshComponent->GetStaticMesh() : nullptr),
				*Other->Bounds.Origin.ToCompactString(), *Other->Bounds.BoxExtent.ToCompactString());
			return true;
		}
	}
	return false;
}

bool PlaceShipSafely(UWorld* World, AKelvinShip* Ship, AActor* Entry, bool bUseEntry, FString& OutReason)
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
			if (Entry->IsA<ASWFinalEncounterShipEntryPoint>())
				FSWFinalEncounterDiagnostics::Write(TEXT("FinalRestore"), TEXT("WaterQueryUnavailable"),
					FString::Printf(TEXT("Candidate=%d Position=%s"), Index, *Candidate.GetLocation().ToCompactString()));
			UE_LOG(LogSWRoom, Warning, TEXT("Room ship candidate %d: water query unavailable"), Index);
			if (Index != 2 || Entry->IsA<ASWFinalEncounterShipEntryPoint>()) continue;
		}
		else if (Entry->IsA<ASWFinalEncounterShipEntryPoint>())
			FSWFinalEncounterDiagnostics::Write(TEXT("FinalRestore"), TEXT("WaterQuery"),
				FString::Printf(TEXT("Candidate=%d Position=%s WaterZ=%.1f"),
					Index, *Candidate.GetLocation().ToCompactString(), WaterZ));
		if (Index == 1)
		{
			FVector Position = Candidate.GetLocation();
			Position.Z = FMath::Max(Position.Z, WaterZ);
			Candidate.SetLocation(Position);
		}
		Ship->SetActorTransform(Candidate, false, nullptr, ETeleportType::TeleportPhysics);
		UE_LOG(LogSWRoom, Display, TEXT("Flow=ShipPlacement Phase=Candidate Index=%d Entry=%s EntryLocation=%s ShipLocation=%s"),
			Index, *Entry->GetPathName(), *Entry->GetActorLocation().ToCompactString(), *Ship->GetActorLocation().ToCompactString());
		UE_LOG(LogSWRoom, Display, TEXT("Flow=ShipPlacement Phase=CandidatePhysics Index=%d WorldTime=%.3f WaterZ=%.3f Actor=%s Root=%s Entry=%s"),
			Index, World->GetTimeSeconds(), WaterZ, *Ship->GetActorTransform().ToString(),
			Ship->GetRootComponent() ? *Ship->GetRootComponent()->GetComponentTransform().ToString() : TEXT("None"), *Entry->GetActorTransform().ToString());
		UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Ship->GetRootComponent());
		if (!Root || (WaterZ != 0.0f && Root->GetComponentLocation().Z < WaterZ - Root->Bounds.BoxExtent.Z))
		{ UE_LOG(LogSWRoom, Warning, TEXT("Room ship candidate %d: root below safe surface"), Index); continue; }
		bool bBlocked = IsShipPartBlocked(World, Ship, Root);
		for (UPrimitiveComponent* Deck : {Cast<UPrimitiveComponent>(Ship->GetDeckMeshSimple()), Cast<UPrimitiveComponent>(Ship->GetDeckMeshComplex())})
			bBlocked |= IsShipPartBlocked(World, Ship, Deck);
		if (bBlocked) { UE_LOG(LogSWRoom, Warning, TEXT("Room ship candidate %d: static blocking overlap"), Index); continue; }
		OutReason = Index == 0 ? TEXT("Saved ship transform") : (Index == 1 ? TEXT("Level ship transform") : TEXT("Ship entry marker fallback"));
		Ship->SetShipRuntimePhysicsEnabled(true);
		if (Entry->IsA<ASWFinalEncounterShipEntryPoint>())
			FSWFinalEncounterDiagnostics::Write(TEXT("FinalRestore"), TEXT("ShipPlaced"),
				FString::Printf(TEXT("Entry=%s Ship=%s Result=%s"),
					*Entry->GetPathName(), *Ship->GetActorLocation().ToCompactString(), *OutReason));
		return true;
	}
	OutReason = TEXT("No safe ship restore transform");
	if (Entry->IsA<ASWFinalEncounterShipEntryPoint>())
		FSWFinalEncounterDiagnostics::Write(TEXT("FinalRestore"), TEXT("PlacementFailed"),
			FString::Printf(TEXT("Entry=%s Reason=%s"), *Entry->GetPathName(), *OutReason));
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

AActor* ResolveShipEntry(UWorld* World, bool bFinalDeparture, int32& OutMarkerCount)
{
	OutMarkerCount = 0;
	AActor* Entry = nullptr;
	if (bFinalDeparture)
	{
		for (TActorIterator<ASWFinalEncounterShipEntryPoint> It(World); It; ++It)
		{
			Entry = *It;
			++OutMarkerCount;
		}
	}
	else
	{
		for (TActorIterator<ASWLevelEntryPoint> It(World); It; ++It)
			if (It->EntryRole == ESWLevelEntryRole::Ship)
			{
				Entry = *It;
				++OutMarkerCount;
			}
	}
	return OutMarkerCount == 1 ? Entry : nullptr;
}
}

void UClassFeatureRoomProgressSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PostLoadHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UClassFeatureRoomProgressSubsystem::HandlePostLoadMap);
}

void UClassFeatureRoomProgressSubsystem::Deinitialize()
{
 if (VoyageTransition) VoyageTransition->Shutdown();
	if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
		if (AMultiGameMode* Mode = World->GetAuthGameMode<AMultiGameMode>()) Mode->OnGameOverRequested.RemoveDynamic(this, &UClassFeatureRoomProgressSubsystem::HandleGameOverRestart);
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadHandle);
	Super::Deinitialize();
}

void UClassFeatureRoomProgressSubsystem::HandlePostLoadMap(UWorld* World)
{
 if (!GetRoom(GetGameInstance()) || !World || World->GetGameInstance() != GetGameInstance() || World->GetNetMode() == NM_Client) return;
 if (!VoyageTransition) VoyageTransition = NewObject<UClassFeatureVoyageTransition>(this);
 FString Error;
 if (!VoyageTransition->BeginBootstrap(World, Error)) VoyageTransition->Fail(Error);
}

void UClassFeatureRoomProgressSubsystem::HandleGameOverRestart()
{
	UE_LOG(LogSWRoom, Display, TEXT("Flow=GameOver Phase=CapturePermanent NoDiskWrite=1"));
 if (UWorld* World = GetGameInstance()->GetWorld())
  for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
   if (ABasePlayerController* Controller = Cast<ABasePlayerController>(It->Get()))
   { FString Error; if (!CaptureControllerProgress(Controller, true, Error)) UE_LOG(LogSWRoom, Error, TEXT("RetryCaptureFailed: %s"), *Error); }
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
	SWRoomLoadDiagnostics::FScopedPhase DiagnosticScope(TEXT("Room.RestoreShared"));
	USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
	const USWRoomSaveGame* Save = Room ? Room->GetActiveRoom() : nullptr;
	if (!World || !Save) return false;
	FString Error;
	return RestoreStoryProgress(World, Save->SharedProgress, Error)
		&& RestoreSharedActors(World, Save->SharedProgress, Error) == ESWVoyageStepResult::Succeeded;
}

bool UClassFeatureRoomProgressSubsystem::RestoreStoryProgress(UWorld* World, const FSWRoomSharedProgress& Target, FString& OutError)
{
	OutError.Reset();
	if (!World || World->GetGameInstance() != GetGameInstance() || World->GetNetMode() == NM_Client)
	{ OutError = TEXT("VoyageStoryRestoreInvalidWorld"); return false; }
	if (UStorySubsystem* Story = GetGameInstance()->GetSubsystem<UStorySubsystem>())
	{
		UStorySaveGame* StoryData = NewObject<UStorySaveGame>(this);
		StoryData->StoryDefinitionId = Target.StoryDefinitionId;
		StoryData->Facts = Target.Facts;
		StoryData->AppliedActionKeys = Target.AppliedActionKeys;
		for (const FSWRoomCounterProgress& Counter : Target.Counters)
		{
			FStoryCounterValue& Value = StoryData->Counters.AddDefaulted_GetRef();
			Value.CounterTag = Counter.Tag;
			Value.Value = Counter.Value;
		}
		if (Target.StoryDefinitionId.IsValid() && !Story->ApplyRoomProgress(StoryData))
		{ OutError = TEXT("VoyageStoryRestoreFailed"); return false; }
	}
	else if (Target.StoryDefinitionId.IsValid()) { OutError = TEXT("VoyageStorySubsystemMissing"); return false; }
	return true;
}

ESWVoyageStepResult UClassFeatureRoomProgressSubsystem::RestoreSharedActors(UWorld* World, const FSWRoomSharedProgress& Target, FString& OutError)
{
	OutError.Reset();
	if (!World || World->GetGameInstance() != GetGameInstance() || World->GetNetMode() == NM_Client)
	{ OutError = TEXT("VoyageSharedRestoreInvalidWorld"); return ESWVoyageStepResult::Failed; }
	USWVoyageResetSubsystem* Voyage = World->GetSubsystem<USWVoyageResetSubsystem>();
	const int32 Generation = Voyage ? Voyage->GetGeneration() : 0;
	if (SharedRestoreWorld.Get() != World || SharedRestoreGeneration != Generation)
	{
		SharedRestoreWorld = World; SharedRestoreGeneration = Generation; bVoyageSharedUpgradeRestored = false; RestoredSharedChests.Reset();
	}
	TSet<FString> RequiredKeys;
	for (const FSWRoomStorageProgress& Storage : Target.Storage)
	{
		const FString Key = StorageKey(Storage.ChestId, Storage.SaveNamespace);
		if (!Storage.ChestId.IsValid() || RequiredKeys.Contains(Key) || Storage.SlotsPerTab <= 0 || Storage.SlotsPerTab > MAX_int32 / 4 || Storage.Slots.Num() != Storage.SlotsPerTab * 4)
		{ OutError = TEXT("VoyageTargetStorageContractInvalid:") + Key; return ESWVoyageStepResult::Failed; }
		RequiredKeys.Add(Key);
	}
	if (!bVoyageSharedUpgradeRestored)
	{
		ASharedShipUpgradeState* Ship = ASharedShipUpgradeState::Find(World);
		if (!Ship && !Target.ShipUpgradeNodeIds.IsEmpty()) return ESWVoyageStepResult::Pending;
		if (Ship)
		{
			UShipUpgradeComponent* Upgrade = Ship->GetUpgradeComponent();
			if (!Upgrade) { OutError = TEXT("VoyageSharedUpgradeComponentMissing"); return ESWVoyageStepResult::Failed; }
			Upgrade->RestoreActiveNodeIds(Target.ShipUpgradeNodeIds);
			if (Upgrade->GetActiveNodeIds() != Target.ShipUpgradeNodeIds) { OutError = TEXT("VoyageSharedUpgradeRestoreMismatch"); return ESWVoyageStepResult::Failed; }
		}
		bVoyageSharedUpgradeRestored = true;
	}
	TSet<FString> Keys;
	for (TActorIterator<ASharedStorageChest> It(World); It; ++It)
	{
		ASharedStorageChest* Chest = *It;
		const FString Key = StorageKey(Chest->PersistentChestId, Chest->SaveNamespace);
		if (!Chest->PersistentChestId.IsValid() || Keys.Contains(Key)) { OutError = TEXT("VoyageStorageKeyDuplicateOrInvalid:") + Key; return ESWVoyageStepResult::Failed; }
		Keys.Add(Key);
		const FSWRoomStorageProgress* Stored = Target.Storage.FindByPredicate([&Key](const FSWRoomStorageProgress& Entry)
		{
			return StorageKey(Entry.ChestId, Entry.SaveNamespace) == Key;
		});
		if (!Stored)
		{
			const USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
			if (VoyageTransition && VoyageTransition->GetContext().bBootstrap && Room && Room->IsNewRoomPending()) continue;
			OutError = TEXT("VoyageStorageTargetKeyMissing:") + Key; return ESWVoyageStepResult::Failed;
		}
		if (const TWeakObjectPtr<AActor>* Restored = RestoredSharedChests.Find(Key); Restored && Restored->Get() == Chest) continue;
		TArray<FInventorySlot> Slots;
		for (const FSWRoomStorageSlot& Entry : Stored->Slots)
		{
			FInventorySlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.ItemTag = Entry.ItemTag;
			Slot.Count = Entry.Count;
		}
		UStorageComponent* Storage = Chest->GetStorageComponent();
		if (!Storage || !Storage->ConfigureTabbedStorage(Chest->InitialSlotsPerTab, Slots, Stored->SlotsPerTab) || Storage->GetSlotsPerTab() != Stored->SlotsPerTab)
		{ OutError = TEXT("VoyageStorageCapacityRestoreFailed:") + Key; return ESWVoyageStepResult::Failed; }
		const TArray<FInventorySlot> Actual = Storage->GetPersistentSlots();
		if (Actual.Num() != Slots.Num()) { OutError = TEXT("VoyageStorageSlotCountMismatch:") + Key; return ESWVoyageStepResult::Failed; }
		for (int32 Index = 0; Index < Actual.Num(); ++Index)
			if (Actual[Index].ItemTag != Slots[Index].ItemTag || Actual[Index].Count != Slots[Index].Count)
			{ OutError = TEXT("VoyageStorageContentRestoreMismatch:") + Key; return ESWVoyageStepResult::Failed; }
		RestoredSharedChests.Add(Key, Chest);
	}
	for (const FString& Key : RequiredKeys) if (!Keys.Contains(Key)) return ESWVoyageStepResult::Pending;
	return ESWVoyageStepResult::Succeeded;
}

bool UClassFeatureRoomProgressSubsystem::CaptureSharedWorld(UWorld* World)
{
	SWRoomLoadDiagnostics::FScopedPhase DiagnosticScope(TEXT("Room.CaptureShared"));
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
		SW_ROOM_DETAIL_LOG(LogSWRoomSave, Display,
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
			SW_ROOM_DETAIL_LOG(LogSWRoomSave, Display, TEXT("Flow=SharedCapture Result=Success RoomId=%s Sequence=%llu Object=ShipUpgrade Actor=%s Nodes=%d"),
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
		SW_ROOM_DETAIL_LOG(LogSWRoomSave, Display,
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
		SW_ROOM_DETAIL_LOG(LogSWRoomSave, Warning, TEXT("Flow=SharedCaptureIssue RoomId=%s Sequence=%llu Domain=%s Field=%s Reason=%s"),
			*Save->RoomId.ToString(), Save->CaptureSequence + 1,
			*Issue.Domain.ToString(), *Issue.FieldKey.ToString(), *Issue.Reason);
	SW_ROOM_DETAIL_LOG(LogSWRoomSave, Display, TEXT("Flow=SharedCapture Result=%s RoomId=%s Sequence=%llu Storage=%d Issues=%d"),
		Shared.CaptureIssues.IsEmpty() ? TEXT("Success") : TEXT("Partial"),
		*Save->RoomId.ToString(), Save->CaptureSequence + 1, Shared.Storage.Num(), Shared.CaptureIssues.Num());
	return true;
}

void UClassFeatureRoomProgressSubsystem::RestorePlayer(ABasePlayer* Player)
{
	SWRoomLoadDiagnostics::FScopedPhase DiagnosticScope(TEXT("Room.RestorePlayer"));
	USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
	const USWRoomSaveGame* Save = Room ? Room->GetActiveRoom() : nullptr;
	AMultiGameMode* Mode = Player && Player->GetWorld() ? Player->GetWorld()->GetAuthGameMode<AMultiGameMode>() : nullptr;
	if (!Save || !Mode || !Player->GetController()) return;
	auto ApplyProgress = [Player, Room](const FSWRoomPlayerProgress& Stored)
	{
		for (const FSWRoomCaptureIssue& Issue : Stored.CaptureIssues)
			SW_ROOM_DETAIL_LOG(LogSWRoom, Warning, TEXT("Flow=PlayerRestore Result=Partial Player=%s Domain=%s Field=%s Reason=%s"),
				*Issue.PlayerKey, *Issue.Domain.ToString(), *Issue.FieldKey.ToString(), *Issue.Reason);
		FSWRoomPlayerProgress Progress = Stored;
		if (Room->IsReturnTravelPending() || Room->IsFinalDepartureTravelPending())
		{
			Progress.bHasResumeTransform = false;
			Progress.bWasDead = false;
			Progress.CurrentHealth = Progress.MaximumHealth;
			Progress.bRestoreFullHealth = true;
			Progress.bHasMovement = false;
			Progress.bEffectsCaptured = false;
			Progress.ActiveEffects.Reset();
		}
		if (Room->IsReturnTravelPending() || Room->IsFinalDepartureTravelPending())
  {
   FString Error; Player->bInitialLifeRestoreSuccessful = Player->RestoreProgressForNewLife(Progress, Error);
   if (!Player->bInitialLifeRestoreSuccessful) UE_LOG(LogSWRoom, Error, TEXT("New life restore failed: %s"), *Error);
  }
  else Player->RestoreRoomProgress(Progress);
	};
	if (Mode->GetPlayerIndex(Player->GetController()) == 0)
	{
		if (Room->IsNewRoomPending() && Save->HostProgress.InventorySlots.IsEmpty()) Player->FinalizeStartingInventory(true);
		else { ApplyProgress(Save->HostProgress); Player->FinalizeStartingInventory(false); }
	}
	else if (const APlayerState* State = Player->GetPlayerState())
	{
		const FString Name = State->GetPlayerName();
		if (const FSWRoomGuestProgress* Guest = Save->Guests.FindByPredicate([&Name](const FSWRoomGuestProgress& Entry) { return Entry.DisplayName == Name; }))
		{
			ApplyProgress(Guest->Progress);
			Player->FinalizeStartingInventory(false);
		}
		else Player->FinalizeStartingInventory(true);
	}
	UE_LOG(LogSWRoom, Display, TEXT("Flow=PlayerRestore RoomId=%s PlayerIndex=%d Result=Applied Return=%d"),
		*Save->RoomId.ToString(), Mode->GetPlayerIndex(Player->GetController()), Room->IsReturnTravelPending());
	if (const USWRoomSnapshotSubsystem* Snapshot = Player->GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>();
		!Snapshot || !Snapshot->IsRestoringSnapshot())
	{
		FString EffectsError;
		if (!Player->FinalizeRoomProgressEffects(EffectsError)) { Player->bInitialLifeRestoreSuccessful = false; UE_LOG(LogSWRoom, Error, TEXT("Immediate player effects restore failed: %s"), *EffectsError); }
	}
	if (VoyageTransition && VoyageTransition->IsRecoveryBootstrap())
 {
  FString PlacementError;
  if (!VoyageTransition->PlaceBootstrapPlayer(Cast<ABasePlayerController>(Player->GetController()), PlacementError))
  { Player->bInitialLifeRestoreSuccessful = false; VoyageTransition->Fail(PlacementError); }
 }

}

bool UClassFeatureRoomProgressSubsystem::TrySave(UWorld* World, ESWRoomSaveKind Kind, FString& OutError)
{
 if (IsInPlaceVoyageBusy() && !VoyageTransition->IsSavingResult()) { OutError = TEXT("VoyageTransitionSaveDeferred"); return false; }
 if (World) if (const USWRoomSnapshotSubsystem* Snapshot = World->GetSubsystem<USWRoomSnapshotSubsystem>(); Snapshot && Snapshot->IsDiscardingVoyage())
 { OutError = TEXT("VoyageDiscardCaptureForbidden"); return false; }

	SWRoomLoadDiagnostics::FScopedPhase DiagnosticScope(TEXT("Room.TrySave"));
	USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
	USWRoomSaveGame* Save = Room ? Room->GetMutableActiveRoom() : nullptr;
	UE_LOG(LogSWRoom, Display, TEXT("Flow=Save Kind=%s RoomId=%s Phase=Requested"), *UEnum::GetValueAsString(Kind),
		Save ? *Save->RoomId.ToString() : TEXT("None"));
	SW_ROOM_DETAIL_LOG(LogSWRoomSave, Display, TEXT("Flow=SaveAttempt Phase=Requested RoomId=%s Sequence=%llu Kind=%s World=%s"),
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
	if (AMultiGameMode* Mode = World->GetAuthGameMode<AMultiGameMode>(); Mode && Kind != ESWRoomSaveKind::Return
  && (Mode->GetSessionLifePhase() == ESWSessionLifePhase::GameOver || Mode->GetSessionLifePhase() == ESWSessionLifePhase::ReturningAfterGameOver))
 { OutError = TEXT("다시하기 후 저장할 수 있습니다"); return false; }
 if (Room->IsGameOverRetryTravelPending() && !ValidateRetryStorage(World, OutError)) return false;
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
	if (LastCaptureIssueCount > 0)
		UE_LOG(LogSWRoom, Warning, TEXT("Flow=SaveCaptureIssues RoomId=%s Sequence=%llu Count=%d DetailedLog=%d"),
			*Save->RoomId.ToString(), Sequence, LastCaptureIssueCount, SWRoomLogging::IsDetailedEnabled());
	SW_ROOM_DETAIL_LOG(LogSWRoomSave, Display,
		TEXT("Flow=SaveAttempt Phase=Captured RoomId=%s Sequence=%llu Kind=%s Actors=%d Unloaded=%d Tombstones=%d Systems=%d Guests=%d Issues=%d Unsupported=%d"),
		*Save->RoomId.ToString(), Sequence, *UEnum::GetValueAsString(Kind), WorldData.Actors.Num(),
		WorldData.UnloadedActors.Num(), WorldData.DestroyedLevelActorIds.Num(), WorldData.Systems.Num(),
		Save->Guests.Num(), LastCaptureIssueCount, Snapshot->GetUnsupportedCandidateCount());
	UE_LOG(LogSWRoom, Display, TEXT("Flow=Save Kind=%s Sequence=%llu Phase=Captured Actors=%d Tombstones=%d Unsupported=%d"),
		*UEnum::GetValueAsString(Kind), Sequence, WorldData.Actors.Num(), WorldData.DestroyedLevelActorIds.Num(), Snapshot->GetUnsupportedCandidateCount());
	if (Kind == ESWRoomSaveKind::Return && !(VoyageTransition && VoyageTransition->IsSavingResult()))
	{
		for (TActorIterator<ASWLevelEntryPoint> It(World); It; ++It)
		{
			if (It->EntryRole == ESWLevelEntryRole::Guest)
			{
				for (FSWRoomGuestProgress& Guest : Save->Guests)
    {
     bool bConnected = false;
     for (FConstPlayerControllerIterator ControllerIt = World->GetPlayerControllerIterator(); ControllerIt; ++ControllerIt)
      if (const APlayerController* Controller = ControllerIt->Get(); Controller && Controller->PlayerState && Controller->PlayerState->GetPlayerName() == Guest.DisplayName) bConnected = true;
     if (bConnected) continue;
					Guest.Progress.bHasResumeTransform = true;
					Guest.Progress.ResumeWorldTransform = It->GetActorTransform();
					Guest.Progress.CurrentHealth = Guest.Progress.MaximumHealth;
					Guest.Progress.bWasDead = false;
     if (Room->IsGameOverRetryTravelPending())
     { Guest.Progress.bEffectsCaptured = false; Guest.Progress.ActiveEffects.Reset(); Guest.Progress.bHasMovement = false; Guest.Progress.bWasMounted = false; Guest.Progress.MountedDeviceId.Invalidate(); Guest.Progress.bWasSwimming = false; Guest.Progress.ShipStableId.Invalidate(); }
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
	const bool bFinalDepartureSave = Kind == ESWRoomSaveKind::Return
		&& Room->IsFinalDepartureTravelPending();
	if (bFinalDepartureSave) Save->bFinalDepartureCompleted = true;
	if (!Room->WriteCheckpoint())
	{
		if (bFinalDepartureSave) Save->bFinalDepartureCompleted = false;
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
	if (bFinalDepartureSave) Room->ClearFinalDepartureTravelPending();
	if (Kind == ESWRoomSaveKind::New)
		if (AMultiGameMode* Mode = World->GetAuthGameMode<AMultiGameMode>()) Mode->SetHostedRoomWorldReady();
	return true;
}

void UClassFeatureRoomProgressSubsystem::CapturePlayer(ABasePlayer* Player)
{
	if (IsInPlaceVoyageBusy() && !VoyageTransition->IsSavingResult()) return;
 SWRoomLoadDiagnostics::FScopedPhase DiagnosticScope(TEXT("Room.CapturePlayer"));
 if (AMultiGameMode* LifeMode = Player && Player->GetWorld() ? Player->GetWorld()->GetAuthGameMode<AMultiGameMode>() : nullptr;
  LifeMode && !(VoyageTransition && VoyageTransition->IsSavingResult())
  && (LifeMode->GetSessionLifePhase() == ESWSessionLifePhase::GameOver || LifeMode->GetSessionLifePhase() == ESWSessionLifePhase::ReturningAfterGameOver))
 {
  for (FConstPlayerControllerIterator It = Player->GetWorld()->GetPlayerControllerIterator(); It; ++It)
   if (ABasePlayerController* Flow = Cast<ABasePlayerController>(It->Get()); Flow && Flow->GetLifeCharacter() == Player)
   { FString Error; CaptureControllerProgress(Flow, true, Error); return; }
  return;
 }
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
		SW_ROOM_DETAIL_LOG(LogSWRoomSave, Display,
			TEXT("Flow=PlayerCapture Result=%s RoomId=%s Sequence=%llu Role=%s Player=%s Key=%s InventorySlots=%d QuickSlots=%d Skills=%d Effects=%d Upgrades=%d Health=%g MaxHealth=%g Dead=%d Resume=%d Location=%s Movement=%d Issues=%d"),
			Progress.CaptureIssues.IsEmpty() ? TEXT("Success") : TEXT("Partial"), *Save->RoomId.ToString(),
			Save->CaptureSequence + 1, Role, *Player->GetPathName(), *PlayerKey,
			Progress.InventorySlots.Num(), Progress.QuickSlotItemTags.Num(), Progress.Skills.Num(),
			Progress.ActiveEffects.Num(), Progress.UpgradeNodeIds.Num(), Progress.CurrentHealth,
			Progress.MaximumHealth, Progress.bWasDead ? 1 : 0, Progress.bHasResumeTransform ? 1 : 0,
			*Progress.ResumeWorldTransform.GetLocation().ToString(), Progress.bHasMovement ? 1 : 0,
			Progress.CaptureIssues.Num());
		for (const FSWRoomCaptureIssue& Issue : Progress.CaptureIssues)
			SW_ROOM_DETAIL_LOG(LogSWRoomSave, Warning,
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
 USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
 AMultiGameMode* Mode = World ? World->GetAuthGameMode<AMultiGameMode>() : nullptr;
 if (!Room || !Mode || !Requester || IsDevelopmentTransitionBusy() || !Mode->CanMutateGameplay(Requester->GetController())) return false;
 FString Error;
 const bool bStarted = StartVoyageTransition(World, ESWVoyageReason::Return, false, Error);
 if (!bStarted) if (APlayerController* Controller = Cast<APlayerController>(Requester->GetController())) Controller->ClientMessage(Error.Left(512));
 return bStarted;
}

bool UClassFeatureRoomProgressSubsystem::TryFinalDeparture(UWorld* World, ABasePlayer* Requester)
{
 FString Error;
 return TryFinalDepartureInternal(World, Requester, Requester ? Cast<ABasePlayerController>(Requester->GetController()) : nullptr, false, Error);
}

bool UClassFeatureRoomProgressSubsystem::TryDevelopmentFinalDeparture(UWorld* World, ABasePlayerController* Requester, FString& OutError)
{
 OutError=TEXT("생존 상태에서 출항 테스트를 실행하세요");
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
 USWRoomProgressSubsystem* Room=GetRoom(GetGameInstance());
 AMultiGameMode* Mode=World ? World->GetAuthGameMode<AMultiGameMode>() : nullptr;
 ASWRoomReadyState* Ready=nullptr;
 if (World) for (TActorIterator<ASWRoomReadyState> It(World);It;++It) { Ready=*It; break; }
 if (!Requester || !Requester->HasAuthority() || !Requester->IsLifeCharacterAlive() || !Mode || !Room
  || !Room->IsDevelopmentTestSessionEnabled(World) || !Mode->IsRoomHostController(Requester)
  || Mode->GetSessionLifePhase()!=ESWSessionLifePhase::Playing || !Ready || !Ready->bWorldReady
  || Ready->RestoreGeneration!=Room->GetRestoreGeneration() || IsDevelopmentTransitionBusy()
  || Room->IsGameOverRetryTravelPending()) return false;
 if (!Requester->CleanupLifeInteraction()) return false;
 if (AShip* Ship=Cast<AShip>(Requester->GetPawn())) Ship->ForceDisembark();
 if (ACannon* Cannon=Cast<ACannon>(Requester->GetPawn())) Cannon->ForceExit();
 ABasePlayer* Character=Requester->GetLifeCharacter();
 if (!Character || Character->GetController()!=Requester || !Requester->IsLifeCharacterAlive()) return false;
 return TryFinalDepartureInternal(World,Character,Requester,true,OutError);
#else
 return false;
#endif
}

bool UClassFeatureRoomProgressSubsystem::TryFinalDepartureInternal(UWorld* World, ABasePlayer* Requester, ABasePlayerController* Controller, bool bDevelopmentTest, FString& OutError)
{
 OutError=TEXT("최종 출항의 안전/진행 조건을 만족하지 않습니다");
	USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
	AMultiGameMode* Mode = World ? World->GetAuthGameMode<AMultiGameMode>() : nullptr;
	UStoryFacadeSubsystem* Story = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UStoryFacadeSubsystem>() : nullptr;
	int32 MarkerCount = 0;
	AKelvinShip* PlayerShip = nullptr; int32 PlayerShipCount = 0;
	bool bRequesterInWorld = false;
	if (World)
	{
		for (TActorIterator<ASWFinalEncounterShipEntryPoint> It(World); It; ++It) ++MarkerCount;
		for (TActorIterator<AKelvinShip> It(World); It; ++It) if (!It->ActorHasTag(TEXT("Enemy"))) { PlayerShip = *It; ++PlayerShipCount; }
		for (TActorIterator<ABasePlayer> It(World); It; ++It) bRequesterInWorld |= *It == Requester;
	}
	const bool bAccepted = Story && Story->IsStoryNodeReached(EStoryNode::UldolmokBattleQuestAccepted);
	const bool bPrerequisite = Story && Story->IsStoryNodeReached(EStoryNode::MiddleBoss3Defeated);
	const bool bRetryAllowed = Room && Room->GetActiveRoom()
		&& !Room->GetActiveRoom()->bFinalDepartureCompleted;
	const bool bValid = Room && Mode && Story && Requester
		&& Controller && Requester->HasAuthority() && Mode->CanMutateGameplay(Controller) && bRequesterInWorld
  && Controller->GetLifeCharacter()==Requester && Requester->GetWorld()==World
  && (bDevelopmentTest || bPrerequisite) && (bDevelopmentTest || bRetryAllowed) && !Story->IsStoryNodeReached(EStoryNode::FinalBossDefeated)
		&& !IsInPlaceVoyageBusy() && !Mode->IsLevelRestartRequested() && !Room->IsNewRoomPending()
		&& !Room->IsReturnTravelPending() && !Room->IsFinalDepartureTravelPending()
		&& !Room->IsGameOverTravelPending() && MarkerCount == 1 && PlayerShipCount == 1 && IsValid(PlayerShip)
		&& (!bDevelopmentTest || Mode->GetPlayerRespawnShip()==PlayerShip)
  && (bDevelopmentTest || bAccepted || Story->CanCompleteStoryNode(EStoryNode::UldolmokBattleQuestAccepted));
	FSWFinalEncounterDiagnostics::Write(TEXT("FinalDeparture"), bValid ? TEXT("Requested") : TEXT("Rejected"),
		FString::Printf(TEXT("Requester=%s Prerequisite=%d Accepted=%d RetryAllowed=%d Markers=%d Ship=%s TransitionActive=%d"),
			*GetNameSafe(Requester), bPrerequisite, bAccepted, bRetryAllowed, MarkerCount,
			*GetNameSafe(PlayerShip), IsInPlaceVoyageBusy()));
	if (!bValid || IsInPlaceVoyageBusy()) return false;
 return StartVoyageTransition(World, ESWVoyageReason::FinalDeparture, bDevelopmentTest, OutError);
}

bool UClassFeatureRoomProgressSubsystem::GetStoredControllerProgress(ABasePlayerController* Controller, FSWRoomPlayerProgress& OutProgress) const
{
 const USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
 const USWRoomSaveGame* Save = Room ? Room->GetActiveRoom() : nullptr;
 const AMultiGameMode* Mode = Controller && Controller->GetWorld() ? Controller->GetWorld()->GetAuthGameMode<AMultiGameMode>() : nullptr;
 if (!Save || !Mode) return false;
 if (Mode->GetPlayerIndex(Controller) == 0) { OutProgress = Save->HostProgress; return true; }
 if (!Controller->PlayerState) return false;
 const FString Name = Controller->PlayerState->GetPlayerName();
 if (const FSWRoomGuestProgress* Guest = Save->Guests.FindByPredicate([&Name](const FSWRoomGuestProgress& Entry) { return Entry.DisplayName == Name; }))
 { OutProgress = Guest->Progress; return true; }
 return false;
}
bool UClassFeatureRoomProgressSubsystem::CaptureControllerProgress(ABasePlayerController* Controller, bool bUseFrozen, FString& OutError)
{
 USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
 USWRoomSaveGame* Save = Room ? Room->GetMutableActiveRoom() : nullptr;
 AMultiGameMode* Mode = Controller && Controller->GetWorld() ? Controller->GetWorld()->GetAuthGameMode<AMultiGameMode>() : nullptr;
 FSWRoomPlayerProgress Progress;
 if (!Save || !Mode || (bUseFrozen && !Controller->bLifeProgressFrozen)
  || (!bUseFrozen && !Controller->CaptureLatestLifeProgress(Controller->GetPawn())) || !Controller->GetLatestLifeProgress(Progress))
 { OutError = TEXT("RetryCaptureFailed: player record unavailable"); return false; }
 if (Mode->GetPlayerIndex(Controller) == 0) Save->HostProgress = Progress;
 else
 {
  if (!Controller->PlayerState) { OutError = TEXT("RetryCaptureFailed: guest identity missing"); return false; }
  const FString Name = Controller->PlayerState->GetPlayerName();
  FSWRoomGuestProgress* Guest = Save->Guests.FindByPredicate([&Name](const FSWRoomGuestProgress& Entry) { return Entry.DisplayName == Name; });
  if (!Guest) Guest = &Save->Guests.AddDefaulted_GetRef();
  Guest->DisplayName = Name; Guest->Progress = Progress;
 }
 SW_ROOM_DETAIL_LOG(LogSWRoomSave, Display, TEXT("Flow=ControllerCapture Sequence=%llu Slot=%d Frozen=%d InventorySlots=%d"), Save->CaptureSequence + 1, Mode->GetPlayerIndex(Controller), bUseFrozen, Progress.InventorySlots.Num());
 return true;
}
bool UClassFeatureRoomProgressSubsystem::ValidateRetryStorage(UWorld* World, FString& OutError) const
{
 const USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
 const USWRoomSaveGame* Save = Room ? Room->GetActiveRoom() : nullptr;
 if (!Save) return false;
 for (TActorIterator<ASharedStorageChest> It(World); It; ++It)
 {
  const ASharedStorageChest* Chest = *It;
  const FSWRoomStorageProgress* Expected = Save->SharedProgress.Storage.FindByPredicate([Chest](const FSWRoomStorageProgress& Entry)
   { return Entry.ChestId == Chest->PersistentChestId && Entry.SaveNamespace == Chest->SaveNamespace; });
  const UStorageComponent* Storage = Chest->GetStorageComponent();
  if (!Expected || !Storage || Storage->GetSlotsPerTab() != Expected->SlotsPerTab)
  { OutError = TEXT("RetryStorageNotEmpty: capacity mismatch"); return false; }
  for (const FInventorySlot& Slot : Storage->GetPersistentSlots()) if (Slot.ItemTag.IsValid() || Slot.Count != 0)
  { OutError = TEXT("RetryStorageNotEmpty"); return false; }
  SW_ROOM_DETAIL_LOG(LogSWRoomSave, Display, TEXT("Flow=RetryStorageValidate Chest=%s Capacity=%d Slots=%d Sequence=%llu"), *Chest->PersistentChestId.ToString(), Storage->GetSlotsPerTab(), Storage->GetPersistentSlots().Num(), Save->CaptureSequence + 1);
 }
 for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
 {
  ABasePlayerController* Controller = Cast<ABasePlayerController>(It->Get());
  FSWRoomPlayerProgress Expected, Actual;
  if (!Controller || !Controller->GetLifeCharacter() || !GetStoredControllerProgress(Controller, Expected))
  { OutError = TEXT("RetryCaptureFailed: restored inventory missing"); return false; }
  Controller->GetLifeCharacter()->CaptureRoomProgress(Actual);
  if (Actual.InventorySlots.Num() != Expected.InventorySlots.Num()) { OutError = TEXT("RetryCaptureFailed: inventory size"); return false; }
  for (int32 Index = 0; Index < Actual.InventorySlots.Num(); ++Index)
  {
   const FSWInventorySlotSnapshot& A = Actual.InventorySlots[Index]; const FSWInventorySlotSnapshot& B = Expected.InventorySlots[Index];
   if (A.Tab != B.Tab || A.SlotIndex != B.SlotIndex || A.ItemTag != B.ItemTag || A.Count != B.Count)
   { OutError = TEXT("RetryCaptureFailed: inventory mismatch"); return false; }
  }
 }
 return true;
}
bool UClassFeatureRoomProgressSubsystem::TryGameOverRetry(UWorld* World, ABasePlayerController* Requester, uint64 RequestId, FString& OutError)
{
 AMultiGameMode* Mode = World ? World->GetAuthGameMode<AMultiGameMode>() : nullptr;
 if (!GetRoom(GetGameInstance()) || !Mode || !Requester || IsDevelopmentTransitionBusy() || !Mode->CanHostRequestGameOverRetry(Requester))
 { OutError = TEXT("RetryRejectedNotHostOrBusy"); return false; }
 RetryRequester = Requester; RetryRequestId = RequestId;
 const bool bStarted = StartVoyageTransition(World, ESWVoyageReason::GameOverRetry, false, OutError);
 if (!bStarted) { RetryRequester.Reset(); RetryRequestId = 0; }
 return bStarted;
}
void UClassFeatureRoomProgressSubsystem::HandleTransitionLogout(ABasePlayerController* Controller)
{
 HandleVoyageParticipantLogout(Controller);
}

bool UClassFeatureRoomProgressSubsystem::IsInPlaceVoyageBusy() const { return VoyageTransition && VoyageTransition->IsBusy(); }
FName UClassFeatureRoomProgressSubsystem::GetVoyageParticipantId_Implementation() const
{
 UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
 USWVoyageResetSubsystem* Core = World ? World->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
 return Core ? Core->ResolveParticipantId(const_cast<UClassFeatureRoomProgressSubsystem*>(this)) : NAME_None;
}
bool UClassFeatureRoomProgressSubsystem::StartVoyageTransition(UWorld* World, ESWVoyageReason Reason, bool bDevelopment, FString& OutError)
{
 if (!VoyageTransition) VoyageTransition = NewObject<UClassFeatureVoyageTransition>(this);
 const bool bStarted = VoyageTransition->Start(World, Reason, bDevelopment, OutError);
 if (!bStarted) UE_LOG(LogSWRoom, Error, TEXT("VoyageStartFailed Reason=%d Error=%s"), static_cast<int32>(Reason), *OutError.Left(512));
 return bStarted;
}
ESWVoyageStepResult UClassFeatureRoomProgressSubsystem::PollInPlaceRestore(FString& OutError)
{
 OutError.Reset();
 if (VoyageTransition && VoyageTransition->GetContext().Phase == ESWVoyagePhase::Failed) { OutError = TEXT("VoyageTransitionFailed"); return ESWVoyageStepResult::Failed; }
 return IsInPlaceVoyageBusy() ? ESWVoyageStepResult::Pending : ESWVoyageStepResult::Succeeded;
}
void UClassFeatureRoomProgressSubsystem::HandleVoyageStageAck(ABasePlayerController* Controller, int64 AttemptId, int32 Generation, ESWVoyageAck Ack)
{ if (VoyageTransition) VoyageTransition->ReceiveAck(Controller, AttemptId, Generation, Ack); }
void UClassFeatureRoomProgressSubsystem::HandleVoyageParticipantLogin(ABasePlayerController* Controller)
{ if (VoyageTransition) VoyageTransition->HandleLogin(Controller); }
void UClassFeatureRoomProgressSubsystem::HandleVoyageParticipantLogout(ABasePlayerController* Controller)
{ if (VoyageTransition) VoyageTransition->HandleLogout(Controller); }
bool UClassFeatureRoomProgressSubsystem::RetryVoyageFailure(ABasePlayerController* Controller, int64 AttemptId, int32 Generation)
{ return VoyageTransition && VoyageTransition->RetryFailure(Controller, AttemptId, Generation); }
void UClassFeatureRoomProgressSubsystem::ReportVoyageFailure(ABasePlayerController* Controller, int64 AttemptId, int32 Generation, const FString& Error)
{
 if (VoyageTransition && VoyageTransition->Matches(Controller, AttemptId, Generation)
  && VoyageTransition->GetContext().Phase >= ESWVoyagePhase::Presentation
  && VoyageTransition->GetContext().Phase <= ESWVoyagePhase::ClientReady)
 { UE_LOG(LogSWRoom, Error, TEXT("VoyageClientFailure Attempt=%lld Generation=%d Error=%s"), AttemptId, Generation, *Error.Left(512)); VoyageTransition->Fail(Error.Left(512)); }
}
bool UClassFeatureRoomProgressSubsystem::PlaceVoyageShip(UWorld* World, bool bFinal, bool bContinue, FString& OutError)
{
 AMultiGameMode* Mode = World ? World->GetAuthGameMode<AMultiGameMode>() : nullptr;
 if (!Mode) { OutError = TEXT("VoyageShipModeMissing"); return false; }
 AKelvinShip* Ship = nullptr; int32 Count = 0;
 for (TActorIterator<AKelvinShip> It(World); It; ++It) if (!It->ActorHasTag(TEXT("Enemy"))) { Ship = *It; ++Count; }
 int32 MarkerCount = 0; AActor* Entry = ResolveShipEntry(World, bFinal, MarkerCount);
 if (Count != 1 || !Entry || !Mode->RegisterPlayerRespawnShip(Ship)) { OutError = TEXT("VoyageShipPlacementContractInvalid"); return false; }
 if (!bContinue)
 {
  float WaterZ = 0.f;
  if (!FWaterSurfaceQueryLibrary::QueryWaterSurface(World, Entry->GetActorLocation(), WaterZ) || !FMath::IsFinite(WaterZ))
  { OutError = TEXT("VoyageShipPlacementWaterUnavailable"); return false; }
 }
 if (!PlaceShipSafely(World, Ship, Entry, !bContinue, OutError)) return false;
 if (!bContinue)
 {
  if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Ship->GetRootComponent()))
  { Root->SetPhysicsLinearVelocity(FVector::ZeroVector); Root->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector); }
  if (bFinal)
  {
   int32 FinalShips = 0;
   for (TActorIterator<AShip> It(World); It; ++It) if (It->IsFinalBossSquadForDeckContent())
   { It->RefreshStoryGateOwnedActors(); ++FinalShips; if (It->IsStoryGateDormantForDeckContent()) { OutError = TEXT("VoyageFinalSquadDormant"); return false; } }
   if (!FinalShips) { OutError = TEXT("VoyageFinalSquadMissing"); return false; }
  }
 }
 return true;
}

void UClassFeatureRoomProgressSubsystem::ConfirmReturnPresentation(ABasePlayerController* Controller)
{
 // Legacy ACK cannot advance the new attempt-scoped protocol.
}
