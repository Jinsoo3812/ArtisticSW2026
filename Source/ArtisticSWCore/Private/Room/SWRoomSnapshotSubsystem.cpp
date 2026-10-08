#include "Room/SWRoomSnapshotSubsystem.h"

#include "Room/SWRoomSnapshotComponent.h"
#include "Room/SWRoomProgressSubsystem.h"
#include "Room/SWRoomStateAdapter.h"
#include "Network/SWNetworkLog.h"
#include "EngineUtils.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Streaming/LevelStreamingDelegates.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Info.h"
#include "GameFramework/Pawn.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "UObject/UnrealType.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
bool IsExcluded(const AActor* Actor)
{
	return !Actor || Actor->IsA<AController>() || Actor->IsA<APlayerState>()
		|| Actor->IsA<AGameModeBase>() || Actor->IsA<AGameStateBase>() || Actor->IsA<AInfo>()
		|| (Cast<APawn>(Actor) && Cast<APawn>(Actor)->IsPlayerControlled()
			&& !Actor->FindComponentByClass<USWRoomSnapshotComponent>());
}

bool IsLevelPlacedActor(const AActor* Actor)
{
	return Actor && Actor->HasAnyFlags(RF_WasLoaded);
}

bool ShouldTraceRoomPhysics(const AActor* Actor)
{
	if (!Actor) return false;
	const FString ClassName = Actor->GetClass()->GetName();
	return ClassName.Contains(TEXT("Chest")) || ClassName.Contains(TEXT("Ship"));
}

void TraceRoomPhysics(const TCHAR* Stage, const AActor* Actor, const FSWRoomActorRecord& Record)
{
	if (!ShouldTraceRoomPhysics(Actor)) return;
	UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor->GetRootComponent());
	UE_LOG(LogSWRoom, Display,
		TEXT("Flow=RoomPhysics Stage=%s Id=%s Actor=%s Origin=%s Parent=%s Owner=%s Scale=%s SavedHasMotion=%d SavedSim=%d RootSim=%d SavedLinear=%s SavedAngular=%s CurrentLinear=%s CurrentAngular=%s"),
		Stage, *Record.StableId.ToString(), *Actor->GetPathName(), *UEnum::GetValueAsString(Record.Origin),
		*GetNameSafe(Actor->GetAttachParentActor()), *GetNameSafe(Actor->GetOwner()),
		*Actor->GetActorScale3D().ToString(), Record.MotionState.bHasMotion ? 1 : 0,
		Record.MotionState.bWasSimulatingPhysics ? 1 : 0, Root && Root->IsSimulatingPhysics() ? 1 : 0,
		*Record.MotionState.LinearVelocity.ToString(), *Record.MotionState.AngularVelocityDegrees.ToString(),
		*(Root ? Root->GetPhysicsLinearVelocity() : FVector::ZeroVector).ToString(),
		*(Root ? Root->GetPhysicsAngularVelocityInDegrees() : FVector::ZeroVector).ToString());
}

bool SerializeValues(UObject* Object, TArray<uint8>& Bytes)
{
	FMemoryWriter Writer(Bytes, true);
	FObjectAndNameAsStringProxyArchive Archive(Writer, false);
	Archive.ArIsSaveGame = true;
	Archive.SetIsPersistent(true);
	Object->Serialize(Archive);
	return !Archive.IsError();
}

void DeserializeValues(UObject* Object, const TArray<uint8>& Bytes)
{
	if (Bytes.IsEmpty()) return;
	FMemoryReader Reader(Bytes, true);
	FObjectAndNameAsStringProxyArchive Archive(Reader, true);
	Archive.ArIsSaveGame = true;
	Archive.SetIsPersistent(true);
	Object->Serialize(Archive);
}

FSoftObjectPath GetMapPath(const UWorld* World)
{
	const FString Package = World->GetOutermost()->GetName();
	return FSoftObjectPath(Package + TEXT(".") + FPackageName::GetShortName(Package));
}

FString GetPartitionKey(const ULevel* Level)
{
	return Level ? Level->GetOutermost()->GetName() + TEXT("|") + Level->GetPathName() : FString();
}

FString GetPartitionKey(const FSWRoomLevelPartition& Partition)
{
	return Partition.PackagePath.ToString() + TEXT("|") + Partition.InstanceName.ToString();
}

FString GetPartitionKey(const FSWRoomDestroyedActorPartition& Partition)
{
	return Partition.PackagePath.ToString() + TEXT("|") + Partition.InstanceName.ToString();
}

FName GetRoomComponentKey(const UActorComponent* Component)
{
	if (!Component) return NAME_None;
	if (Component->CreationMethod == EComponentCreationMethod::Native)
		return Component->GetFName();
	const FString Prefix = TEXT("SWRoomComponentId=");
	FName Found = NAME_None;
	for (const FName& Tag : Component->ComponentTags)
	{
		const FString Value = Tag.ToString();
		if (!Value.StartsWith(Prefix)) continue;
		if (!Found.IsNone()) return NAME_None;
		const FString Suffix = Value.RightChop(Prefix.Len());
		if (Suffix.IsEmpty()) return NAME_None;
		Found = FName(*Suffix);
	}
	return Found;
}

FSWRoomMotionState CaptureMotion(const AActor* Actor, UPrimitiveComponent* Primitive)
{
	FSWRoomMotionState Result;
	if (Primitive && Primitive->IsSimulatingPhysics())
	{
		Result.bHasMotion = true;
		Result.bWasSimulatingPhysics = true;
		Result.LinearVelocity = Primitive->GetPhysicsLinearVelocity();
		Result.AngularVelocityDegrees = Primitive->GetPhysicsAngularVelocityInDegrees();
	}
	else if (Actor)
	{
		if (const UProjectileMovementComponent* Projectile = Actor->FindComponentByClass<UProjectileMovementComponent>())
		{
			Result.bHasMotion = true;
			Result.bWasProjectileMovementActive = Projectile->IsActive();
			Result.LinearVelocity = Projectile->Velocity;
		}
	}
	return Result;
}

void AddMotionIssue(FSWRoomWorldSnapshot& Snapshot, const FSWRoomActorRecord& Record, FName FieldKey)
{
	FSWRoomCaptureIssue& Issue = Snapshot.CaptureIssues.AddDefaulted_GetRef();
	Issue.StableId = Record.StableId;
	Issue.ClassPath = Record.ClassPath;
	Issue.Domain = TEXT("Motion");
	Issue.FieldKey = FieldKey;
	Issue.Reason = TEXT("Non-finite linear or angular velocity");
	UE_LOG(LogSWRoom, Warning, TEXT("Flow=WorldCapture Result=Partial Id=%s Class=%s Domain=Motion Field=%s Reason=%s"),
		*Issue.StableId.ToString(), *Issue.ClassPath.ToString(), *Issue.FieldKey.ToString(), *Issue.Reason);
}

void RestoreMotion(AActor* Actor, UPrimitiveComponent* Primitive, const FSWRoomMotionState& Motion)
{
	if (Primitive && !Motion.bWasSimulatingPhysics && Primitive->IsSimulatingPhysics())
	{
		Primitive->SetSimulatePhysics(false);
	}
	if (!Motion.bHasMotion) return;
	if (Motion.bWasSimulatingPhysics && Primitive)
	{
		Primitive->SetSimulatePhysics(true);
		Primitive->SetPhysicsLinearVelocity(Motion.LinearVelocity);
		Primitive->SetPhysicsAngularVelocityInDegrees(Motion.AngularVelocityDegrees);
	}
	else if (Actor)
	{
		if (UProjectileMovementComponent* Projectile = Actor->FindComponentByClass<UProjectileMovementComponent>())
		{
			Projectile->Velocity = Motion.LinearVelocity;
			if (Motion.bWasProjectileMovementActive) Projectile->Activate(true);
			else Projectile->Deactivate();
		}
	}
}
}

void USWRoomSnapshotSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (InWorld.GetNetMode() == NM_Client) return;
	if (UGameInstance* GameInstance = InWorld.GetGameInstance())
		if (const USWRoomProgressSubsystem* Room = GameInstance->GetSubsystem<USWRoomProgressSubsystem>())
			bRestoring = Room->IsHostedRoom() && !Room->IsNewRoomPending()
				&& !Room->IsReturnTravelPending() && !Room->IsFinalDepartureTravelPending()
				&& !Room->IsGameOverTravelPending();
	SpawnHandle = InWorld.AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &USWRoomSnapshotSubsystem::HandleActorSpawned));
	DestroyHandle = InWorld.AddOnActorDestroyedHandler(FOnActorDestroyed::FDelegate::CreateUObject(this, &USWRoomSnapshotSubsystem::HandleActorDestroyed));
	PreLevelRemovedHandle = FWorldDelegates::PreLevelRemovedFromWorld.AddUObject(this, &USWRoomSnapshotSubsystem::HandlePreLevelRemoved);
	LevelAddedHandle = FWorldDelegates::LevelAddedToWorld.AddUObject(this, &USWRoomSnapshotSubsystem::HandleLevelAdded);
	LevelBeginVisibleHandle = FLevelStreamingDelegates::OnLevelBeginMakingVisible.AddUObject(this, &USWRoomSnapshotSubsystem::HandleLevelBeginMakingVisible);
	for (TActorIterator<AActor> It(&InWorld); It; ++It) HandleActorSpawned(*It);
}

void USWRoomSnapshotSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (SpawnHandle.IsValid()) World->RemoveOnActorSpawnedHandler(SpawnHandle);
		if (DestroyHandle.IsValid()) World->RemoveOnActorDestroyedHandler(DestroyHandle);
	}
	if (PreLevelRemovedHandle.IsValid()) FWorldDelegates::PreLevelRemovedFromWorld.Remove(PreLevelRemovedHandle);
	if (LevelAddedHandle.IsValid()) FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedHandle);
	if (LevelBeginVisibleHandle.IsValid()) FLevelStreamingDelegates::OnLevelBeginMakingVisible.Remove(LevelBeginVisibleHandle);
	RegisteredActors.Reset();
	UnloadedRecords.Reset();
	PromotedUnloadedIds.Reset();
	CachedPartitions.Reset();
	FailedPartitions.Reset();
	FailedInstancePartitions.Reset();
	bStructuralPartitionFailure = false;
	DestroyedActorPartitions.Reset();
	DestroyedLevelActorIds.Reset();
	RestoreIssues.Reset();
	bRestoring = false;
	OnRestoreCompleted.Clear();
	Super::Deinitialize();
}

bool USWRoomSnapshotSubsystem::CompleteRestore(FString& OutError)
{
	if (!bRestoring) return true;
	TMap<FGuid, AActor*> ActorsById;
	for (const TPair<FGuid, TWeakObjectPtr<AActor>>& Pair : RegisteredActors)
		if (AActor* Actor = Pair.Value.Get()) ActorsById.Add(Pair.Key, Actor);
	for (const TPair<FGuid, AActor*>& Pair : ActorsById)
		if (ISWRoomStateAdapter* Adapter = Cast<ISWRoomStateAdapter>(Pair.Value))
			if (!Adapter->FinalizeRoomRestore(ActorsById, OutError))
			{
				OutError = FString::Printf(TEXT("Actor final restore failed: ID=%s Class=%s Reason=%s"),
					*Pair.Key.ToString(), *Pair.Value->GetClass()->GetPathName(), *OutError);
				return false;
			}
	bRestoring = false;
	OnRestoreCompleted.Broadcast();
	return true;
}

