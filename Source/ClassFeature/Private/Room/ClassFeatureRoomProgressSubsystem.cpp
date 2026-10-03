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
	if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
		World->GetTimerManager().ClearTimer(ReturnPresentationTimeoutHandle);
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadHandle);
	if (RestoreTickerHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(RestoreTickerHandle);
	Super::Deinitialize();
}

void UClassFeatureRoomProgressSubsystem::HandlePostLoadMap(UWorld* World)
{
	if (!GetRoom(GetGameInstance()) || !World || World->GetGameInstance() != GetGameInstance()) return;
	PendingWorld = World;
	USWRoomProgressSubsystem* TransitionRoom = GetRoom(GetGameInstance());
 if (World->GetNetMode()!=NM_Client) TransitionRoom->ConsumeDevelopmentFinalDeparturePending(World);
 RestoreDeadline = FPlatformTime::Seconds() + (TransitionRoom && (TransitionRoom->IsReturnTravelPending() || TransitionRoom->IsFinalDepartureTravelPending()) ? 30.0 : 10.0);
 FinalPlacedControllers.Reset(); bFinalDeparturePlayersPlaced = false; bTravelAccepted = false;
	bReturning = false;
	TransitionReason = ERoomTransitionReason::Return;
	ReturnControllers.Reset();
	PendingReturnControllers.Reset();
	bWorldSnapshotRestored = false;
	bFinalDepartureSharedRestored = false;
	bReturnShipPlaced = false;
	bShipSafetyFallbackUsed = false;
	ShipSafetyCheckAt = 0.0;
	ShipPlacementRealTime = 0.0;
	ShipPlacementWorldTime = 0.0;
	LastShipSafetyDiagnosticWorldTime = -1.0;
	if (RestoreTickerHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(RestoreTickerHandle);
	RestoreTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UClassFeatureRoomProgressSubsystem::TickRestore), 0.0f);
}