void USWRoomSnapshotSubsystem::UpdateRegisteredActorId(AActor* Actor, const FGuid& PreviousId, const FGuid& ReservedId)
{
	if (!Actor || !ReservedId.IsValid()) return;
	if (const TWeakObjectPtr<AActor>* Previous = RegisteredActors.Find(PreviousId);
		Previous && Previous->Get() == Actor) RegisteredActors.Remove(PreviousId);
	RegisteredActors.Add(ReservedId, Actor);
}

void USWRoomSnapshotSubsystem::HandleActorSpawned(AActor* Actor)
{
	if (IsExcluded(Actor)) return;
	USWRoomSnapshotComponent* Component = Actor->FindComponentByClass<USWRoomSnapshotComponent>();
	if (!Component || Component->PersistenceClass == ESWRoomPersistenceClass::Transient) return;
	if (IsLevelPlacedActor(Actor)) Component->RefreshLevelInstanceId();
	if (!IsLevelPlacedActor(Actor) && !bRestoring)
	{
		if (!Component->StableId.IsValid()) Component->SetRuntimeId(FGuid::NewGuid());
		if (Component->GetCreatorSequence() == 0)
		{
			FGuid Creator(0, 0, 0, 1);
			if (AActor* Owner = Actor->GetOwner())
				if (const USWRoomSnapshotComponent* OwnerSnapshot = Owner->FindComponentByClass<USWRoomSnapshotComponent>())
					if (OwnerSnapshot->StableId.IsValid()) Creator = OwnerSnapshot->StableId;
			Component->SetRuntimeOrigin(Creator, ++NextCreatorSequence);
		}
	}
	if (Component->StableId.IsValid()) RegisteredActors.Add(Component->StableId, Actor);
}

void USWRoomSnapshotSubsystem::HandleActorDestroyed(AActor* Actor)
{
	if (bRestoring || !Actor || !GetWorld() || GetWorld()->bIsTearingDown) return;
	const USWRoomSnapshotComponent* Component = Actor->FindComponentByClass<USWRoomSnapshotComponent>();
	if (!Component || !Component->StableId.IsValid()) return;
	RegisteredActors.Remove(Component->StableId);
	if (IsLevelPlacedActor(Actor) && !UnloadedRecords.Contains(Component->StableId))
	{
		DestroyedLevelActorIds.Add(Component->StableId, Component->PersistenceClass);
		DestroyedActorPartitions.Add(Component->StableId, GetPartitionKey(Actor->GetLevel()));
	}
}

void USWRoomSnapshotSubsystem::HandlePreLevelRemoved(ULevel* Level, UWorld* World)
{
	if (!Level || World != GetWorld() || World->GetNetMode() == NM_Client || bRestoring || World->bIsTearingDown) return;
	const FString Package = GetPartitionKey(Level);
	FSWRoomWorldSnapshot Captured;
	FString Error;
	ESWRoomCaptureFailureKind FailureKind = ESWRoomCaptureFailureKind::None;
	if (!Capture(Captured, ESWRoomSaveKind::Manual, 1, Error, &FailureKind))
	{
		for (auto It = UnloadedRecords.CreateIterator(); It; ++It)
			if (GetPartitionKey(It.Value().LevelPartition) == Package) It.RemoveCurrent();
		CachedPartitions.Remove(Package);
		FailedPartitions.Add(Package, Error);
		bStructuralPartitionFailure |= FailureKind == ESWRoomCaptureFailureKind::Structural;
		UE_LOG(LogSWRoom, Error, TEXT("Flow=PartitionUnload Result=CaptureFailed Partition=%s Reason=%s"), *Package, *Error);
		return;
	}
	FailedPartitions.Remove(Package);
	for (auto It = UnloadedRecords.CreateIterator(); It; ++It)
		if (GetPartitionKey(It.Value().LevelPartition) == Package) It.RemoveCurrent();
	CachedPartitions.Add(Package);
	int32 Count = 0;
	for (FSWRoomActorRecord& Record : Captured.Actors)
		if (GetPartitionKey(Record.LevelPartition) == Package)
		{
			UnloadedRecords.Add(Record.StableId, MoveTemp(Record));
			++Count;
		}
	UE_LOG(LogSWRoom, Display, TEXT("Flow=PartitionUnload Result=Cached Partition=%s Actors=%d"), *Package, Count);
}

void USWRoomSnapshotSubsystem::HandleLevelBeginMakingVisible(UWorld* World, const ULevelStreaming* Streaming, ULevel* Level)
{
	if (World == GetWorld() && Level && CachedPartitions.Contains(GetPartitionKey(Level)))
		bRestoring = true;
}

void USWRoomSnapshotSubsystem::HandleLevelAdded(ULevel* Level, UWorld* World)
{
	if (!Level || World != GetWorld() || World->GetNetMode() == NM_Client) return;
	const FString CurrentKey = GetPartitionKey(Level);
	if (!CachedPartitions.Contains(CurrentKey))
	{
		const FString Prefix = Level->GetOutermost()->GetName() + TEXT("|");
		for (ULevel* Loaded : World->GetLevels())
			if (Loaded && Loaded != Level && Loaded->bIsVisible && CachedPartitions.Contains(GetPartitionKey(Loaded))
				&& GetPartitionKey(Loaded).StartsWith(Prefix))
			{
				bStructuralPartitionFailure = true;
				UE_LOG(LogSWRoom, Error, TEXT("Flow=PartitionReload Result=DuplicatePackageInstance Package=%s Existing=%s Current=%s"),
					*Level->GetOutermost()->GetName(), *GetPartitionKey(Loaded), *CurrentKey);
				return;
			}
		TArray<FString> StaleKeys;
		for (const FString& Cached : CachedPartitions)
			if (Cached.StartsWith(Prefix)) StaleKeys.Add(Cached);
		for (const FString& Stale : StaleKeys)
		{
			UE_LOG(LogSWRoom, Warning, TEXT("Flow=PartitionReload Result=InstanceNameMismatch Package=%s Previous=%s Current=%s"),
				*Level->GetOutermost()->GetName(), *Stale, *CurrentKey);
			FailedInstancePartitions.Add(Stale, TEXT("Current instance=") + CurrentKey);
			CachedPartitions.Remove(Stale);
			for (auto It = UnloadedRecords.CreateIterator(); It; ++It)
				if (GetPartitionKey(It.Value().LevelPartition) == Stale) It.RemoveCurrent();
		}
		return;
	}
	const FString Package = GetPartitionKey(Level);
	FSWRoomWorldSnapshot PartitionSnapshot;
	PartitionSnapshot.MapPath = GetMapPath(World);
	PartitionSnapshot.CaptureSequence = 1;
	for (const TPair<FGuid, FSWRoomActorRecord>& Pair : UnloadedRecords)
		if (GetPartitionKey(Pair.Value.LevelPartition) == Package)
			PartitionSnapshot.Actors.Add(Pair.Value);
	for (const TPair<FGuid, FString>& Pair : DestroyedActorPartitions)
		if (Pair.Value == Package) PartitionSnapshot.DestroyedLevelActorIds.Add(Pair.Key);
	FString Error;
	PartitionRestorePackage = Package;
	if (!Restore(PartitionSnapshot, Error, true) || !CompleteRestore(Error))
	{
		bRestoring = true;
		PartitionRestorePackage.Reset();
		UE_LOG(LogSWRoom, Error, TEXT("Flow=PartitionReload Result=Failed Partition=%s Reason=%s"), *Package, *Error);
		return;
	}
	PartitionRestorePackage.Reset();
	for (auto It = UnloadedRecords.CreateIterator(); It; ++It)
		if (GetPartitionKey(It.Value().LevelPartition) == Package) It.RemoveCurrent();
	CachedPartitions.Remove(Package);
	UE_LOG(LogSWRoom, Display, TEXT("Flow=PartitionReload Result=Restored Partition=%s Actors=%d"), *Package, PartitionSnapshot.Actors.Num());
}

bool USWRoomSnapshotSubsystem::Audit(FString& OutError)
{
	UnsupportedCandidates.Reset();
	AuditRows.Reset();
	RegistrationIssues.Reset();
	RegisteredActors.Reset();
	TSet<FGuid> Seen;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		if (IsExcluded(Actor)) continue;
		USWRoomSnapshotComponent* Component = Actor->FindComponentByClass<USWRoomSnapshotComponent>();
		if (!Component)
		{
			if (Actor->ActorHasTag(TEXT("RoomDerived")))
			{
				AuditRows.Add(Actor->GetPathName() + TEXT(" | ") + Actor->GetClass()->GetPathName()
					+ TEXT(" | Component= | Field=Actor | Reason=State owned by registered child spawn points | Disposition=Derived"));
				continue;
			}
			bool bCandidate = Actor->GetIsReplicated() || Actor->ActorHasTag(TEXT("RoomGameplay"));
			for (const UActorComponent* Candidate : Actor->GetComponents())
			{
				if (!Candidate) continue;
				if (Candidate->ComponentHasTag(TEXT("RoomGameplay"))) bCandidate = true;
				if (const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Candidate))
					if (Primitive->IsSimulatingPhysics()) bCandidate = true;
			}
			if (bCandidate)
			{
				const FString CandidateKey = Actor->GetPathName() + TEXT(" | ") + Actor->GetClass()->GetPathName();
				UnsupportedCandidates.Add(CandidateKey);
				AuditRows.Add(CandidateKey + TEXT(" | Component= | Field=Actor | Reason=Persistent gameplay candidate has no room contract | Disposition=Unclassified"));
				FSWRoomCaptureIssue& Issue = RegistrationIssues.AddDefaulted_GetRef();
				Issue.OwnerPath = Actor->GetPathName();
				Issue.ClassPath = FSoftClassPath(Actor->GetClass());
				Issue.Domain = TEXT("Audit");
				Issue.FieldKey = TEXT("Registration");
				Issue.Reason = TEXT("Unclassified gameplay actor has no room snapshot component");
			}
			continue;
		}
		if (Component->PersistenceClass == ESWRoomPersistenceClass::Transient)
		{
			AuditRows.Add(Actor->GetPathName() + TEXT(" | ") + Actor->GetClass()->GetPathName()
				+ TEXT(" | Component= | Field=Actor | Reason=Explicit transient room policy | Disposition=Transient"));
			continue;
		}
		if (IsLevelPlacedActor(Actor) && !Component->RefreshLevelInstanceId())
		{
			FSWRoomCaptureIssue& Issue = RegistrationIssues.AddDefaulted_GetRef();
			Issue.OwnerPath = Actor->GetPathName();
			Issue.ClassPath = FSoftClassPath(Actor->GetClass());
			Issue.Domain = TEXT("Identity");
			Issue.FieldKey = TEXT("StableId");
			Issue.Reason = TEXT("Level actor has no persisted SWRoomStableId tag");
			UE_LOG(LogSWRoom, Warning, TEXT("Flow=Audit Result=Partial Actor=%s Class=%s Field=StableId Reason=MissingPersistedTag"),
				*Issue.OwnerPath, *Issue.ClassPath.ToString());
			continue;
		}
		if (!Component->StableId.IsValid() || Seen.Contains(Component->StableId)
			|| (DestroyedLevelActorIds.Contains(Component->StableId) && !bRestoring))
		{
			OutError = FString::Printf(TEXT("Invalid or duplicate room actor ID: %s"), *Actor->GetPathName());
			return false;
		}
		Seen.Add(Component->StableId);
		RegisteredActors.Add(Component->StableId, Actor);
		for (TFieldIterator<FProperty> Field(Actor->GetClass()); Field; ++Field)
		{
			const FProperty* Property = *Field;
			if (!Property->HasAnyPropertyFlags(CPF_Net | CPF_SaveGame)) continue;
			const bool bSerialized = Property->HasAnyPropertyFlags(CPF_SaveGame);
			const FString FieldName = Property->GetName();
			const FString OwnerPackage = Property->GetOwnerStruct() ? Property->GetOwnerStruct()->GetOutermost()->GetName() : FString();
			const FString OwnerType = Property->GetOwnerStruct() ? Property->GetOwnerStruct()->GetName() : FString();
			const bool bEngineField = OwnerPackage.StartsWith(TEXT("/Script/Engine"));
			const bool bShipPersisted = OwnerType == TEXT("Ship")
				&& (FieldName == TEXT("bIsSinking") || FieldName == TEXT("bIsAnchorDropped") || FieldName == TEXT("AnchorOriginXY"));
			const bool bEnemyShipPersisted = OwnerType == TEXT("EnemyShip") && FieldName == TEXT("bCrewDefeated");
			bool bHasDeckPoolAdapter = false;
			for (const UClass* Type = Actor->GetClass(); Type; Type = Type->GetSuperClass())
				if (Type->GetFName() == TEXT("DeckEnemy")) { bHasDeckPoolAdapter = true; break; }
			const bool bDeckPoolPersisted = bHasDeckPoolAdapter && ((OwnerType == TEXT("DeckEnemy") && FieldName == TEXT("PoolNetState"))
				|| (OwnerType == TEXT("RangedEnemy") && FieldName == TEXT("HostShip")));
			const bool bTransient = OwnerType == TEXT("Ship")
				&& (FieldName == TEXT("RidingPlayer") || FieldName == TEXT("bBombardmentTargeting")
					|| FieldName == TEXT("ActiveBombardmentClass") || FieldName == TEXT("ReplicatedState")
					|| FieldName == TEXT("ServerPhysicsTimeOrigin") || FieldName == TEXT("ServerPhysicsStepSeconds")
					|| FieldName == TEXT("CurrentAIPropulsionScale") || FieldName == TEXT("CurrentAITurnScale"));
			const bool bDerived = bEngineField || (OwnerType == TEXT("EnemyShip")
				&& (FieldName == TEXT("bDistanceOptimizationDormant") || FieldName == TEXT("RuntimeState")));
			const bool bPersisted = bSerialized || bShipPersisted || bEnemyShipPersisted || bDeckPoolPersisted;
			const TCHAR* Disposition = bPersisted ? TEXT("Persisted") : bTransient ? TEXT("Transient")
				: bDerived ? TEXT("Derived") : TEXT("Unclassified");
			const TCHAR* Reason = bSerialized ? TEXT("SaveGame field") : bShipPersisted || bEnemyShipPersisted || bDeckPoolPersisted
				? TEXT("Explicit room adapter field") : bTransient ? TEXT("Explicit restart policy")
				: bDerived ? TEXT("Engine or derived field") : TEXT("Project replicated field lacks explicit room disposition");
			AuditRows.Add(FString::Printf(TEXT("%s | %s | Component= | Field=%s | Reason=%s | Disposition=%s"),
				*Component->StableId.ToString(), *Actor->GetClass()->GetPathName(), *Property->GetName(), Reason, Disposition));
			if (!bPersisted && !bTransient && !bDerived)
			{
				UnsupportedCandidates.Add(FString::Printf(TEXT("ID=%s Actor=%s Class=%s Component= Field=%s Reason=%s"),
					*Component->StableId.ToString(), *Actor->GetPathName(), *Actor->GetClass()->GetPathName(),
					*FieldName, Reason));
				FSWRoomCaptureIssue& Issue = RegistrationIssues.AddDefaulted_GetRef();
				Issue.StableId = Component->StableId;
				Issue.OwnerPath = Actor->GetPathName();
				Issue.ClassPath = FSoftClassPath(Actor->GetClass());
				Issue.Domain = TEXT("Audit");
				Issue.FieldKey = Property->GetFName();
				Issue.Reason = Reason;
			}
		}
	}
	if (!AuditRows.IsEmpty())
	{
		const FString AuditPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs"), TEXT("SWRoom"), TEXT("UnsupportedCandidates.txt"));
		FFileHelper::SaveStringToFile(FString::Join(AuditRows, TEXT("\n")), *AuditPath);
		UE_LOG(LogSWRoom, Warning, TEXT("Flow=Audit Result=Classified Candidates=%d Unclassified=%d FullList=%s"),
			AuditRows.Num(), UnsupportedCandidates.Num(), *AuditPath);
	}
	return true;
}