bool UClassFeatureRoomProgressSubsystem::TickRestore(float DeltaTime)
{
 // Clear temporary encounter permission on every unsuccessful restore exit.
 struct FDevelopmentRestoreScope
 {
  USWRoomProgressSubsystem* Room; UWorld* World; bool bKeep = false;
  ~FDevelopmentRestoreScope() { if (!bKeep && Room) Room->ClearDevelopmentFinalEncounterWorld(World); }
 } DevelopmentScope{GetRoom(GetGameInstance()), PendingWorld.Get()};
	UWorld* World = PendingWorld.Get();
	if (!World || World->GetGameInstance() != GetGameInstance()) return false;
	AMultiGameMode* Mode = World->GetAuthGameMode<AMultiGameMode>();
	if (!Mode || !World->HasBegunPlay())
	{
		if (FPlatformTime::Seconds() < RestoreDeadline) { DevelopmentScope.bKeep=true; return true; }
		UE_LOG(LogSWRoom, Error, TEXT("Hosted room world did not become ready"));
		FPlatformMisc::RequestExit(false);
		return false;
	}
	USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
	if (!Room || !Room->GetActiveRoom()) return false;
	const bool bTransitionRestore = Room->IsReturnTravelPending() || Room->IsFinalDepartureTravelPending();
	if (!bWorldSnapshotRestored)
		UE_LOG(LogSWRoom, Display, TEXT("Flow=WorldRestore RoomId=%s Mode=%s Phase=Begin"),
			*Room->GetActiveRoom()->RoomId.ToString(), Room->IsNewRoomPending() ? TEXT("New")
			: Room->IsFinalDepartureTravelPending() ? TEXT("FinalDeparture")
			: Room->IsReturnTravelPending() ? TEXT("Return") : Room->IsGameOverTravelPending() ? TEXT("GameOver") : TEXT("Continue"));
	if ((Room->IsNewRoomPending() || Room->IsReturnTravelPending()
		|| Room->IsFinalDepartureTravelPending() || Room->IsGameOverTravelPending()) && !bWorldSnapshotRestored)
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
	if (Room->IsFinalDepartureTravelPending() && !bFinalDepartureSharedRestored)
	{
		if (!RestoreSharedWorld(World))
		{
			if (FPlatformTime::Seconds() < RestoreDeadline) { DevelopmentScope.bKeep=true; return true; }
			FSWFinalEncounterDiagnostics::Write(TEXT("FinalRestore"), TEXT("SharedRestoreFailed"),
				FString::Printf(TEXT("AttemptId=%d"), ActiveFinalDepartureAttemptId));
			return false;
		}
		bFinalDepartureSharedRestored = true;
		FSWFinalEncounterDiagnostics::Write(TEXT("FinalRestore"), TEXT("SharedRestored"),
			FString::Printf(TEXT("AttemptId=%d"), ActiveFinalDepartureAttemptId));
	}
	if (!bReturnShipPlaced)
	{
		AKelvinShip* Ship = nullptr;
		AActor* ShipEntry = nullptr;
		int32 MarkerCount = 0;
		int32 ShipCount = 0;
  for (TActorIterator<AKelvinShip> It(World); It; ++It) if (!It->ActorHasTag(TEXT("Enemy"))) { Ship = *It; ++ShipCount; }
  if (ShipCount != 1 || !Mode->RegisterPlayerRespawnShip(Ship))
  { UE_LOG(LogSWRoom, Error, TEXT("RespawnShipMissing Count=%d"), ShipCount); FPlatformMisc::RequestExit(false); return false; }
  if (Ship->IsSinking()) Mode->NotifyPlayerShipSinking(Ship);
		ShipEntry = ResolveShipEntry(World, Room->IsFinalDepartureTravelPending(), MarkerCount);
		if (Room->IsFinalDepartureTravelPending())
			FSWFinalEncounterDiagnostics::Write(TEXT("FinalRestore"), TEXT("MarkerResolved"),
				FString::Printf(TEXT("Count=%d Transform=%s"), MarkerCount,
					ShipEntry ? *ShipEntry->GetActorTransform().ToString() : TEXT("Missing")));
		if (!Ship || !ShipEntry) { UE_LOG(LogSWRoom, Error, TEXT("Return ship or entry missing")); return false; }
		FString Placement;
		if (!PlaceShipSafely(World, Ship, ShipEntry,
			Room->IsNewRoomPending() || Room->IsReturnTravelPending()
				|| Room->IsFinalDepartureTravelPending() || Room->IsGameOverTravelPending(), Placement))
		{ UE_LOG(LogSWRoom, Error, TEXT("Room ship safety failed: %s"), *Placement); FPlatformMisc::RequestExit(false); return false; }
		if (!Room->IsNewRoomPending() && !Room->IsReturnTravelPending()
			&& !Room->IsFinalDepartureTravelPending() && !Room->IsGameOverTravelPending())
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
		ShipPlacementRealTime = FPlatformTime::Seconds();
		ShipPlacementWorldTime = World->GetTimeSeconds();
		ShipSafetyCheckAt = FPlatformTime::Seconds() + 0.5;
		DevelopmentScope.bKeep=true; return true;
	}
	if (bReturnShipPlaced)
	{
		if (FPlatformTime::Seconds() < ShipSafetyCheckAt) { DevelopmentScope.bKeep=true; return true; }
		AKelvinShip* Ship = nullptr;
		AActor* ShipEntry = nullptr;
		int32 MarkerCount = 0;
		int32 ShipCount = 0;
  for (TActorIterator<AKelvinShip> It(World); It; ++It) if (!It->ActorHasTag(TEXT("Enemy"))) { Ship = *It; ++ShipCount; }
  if (ShipCount != 1 || !Mode->RegisterPlayerRespawnShip(Ship))
  { UE_LOG(LogSWRoom, Error, TEXT("RespawnShipMissing Count=%d"), ShipCount); FPlatformMisc::RequestExit(false); return false; }
  if (Ship->IsSinking()) Mode->NotifyPlayerShipSinking(Ship);
		ShipEntry = ResolveShipEntry(World, Room->IsFinalDepartureTravelPending(), MarkerCount);
		if (!Ship || !ShipEntry) return false;
		const bool bPostPlacementBlocked = HasPostPlacementBlock(World, Ship);
		if (bPostPlacementBlocked || LastShipSafetyDiagnosticWorldTime < 0.0 || World->GetTimeSeconds() - LastShipSafetyDiagnosticWorldTime >= 1.0)
		{
			LastShipSafetyDiagnosticWorldTime = World->GetTimeSeconds();
			UE_LOG(LogSWRoom, Display, TEXT("Flow=ShipPlacement Phase=PostPhysicsCheck New=%d Return=%d GameOver=%d Blocked=%d RealElapsed=%.3f WorldElapsed=%.3f TickDelta=%.3f ParticipantsReady=%d Actor=%s Root=%s"),
				Room->IsNewRoomPending(), Room->IsReturnTravelPending(), Room->IsGameOverTravelPending(), bPostPlacementBlocked,
				FPlatformTime::Seconds() - ShipPlacementRealTime, World->GetTimeSeconds() - ShipPlacementWorldTime, DeltaTime,
				AreTransitionParticipantsReady(World), *Ship->GetActorTransform().ToString(), *Ship->GetRootComponent()->GetComponentTransform().ToString());
		}
		if (bPostPlacementBlocked)
		{
			if (!Room->IsNewRoomPending() && !Room->IsReturnTravelPending()
				&& !Room->IsFinalDepartureTravelPending() && !Room->IsGameOverTravelPending())
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
			DevelopmentScope.bKeep=true; return true;
			}
		}
	}
	if (!bFinalDepartureSharedRestored && !RestoreSharedWorld(World))
	{
		if (FPlatformTime::Seconds() < RestoreDeadline) { DevelopmentScope.bKeep=true; return true; }
		UE_LOG(LogSWRoom, Error, TEXT("Hosted room shared progress restore failed"));
		FPlatformMisc::RequestExit(false);
		return false;
	}
	if (Room->IsReturnTravelPending() || Room->IsFinalDepartureTravelPending())
 {
  if (!AreTransitionParticipantsReady(World))
  {
   if (FPlatformTime::Seconds() < RestoreDeadline) { DevelopmentScope.bKeep=true; return true; }
   UE_LOG(LogSWRoom, Error, TEXT("World restore participant readiness timeout Generation=%d"), Room->GetRestoreGeneration());
   FPlatformMisc::RequestExit(false); return false;
  }
 }
 if (Room->IsFinalDepartureTravelPending() && !bFinalDeparturePlayersPlaced)
 {
  AKelvinShip* Ship = Cast<AKelvinShip>(Mode->GetPlayerRespawnShip());
  if (!Ship) { UE_LOG(LogSWRoom, Error, TEXT("FinalPlayersPlacementFailed: ship missing")); return false; }
  for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
  {
   ABasePlayerController* Controller = Cast<ABasePlayerController>(It->Get());
   if (!Controller || FinalPlacedControllers.Contains(Controller)) continue;
   ABasePlayer* Player = Controller->GetLifeCharacter();
   const int32 Slot = Mode->GetPlayerIndex(Controller);
   TArray<UPlayerRespawnPointComponent*> Points; Ship->GetComponents(Points);
   UPlayerRespawnPointComponent* Point = nullptr; int32 Count = 0;
   for (UPlayerRespawnPointComponent* Candidate : Points)
    if (Slot >= 0 && Slot <= 1 && Candidate->PlayerSlot == static_cast<ESWPlayerSlot>(Slot)) { Point = Candidate; ++Count; }
   if (!Player || Player->GetController() != Controller || !Player->HasCompletedInitialPossession()) { DevelopmentScope.bKeep=true; return true; }
   if (Count != 1 || !Point || !Point->IsRegistered() || Point->GetOwner() != Ship || Point->GetComponentTransform().ContainsNaN()
    || !Player->TeleportTo(Point->GetComponentLocation(), Point->GetComponentRotation(), false, false))
   { UE_LOG(LogSWRoom, Error, TEXT("FinalPlayersPlacementFailed Slot=%d"), Slot); FPlatformMisc::RequestExit(false); return false; }
   Controller->SetControlRotation(FRotator(0, Point->GetComponentRotation().Yaw, 0));
   if (UCharacterMovementComponent* Movement = Player->GetCharacterMovement()) { Movement->StopMovementImmediately(); Movement->SetMovementMode(MOVE_Walking); }
   FinalPlacedControllers.Add(Controller);
  }
  bFinalDeparturePlayersPlaced = true;
 }
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
	if (Room->IsReturnTravelPending())
	{
		bool bHostPresent = false;
		for (TActorIterator<ABasePlayer> It(World); It; ++It)
			if (It->GetController() && Mode->GetPlayerIndex(It->GetController()) == 0) bHostPresent = true;
		if (!bHostPresent) { DevelopmentScope.bKeep=true; return true; }
		FString Error;
		if (!TrySave(World, ESWRoomSaveKind::Return, Error))
		{
			UE_LOG(LogSWRoom, Error, TEXT("Return happened but room save failed: %s"), *Error);
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
				if (APlayerController* Controller = It->Get()) Controller->ClientMessage(TEXT("귀환은 되었지만 저장에 실패했습니다. 수동 저장을 다시 시도하세요. 서버 종료 시 이전 저장으로 돌아갈 수 있습니다."));
			Room->ClearReturnTravelPending();
		}
	}
	if (Room->IsFinalDepartureTravelPending())
	{
		bool bHostPresent = false;
		for (TActorIterator<ABasePlayer> It(World); It; ++It)
			if (It->GetController() && Mode->GetPlayerIndex(It->GetController()) == 0) bHostPresent = true;
		if (!bHostPresent) { DevelopmentScope.bKeep=true; return true; }
		// Refresh after restoration and player placement. Story-dormant actors
		// cannot use their disabled Tick to open the gate.
		int32 FinalShips = 0;
		int32 DormantFinalShips = 0;
		for (TActorIterator<AShip> It(World); It; ++It)
		{
			if (!It->IsFinalBossSquadForDeckContent()) continue;
			It->RefreshStoryGateOwnedActors();
			++FinalShips;
			if (It->IsStoryGateDormantForDeckContent()) ++DormantFinalShips;
		}
		FSWFinalEncounterDiagnostics::Write(TEXT("FinalRestore"), TEXT("SquadActivation"),
			FString::Printf(TEXT("Count=%d Dormant=%d"), FinalShips, DormantFinalShips));
		if (FinalShips == 0 || DormantFinalShips != 0)
		{
			UE_LOG(LogSWRoom, Error, TEXT("Final squad activation failed Count=%d Dormant=%d"), FinalShips, DormantFinalShips);
			return false;
		}
		FString Error;
		if (!TrySave(World, ESWRoomSaveKind::Return, Error))
		{
			FSWFinalEncounterDiagnostics::Write(TEXT("FinalRestore"), TEXT("SaveFailed"), Error);
			Room->ClearFinalDepartureTravelPending();
   Room->ClearDevelopmentFinalEncounterWorld(World);
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
				if (APlayerController* Controller = It->Get())
					Controller->ClientMessage(TEXT("울돌목 출항 저장에 실패했습니다. 다시 시도해 주세요."));
		}
		else
		{
			FSWFinalEncounterDiagnostics::Write(TEXT("FinalRestore"), TEXT("SaveSucceeded"),
				FString::Printf(TEXT("Completed=%d"), Room->GetActiveRoom()->bFinalDepartureCompleted));
		}
  bDevelopmentFinalDeparture=false;
	}
	Room->ClearGameOverTravelPending();
	Room->ClearGameOverRetryTravelPending(); bHasRetryRollback = false; RollbackHost = FSWRoomPlayerProgress(); RollbackGuests.Reset(); RollbackShared = FSWRoomSharedProgress(); Room->ExpectedTransitionPlayers.Reset();
	if (!bTransitionRestore && !Room->IsNewRoomPending() && !Room->IsReturnTravelPending()
		&& !Room->IsFinalDepartureTravelPending())
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
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		if (ABasePlayerController* Controller = Cast<ABasePlayerController>(It->Get())) Controller->ReleaseFrozenLifeProgress();
	if (!Room->IsNewRoomPending()) Mode->SetHostedRoomWorldReady();
	Mode->MarkHostedRoomWorldReady();
	UE_LOG(LogSWRoom, Display, TEXT("Flow=WorldRestore RoomId=%s Result=Ready ReturnPending=%d"),
		*Room->GetActiveRoom()->RoomId.ToString(), Room->IsReturnTravelPending());
	Mode->OnGameOverRequested.AddDynamic(this, &UClassFeatureRoomProgressSubsystem::HandleGameOverRestart);
	PendingWorld.Reset();
	RestoreTickerHandle.Reset();
 DevelopmentScope.bKeep=true;
	return false;
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
		if (!Stored) { if (Room->IsGameOverRetryTravelPending()) return false; continue; }
		TArray<FInventorySlot> Slots;
		for (const FSWRoomStorageSlot& Entry : Stored->Slots)
		{
			FInventorySlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.ItemTag = Entry.ItemTag;
			Slot.Count = Entry.Count;
		}
		if (Room->IsGameOverRetryTravelPending()) { Slots.Reset(); Slots.SetNum(Stored->SlotsPerTab * 4); }
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
{ if (AMultiGameMode* LifeMode = Player && Player->GetWorld() ? Player->GetWorld()->GetAuthGameMode<AMultiGameMode>() : nullptr;
  LifeMode && (LifeMode->GetSessionLifePhase() == ESWSessionLifePhase::GameOver || LifeMode->GetSessionLifePhase() == ESWSessionLifePhase::ReturningAfterGameOver))
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
	if (!Room || !Mode || !Requester || bReturning || !Mode->CanMutateGameplay(Requester->GetController()))
	{
		UE_LOG(LogSWRoom, Warning,
			TEXT("Flow=Return Result=Rejected World=%s HostedRoom=%d GameMode=%s Requester=%s AlreadyReturning=%d"),
			*GetNameSafe(World), Room ? 1 : 0, *GetNameSafe(Mode), *GetNameSafe(Requester), bReturning ? 1 : 0);
		return false;
	}
	TransitionReason = ERoomTransitionReason::Return;
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
	ReturnControllers.Reset();
	PendingReturnControllers.Reset();
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		ABasePlayerController* Controller = Cast<ABasePlayerController>(It->Get());
		if (!Controller)
		{
			UE_LOG(LogSWRoom, Error, TEXT("Flow=Return Result=Failed Phase=Presentation Reason=UnexpectedController"));
			ReturnControllers.Reset();
			return false;
		}
		ReturnControllers.Add(Controller);
		PendingReturnControllers.Add(Controller);
	}
	if (ReturnControllers.IsEmpty())
	{
		UE_LOG(LogSWRoom, Error, TEXT("Flow=Return Result=Failed Phase=Presentation Reason=NoControllers"));
		return false;
	}
	RecordTransitionParticipants(World);
	bReturning = true;
	World->GetTimerManager().SetTimer(ReturnPresentationTimeoutHandle, this,
		&UClassFeatureRoomProgressSubsystem::HandleReturnPresentationTimeout, 3.0f, false);
	UE_LOG(LogSWRoom, Display, TEXT("Flow=Return RoomId=%s Phase=PresentationRequested Clients=%d"),
		*Room->GetActiveRoom()->RoomId.ToString(), ReturnControllers.Num());
	for (const TWeakObjectPtr<ABasePlayerController>& Controller : ReturnControllers)
		if (Controller.IsValid()) Controller->ClientBeginRoomReturn();
	return true;
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
		&& !bReturning && !Mode->IsLevelRestartRequested() && !Room->IsNewRoomPending()
		&& !Room->IsReturnTravelPending() && !Room->IsFinalDepartureTravelPending()
		&& !Room->IsGameOverTravelPending() && MarkerCount == 1 && PlayerShipCount == 1 && IsValid(PlayerShip)
		&& (!bDevelopmentTest || Mode->GetPlayerRespawnShip()==PlayerShip)
  && (bDevelopmentTest || bAccepted || Story->CanCompleteStoryNode(EStoryNode::UldolmokBattleQuestAccepted));
	FSWFinalEncounterDiagnostics::Write(TEXT("FinalDeparture"), bValid ? TEXT("Requested") : TEXT("Rejected"),
		FString::Printf(TEXT("Requester=%s Prerequisite=%d Accepted=%d RetryAllowed=%d Markers=%d Ship=%s TransitionActive=%d"),
			*GetNameSafe(Requester), bPrerequisite, bAccepted, bRetryAllowed, MarkerCount,
			*GetNameSafe(PlayerShip), bReturning));
	if (!bValid) return false;

	ReturnControllers.Reset();
	PendingReturnControllers.Reset();
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		ABasePlayerController* Controller = Cast<ABasePlayerController>(It->Get());
		if (!Controller)
		{
			ReturnControllers.Reset();
			PendingReturnControllers.Reset();
			return false;
		}
		ReturnControllers.Add(Controller);
		PendingReturnControllers.Add(Controller);
	}
	if (ReturnControllers.IsEmpty() || (bDevelopmentTest && ReturnControllers.Num()!=2)) return false;
 if (bDevelopmentTest) for (const TWeakObjectPtr<ABasePlayerController>& Participant:ReturnControllers)
  if (!Participant.IsValid() || !CaptureControllerProgress(Participant.Get(),false,OutError)) return false;
	RecordTransitionParticipants(World);
	bReturning = true;
 bDevelopmentFinalDeparture = bDevelopmentTest;
	TransitionReason = ERoomTransitionReason::FinalDeparture;
	ActiveFinalDepartureAttemptId = ++FinalDepartureAttemptSerial;
	World->GetTimerManager().SetTimer(ReturnPresentationTimeoutHandle, this,
		&UClassFeatureRoomProgressSubsystem::HandleReturnPresentationTimeout, 3.0f, false);
	FSWFinalEncounterDiagnostics::Write(TEXT("FinalDeparture"), TEXT("PresentationRequested"),
		FString::Printf(TEXT("AttemptId=%d Clients=%d"), ActiveFinalDepartureAttemptId, ReturnControllers.Num()));
	for (const TWeakObjectPtr<ABasePlayerController>& Controller : ReturnControllers)
		if (Controller.IsValid()) Controller->ClientBeginFinalDeparture(ActiveFinalDepartureAttemptId);
	return true;
}

void UClassFeatureRoomProgressSubsystem::ConfirmReturnPresentation(ABasePlayerController* Controller)
{
	if (!bReturning || !Controller || !Controller->HasAuthority()) return;
	const int32 Removed = PendingReturnControllers.RemoveAll(
		[Controller](const TWeakObjectPtr<ABasePlayerController>& Pending) { return Pending.Get() == Controller; });
	if (Removed == 0) return;
	if (TransitionReason == ERoomTransitionReason::FinalDeparture)
		FSWFinalEncounterDiagnostics::Write(TEXT("FinalDeparture"), TEXT("PresentationConfirmed"),
			FString::Printf(TEXT("AttemptId=%d Remaining=%d"), ActiveFinalDepartureAttemptId,
				PendingReturnControllers.Num()));
	UE_LOG(LogSWRoom, Display, TEXT("Flow=Return Phase=PresentationConfirmed Controller=%s Remaining=%d"),
		*GetNameSafe(Controller), PendingReturnControllers.Num());
	if (PendingReturnControllers.IsEmpty()) BeginReturnTravel();
}

void UClassFeatureRoomProgressSubsystem::BeginReturnTravel()
{
	UE_LOG(LogSWRoom, Display, TEXT("[SWLifeDiag] Event=ReturnTravelEntered Returning=%d Reason=%d PendingAck=%d"), bReturning, static_cast<int32>(TransitionReason), PendingReturnControllers.Num());
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!bReturning || !World) return;
	World->GetTimerManager().ClearTimer(ReturnPresentationTimeoutHandle);
	AMultiGameMode* Mode = World->GetAuthGameMode<AMultiGameMode>();
	RecordTransitionParticipants(World);
	if (TransitionReason == ERoomTransitionReason::FinalDeparture)
	{
		UStoryFacadeSubsystem* Story = GetGameInstance()->GetSubsystem<UStoryFacadeSubsystem>();
		const bool bAlreadyAccepted = Story
			&& Story->IsStoryNodeReached(EStoryNode::UldolmokBattleQuestAccepted);
  const bool bCompleted = Story && (bAlreadyAccepted || (bDevelopmentFinalDeparture
   ? Story->ActivateDevelopmentFinalBattle() : Story->CompleteStoryNode(EStoryNode::UldolmokBattleQuestAccepted)));
  if (bDevelopmentFinalDeparture) UE_LOG(LogSWRoom, Display, TEXT("StoryCommit=DevelopmentFinalBattle Accepted=%d"), Story && Story->IsStoryNodeReached(EStoryNode::UldolmokBattleQuestAccepted));
		FSWFinalEncounterDiagnostics::Write(TEXT("FinalDeparture"), TEXT("StoryCommit"),
			FString::Printf(TEXT("AttemptId=%d Before=%d Result=%d"), ActiveFinalDepartureAttemptId,
				bAlreadyAccepted, bCompleted));
		if (!bCompleted)
		{
			CancelReturnPresentation();
			return;
		}
		int32 PlayerCount = 0;
		for (TActorIterator<ABasePlayer> It(World); It; ++It)
		{
			if (UInventoryComponent* Inventory = It->GetInventoryComponent()) Inventory->ReturnCursorToOriginalSlot();
			CapturePlayer(*It);
			++PlayerCount;
		}
  bool bPlayersCaptured=true;
  if (bDevelopmentFinalDeparture) for (const TWeakObjectPtr<ABasePlayerController>& Participant:ReturnControllers)
  { FString Error; bPlayersCaptured &= Participant.IsValid() && CaptureControllerProgress(Participant.Get(),false,Error); }
		const bool bCaptured = bPlayersCaptured && CaptureSharedWorld(World);
		FSWFinalEncounterDiagnostics::Write(TEXT("FinalDeparture"), TEXT("ProgressCaptured"),
			FString::Printf(TEXT("AttemptId=%d Players=%d Shared=%d"), ActiveFinalDepartureAttemptId,
				PlayerCount, bCaptured));
		if (!bCaptured || !Mode)
		{
			CancelReturnPresentation();
			return;
		}
  if (bDevelopmentFinalDeparture) GetRoom(GetGameInstance())->SetDevelopmentFinalDeparturePending(World,true,true);
		const bool bFinalTravelAccepted = Mode->RequestHostedRoomFinalDepartureTravel();
		FSWFinalEncounterDiagnostics::Write(TEXT("FinalDeparture"), TEXT("ServerTravel"),
			FString::Printf(TEXT("AttemptId=%d Accepted=%d"), ActiveFinalDepartureAttemptId, bFinalTravelAccepted));
		if (!bFinalTravelAccepted) CancelReturnPresentation(); else bTravelAccepted = true;
		return;
	}
	UE_LOG(LogSWRoom, Display, TEXT("Flow=Return Phase=TravelRequested DiskWrite=0"));
	if (Mode && Mode->RequestHostedRoomReturnTravel(TransitionReason == ERoomTransitionReason::GameOverRetry)) { bTravelAccepted = true; return; }
	UE_LOG(LogSWRoom, Error, TEXT("Flow=Return Result=Failed Phase=TravelRequested"));
	CancelReturnPresentation();
}