bool USWRoomSnapshotSubsystem::Capture(FSWRoomWorldSnapshot& OutSnapshot, ESWRoomSaveKind Kind,
	uint64 Sequence, FString& OutError, ESWRoomCaptureFailureKind* OutFailureKind)
{
	UE_LOG(LogSWRoomSave, Display, TEXT("Flow=WorldCapture Phase=Begin Sequence=%llu Kind=%s World=%s"),
		Sequence, *UEnum::GetValueAsString(Kind), *GetNameSafe(GetWorld()));
	if (OutFailureKind) *OutFailureKind = ESWRoomCaptureFailureKind::None;
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client || bRestoring)
	{
		OutError = TEXT("Room world capture unavailable");
		UE_LOG(LogSWRoomSave, Error, TEXT("Flow=WorldCapture Result=Failed Sequence=%llu Phase=Precondition Reason=%s"), Sequence, *OutError);
		if (OutFailureKind) *OutFailureKind = ESWRoomCaptureFailureKind::StateUnavailable;
		return false;
	}
	if (bStructuralPartitionFailure || !Audit(OutError))
	{
		if (bStructuralPartitionFailure) OutError = TEXT("Structural partition capture failure: ") + FString::JoinBy(FailedPartitions, TEXT(", "),
			[](const TPair<FString, FString>& Pair) { return Pair.Key + TEXT("=") + Pair.Value; });
		UE_LOG(LogSWRoomSave, Error, TEXT("Flow=WorldCapture Result=Failed Sequence=%llu Phase=Audit Reason=%s"), Sequence, *OutError);
		if (OutFailureKind) *OutFailureKind = ESWRoomCaptureFailureKind::Structural;
		return false;
	}
	OutSnapshot = FSWRoomWorldSnapshot();
	OutSnapshot.MapPath = GetMapPath(GetWorld());
	OutSnapshot.CaptureSequence = Sequence;
	OutSnapshot.CaptureIssues = RegistrationIssues;
	for (const TPair<FGuid, TWeakObjectPtr<AActor>>& Pair : RegisteredActors)
	{
		AActor* Actor = Pair.Value.Get();
		if (!Actor || Actor->IsActorBeingDestroyed())
		{
			OutError = TEXT("Registered actor disappeared during capture");
			UE_LOG(LogSWRoomSave, Error, TEXT("Flow=ActorCapture Result=Failed Sequence=%llu Id=%s Reason=%s"),
				Sequence, *Pair.Key.ToString(), *OutError);
			if (OutFailureKind) *OutFailureKind = ESWRoomCaptureFailureKind::StateUnavailable;
			return false;
		}
		USWRoomSnapshotComponent* Component = Actor->FindComponentByClass<USWRoomSnapshotComponent>();
		if (!Component)
		{
			UE_LOG(LogSWRoomSave, Warning, TEXT("Flow=ActorCapture Result=Skipped Sequence=%llu Id=%s Actor=%s Reason=MissingSnapshotComponent"),
				Sequence, *Pair.Key.ToString(), *Actor->GetPathName());
			continue;
		}
		const int32 IssueStart = OutSnapshot.CaptureIssues.Num();
		FSWRoomActorRecord& Record = OutSnapshot.Actors.AddDefaulted_GetRef();
		Record.StableId = Pair.Key;
		Record.ClassPath = FSoftClassPath(Actor->GetClass());
		if (!Record.StableId.IsValid() || Record.ClassPath.IsNull() || Actor->GetActorTransform().ContainsNaN())
		{
			FSWRoomCaptureIssue& Issue = OutSnapshot.CaptureIssues.AddDefaulted_GetRef();
			Issue.StableId = Record.StableId;
			Issue.OwnerPath = Actor->GetPathName();
			Issue.ClassPath = Record.ClassPath;
			Issue.Domain = TEXT("WorldActor");
			Issue.FieldKey = TEXT("IdentityOrTransform");
			Issue.Reason = TEXT("Actor ID, class, or transform unavailable during capture");
			UE_LOG(LogSWRoomSave, Warning, TEXT("Flow=ActorCapture Result=Skipped Sequence=%llu Id=%s Actor=%s Class=%s Reason=%s"),
				Sequence, *Record.StableId.ToString(), *Actor->GetPathName(), *Record.ClassPath.ToString(), *Issue.Reason);
			OutSnapshot.Actors.Pop();
			continue;
		}
		Record.LevelPartition.PackagePath = FSoftObjectPath(Actor->GetLevel()->GetOutermost()->GetName());
		Record.LevelPartition.InstanceName = FName(*Actor->GetLevel()->GetPathName());
		Record.LevelPartition.InstanceId = FGuid(0, 0, 0, 1);
		Record.Origin = IsLevelPlacedActor(Actor) ? ESWRoomSpawnOrigin::LevelPlaced : ESWRoomSpawnOrigin::Runtime;
		if (Record.Origin == ESWRoomSpawnOrigin::Runtime)
		{
			Record.CreatorId = Component->GetCreatorId();
			Record.CreatorSequence = Component->GetCreatorSequence();
		}
		Record.PersistenceClass = Component->PersistenceClass;
		Record.ContractVersion = Component->ContractVersion;
		Record.bRequired = Component->bRequired;
		Record.WorldTransform = Actor->GetActorTransform();
		if (AActor* Parent = Actor->GetAttachParentActor())
		{
			const USWRoomSnapshotComponent* ParentId = Parent->FindComponentByClass<USWRoomSnapshotComponent>();
			const USceneComponent* ParentComponent = Actor->GetRootComponent()
				? Actor->GetRootComponent()->GetAttachParent() : nullptr;
			const TWeakObjectPtr<AActor>* RegisteredParent = ParentId ? RegisteredActors.Find(ParentId->StableId) : nullptr;
			if (RegisteredParent && RegisteredParent->Get() == Parent && ParentComponent
				&& ParentComponent->GetOwner() == Parent)
			{
				Record.AttachParentId = ParentId->StableId;
				Record.AttachParentComponentName = ParentComponent->GetFName();
				Record.AttachSocketName = Actor->GetRootComponent()->GetAttachSocketName();
			}
			else
			{
				FSWRoomCaptureIssue& Issue = OutSnapshot.CaptureIssues.AddDefaulted_GetRef();
				Issue.StableId = Record.StableId;
				Issue.OwnerPath = Actor->GetPathName();
				Issue.ClassPath = Record.ClassPath;
				Issue.Domain = TEXT("Attachment");
				Issue.FieldKey = TEXT("Parent");
				Issue.Reason = FString::Printf(TEXT("Attached parent is not a registered room actor: %s"), *Parent->GetPathName());
			}
		}
		Record.MotionState = CaptureMotion(Actor, Cast<UPrimitiveComponent>(Actor->GetRootComponent()));
		TraceRoomPhysics(TEXT("Capture"), Actor, Record);
		if (Record.MotionState.bHasMotion && (Record.MotionState.LinearVelocity.ContainsNaN()
			|| Record.MotionState.AngularVelocityDegrees.ContainsNaN()))
		{
			Record.MotionState = FSWRoomMotionState();
			AddMotionIssue(OutSnapshot, Record, TEXT("RootVelocity"));
		}
		if (!SerializeValues(Actor, Record.SaveGameBytes))
		{
			Record.SaveGameBytes.Reset();
			FSWRoomCaptureIssue& Issue = OutSnapshot.CaptureIssues.AddDefaulted_GetRef();
			Issue.StableId = Record.StableId;
			Issue.OwnerPath = Actor->GetPathName();
			Issue.ClassPath = Record.ClassPath;
			Issue.Domain = TEXT("CaptureFailed");
			Issue.FieldKey = TEXT("Actor/SaveGame");
			Issue.Reason = TEXT("Actor SaveGame properties could not be serialized");
		}
		if (const ISWRoomStateAdapter* Adapter = Cast<ISWRoomStateAdapter>(Actor))
		{
			FSWRoomDomainPayload Payload;
			TArray<FSWRoomCaptureIssue> AdapterIssues;
			Adapter->CaptureRoomDomains(Payload.Parts, AdapterIssues);
			if (Payload.Parts.IsEmpty() && AdapterIssues.IsEmpty())
			{
				FSWRoomCaptureIssue& Issue = AdapterIssues.AddDefaulted_GetRef();
				Issue.Domain = TEXT("Adapter");
				Issue.FieldKey = TEXT("Payload");
				Issue.Reason = TEXT("Declared room state adapter returned no domain payload");
			}
			Payload.Parts.Sort([](const FSWRoomDomainPart& A, const FSWRoomDomainPart& B)
			{
			return static_cast<uint8>(A.Domain) < static_cast<uint8>(B.Domain);
			});
			for (int32 Index = Payload.Parts.Num() - 1; Index >= 0; --Index)
			{
				TArray<uint8> CheckBytes;
				if (!Payload.Parts[Index].Bytes.IsEmpty() && FSWRoomStructCodec::Write(Payload.Parts[Index], CheckBytes)) continue;
				FSWRoomCaptureIssue& Issue = AdapterIssues.AddDefaulted_GetRef();
				Issue.Domain = TEXT("CaptureFailed");
				Issue.FieldKey = FName(*FString::Printf(TEXT("Domain:%d"), static_cast<int32>(Payload.Parts[Index].Domain)));
				Issue.Reason = TEXT("Actor semantic domain could not be serialized");
				Payload.Parts.RemoveAt(Index);
			}
			for (FSWRoomCaptureIssue& Issue : AdapterIssues)
			{
				Issue.Scope = ESWRoomIssueScope::WorldActor;
				Issue.StableId = Record.StableId;
				Issue.ClassPath = Record.ClassPath;
				OutSnapshot.CaptureIssues.Add(MoveTemp(Issue));
			}
			if (!Payload.Parts.IsEmpty())
			{
				Record.AdapterType = TEXT("Domains");
				Record.AdapterVersion = 1;
				if (!FSWRoomStructCodec::Write(Payload, Record.AdapterBytes))
				{
					FSWRoomCaptureIssue& Issue = OutSnapshot.CaptureIssues.AddDefaulted_GetRef();
					Issue.StableId = Record.StableId;
					Issue.OwnerPath = Actor->GetPathName();
					Issue.ClassPath = Record.ClassPath;
					Issue.Domain = TEXT("CaptureFailed");
					Issue.FieldKey = TEXT("Adapter/Payload");
					Issue.Reason = TEXT("Actor domain payload serialization failed");
					Record.AdapterType = NAME_None;
					Record.AdapterVersion = 0;
				}
				else
					for (const FSWRoomDomainPart& Part : Payload.Parts)
						UE_LOG(LogSWRoomSave, Display,
							TEXT("Flow=DomainCapture Result=Success Sequence=%llu ActorId=%s Actor=%s Domain=%d Version=%d Bytes=%d"),
							Sequence, *Record.StableId.ToString(), *Actor->GetPathName(),
							static_cast<int32>(Part.Domain), Part.Version, Part.Bytes.Num());
			}
		}
		TSet<FName> Keys;
		for (UActorComponent* Child : Actor->GetComponents())
		{
			if (!Child || Child == Component) continue;
			const bool bIndependentPhysics = Cast<UPrimitiveComponent>(Child)
				&& Child != Actor->GetRootComponent()
				&& CastChecked<UPrimitiveComponent>(Child)->IsSimulatingPhysics()
				&& CastChecked<UPrimitiveComponent>(Child)->GetCollisionEnabled() != ECollisionEnabled::NoCollision;
			const FName Key = GetRoomComponentKey(Child);
			if (Key.IsNone() || Keys.Contains(Key))
			{
				if (bIndependentPhysics)
				{
					FSWRoomCaptureIssue& Issue = OutSnapshot.CaptureIssues.AddDefaulted_GetRef();
					Issue.StableId = Record.StableId;
					Issue.ClassPath = Record.ClassPath;
					Issue.Domain = TEXT("Motion");
					Issue.FieldKey = Child->GetFName();
					Issue.Reason = Key.IsNone() ? TEXT("Dynamic physics component lacks SWRoomComponentId tag")
						: TEXT("Duplicate physics component stable key");
				}
				continue;
			}
			Keys.Add(Key);
			FSWRoomComponentRecord& ChildRecord = Record.Components.AddDefaulted_GetRef();
			ChildRecord.StableKey = Key;
			if (!SerializeValues(Child, ChildRecord.SaveGameBytes))
			{
				ChildRecord.SaveGameBytes.Reset();
				FSWRoomCaptureIssue& Issue = OutSnapshot.CaptureIssues.AddDefaulted_GetRef();
				Issue.StableId = Record.StableId;
				Issue.OwnerPath = Actor->GetPathName();
				Issue.ClassPath = Record.ClassPath;
				Issue.Domain = TEXT("CaptureFailed");
				Issue.FieldKey = FName(*(TEXT("Component/") + Key.ToString()));
				Issue.Reason = TEXT("Component SaveGame properties could not be serialized");
			}
			if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Child);
				Primitive && Primitive != Actor->GetRootComponent() && Primitive->IsSimulatingPhysics()
				&& Primitive->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
			{
				ChildRecord.WorldTransform = Primitive->GetComponentTransform();
				ChildRecord.MotionState = CaptureMotion(nullptr, Primitive);
				if (ChildRecord.MotionState.LinearVelocity.ContainsNaN()
					|| ChildRecord.MotionState.AngularVelocityDegrees.ContainsNaN())
				{
					ChildRecord.MotionState = FSWRoomMotionState();
					AddMotionIssue(OutSnapshot, Record, Key);
				}
			}
			UE_LOG(LogSWRoomSave, Display,
				TEXT("Flow=ComponentCapture Result=Recorded Sequence=%llu ActorId=%s Actor=%s Key=%s Component=%s Class=%s SaveGameBytes=%d Motion=%d Sim=%d"),
				Sequence, *Record.StableId.ToString(), *Actor->GetPathName(), *Key.ToString(),
				*Child->GetPathName(), *Child->GetClass()->GetPathName(), ChildRecord.SaveGameBytes.Num(),
				ChildRecord.MotionState.bHasMotion ? 1 : 0, ChildRecord.MotionState.bWasSimulatingPhysics ? 1 : 0);
		}
		UE_LOG(LogSWRoomSave, Display,
			TEXT("Flow=ActorCapture Result=%s Sequence=%llu Id=%s Actor=%s Class=%s Origin=%s Partition=%s Required=%d SaveGameBytes=%d AdapterBytes=%d Components=%d ParentId=%s ParentComponent=%s Socket=%s RootMotion=%d RootSim=%d Issues=%d"),
			OutSnapshot.CaptureIssues.Num() == IssueStart ? TEXT("Success") : TEXT("Partial"), Sequence,
			*Record.StableId.ToString(), *Actor->GetPathName(), *Record.ClassPath.ToString(),
			*UEnum::GetValueAsString(Record.Origin), *Record.LevelPartition.PackagePath.ToString(),
			Record.bRequired ? 1 : 0, Record.SaveGameBytes.Num(), Record.AdapterBytes.Num(), Record.Components.Num(),
			*Record.AttachParentId.ToString(), *Record.AttachParentComponentName.ToString(), *Record.AttachSocketName.ToString(),
			Record.MotionState.bHasMotion ? 1 : 0, Record.MotionState.bWasSimulatingPhysics ? 1 : 0,
			OutSnapshot.CaptureIssues.Num() - IssueStart);
	}
	for (const TPair<FGuid, ESWRoomPersistenceClass>& Pair : DestroyedLevelActorIds)
	{
		OutSnapshot.DestroyedLevelActorIds.Add(Pair.Key);
		UE_LOG(LogSWRoomSave, Display, TEXT("Flow=TombstoneCapture Result=Recorded Sequence=%llu Id=%s Persistence=%s"),
			Sequence, *Pair.Key.ToString(), *UEnum::GetValueAsString(Pair.Value));
		if (const FString* Package = DestroyedActorPartitions.Find(Pair.Key))
		{
			FSWRoomDestroyedActorPartition& Partition = OutSnapshot.DestroyedActorPartitions.AddDefaulted_GetRef();
			Partition.StableId = Pair.Key;
			FString PackagePath, InstanceName;
			if (Package->Split(TEXT("|"), &PackagePath, &InstanceName))
			{
				Partition.PackagePath = FSoftObjectPath(PackagePath);
				Partition.InstanceName = FName(*InstanceName);
			}
		}
	}
	for (const TPair<FGuid, FSWRoomActorRecord>& Pair : UnloadedRecords)
		if (!RegisteredActors.Contains(Pair.Key))
		{
			OutSnapshot.UnloadedActors.Add(Pair.Value);
			UE_LOG(LogSWRoomSave, Display,
				TEXT("Flow=UnloadedActorCapture Result=Reused Sequence=%llu Id=%s Class=%s Partition=%s SaveGameBytes=%d AdapterBytes=%d Components=%d ParentId=%s"),
				Sequence, *Pair.Key.ToString(), *Pair.Value.ClassPath.ToString(),
				*Pair.Value.LevelPartition.PackagePath.ToString(), Pair.Value.SaveGameBytes.Num(),
				Pair.Value.AdapterBytes.Num(), Pair.Value.Components.Num(), *Pair.Value.AttachParentId.ToString());
		}
	TSet<FGuid> CapturedActorIds;
	for (const FSWRoomActorRecord& Record : OutSnapshot.Actors) CapturedActorIds.Add(Record.StableId);
	for (const FSWRoomActorRecord& Record : OutSnapshot.UnloadedActors) CapturedActorIds.Add(Record.StableId);
	auto RemoveMissingAttachment = [&](FSWRoomActorRecord& Record)
	{
		if (!Record.AttachParentId.IsValid() || CapturedActorIds.Contains(Record.AttachParentId)) return;
		FSWRoomCaptureIssue& Issue = OutSnapshot.CaptureIssues.AddDefaulted_GetRef();
		Issue.StableId = Record.StableId;
		Issue.ClassPath = Record.ClassPath;
		Issue.OwnerPath = Record.ClassPath.ToString();
		Issue.Domain = TEXT("Attachment");
		Issue.FieldKey = TEXT("Parent");
		Issue.Reason = FString::Printf(TEXT("Attached parent is absent from the room snapshot: %s"),
			*Record.AttachParentId.ToString());
		Record.AttachParentId.Invalidate();
		Record.AttachParentComponentName = NAME_None;
		Record.AttachSocketName = NAME_None;
	};
	for (FSWRoomActorRecord& Record : OutSnapshot.Actors) RemoveMissingAttachment(Record);
	for (FSWRoomActorRecord& Record : OutSnapshot.UnloadedActors) RemoveMissingAttachment(Record);
	for (const FString& Package : CachedPartitions)
	{
		FSWRoomSystemRecord& System = OutSnapshot.Systems.AddDefaulted_GetRef();
		System.StableKey = FName(*(TEXT("Partition:") + Package));
		System.ContractVersion = 1;
	}
	for (const TPair<FString, FString>& Failed : FailedPartitions)
	{
		FSWRoomCaptureIssue& Issue = OutSnapshot.CaptureIssues.AddDefaulted_GetRef();
		Issue.OwnerPath = Failed.Key;
		Issue.Domain = TEXT("Partition");
		Issue.FieldKey = TEXT("State");
		Issue.Reason = Failed.Value;
	}
	for (const TPair<FString, FString>& Failed : FailedInstancePartitions)
	{
		FSWRoomCaptureIssue& Issue = OutSnapshot.CaptureIssues.AddDefaulted_GetRef();
		Issue.OwnerPath = Failed.Key;
		Issue.Domain = TEXT("Partition");
		Issue.FieldKey = TEXT("InstanceName");
		Issue.Reason = Failed.Value;
	}
	TSet<FString> IssueKeys;
	for (int32 Index = OutSnapshot.CaptureIssues.Num() - 1; Index >= 0; --Index)
	{
		const FSWRoomCaptureIssue& Issue = OutSnapshot.CaptureIssues[Index];
		const FString Owner = Issue.StableId.IsValid() ? Issue.StableId.ToString()
			: !Issue.PlayerKey.IsEmpty() ? Issue.PlayerKey : Issue.OwnerPath;
		const FString Key = FString::FromInt(static_cast<int32>(Issue.Scope)) + TEXT("|") + Owner
			+ TEXT("|") + Issue.Domain.ToString() + TEXT("|") + Issue.FieldKey.ToString();
		if (IssueKeys.Contains(Key)) OutSnapshot.CaptureIssues.RemoveAt(Index);
		else IssueKeys.Add(Key);
	}
	for (const FSWRoomCaptureIssue& Issue : OutSnapshot.CaptureIssues)
		UE_LOG(LogSWRoomSave, Warning,
			TEXT("Flow=CaptureIssue Sequence=%llu Scope=%d Id=%s Player=%s Actor=%s Class=%s Domain=%s Field=%s Reason=%s"),
			Sequence, static_cast<int32>(Issue.Scope), *Issue.StableId.ToString(), *Issue.PlayerKey,
			*Issue.OwnerPath, *Issue.ClassPath.ToString(), *Issue.Domain.ToString(), *Issue.FieldKey.ToString(), *Issue.Reason);
	UE_LOG(LogSWRoomSave, Display,
		TEXT("Flow=WorldCapture Result=%s Sequence=%llu Actors=%d Unloaded=%d Tombstones=%d Systems=%d Issues=%d Unsupported=%d"),
		OutSnapshot.CaptureIssues.IsEmpty() ? TEXT("Success") : TEXT("Partial"), Sequence,
		OutSnapshot.Actors.Num(), OutSnapshot.UnloadedActors.Num(), OutSnapshot.DestroyedLevelActorIds.Num(),
		OutSnapshot.Systems.Num(), OutSnapshot.CaptureIssues.Num(), UnsupportedCandidates.Num());
	return true;
}