void UClassFeatureRoomProgressSubsystem::HandleReturnPresentationTimeout()
{
	UE_LOG(LogSWRoom, Display, TEXT("[SWLifeDiag] Event=ReturnLoadingAckTimeout Returning=%d Reason=%d PendingAck=%d"), bReturning, static_cast<int32>(TransitionReason), PendingReturnControllers.Num());
	if (TransitionReason == ERoomTransitionReason::FinalDeparture)
		FSWFinalEncounterDiagnostics::Write(TEXT("FinalDeparture"), TEXT("PresentationTimeout"),
			FString::Printf(TEXT("AttemptId=%d Remaining=%d"), ActiveFinalDepartureAttemptId,
				PendingReturnControllers.Num()));
	RetryCancelReason = TEXT("RetryPresentationTimeout");
	UE_LOG(LogSWRoom, Error, TEXT("Flow=Return Result=Failed Phase=PresentationTimeout Remaining=%d"),
		PendingReturnControllers.Num());
	if (TransitionReason == ERoomTransitionReason::Return)
		for (const TWeakObjectPtr<ABasePlayerController>& Controller : ReturnControllers)
			if (Controller.IsValid()) Controller->ClientMessage(TEXT("귀환 준비에 실패했습니다. 다시 시도해 주세요."));
	CancelReturnPresentation();
}