bool USWRoomSnapshotSubsystem::Restore(const FSWRoomWorldSnapshot& Snapshot, FString& OutError, bool bPartitionRestore)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client || Snapshot.MapPath != GetMapPath(GetWorld()) || !Audit(OutError))
	{
		if (OutError.IsEmpty()) OutError = TEXT("Room map mismatch");
		return false;
	}
	// Keep the barrier closed after Deserialize; only CompleteRestore may reopen it.
	bRestoring = true;
	TSet<FGuid> DiskIds;
	auto ValidateDiskRecord = [&](const FSWRoomActorRecord& Record) -> bool
	{
		if (Record.AttachParentId == Record.StableId
			|| Record.AttachParentId.IsValid() == Record.AttachParentComponentName.IsNone())
		{
			OutError = FString::Printf(TEXT("Invalid stored actor attachment: ID=%s ParentId=%s Component=%s"),
				*Record.StableId.ToString(), *Record.AttachParentId.ToString(), *Record.AttachParentComponentName.ToString());
			return false;
		}
		if (!Record.StableId.IsValid() || DiskIds.Contains(Record.StableId))
		{
			OutError = FString::Printf(TEXT("Duplicate or invalid stored actor ID: ID=%s Class=%s Partition=%s"),
				*Record.StableId.ToString(), *Record.ClassPath.ToString(), *GetPartitionKey(Record.LevelPartition));
			return false;
		}
		DiskIds.Add(Record.StableId);
		UClass* SavedClass = Record.ClassPath.TryLoadClass<AActor>();
		if (!SavedClass)
		{
			OutError = FString::Printf(TEXT("Stored actor class missing: ID=%s Class=%s Partition=%s"),
				*Record.StableId.ToString(), *Record.ClassPath.ToString(), *GetPartitionKey(Record.LevelPartition));
			return false;
		}
		if (!Record.AdapterBytes.IsEmpty() && !SavedClass->ImplementsInterface(USWRoomStateAdapter::StaticClass()))
		{
			OutError = FString::Printf(TEXT("Stored actor adapter unsupported: ID=%s Class=%s"),
				*Record.StableId.ToString(), *Record.ClassPath.ToString());
			return false;
		}
		return true;
	};
	for (const FSWRoomActorRecord& Record : Snapshot.Actors) if (!ValidateDiskRecord(Record)) return false;
	for (const FSWRoomActorRecord& Record : Snapshot.UnloadedActors) if (!ValidateDiskRecord(Record)) return false;
	for (const FGuid& Id : Snapshot.DestroyedLevelActorIds)
	{
		if (!Id.IsValid() || DiskIds.Contains(Id))
		{
			OutError = FString::Printf(TEXT("Duplicate or invalid stored tombstone ID: ID=%s"), *Id.ToString());
			return false;
		}
		DiskIds.Add(Id);
	}
	if (!bPartitionRestore)
	{
		PromotedUnloadedIds.Reset();
		RestoreIssues.Reset();
	}
	FSWRoomWorldSnapshot Applied = Snapshot;
	if (!bPartitionRestore)
	{
		for (int32 Index = Applied.UnloadedActors.Num() - 1; Index >= 0; --Index)
		{
			const FSWRoomActorRecord Record = Applied.UnloadedActors[Index];
			bool bPackageLoadedUnderDifferentName = false;
			for (ULevel* Level : GetWorld()->GetLevels())
				if (Level && Level->bIsVisible)
				{
					if (Level->GetOutermost()->GetName() == Record.LevelPartition.PackagePath.ToString()
						&& GetPartitionKey(Level) != GetPartitionKey(Record.LevelPartition))
						bPackageLoadedUnderDifferentName = true;
					if (GetPartitionKey(Level) != GetPartitionKey(Record.LevelPartition)) continue;
					PromotedUnloadedIds.Add(Record.StableId);
					Applied.Actors.Add(Record);
					Applied.UnloadedActors.RemoveAt(Index);
					break;
				}
			if (bPackageLoadedUnderDifferentName && !PromotedUnloadedIds.Contains(Record.StableId))
			{
				FSWRoomCaptureIssue& Issue = RestoreIssues.AddDefaulted_GetRef();
				Issue.StableId = Record.StableId;
				Issue.ClassPath = Record.ClassPath;
				Issue.OwnerPath = GetPartitionKey(Record.LevelPartition);
				Issue.Domain = TEXT("Partition");
				Issue.FieldKey = TEXT("InstanceName");
				Issue.Reason = TEXT("Saved partition instance name differs from loaded instance");
				Applied.UnloadedActors.RemoveAt(Index);
			}
		}
		for (const FSWRoomDestroyedActorPartition& Partition : Snapshot.DestroyedActorPartitions)
		{
			bool bExactLoaded = false, bOtherInstanceLoaded = false;
			for (ULevel* Level : GetWorld()->GetLevels())
				if (Level && Level->bIsVisible && Level->GetOutermost()->GetName() == Partition.PackagePath.ToString())
				{
					bExactLoaded |= GetPartitionKey(Level) == GetPartitionKey(Partition);
					bOtherInstanceLoaded |= GetPartitionKey(Level) != GetPartitionKey(Partition);
				}
			if (bOtherInstanceLoaded && !bExactLoaded)
			{
				Applied.DestroyedLevelActorIds.Remove(Partition.StableId);
				FSWRoomCaptureIssue& Issue = RestoreIssues.AddDefaulted_GetRef();
				Issue.StableId = Partition.StableId;
				Issue.OwnerPath = GetPartitionKey(Partition);
				Issue.Domain = TEXT("Partition");
				Issue.FieldKey = TEXT("InstanceName");
				Issue.Reason = TEXT("Tombstone partition instance name differs from loaded instance");
			}
		}
	}
	for (const FSWRoomCaptureIssue& Issue : Snapshot.CaptureIssues)
		UE_LOG(LogSWRoom, Warning, TEXT("Flow=WorldRestore Result=Partial Id=%s Actor=%s Class=%s Domain=%s Field=%s Reason=%s"),
			*Issue.StableId.ToString(), *Issue.OwnerPath, *Issue.ClassPath.ToString(), *Issue.Domain.ToString(),
			*Issue.FieldKey.ToString(), *Issue.Reason);
	TSet<FGuid> SavedIds;
	for (const FSWRoomActorRecord& Record : Applied.Actors) SavedIds.Add(Record.StableId);
	for (const TPair<FGuid, TWeakObjectPtr<AActor>>& Pair : RegisteredActors)
		if (AActor* Actor = Pair.Value.Get(); Actor && !IsLevelPlacedActor(Actor) && !SavedIds.Contains(Pair.Key)
			&& (!bPartitionRestore || GetPartitionKey(Actor->GetLevel()) == PartitionRestorePackage)) Actor->Destroy();
	TMap<FGuid, ESWRoomPersistenceClass> RestoredDestroyedClasses;
	for (const FGuid& Id : Applied.DestroyedLevelActorIds)
	{
		if (TWeakObjectPtr<AActor>* Found = RegisteredActors.Find(Id))
			if (AActor* Actor = Found->Get())
			{
				if (const USWRoomSnapshotComponent* Component = Actor->FindComponentByClass<USWRoomSnapshotComponent>())
					RestoredDestroyedClasses.Add(Id, Component->PersistenceClass);
				Actor->Destroy();
			}
	}
	TMap<FGuid, AActor*> RestoredActorsById;
	for (const FSWRoomActorRecord& Record : Applied.Actors)
	{
		AActor* Actor = nullptr;
		if (TWeakObjectPtr<AActor>* Found = RegisteredActors.Find(Record.StableId)) Actor = Found->Get();
		if (!Actor && Record.Origin == ESWRoomSpawnOrigin::Runtime)
		{
			UClass* Class = Record.ClassPath.TryLoadClass<AActor>();
			if (!Class) { OutError = TEXT("Room actor class missing"); return false; }
			FActorSpawnParameters SpawnInfo;
			SpawnInfo.bDeferConstruction = true;
			SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			for (ULevel* CandidateLevel : GetWorld()->GetLevels())
				if (CandidateLevel && GetPartitionKey(CandidateLevel) == GetPartitionKey(Record.LevelPartition))
				{
					SpawnInfo.OverrideLevel = CandidateLevel;
					break;
				}
			Actor = GetWorld()->SpawnActor<AActor>(Class, Record.WorldTransform, SpawnInfo);
			if (Actor)
			{
				USWRoomSnapshotComponent* Component = Actor->FindComponentByClass<USWRoomSnapshotComponent>();
				if (Component) Component->SetRuntimeId(Record.StableId);
				if (Component) Component->SetRuntimeOrigin(Record.CreatorId, Record.CreatorSequence);
				NextCreatorSequence = FMath::Max(NextCreatorSequence, Record.CreatorSequence);
				Actor->FinishSpawning(Record.WorldTransform);
			}
		}
		if (!Actor)
		{
			if (!Record.bRequired)
			{
				FSWRoomCaptureIssue& Issue = RestoreIssues.AddDefaulted_GetRef();
				Issue.StableId = Record.StableId;
				Issue.ClassPath = Record.ClassPath;
				Issue.OwnerPath = GetPartitionKey(Record.LevelPartition);
				Issue.Domain = TEXT("WorldActor");
				Issue.FieldKey = TEXT("ID/Existence");
				Issue.Reason = TEXT("Optional stored actor missing in current world");
				UE_LOG(LogSWRoom, Warning, TEXT("Flow=WorldRestore OptionalActorMissing Id=%s Class=%s"),
					*Record.StableId.ToString(), *Record.ClassPath.ToString());
				continue;
			}
			OutError = FString::Printf(TEXT("Required room actor missing: ID=%s Class=%s Origin=%s"),
				*Record.StableId.ToString(), *Record.ClassPath.ToString(), *UEnum::GetValueAsString(Record.Origin));
			return false;
		}
		USWRoomSnapshotComponent* Component = Actor->FindComponentByClass<USWRoomSnapshotComponent>();
		if (!Component || Component->StableId != Record.StableId || Component->ContractVersion != Record.ContractVersion
			|| FSoftClassPath(Actor->GetClass()) != Record.ClassPath)
		{ OutError = TEXT("Room actor contract mismatch"); return false; }
		RegisteredActors.Add(Record.StableId, Actor);
		RestoredActorsById.Add(Record.StableId, Actor);
		DeserializeValues(Actor, Record.SaveGameBytes);
		if (!Record.AdapterBytes.IsEmpty())
		{
			FSWRoomDomainPayload Payload;
			if (!FSWRoomStructCodec::Read(Record.AdapterBytes, Payload))
			{
				OutError = FString::Printf(TEXT("Actor adapter payload invalid: ID=%s Class=%s"),
					*Record.StableId.ToString(), *Record.ClassPath.ToString());
				return false;
			}
			ISWRoomStateAdapter* Adapter = Cast<ISWRoomStateAdapter>(Actor);
			for (const FSWRoomDomainPart& Part : Payload.Parts)
				if (!Adapter || !Adapter->RestoreRoomDomain(Part, OutError))
				{
					OutError = FString::Printf(TEXT("Actor domain restore failed: ID=%s Class=%s Domain=%d Reason=%s"),
						*Record.StableId.ToString(), *Record.ClassPath.ToString(), static_cast<int32>(Part.Domain), *OutError);
					return false;
				}
		}
		for (const FSWRoomComponentRecord& ChildRecord : Record.Components)
		{
			UActorComponent* Child = nullptr;
			for (UActorComponent* Candidate : Actor->GetComponents()) if (GetRoomComponentKey(Candidate) == ChildRecord.StableKey) { Child = Candidate; break; }
			if (!Child) { OutError = TEXT("Room component missing"); return false; }
			DeserializeValues(Child, ChildRecord.SaveGameBytes);
		}
		Actor->SetActorScale3D(Record.WorldTransform.GetScale3D());
		if (!Actor->TeleportTo(Record.WorldTransform.GetLocation(), Record.WorldTransform.Rotator(), false, false))
		{
			UE_LOG(LogSWRoom, Warning, TEXT("Flow=WorldRestore ForcedTransform Id=%s Class=%s"), *Record.StableId.ToString(), *Record.ClassPath.ToString());
			Actor->SetActorTransform(Record.WorldTransform, false, nullptr, ETeleportType::TeleportPhysics);
		}
		RestoreMotion(Actor, Cast<UPrimitiveComponent>(Actor->GetRootComponent()), Record.MotionState);
		TraceRoomPhysics(TEXT("Restored"), Actor, Record);
		for (const FSWRoomComponentRecord& ChildRecord : Record.Components)
		{
			if (!ChildRecord.MotionState.bHasMotion) continue;
			for (UActorComponent* Candidate : Actor->GetComponents())
			{
				if (GetRoomComponentKey(Candidate) == ChildRecord.StableKey)
				{
					if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Candidate))
					{
						Primitive->SetWorldTransform(ChildRecord.WorldTransform, false, nullptr, ETeleportType::TeleportPhysics);
						RestoreMotion(nullptr, Primitive, ChildRecord.MotionState);
					}
					break;
				}
			}
		}
	}
	for (const FSWRoomActorRecord& Record : Applied.Actors)
	{
		AActor* const* ChildEntry = RestoredActorsById.Find(Record.StableId);
		if (!ChildEntry) continue;
		AActor* Child = *ChildEntry;
		if (!Record.AttachParentId.IsValid())
		{
			if (Child->GetAttachParentActor()) Child->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			continue;
		}
		AActor* Parent = nullptr;
		if (AActor* const* RestoredParent = RestoredActorsById.Find(Record.AttachParentId)) Parent = *RestoredParent;
		else if (const TWeakObjectPtr<AActor>* RegisteredParent = RegisteredActors.Find(Record.AttachParentId))
			Parent = RegisteredParent->Get();
		USceneComponent* ParentComponent = nullptr;
		if (Parent)
			for (UActorComponent* Candidate : Parent->GetComponents())
				if (Candidate && Candidate->GetFName() == Record.AttachParentComponentName)
				{
					ParentComponent = Cast<USceneComponent>(Candidate);
					break;
				}
		if (!ParentComponent || !Child->GetRootComponent())
		{
			OutError = FString::Printf(TEXT("Room attachment parent/component missing: Child=%s ParentId=%s Component=%s"),
				*Record.StableId.ToString(), *Record.AttachParentId.ToString(), *Record.AttachParentComponentName.ToString());
			return false;
		}
		if (Child->GetRootComponent()->GetAttachParent() != ParentComponent
			|| Child->GetRootComponent()->GetAttachSocketName() != Record.AttachSocketName)
			if (!Child->AttachToComponent(ParentComponent, FAttachmentTransformRules::KeepWorldTransform, Record.AttachSocketName))
			{
				OutError = FString::Printf(TEXT("Room actor attachment failed: Child=%s ParentId=%s Component=%s"),
					*Record.StableId.ToString(), *Record.AttachParentId.ToString(), *Record.AttachParentComponentName.ToString());
				return false;
			}
		UE_LOG(LogSWRoom, Display, TEXT("Flow=RoomAttachment Result=Restored Child=%s Parent=%s Component=%s Socket=%s"),
			*Record.StableId.ToString(), *Record.AttachParentId.ToString(), *Record.AttachParentComponentName.ToString(),
			*Record.AttachSocketName.ToString());
	}
	if (!bPartitionRestore) DestroyedLevelActorIds.Reset();
	for (const FGuid& Id : Applied.DestroyedLevelActorIds)
		DestroyedLevelActorIds.Add(Id, RestoredDestroyedClasses.Contains(Id)
			? RestoredDestroyedClasses.FindRef(Id) : ESWRoomPersistenceClass::ManualOnly);
	if (!bPartitionRestore)
	{
		DestroyedActorPartitions.Reset();
		for (const FSWRoomDestroyedActorPartition& Partition : Snapshot.DestroyedActorPartitions)
			DestroyedActorPartitions.Add(Partition.StableId, GetPartitionKey(Partition));
		UnloadedRecords.Reset();
		CachedPartitions.Reset();
		for (const FSWRoomActorRecord& Record : Applied.UnloadedActors)
		{
			UnloadedRecords.Add(Record.StableId, Record);
			CachedPartitions.Add(GetPartitionKey(Record.LevelPartition));
		}
		for (const FSWRoomSystemRecord& System : Snapshot.Systems)
		{
			const FString Key = System.StableKey.ToString();
			if (!Key.StartsWith(TEXT("Partition:"))) continue;
			const FString PartitionKey = Key.RightChop(10);
			bool bAlreadyLoaded = false;
			for (ULevel* Level : GetWorld()->GetLevels())
				if (Level && Level->bIsVisible && GetPartitionKey(Level) == PartitionKey)
					bAlreadyLoaded = true;
			if (!bAlreadyLoaded) CachedPartitions.Add(PartitionKey);
		}
	}
	return true;
}

bool USWRoomSnapshotSubsystem::CompareDeclared(const FSWRoomWorldSnapshot& Expected, const FSWRoomWorldSnapshot& Actual, TArray<FString>& OutDifferences)
{
	OutDifferences.Reset();
	if (Expected.MapPath != Actual.MapPath) OutDifferences.Add(TEXT("MapPath"));
	TMap<FGuid, const FSWRoomActorRecord*> ActualActors;
	for (const FSWRoomActorRecord& Record : Actual.Actors)
	{
		if (ActualActors.Contains(Record.StableId)) OutDifferences.Add(TEXT("Duplicate actor ID: ") + Record.StableId.ToString());
		ActualActors.Add(Record.StableId, &Record);
	}
	for (const FSWRoomActorRecord& Before : Expected.Actors)
	{
		const FSWRoomActorRecord* const* Found = ActualActors.Find(Before.StableId);
		if (!Found) { OutDifferences.Add(TEXT("Missing actor: ") + Before.StableId.ToString()); continue; }
		const FSWRoomActorRecord& After = **Found;
		const FString Prefix = Before.StableId.ToString() + TEXT(": ");
		if (Before.ClassPath != After.ClassPath || Before.ContractVersion != After.ContractVersion
			|| Before.Origin != After.Origin || Before.PersistenceClass != After.PersistenceClass
			|| Before.CreatorId != After.CreatorId || Before.CreatorSequence != After.CreatorSequence
			|| Before.LevelPartition.PackagePath != After.LevelPartition.PackagePath
			|| Before.LevelPartition.InstanceName != After.LevelPartition.InstanceName
			|| Before.LevelPartition.InstanceId != After.LevelPartition.InstanceId)
			OutDifferences.Add(Prefix + TEXT("identity/contract"));
		if (!Before.WorldTransform.Equals(After.WorldTransform, 0.01f)) OutDifferences.Add(Prefix + TEXT("Transform"));
		if (Before.AttachParentId != After.AttachParentId
			|| Before.AttachParentComponentName != After.AttachParentComponentName
			|| Before.AttachSocketName != After.AttachSocketName)
			OutDifferences.Add(Prefix + TEXT("attachment"));
		if (Before.MotionState.bHasMotion != After.MotionState.bHasMotion
			|| Before.MotionState.bWasSimulatingPhysics != After.MotionState.bWasSimulatingPhysics
			|| Before.MotionState.bWasProjectileMovementActive != After.MotionState.bWasProjectileMovementActive
			|| !Before.MotionState.LinearVelocity.Equals(After.MotionState.LinearVelocity, 1.f)
			|| !Before.MotionState.AngularVelocityDegrees.Equals(After.MotionState.AngularVelocityDegrees, 1.f))
			OutDifferences.Add(Prefix + TEXT("motion"));
		if (Before.SaveGameBytes != After.SaveGameBytes) OutDifferences.Add(Prefix + TEXT("Actor SaveGame"));
		if (Before.AdapterVersion != After.AdapterVersion || Before.AdapterType != After.AdapterType
			|| Before.AdapterBytes != After.AdapterBytes || Before.ReferenceIds != After.ReferenceIds)
			OutDifferences.Add(Prefix + TEXT("adapter/references"));
		TMap<FName, const FSWRoomComponentRecord*> Components;
		for (const FSWRoomComponentRecord& Child : After.Components) Components.Add(Child.StableKey, &Child);
		for (const FSWRoomComponentRecord& Child : Before.Components)
		{
			const FSWRoomComponentRecord* const* Other = Components.Find(Child.StableKey);
			if (!Other || Child.SaveGameBytes != (*Other)->SaveGameBytes
				|| Child.MotionState.bHasMotion != (*Other)->MotionState.bHasMotion
				|| !Child.MotionState.LinearVelocity.Equals((*Other)->MotionState.LinearVelocity, 1.f)
				|| !Child.MotionState.AngularVelocityDegrees.Equals((*Other)->MotionState.AngularVelocityDegrees, 1.f))
				OutDifferences.Add(Prefix + TEXT("component ") + Child.StableKey.ToString());
		}
		if (Before.Components.Num() != After.Components.Num()) OutDifferences.Add(Prefix + TEXT("component count"));
	}
	if (Expected.Actors.Num() != Actual.Actors.Num()) OutDifferences.Add(TEXT("Actor count"));
	TSet<FGuid> ExpectedDestroyed;
	TSet<FGuid> ActualDestroyed;
	for (const FGuid& Id : Expected.DestroyedLevelActorIds) ExpectedDestroyed.Add(Id);
	for (const FGuid& Id : Actual.DestroyedLevelActorIds) ActualDestroyed.Add(Id);
	if (ExpectedDestroyed.Num() != ActualDestroyed.Num()) OutDifferences.Add(TEXT("Destroyed actor ID count"));
	for (const FGuid& Id : ExpectedDestroyed)
		if (!ActualDestroyed.Contains(Id)) OutDifferences.Add(TEXT("Missing destroyed actor ID: ") + Id.ToString());
	if (Expected.DestroyedActorPartitions.Num() != Actual.DestroyedActorPartitions.Num())
		OutDifferences.Add(TEXT("Destroyed actor partition count"));
	if (Expected.UnloadedActors.Num() != Actual.UnloadedActors.Num()) OutDifferences.Add(TEXT("Unloaded actor count"));
	if (Expected.Systems.Num() != Actual.Systems.Num()) OutDifferences.Add(TEXT("System count"));
	if (Expected.ReferenceIds != Actual.ReferenceIds) OutDifferences.Add(TEXT("World references"));
	return OutDifferences.IsEmpty();
}