void UClassFeatureRoomProgressSubsystem::CancelReturnPresentation()
{
	UE_LOG(LogSWRoom, Display, TEXT("[SWLifeDiag] Event=ReturnCancelled Reason=%d RetryRequest=%llu Message=%s"), static_cast<int32>(TransitionReason), RetryRequestId, *RetryCancelReason);
 if (USWRoomProgressSubsystem* Room=GetRoom(GetGameInstance())) Room->ClearDevelopmentFinalEncounterWorld(GetGameInstance()->GetWorld());
 bDevelopmentFinalDeparture=false;
	const bool bRetry = TransitionReason == ERoomTransitionReason::GameOverRetry;
 if (bRetry)
 {
  if (USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance()))
  {
   if (USWRoomSaveGame* Save = Room->GetMutableActiveRoom(); Save && bHasRetryRollback)
   { Save->HostProgress = RollbackHost; Save->Guests = RollbackGuests; Save->SharedProgress = RollbackShared; Save->bFinalDepartureCompleted = bRollbackFinalDepartureCompleted; }
   Room->ClearGameOverRetryTravelPending(); Room->ClearReturnTravelPending(); Room->ExpectedTransitionPlayers.Reset();
  }
  bHasRetryRollback = false;
  if (UWorld* World = GetGameInstance()->GetWorld()) if (AMultiGameMode* Mode = World->GetAuthGameMode<AMultiGameMode>()) Mode->SetGameOverRetryTransitionPending(false);
  if (RetryRequester.IsValid()) RetryRequester->ReportGameOverRetryResult(RetryRequestId, false, RetryCancelReason.IsEmpty() ? TEXT("RetryTravelFailed") : RetryCancelReason);
 }
 const bool bFinalDeparture = TransitionReason == ERoomTransitionReason::FinalDeparture;
	if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
		World->GetTimerManager().ClearTimer(ReturnPresentationTimeoutHandle);
	for (const TWeakObjectPtr<ABasePlayerController>& Controller : ReturnControllers)
		if (Controller.IsValid())
		{
			Controller->ClientCancelRoomReturn();
			if (bFinalDeparture) Controller->ClientMessage(TEXT("울돌목 출항에 실패했습니다. 다시 시도해 주세요."));
		}
	ReturnControllers.Reset();
	PendingReturnControllers.Reset();
	bReturning = false;
	TransitionReason = ERoomTransitionReason::Return;
	ActiveFinalDepartureAttemptId = 0;
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
 UE_LOG(LogSWRoomSave, Display, TEXT("Flow=ControllerCapture Sequence=%llu Slot=%d Frozen=%d InventorySlots=%d"), Save->CaptureSequence + 1, Mode->GetPlayerIndex(Controller), bUseFrozen, Progress.InventorySlots.Num());
 return true;
}
void UClassFeatureRoomProgressSubsystem::RecordTransitionParticipants(UWorld* World)
{
 USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
 AMultiGameMode* Mode = World ? World->GetAuthGameMode<AMultiGameMode>() : nullptr;
 if (!Room || !Mode) return;
 Room->ExpectedTransitionPlayers.Reset();
 for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
  if (ABasePlayerController* Controller = Cast<ABasePlayerController>(It->Get()))
  {
   FSWExpectedTransitionPlayer& Expected = Room->ExpectedTransitionPlayers.AddDefaulted_GetRef();
   Expected.Slot = Mode->GetPlayerIndex(Controller);
   Expected.GuestKey = Controller->PlayerState ? Controller->PlayerState->GetPlayerName() : FString();
  }
}
bool UClassFeatureRoomProgressSubsystem::AreTransitionParticipantsReady(UWorld* World) const
{
 const USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
 const AMultiGameMode* Mode = World ? World->GetAuthGameMode<AMultiGameMode>() : nullptr;
 if (!Room || !Mode) return false;
 TArray<ABasePlayerController*> Ready;
 for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
 {
  ABasePlayerController* Controller = Cast<ABasePlayerController>(It->Get());
  ABasePlayer* Player = Controller ? Controller->GetLifeCharacter() : nullptr;
  if (!Player || Player->GetController() != Controller || !Player->HasCompletedInitialPossession()) return false;
  Ready.Add(Controller);
 }
 if (Ready.IsEmpty()) return false;
 for (const FSWExpectedTransitionPlayer& Expected : Room->ExpectedTransitionPlayers)
 {
  if (!Ready.ContainsByPredicate([&Expected, Mode](ABasePlayerController* Controller)
   { return Mode->GetPlayerIndex(Controller) == Expected.Slot && (Expected.Slot == 0 || (Controller->PlayerState && Controller->PlayerState->GetPlayerName() == Expected.GuestKey)); })) return false;
 }
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
  UE_LOG(LogSWRoomSave, Display, TEXT("Flow=RetryStorageValidate Chest=%s Capacity=%d Slots=%d Sequence=%llu"), *Chest->PersistentChestId.ToString(), Storage->GetSlotsPerTab(), Storage->GetPersistentSlots().Num(), Save->CaptureSequence + 1);
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
 UE_LOG(LogSWRoom, Display, TEXT("[SWLifeDiag] Event=RetrySubsystemEntered Controller=%s Request=%llu World=%s Returning=%d"), *GetNameSafe(Requester), RequestId, *GetNameSafe(World), bReturning);
 USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
 USWRoomSaveGame* Save = Room ? Room->GetMutableActiveRoom() : nullptr;
 AMultiGameMode* Mode = World ? World->GetAuthGameMode<AMultiGameMode>() : nullptr;
 if (!Save || !Mode || !Requester || bReturning || !Mode->CanHostRequestGameOverRetry(Requester))
 { OutError = TEXT("RetryRejectedNotHost 또는 전환 처리 중"); return false; }
 // Return shared/loot reservations before private inventory cleanup. Shared items never become private items.
 for (TActorIterator<AStorageChest> It(World); It; ++It)
  if (UStorageComponent* Storage = It->GetStorageComponent()) Storage->ReturnAllReservedCursors();
 for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
 {
  ABasePlayerController* Controller = Cast<ABasePlayerController>(It->Get());
  if (!Controller || !Controller->CleanupLifeInteraction() || !CaptureControllerProgress(Controller, true, OutError))
  { OutError = TEXT("RetryCaptureFailed: cursor or frozen record"); return false; }
 }
 if (!CaptureSharedWorld(World)) { OutError = TEXT("RetryCaptureFailed: shared record"); return false; }
 RollbackHost = Save->HostProgress; RollbackGuests = Save->Guests; RollbackShared = Save->SharedProgress;
 bRollbackFinalDepartureCompleted = Save->bFinalDepartureCompleted; bHasRetryRollback = true;
 for (FSWRoomStorageProgress& Storage : Save->SharedProgress.Storage)
 { Storage.Slots.Reset(); Storage.Slots.SetNum(Storage.SlotsPerTab * 4); }
 Save->bFinalDepartureCompleted = false;
 ReturnControllers.Reset(); PendingReturnControllers.Reset();
 for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
  if (ABasePlayerController* Controller = Cast<ABasePlayerController>(It->Get()))
  { ReturnControllers.Add(Controller); PendingReturnControllers.Add(Controller); }
 RetryRequester = Requester; RetryRequestId = RequestId; RetryCancelReason.Reset();
 UE_LOG(LogSWRoom, Display, TEXT("[SWLifeDiag] Event=RetryCaptureSucceeded Controller=%s Request=%llu Participants=%d"), *GetNameSafe(Requester), RequestId, ReturnControllers.Num());
 TransitionReason = ERoomTransitionReason::GameOverRetry; bReturning = true; bTravelAccepted = false;
 RecordTransitionParticipants(World); Mode->SetGameOverRetryTransitionPending(true);
 World->GetTimerManager().SetTimer(ReturnPresentationTimeoutHandle, this, &UClassFeatureRoomProgressSubsystem::HandleReturnPresentationTimeout, 3.f, false);
 for (const auto& Controller : ReturnControllers) if (Controller.IsValid()) Controller->ClientBeginRoomReturn();
 return true;
}
void UClassFeatureRoomProgressSubsystem::HandleTransitionLogout(ABasePlayerController* Controller)
{
 if (!bReturning || bTravelAccepted || !Controller) return;
 USWRoomProgressSubsystem* Room = GetRoom(GetGameInstance());
 AMultiGameMode* Mode = Controller->GetWorld() ? Controller->GetWorld()->GetAuthGameMode<AMultiGameMode>() : nullptr;
 int32 Slot = Mode ? Mode->GetPlayerIndex(Controller) : INDEX_NONE;
 if (Slot == INDEX_NONE && Room && Controller->PlayerState)
  for (const FSWExpectedTransitionPlayer& Expected : Room->ExpectedTransitionPlayers)
   if (Expected.GuestKey == Controller->PlayerState->GetPlayerName()) { Slot = Expected.Slot; break; }
 if (Room) Room->ExpectedTransitionPlayers.RemoveAll([Slot](const FSWExpectedTransitionPlayer& Entry) { return Entry.Slot == Slot; });
 ReturnControllers.Remove(Controller); PendingReturnControllers.Remove(Controller);
 if (Slot != 0 && PendingReturnControllers.IsEmpty()) BeginReturnTravel();
}