bool USWRoomSnapshotSubsystem::CompareRestored(const FSWRoomWorldSnapshot& Expected,
	const FSWRoomWorldSnapshot& Actual, TArray<FString>& OutDifferences) const
{
	OutDifferences.Reset();
	if (Expected.MapPath != Actual.MapPath) OutDifferences.Add(TEXT("MapPath"));
	TMap<FGuid, const FSWRoomActorRecord*> ActualActors;
	auto HasRestoreIssue = [&](const FGuid& Id, FName Domain, FName Field)
	{
		for (const FSWRoomCaptureIssue& Issue : RestoreIssues)
			if (Issue.StableId == Id && Issue.Domain == Domain && Issue.FieldKey == Field) return true;
		return false;
	};
	for (const FSWRoomActorRecord& Record : Actual.Actors)
	{
		if (ActualActors.Contains(Record.StableId)) OutDifferences.Add(TEXT("Duplicate observed actor ID: ") + Record.StableId.ToString());
		ActualActors.Add(Record.StableId, &Record);
	}
	TSet<FGuid> ExpectedIds;
	auto CompareActor = [&](const FSWRoomActorRecord& Before, const FSWRoomActorRecord* After)
	{
		const FString Prefix = FString::Printf(TEXT("ID=%s Class=%s"), *Before.StableId.ToString(), *Before.ClassPath.ToString());
		if (!After)
		{
			OutDifferences.Add(Prefix + TEXT(" Field=Existence Expected=Present Actual=Missing"));
			return;
		}
		if (Before.ClassPath != After->ClassPath || Before.ContractVersion != After->ContractVersion)
			OutDifferences.Add(Prefix + TEXT(" Field=ClassOrContract Expected=") + Before.ClassPath.ToString()
				+ TEXT(" Actual=") + After->ClassPath.ToString());
		if (!Before.WorldTransform.GetLocation().Equals(After->WorldTransform.GetLocation(), 1.f)
			|| !Before.WorldTransform.GetRotation().Equals(After->WorldTransform.GetRotation(), FMath::DegreesToRadians(0.1f))
			|| !Before.WorldTransform.GetScale3D().Equals(After->WorldTransform.GetScale3D(), 0.01f))
			OutDifferences.Add(Prefix + TEXT(" Field=Transform Expected=") + Before.WorldTransform.ToString()
				+ TEXT(" Actual=") + After->WorldTransform.ToString());
		if (Before.AttachParentId != After->AttachParentId
			|| Before.AttachParentComponentName != After->AttachParentComponentName
			|| Before.AttachSocketName != After->AttachSocketName)
			OutDifferences.Add(Prefix + TEXT(" Field=Attachment ExpectedParent=") + Before.AttachParentId.ToString()
				+ TEXT(" ActualParent=") + After->AttachParentId.ToString()
				+ TEXT(" ExpectedComponent=") + Before.AttachParentComponentName.ToString()
				+ TEXT(" ActualComponent=") + After->AttachParentComponentName.ToString()
				+ TEXT(" ExpectedSocket=") + Before.AttachSocketName.ToString()
				+ TEXT(" ActualSocket=") + After->AttachSocketName.ToString());
		auto CompareMotion = [&](const FSWRoomMotionState& A, const FSWRoomMotionState& B, const FString& Field)
		{
			if (A.bHasMotion != B.bHasMotion || A.bWasSimulatingPhysics != B.bWasSimulatingPhysics
				|| A.bWasProjectileMovementActive != B.bWasProjectileMovementActive
				|| !A.LinearVelocity.Equals(B.LinearVelocity, 1.f)
				|| !A.AngularVelocityDegrees.Equals(B.AngularVelocityDegrees, 1.f))
				OutDifferences.Add(Prefix + TEXT(" Field=") + Field
					+ FString::Printf(TEXT(" ExpectedHasMotion=%d ActualHasMotion=%d ExpectedSim=%d ActualSim=%d ExpectedProjectile=%d ActualProjectile=%d"),
						A.bHasMotion ? 1 : 0, B.bHasMotion ? 1 : 0, A.bWasSimulatingPhysics ? 1 : 0,
						B.bWasSimulatingPhysics ? 1 : 0, A.bWasProjectileMovementActive ? 1 : 0,
						B.bWasProjectileMovementActive ? 1 : 0)
					+ TEXT(" ExpectedLinear=") + A.LinearVelocity.ToString() + TEXT(" ActualLinear=") + B.LinearVelocity.ToString()
					+ TEXT(" ExpectedAngular=") + A.AngularVelocityDegrees.ToString()
					+ TEXT(" ActualAngular=") + B.AngularVelocityDegrees.ToString());
		};
		CompareMotion(Before.MotionState, After->MotionState, TEXT("Motion"));
		if (Before.SaveGameBytes != After->SaveGameBytes)
			OutDifferences.Add(Prefix + TEXT(" Field=SaveGameBytes Expected=") + FString::FromInt(Before.SaveGameBytes.Num())
				+ TEXT(" bytes Actual=") + FString::FromInt(After->SaveGameBytes.Num()) + TEXT(" bytes"));
		TMap<FName, const FSWRoomComponentRecord*> ActualComponents;
		for (const FSWRoomComponentRecord& Child : After->Components)
		{
			if (ActualComponents.Contains(Child.StableKey)) OutDifferences.Add(Prefix + TEXT(" Field=Component.DuplicateKey Actual=") + Child.StableKey.ToString());
			ActualComponents.Add(Child.StableKey, &Child);
		}
		TSet<FName> ExpectedKeys;
		for (const FSWRoomComponentRecord& Child : Before.Components)
		{
			if (ExpectedKeys.Contains(Child.StableKey)) OutDifferences.Add(Prefix + TEXT(" Field=Component.DuplicateKey Expected=") + Child.StableKey.ToString());
			ExpectedKeys.Add(Child.StableKey);
			const FSWRoomComponentRecord* const* Found = ActualComponents.Find(Child.StableKey);
			if (!Found) { OutDifferences.Add(Prefix + TEXT(" Field=Component.Missing Key=") + Child.StableKey.ToString()); continue; }
			if (Child.SaveGameBytes != (*Found)->SaveGameBytes)
				OutDifferences.Add(Prefix + TEXT(" Field=Component.SaveGameBytes Key=") + Child.StableKey.ToString());
			if (!Child.WorldTransform.GetLocation().Equals((*Found)->WorldTransform.GetLocation(), 1.f)
				|| !Child.WorldTransform.GetRotation().Equals((*Found)->WorldTransform.GetRotation(), FMath::DegreesToRadians(0.1f)))
				OutDifferences.Add(Prefix + TEXT(" Field=Component.Transform Key=") + Child.StableKey.ToString());
			CompareMotion(Child.MotionState, (*Found)->MotionState, TEXT("Component.Motion.") + Child.StableKey.ToString());
		}
		if (Before.Components.Num() != After->Components.Num()) OutDifferences.Add(Prefix + TEXT(" Field=Component.Count"));
		TSet<FGuid> BeforeRefs(Before.ReferenceIds), AfterRefs(After->ReferenceIds);
		if (BeforeRefs.Difference(AfterRefs).Num() || AfterRefs.Difference(BeforeRefs).Num())
			OutDifferences.Add(Prefix + TEXT(" Field=ReferenceIds"));
		if (Before.AdapterType != After->AdapterType || Before.AdapterVersion != After->AdapterVersion)
			OutDifferences.Add(Prefix + TEXT(" Field=AdapterContract"));
		if (Before.AdapterBytes.IsEmpty() && After->AdapterBytes.IsEmpty()) return;
		FSWRoomDomainPayload BeforePayload, AfterPayload;
		if (!FSWRoomStructCodec::Read(Before.AdapterBytes, BeforePayload)
			|| !FSWRoomStructCodec::Read(After->AdapterBytes, AfterPayload))
		{
			OutDifferences.Add(Prefix + TEXT(" Field=Adapter.Payload Expected=Readable Actual=Invalid"));
			return;
		}
		AActor* Registered = nullptr;
		if (const TWeakObjectPtr<AActor>* Found = RegisteredActors.Find(Before.StableId)) Registered = Found->Get();
		const ISWRoomStateAdapter* Adapter = Cast<ISWRoomStateAdapter>(Registered);
		if (BeforePayload.Parts.Num() != AfterPayload.Parts.Num())
		{
			bool bSupportedMigration = Adapter && AfterPayload.Parts.Num() > BeforePayload.Parts.Num();
			for (const FSWRoomDomainPart& Added : AfterPayload.Parts)
				if (!BeforePayload.Parts.ContainsByPredicate([&Added](const FSWRoomDomainPart& Part) { return Part.Domain == Added.Domain; }))
					bSupportedMigration &= Adapter && Adapter->AllowsMigratedRoomDomain(Added, BeforePayload.Parts);
			if (!bSupportedMigration) OutDifferences.Add(Prefix + TEXT(" Field=DomainCount"));
		}
		for (const FSWRoomDomainPart& Part : BeforePayload.Parts)
		{
			const FSWRoomDomainPart* Other = AfterPayload.Parts.FindByPredicate([&](const FSWRoomDomainPart& Candidate)
			{ return Candidate.Domain == Part.Domain; });
			if (!Adapter || !Other)
			{
				OutDifferences.Add(Prefix + FString::Printf(TEXT(" Field=Domain.%d Expected=Present Actual=Missing"), static_cast<int32>(Part.Domain)));
				continue;
			}
			TArray<FString> Fields;
			Adapter->CompareRoomDomain(Part, *Other,
				FMath::Max(GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f, 0.05f), Fields);
			const FString DomainName = StaticEnum<ESWRoomDomain>()->GetNameStringByValue(static_cast<int64>(Part.Domain));
			for (const FString& Field : Fields)
				OutDifferences.Add(Prefix + TEXT(" Domain=") + DomainName + TEXT(" ") + Field);
		}
	};
	int32 SkippedOptionalActors = 0;
	for (const FSWRoomActorRecord& Before : Expected.Actors)
	{
		if (HasRestoreIssue(Before.StableId, TEXT("WorldActor"), TEXT("ID/Existence")))
		{
			++SkippedOptionalActors;
			continue;
		}
		ExpectedIds.Add(Before.StableId);
		const FSWRoomActorRecord* const* Found = ActualActors.Find(Before.StableId);
		CompareActor(Before, Found ? *Found : nullptr);
	}
	if (Expected.Actors.Num() + PromotedUnloadedIds.Num() - SkippedOptionalActors != Actual.Actors.Num())
		OutDifferences.Add(FString::Printf(TEXT("ActorCount expected=%d actual=%d"),
			Expected.Actors.Num() + PromotedUnloadedIds.Num() - SkippedOptionalActors, Actual.Actors.Num()));
	TMap<FGuid, const FSWRoomActorRecord*> ActualUnloaded;
	for (const FSWRoomActorRecord& Record : Actual.UnloadedActors)
	{
		if (ActualUnloaded.Contains(Record.StableId)) OutDifferences.Add(TEXT("Duplicate unloaded actor ID: ") + Record.StableId.ToString());
		ActualUnloaded.Add(Record.StableId, &Record);
	}
	int32 SkippedPartitions = 0;
	for (const FSWRoomActorRecord& Before : Expected.UnloadedActors)
	{
		if (HasRestoreIssue(Before.StableId, TEXT("Partition"), TEXT("InstanceName"))
			|| HasRestoreIssue(Before.StableId, TEXT("Partition"), TEXT("State")))
		{
			++SkippedPartitions;
			continue;
		}
		if (PromotedUnloadedIds.Contains(Before.StableId))
		{
			const FSWRoomActorRecord* const* Promoted = ActualActors.Find(Before.StableId);
			CompareActor(Before, Promoted ? *Promoted : nullptr);
			continue;
		}
		const FSWRoomActorRecord* const* Found = ActualUnloaded.Find(Before.StableId);
		if (!Found) { OutDifferences.Add(TEXT("Missing unloaded actor ID: ") + Before.StableId.ToString()); continue; }
		TArray<uint8> BeforeBytes, AfterBytes;
		if (!FSWRoomStructCodec::Write(Before, BeforeBytes) || !FSWRoomStructCodec::Write(**Found, AfterBytes)
			|| BeforeBytes != AfterBytes)
			OutDifferences.Add(TEXT("Unloaded actor value mismatch ID=") + Before.StableId.ToString());
	}
	if (Expected.UnloadedActors.Num() - PromotedUnloadedIds.Num() - SkippedPartitions != Actual.UnloadedActors.Num())
		OutDifferences.Add(TEXT("Unloaded actor count"));
	TMap<FName, const FSWRoomSystemRecord*> ActualSystems;
	auto IsLoadedPartitionSystem = [&](FName StableKey)
	{
		const FString Key = StableKey.ToString();
		if (!Key.StartsWith(TEXT("Partition:"))) return false;
		for (ULevel* Level : GetWorld()->GetLevels())
			if (Level && Level->bIsVisible && GetPartitionKey(Level) == Key.RightChop(10)) return true;
		return false;
	};
	for (const FSWRoomSystemRecord& System : Actual.Systems)
	{
		if (ActualSystems.Contains(System.StableKey)) OutDifferences.Add(TEXT("Duplicate system key: ") + System.StableKey.ToString());
		ActualSystems.Add(System.StableKey, &System);
	}
	for (const FSWRoomSystemRecord& Before : Expected.Systems)
	{
		if (IsLoadedPartitionSystem(Before.StableKey)) continue;
		const FSWRoomSystemRecord* const* Found = ActualSystems.Find(Before.StableKey);
		if (!Found || Before.ContractVersion != (*Found)->ContractVersion || Before.SaveGameBytes != (*Found)->SaveGameBytes)
			OutDifferences.Add(TEXT("System value mismatch Key=") + Before.StableKey.ToString());
	}
	int32 ExpectedCachedSystemCount = 0;
	for (const FSWRoomSystemRecord& Before : Expected.Systems)
		if (!IsLoadedPartitionSystem(Before.StableKey)) ++ExpectedCachedSystemCount;
	if (ExpectedCachedSystemCount != Actual.Systems.Num()) OutDifferences.Add(TEXT("System count"));
	TSet<FGuid> ExpectedDestroyed(Expected.DestroyedLevelActorIds), ActualDestroyed(Actual.DestroyedLevelActorIds);
	if (ExpectedDestroyed.Difference(ActualDestroyed).Num() || ActualDestroyed.Difference(ExpectedDestroyed).Num())
		OutDifferences.Add(TEXT("Destroyed actor ID set"));
	TSet<FString> ExpectedPartitions, ActualPartitions;
	for (const FSWRoomDestroyedActorPartition& Partition : Expected.DestroyedActorPartitions)
		ExpectedPartitions.Add(Partition.StableId.ToString() + TEXT("|") + GetPartitionKey(Partition));
	for (const FSWRoomDestroyedActorPartition& Partition : Actual.DestroyedActorPartitions)
		ActualPartitions.Add(Partition.StableId.ToString() + TEXT("|") + GetPartitionKey(Partition));
	if (ExpectedPartitions.Difference(ActualPartitions).Num() || ActualPartitions.Difference(ExpectedPartitions).Num())
		OutDifferences.Add(TEXT("Destroyed actor partition set"));
	TSet<FGuid> ExpectedRefs(Expected.ReferenceIds), ActualRefs(Actual.ReferenceIds);
	if (ExpectedRefs.Difference(ActualRefs).Num() || ActualRefs.Difference(ExpectedRefs).Num())
		OutDifferences.Add(TEXT("World reference ID set"));
	for (int32 Index = OutDifferences.Num() - 1; Index >= 0; --Index)
	{
		const FString& Difference = OutDifferences[Index];
		if (!Difference.StartsWith(TEXT("ID="))) continue;
		FString IdText, Tail;
		if (!Difference.RightChop(3).Split(TEXT(" "), &IdText, &Tail)) continue;
		FGuid Id;
		if (!FGuid::Parse(IdText, Id)) continue;
		auto IsCovered = [&](const FSWRoomCaptureIssue& Issue)
		{
			if (Issue.StableId != Id) return false;
			if (Issue.Domain == TEXT("Adapter") && Issue.FieldKey == TEXT("Payload"))
				return Difference.Contains(TEXT(" Domain=")) || Difference.Contains(TEXT(" Field=Adapter"));
			if (Issue.Domain == TEXT("CaptureFailed") && Issue.FieldKey == TEXT("Adapter/Payload"))
				return Difference.Contains(TEXT(" Domain=")) || Difference.Contains(TEXT(" Field=Adapter"));
			if (Issue.Domain == TEXT("Motion") && Issue.FieldKey == TEXT("RootVelocity"))
				return Difference.Contains(TEXT(" Field=Motion"));
			if (Issue.Domain == TEXT("GameplayEffect") && Issue.FieldKey.ToString().Contains(TEXT("#")))
				return Difference.Contains(Issue.FieldKey.ToString());
			const FString Field = TEXT("Field=") + Issue.FieldKey.ToString();
			return Difference.Contains(Field + TEXT(" ")) || Difference.EndsWith(Field);
		};
		bool bCovered = false;
		for (const FSWRoomCaptureIssue& Issue : Expected.CaptureIssues) bCovered |= IsCovered(Issue);
		for (const FSWRoomCaptureIssue& Issue : RestoreIssues) bCovered |= IsCovered(Issue);
		if (bCovered) OutDifferences.RemoveAt(Index);
	}
	return OutDifferences.IsEmpty();
}
