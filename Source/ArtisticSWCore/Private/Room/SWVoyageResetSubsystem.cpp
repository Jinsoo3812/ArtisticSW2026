#include "Room/SWVoyageResetSubsystem.h"
#include "Room/SWVoyageResetAnchor.h"
#include "Room/SWVoyageResetProfile.h"
#include "Room/SWVoyageResetParticipant.h"
#include "Room/SWRoomProgressSubsystem.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "Room/SWRoomSnapshotSubsystem.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/NetDriver.h"
#include "Engine/PackageMapClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/Brush.h"
#include "Components/PrimitiveComponent.h"
#include "UObject/UObjectGlobals.h"

namespace
{
// INDEX_NONE means absent; -2 means more than one explicit lifetime.
int32 ResolveVoyageLifetimeTag(const AActor* Actor)
{
	struct FLifetimeTag { const TCHAR* Tag; ESWVoyageActorLifetime Lifetime; };
	const FLifetimeTag Tags[] = {
		{ TEXT("SWVoyage.Environment"), ESWVoyageActorLifetime::Environment },
		{ TEXT("SWVoyage.Anchor"), ESWVoyageActorLifetime::Anchor },
		{ TEXT("SWVoyage.Voyage"), ESWVoyageActorLifetime::Voyage },
		{ TEXT("SWVoyage.SharedService"), ESWVoyageActorLifetime::SharedService },
		{ TEXT("SWVoyage.PlayerLife"), ESWVoyageActorLifetime::PlayerLife },
		{ TEXT("SWVoyage.LocalPresentation"), ESWVoyageActorLifetime::LocalPresentation }
	};
	int32 Found = INDEX_NONE;
	for (const FLifetimeTag& Entry : Tags)
	{
		if (!Actor->ActorHasTag(Entry.Tag)) continue;
		if (Found != INDEX_NONE) return -2;
		Found = static_cast<int32>(Entry.Lifetime);
	}
	return Found;
}
}
bool USWVoyageResetSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
}

void USWVoyageResetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UGameInstance* Instance = GetWorld()->GetGameInstance())
		if (USWRoomProgressSubsystem* Room = Instance->GetSubsystem<USWRoomProgressSubsystem>(); Room && Room->IsHostedRoom() && GetWorld()->GetNetMode() != NM_Client)
			ActorGeneration = Room->GetRestoreGeneration() < MAX_int32 ? Room->GetRestoreGeneration() + 1 : 0;
	PostGcHandle = FCoreUObjectDelegates::GetPostGarbageCollect().AddUObject(this, &USWVoyageResetSubsystem::HandlePostGarbageCollect);
	ActorSpawnedHandle = GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &USWVoyageResetSubsystem::HandleActorSpawned));
	CoreTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &USWVoyageResetSubsystem::Tick));
}

void USWVoyageResetSubsystem::Deinitialize()
{
	if (AsyncGuard) AsyncGuard->bCancelled.Store(true);
	AsyncGuard.Reset();
	FTSTicker::GetCoreTicker().RemoveTicker(CoreTickerHandle);
	FCoreUObjectDelegates::GetPostGarbageCollect().Remove(PostGcHandle);
	if (GetWorld()) GetWorld()->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
	RestoreLocalPause();
	TArray<FGuid> CleanupIds;
	PresentationCleanupOwners.GetKeys(CleanupIds);
	for (const FGuid& CleanupId : CleanupIds) UnregisterLocalPresentationCleanupOwner(CleanupId);
	Actors.Empty(); Participants.Empty(); OrderedParticipants.Empty(); OldActors.Empty();
	ParticipantRuntimeIds.Empty();
	Profile = nullptr; GameplayStreaming.Reset(); OldLevel.Reset();
	Super::Deinitialize();
}

void USWVoyageResetSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	UGameInstance* Instance = InWorld.GetGameInstance();
	USWRoomProgressSubsystem* Room = Instance ? Instance->GetSubsystem<USWRoomProgressSubsystem>() : nullptr;
	if (!bActive && Room && Room->IsHostedRoom() && InWorld.GetNetMode() != NM_Client)
		ActorGeneration = Room->GetRestoreGeneration() < MAX_int32 ? Room->GetRestoreGeneration() + 1 : 0;
	FString Error;
	if (!ConfigureFromAnchor(Error)) return;
	UpdateAsyncGuard();
}

bool USWVoyageResetSubsystem::BeginBootstrapPreparation(int32 Generation, FString& OutError)
{
	check(IsInGameThread());
	USWRoomProgressSubsystem* Room = GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>() : nullptr;
	if (!Room || !Room->IsHostedRoom() || GetWorld()->GetNetMode() == NM_Client || bActive
		|| Generation <= 0 || Generation != ActorGeneration || Generation != Room->GetRestoreGeneration())
	{
		OutError = TEXT("VoyageBootstrapGenerationContractInvalid"); return false;
	}
	if (!ConfigureFromAnchor(OutError)) return false;
	bActive = true; bBlocked = true;
	Context.Generation = Generation; Context.bAuthority = true; Context.bBootstrap = true;
	Context.GameplayPackage = GameplayStreaming->GetWorldAssetPackageFName();
	UpdateAsyncGuard();
	return true;
}

void USWVoyageResetSubsystem::UpdateAsyncGuard()
{
	check(IsInGameThread());
	if (!AsyncGuard || AsyncGuard->Generation != ActorGeneration)
	{
		if (AsyncGuard) AsyncGuard->bCancelled.Store(true);
		AsyncGuard = MakeShared<FSWVoyageAsyncGuard, ESPMode::ThreadSafe>();
		AsyncGuard->Generation = ActorGeneration;
	}
	AsyncGuard->bGameplayBlocked.Store(bBlocked);
}

TSharedPtr<FSWVoyageAsyncGuard, ESPMode::ThreadSafe> USWVoyageResetSubsystem::GetAsyncGuard()
{
	UpdateAsyncGuard();
	return AsyncGuard;
}

bool USWVoyageResetSubsystem::ConfigureFromAnchor(FString& OutError)
{
	check(IsInGameThread());
	OutError.Reset();
	ASWVoyageResetAnchor* Anchor = nullptr;
	for (TActorIterator<ASWVoyageResetAnchor> It(GetWorld()); It; ++It)
	{
		if (Anchor || It->GetLevel() != GetWorld()->PersistentLevel) { OutError = TEXT("VoyageAnchorAmbiguous"); return false; }
		Anchor = *It;
	}
	if (!Anchor || !Anchor->Profile) { OutError = TEXT("VoyageAnchorOrProfileMissing"); return false; }
	if (!Anchor->Profile->ValidateProfile(OutError)) return false;
	ULevelStreaming* Found = nullptr;
	const FString Package = Anchor->Profile->GameplayLevel.ToSoftObjectPath().GetLongPackageName();
	for (ULevelStreaming* Streaming : GetWorld()->GetStreamingLevels())
	{
		if (!Streaming || UWorld::RemovePIEPrefix(Streaming->GetWorldAssetPackageName()) != Package) continue;
		const ULevelStreamingDynamic* Dynamic = Cast<ULevelStreamingDynamic>(Streaming);
		if (Found || !Dynamic || Streaming->LevelTransform.ContainsNaN() || !Streaming->LevelTransform.Equals(FTransform::Identity)
			|| !Streaming->EditorStreamingVolumes.IsEmpty() || !Dynamic->bInitiallyLoaded || !Dynamic->bInitiallyVisible)
		{ OutError = TEXT("VoyageGameplayStreamingContractInvalid"); return false; }
		Found = Streaming;
	}
	if (!Found) { OutError = TEXT("VoyageGameplayStreamingMissing"); return false; }
	Profile = Anchor->Profile; GameplayStreaming = Found;
	return AuditWorldActors(OutError);
}

void USWVoyageResetSubsystem::DiscoverParticipants(TArray<UObject*>& OutObjects) const
{
	for (UWorldSubsystem* Subsystem : GetWorld()->GetSubsystemArrayCopy<UWorldSubsystem>())
		if (Subsystem != this) OutObjects.AddUnique(Subsystem);
	if (UGameInstance* Instance = GetWorld()->GetGameInstance())
		for (UGameInstanceSubsystem* Subsystem : Instance->GetSubsystemArrayCopy<UGameInstanceSubsystem>()) OutObjects.AddUnique(Subsystem);
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->GetClass()->ImplementsInterface(USWVoyageResetParticipant::StaticClass())) OutObjects.AddUnique(*It);
		TInlineComponentArray<UActorComponent*> Components(*It);
		for (UActorComponent* Component : Components)
			if (Component && Component->GetClass()->ImplementsInterface(USWVoyageResetParticipant::StaticClass())) OutObjects.AddUnique(Component);
	}
	for (const TWeakObjectPtr<UObject>& Object : Participants) if (Object.IsValid()) OutObjects.AddUnique(Object.Get());
}

bool USWVoyageResetSubsystem::ValidateVoyageContracts(FString& OutError) const
{
	TArray<TWeakObjectPtr<UObject>> Order;
	return CollectValidatedParticipantOrder(Order, OutError);
}

bool USWVoyageResetSubsystem::BuildParticipantOrder(FString& OutError)
{
	TArray<TWeakObjectPtr<UObject>> Order;
	if (!CollectValidatedParticipantOrder(Order, OutError)) return false;
	OrderedParticipants = MoveTemp(Order);
	return true;
}

bool USWVoyageResetSubsystem::CollectValidatedParticipantOrder(TArray<TWeakObjectPtr<UObject>>& OutOrder, FString& OutError) const
{
	check(IsInGameThread());
	OutOrder.Reset(); OutError.Reset();
	if (!Profile || !GameplayStreaming.IsValid()) { OutError = TEXT("VoyageNotConfigured"); return false; }
	if (!RegistrationFailure.IsEmpty()) { OutError = RegistrationFailure; return false; }
	struct FParticipantContract
	{
		UObject* Object;
		ESWVoyagePolicy Policy;
		ESWVoyageRestoreStage Stage;
		TArray<FName> Dependencies;
	};
	TArray<UObject*> Objects; DiscoverParticipants(Objects);
	TMap<FName, FParticipantContract> Contracts;
	for (UObject* Object : Objects)
	{
		const UClass* NativeClass = Object->GetClass();
		while (NativeClass && !NativeClass->HasAnyClassFlags(CLASS_Native)) NativeClass = NativeClass->GetSuperClass();
		const bool bRequired = NativeClass && Profile->ProjectScriptPackages.Contains(NativeClass->GetOutermost()->GetFName());
		if (!Object->GetClass()->ImplementsInterface(USWVoyageResetParticipant::StaticClass()))
		{
			if (bRequired) { OutError = TEXT("VoyageParticipantMissing:") + Object->GetPathName(); return false; }
			continue;
		}
		const ESWVoyagePolicy Policy = ISWVoyageResetParticipant::Execute_GetVoyagePolicy(Object);
		if (Policy == ESWVoyagePolicy::Unsupported)
		{ OutError = TEXT("VoyageResetUnsupportedInDiagnosticSession:") + Object->GetPathName(); return false; }
		const FName Id = ISWVoyageResetParticipant::Execute_GetVoyageParticipantId(Object);
		if (Id.IsNone() || Contracts.Contains(Id)) { OutError = TEXT("VoyageParticipantIdInvalid:") + Object->GetPathName(); return false; }
		Contracts.Add(Id, { Object, Policy, ISWVoyageResetParticipant::Execute_GetVoyageRestoreStage(Object),
			ISWVoyageResetParticipant::Execute_GetVoyageAfterParticipants(Object) });
	}
	for (const auto& Pair : Contracts)
		for (FName Dependency : Pair.Value.Dependencies)
		{
			const FParticipantContract* Required = Contracts.Find(Dependency);
			if (!Required) { OutError = TEXT("VoyageParticipantDependencyMissing:") + Dependency.ToString(); return false; }
			if (Required->Policy != ESWVoyagePolicy::Preserve && Required->Stage > Pair.Value.Stage)
			{ OutError = TEXT("VoyageParticipantFutureStageDependency:") + Pair.Key.ToString(); return false; }
		}
	TSet<FName> Resolved;
	while (Resolved.Num() < Contracts.Num())
	{
		TArray<FName> ReadyIds;
		for (const auto& Pair : Contracts)
		{
			if (Resolved.Contains(Pair.Key)) continue;
			bool bReady = true;
			for (FName Dependency : Pair.Value.Dependencies) bReady &= Resolved.Contains(Dependency);
			if (bReady) ReadyIds.Add(Pair.Key);
		}
		ReadyIds.Sort([&Contracts](FName Left, FName Right)
		{
			const ESWVoyageRestoreStage LeftStage = Contracts.FindChecked(Left).Stage;
			const ESWVoyageRestoreStage RightStage = Contracts.FindChecked(Right).Stage;
			return LeftStage == RightStage ? Left.LexicalLess(Right) : LeftStage < RightStage;
		});
		if (ReadyIds.IsEmpty()) { OutError = TEXT("VoyageParticipantDependencyCycle"); return false; }
		const FName Id = ReadyIds[0];
		OutOrder.Add(Contracts.FindChecked(Id).Object); Resolved.Add(Id);
	}
	return true;
}
bool USWVoyageResetSubsystem::RegisterParticipant(UObject* Object, FString& OutError)
{
	check(IsInGameThread()); OutError.Reset();
	if (!IsValid(Object) || !Object->GetClass()->ImplementsInterface(USWVoyageResetParticipant::StaticClass()))
	{ OutError = TEXT("VoyageParticipantInterfaceMissing"); return false; }
	Participants.Add(Object); return true;
}

FName USWVoyageResetSubsystem::ResolveParticipantId(UObject* Object)
{
	check(IsInGameThread());
	if (!IsValid(Object)) return NAME_None;
	if (Object->IsA<UWorldSubsystem>() || Object->IsA<UGameInstanceSubsystem>())
	{
		const UClass* NativeClass = Object->GetClass();
		while (NativeClass && !NativeClass->HasAnyClassFlags(CLASS_Native)) NativeClass = NativeClass->GetSuperClass();
		return NativeClass ? FName(*NativeClass->GetPathName()) : NAME_None;
	}
	if (UActorComponent* Component = Cast<UActorComponent>(Object))
	{
		const FName OwnerId = ResolveParticipantId(Component->GetOwner());
		return OwnerId.IsNone() ? NAME_None : FName(*(OwnerId.ToString() + TEXT("/Component/") + Component->GetName()));
	}
	if (AActor* Actor = Cast<AActor>(Object))
	{
		const USWRoomSnapshotComponent* Snapshot = Actor->FindComponentByClass<USWRoomSnapshotComponent>();
		if (Snapshot && Snapshot->StableId.IsValid())
			return FName(*(TEXT("Actor/") + Snapshot->StableId.ToString(EGuidFormats::Digits)));
	}
	FGuid& RuntimeId = ParticipantRuntimeIds.FindOrAdd(Object);
	if (!RuntimeId.IsValid()) RuntimeId = FGuid::NewGuid();
	return FName(*(TEXT("Runtime/") + RuntimeId.ToString(EGuidFormats::Digits)));
}

void USWVoyageResetSubsystem::UnregisterParticipant(UObject* Object) { check(IsInGameThread()); Participants.Remove(Object); }

bool USWVoyageResetSubsystem::RegisterActor(AActor* Actor, ESWVoyageActorLifetime Lifetime, int32 Generation, FString& OutError)
{
	return RegisterActor(Actor, Lifetime, Generation, FGuid(), OutError);
}

bool USWVoyageResetSubsystem::RegisterActor(AActor* Actor, ESWVoyageActorLifetime Lifetime, int32 Generation,
	const FGuid& PresentationCleanupId, FString& OutError)
{
	check(IsInGameThread()); OutError.Reset();
	if (!IsValid(Actor) || Actor->GetWorld() != GetWorld() || Generation != ActorGeneration)
	{ OutError = TEXT("VoyageActorRegistrationInvalid"); return false; }
	if ((Lifetime != ESWVoyageActorLifetime::LocalPresentation && PresentationCleanupId.IsValid())
		|| (bActive && Lifetime == ESWVoyageActorLifetime::LocalPresentation
			&& (!CanSpawnVoyageActor(Lifetime, Generation, PresentationCleanupId) || Actor->GetIsReplicated())))
	{ OutError = TEXT("VoyagePresentationCleanupInvalid"); return false; }
	if (const FActorRegistration* Existing = Actors.Find(Actor))
	{
		if (Existing->Lifetime != Lifetime || Existing->Generation != Generation || Existing->PresentationCleanupId != PresentationCleanupId)
		{ OutError = TEXT("VoyageActorRegistrationConflict"); return false; }
		return true;
	}
	Actors.Add(Actor, { Lifetime, Generation, PresentationCleanupId }); return true;
}

bool USWVoyageResetSubsystem::IsActiveVoyageSession() const
{
	check(IsInGameThread());
	return bActive;
}

bool USWVoyageResetSubsystem::AdoptInitialClientGeneration(const FSWVoyageReplicatedState& State,
	const FGuid& RoomRunId, FString& OutError)
{
	check(IsInGameThread()); OutError.Reset();
	if (GetWorld()->GetNetMode() != NM_Client || !RoomRunId.IsValid() || State.Generation <= 0
		|| (State.Phase != ESWVoyagePhase::Idle && State.Phase != ESWVoyagePhase::Release))
	{ OutError = TEXT("VoyageInitialClientGenerationInvalid"); return false; }
	if (!Profile && !ConfigureFromAnchor(OutError)) return false;
	if (!GameplayStreaming.IsValid() || UWorld::RemovePIEPrefix(State.GameplayPackage.ToString())
		!= UWorld::RemovePIEPrefix(GameplayStreaming->GetWorldAssetPackageName()))
	{ OutError = TEXT("VoyageInitialClientGameplayMismatch"); return false; }
	if (InitialClientRoomRunId.IsValid())
	{
		if (InitialClientRoomRunId == RoomRunId && InitialClientGeneration == State.Generation) return true;
		OutError = TEXT("VoyageInitialClientGenerationAlreadyAdopted"); return false;
	}
	if (ActorGeneration != 0 || Context.AttemptId != 0)
	{ OutError = TEXT("VoyageInitialClientGenerationUnavailable"); return false; }
	for (const auto& Pair : Actors)
		if (Pair.Value.Generation != 0) { OutError = TEXT("VoyageInitialClientRegistryGenerationInvalid"); return false; }
	InitialClientRoomRunId = RoomRunId; InitialClientGeneration = State.Generation; ActorGeneration = State.Generation;
	for (auto& Pair : Actors) Pair.Value.Generation = ActorGeneration;
	Context.Generation = ActorGeneration; Context.GameplayPackage = State.GameplayPackage;
	bActive = true; bBlocked = false; bPreparationSpawnAllowed = false; bDestructiveStarted = false;
	UpdateAsyncGuard();
	return true;
}

bool USWVoyageResetSubsystem::CanSpawnVoyageActor(ESWVoyageActorLifetime Lifetime, int32 ExpectedGeneration) const
{
	return CanSpawnVoyageActor(Lifetime, ExpectedGeneration, FGuid());
}

bool USWVoyageResetSubsystem::CanSpawnVoyageActor(ESWVoyageActorLifetime Lifetime, int32 ExpectedGeneration,
	const FGuid& PresentationCleanupId) const
{
	check(IsInGameThread());
	if (Lifetime != ESWVoyageActorLifetime::LocalPresentation && PresentationCleanupId.IsValid()) return false;
	if (!bActive) return ExpectedGeneration == 0;
	if (ExpectedGeneration != ActorGeneration) return false;
	if (Lifetime == ESWVoyageActorLifetime::LocalPresentation)
	{
		const FPresentationCleanupRegistration* Registration = PresentationCleanupOwners.Find(PresentationCleanupId);
		return GetWorld()->GetNetMode() != NM_DedicatedServer && Registration && Registration->Generation == ActorGeneration
			&& Registration->Owner.IsValid() && !Registration->Owner->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
			&& Registration->Owner->GetWorld() == GetWorld();
	}
	if (Lifetime == ESWVoyageActorLifetime::Environment || Lifetime == ESWVoyageActorLifetime::Anchor) return false;
	if (Lifetime == ESWVoyageActorLifetime::Voyage && (!GameplayStreaming.IsValid() || !GameplayStreaming->GetLoadedLevel())) return false;
	if (Context.bBootstrap && bBlocked)
		return Lifetime == ESWVoyageActorLifetime::SharedService
			|| (Lifetime == ESWVoyageActorLifetime::Voyage && bPreparationSpawnAllowed)
			|| (Lifetime == ESWVoyageActorLifetime::PlayerLife && Context.bContinue
				&& Context.Phase == ESWVoyagePhase::Restore && bPreparationSpawnAllowed);
	switch (Context.Phase)
	{
	case ESWVoyagePhase::Presentation:
	case ESWVoyagePhase::Quiesce:
	case ESWVoyagePhase::Unload:
	case ESWVoyagePhase::Purge:
	case ESWVoyagePhase::Failed:
	case ESWVoyagePhase::RecoveryTravel: return false;
	case ESWVoyagePhase::Load:
	case ESWVoyagePhase::Restore: return bPreparationSpawnAllowed;
	case ESWVoyagePhase::ClientReady:
		return !bBlocked || (Context.bAuthority
			&& Lifetime == ESWVoyageActorLifetime::PlayerLife
			&& bPreparationSpawnAllowed);
	default: return !bBlocked;
	}
}

bool USWVoyageResetSubsystem::RegisterLocalPresentationCleanupOwner(UObject* CleanupOwner, int32 ExpectedGeneration,
	FGuid& OutCleanupId, FString& OutError)
{
	check(IsInGameThread());
	OutCleanupId.Invalidate(); OutError.Reset();
	if (!IsValid(CleanupOwner) || CleanupOwner->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
		|| CleanupOwner->GetWorld() != GetWorld() || ExpectedGeneration != ActorGeneration)
	{ OutError = TEXT("VoyagePresentationCleanupOwnerInvalid"); return false; }
	for (const auto& Pair : PresentationCleanupOwners)
		if (Pair.Value.Owner.Get() == CleanupOwner && Pair.Value.Generation == ExpectedGeneration)
		{ OutCleanupId = Pair.Key; return true; }
	do { OutCleanupId = FGuid::NewGuid(); } while (PresentationCleanupOwners.Contains(OutCleanupId));
	PresentationCleanupOwners.Add(OutCleanupId, { CleanupOwner, ExpectedGeneration });
	return true;
}

void USWVoyageResetSubsystem::UnregisterLocalPresentationCleanupOwner(FGuid CleanupId)
{
	check(IsInGameThread());
	if (!PresentationCleanupOwners.Remove(CleanupId)) return;
	TArray<TWeakObjectPtr<AActor>> OwnedActors;
	for (const auto& Pair : Actors)
		if (Pair.Value.PresentationCleanupId == CleanupId) OwnedActors.Add(Pair.Key);
	for (const TWeakObjectPtr<AActor>& Actor : OwnedActors)
	{
		if (Actor.IsValid()) Actor->Destroy();
		Actors.Remove(Actor);
	}
}

bool USWVoyageResetSubsystem::GetActorPresentationCleanupId(const AActor* Actor, FGuid& OutCleanupId) const
{
	check(IsInGameThread()); OutCleanupId.Invalidate();
	const FActorRegistration* Registration = Actors.Find(TWeakObjectPtr<AActor>(const_cast<AActor*>(Actor)));
	if (!Registration) return false;
	OutCleanupId = Registration->PresentationCleanupId;
	return true;
}
void USWVoyageResetSubsystem::UnregisterActor(AActor* Actor) { check(IsInGameThread()); Actors.Remove(Actor); }
bool USWVoyageResetSubsystem::IsCurrentGeneration(int32 Generation) const { check(IsInGameThread()); return Generation == ActorGeneration; }
bool USWVoyageResetSubsystem::IsGameplayBlocked() const { check(IsInGameThread()); return bActive && bBlocked; }
bool USWVoyageResetSubsystem::IsPreparationSpawnAllowed() const { check(IsInGameThread()); return bPreparationSpawnAllowed; }
int32 USWVoyageResetSubsystem::GetGeneration() const { check(IsInGameThread()); return ActorGeneration; }
int32 USWVoyageResetSubsystem::GetActorGeneration(const AActor* Actor) const
{
	check(IsInGameThread());
	const FActorRegistration* Registration = Actors.Find(TWeakObjectPtr<AActor>(const_cast<AActor*>(Actor)));
	return Registration ? Registration->Generation : -1;
}
bool USWVoyageResetSubsystem::GetActorLifetime(const AActor* Actor, ESWVoyageActorLifetime& OutLifetime) const
{
	check(IsInGameThread());
	if (const FActorRegistration* Registration = Actors.Find(TWeakObjectPtr<AActor>(const_cast<AActor*>(Actor))))
	{ OutLifetime = Registration->Lifetime; return true; }
	return false;
}
ULevelStreaming* USWVoyageResetSubsystem::GetGameplayStreamingLevel() const { check(IsInGameThread()); return GameplayStreaming.Get(); }
void USWVoyageResetSubsystem::SetPreparationSpawnAllowed(bool bAllowed) { check(IsInGameThread()); bPreparationSpawnAllowed = bAllowed; }

void USWVoyageResetSubsystem::HandlePostGarbageCollect() { ++ObservedGcSerial; }

void USWVoyageResetSubsystem::HandleActorSpawned(AActor* Actor)
{
	if (!Actor || Actor == LocalPauseSentinel || Actors.Contains(Actor) || !Profile) return;
	const int32 Found = ResolveVoyageLifetimeTag(Actor);
	if (Found == -2) { RegistrationFailure = TEXT("VoyageActorLifetimeAmbiguous:") + Actor->GetPathName(); return; }
	if (Found != INDEX_NONE)
	{
		if (Found == static_cast<int32>(ESWVoyageActorLifetime::LocalPresentation) && !Actor->IsActorInitialized()) return;
		FString Error; if (!RegisterActor(Actor, static_cast<ESWVoyageActorLifetime>(Found), ActorGeneration, Error)) RegistrationFailure = Error;
	}
	else if (bActive && !USWRoomSnapshotComponent::IsAuthoredRoomActor(Actor))
	{
		if (!Actor->IsActorInitialized()) return;
		const UClass* NativeClass = Actor->GetClass();
		while (NativeClass && !NativeClass->HasAnyClassFlags(CLASS_Native)) NativeClass = NativeClass->GetSuperClass();
		if (NativeClass && Profile->ProjectScriptPackages.Contains(NativeClass->GetOutermost()->GetFName()))
			RegistrationFailure = TEXT("VoyageRuntimeFactoryUndeclared:") + Actor->GetPathName();
	}
}

bool USWVoyageResetSubsystem::Tick(float DeltaSeconds)
{
	TArray<FGuid> ExpiredCleanupIds;
	for (const auto& Pair : PresentationCleanupOwners)
		if (!Pair.Value.Owner.IsValid() || Pair.Value.Owner->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
			|| Pair.Value.Owner->GetWorld() != GetWorld() || Pair.Value.Generation != ActorGeneration) ExpiredCleanupIds.Add(Pair.Key);
	for (const FGuid& CleanupId : ExpiredCleanupIds) UnregisterLocalPresentationCleanupOwner(CleanupId);
	for (auto It = Actors.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
	for (auto It = Participants.CreateIterator(); It; ++It) if (!It->IsValid()) It.RemoveCurrent();
	for (auto It = ParticipantRuntimeIds.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
	if (bActive && bBlocked) MaintainLocalPause();
	return true;
}

void USWVoyageResetSubsystem::MaintainLocalPause()
{
	if (!bActive || !bBlocked || GetWorld()->GetNetMode() != NM_Client) return;
	AWorldSettings* Settings = GetWorld()->GetWorldSettings();
	if (!Settings) return;
	if (!LocalPauseSentinel)
	{
		PreviousPauser = Settings->GetPauserPlayerState();
		FActorSpawnParameters Parameters; Parameters.OverrideLevel = GetWorld()->PersistentLevel;
		Parameters.bDeferConstruction = true;
		LocalPauseSentinel = GetWorld()->SpawnActor<APlayerState>(APlayerState::StaticClass(), Parameters);
		if (LocalPauseSentinel) { LocalPauseSentinel->SetReplicates(false); LocalPauseSentinel->FinishSpawning(FTransform::Identity); }
	}
	if (LocalPauseSentinel) Settings->SetPauserPlayerState(LocalPauseSentinel);
}

void USWVoyageResetSubsystem::RestoreLocalPause()
{
	if (LocalPauseSentinel && GetWorld() && GetWorld()->GetWorldSettings())
	{
		AWorldSettings* Settings = GetWorld()->GetWorldSettings();
		if (Settings->GetPauserPlayerState() == LocalPauseSentinel) Settings->SetPauserPlayerState(PreviousPauser.Get());
		LocalPauseSentinel->Destroy(); LocalPauseSentinel = nullptr;
	}
	PreviousPauser.Reset();
}

bool USWVoyageResetSubsystem::BeginLocalPhase(const FSWVoyageResetContext& InContext, FString& OutError)
{
	check(IsInGameThread()); OutError.Reset();
	if (InContext.AttemptId < Context.AttemptId || InContext.Generation < ActorGeneration)
	{ OutError = TEXT("VoyageStalePhase"); return false; }
	if (InContext.Phase == ESWVoyagePhase::Idle)
	{
		if (InContext.AttemptId != Context.AttemptId || (bDestructiveStarted && Context.Phase != ESWVoyagePhase::Release))
		{ OutError = TEXT("VoyageIdlePhaseInvalid"); return false; }
		if (!bDestructiveStarted && Context.Phase != ESWVoyagePhase::Release)
			for (const TWeakObjectPtr<UObject>& Object : PreparedParticipants)
				if (Object.IsValid()) ISWVoyageResetParticipant::Execute_CancelVoyagePreparation(Object.Get(), Context);
		Context = InContext;
		bBlocked = false; bPreparationSpawnAllowed = false; bDestructiveStarted = false;
		UpdateAsyncGuard();
		RestoreLocalPause(); PreparedParticipants.Reset(); CompletedParticipants.Reset();
		return true;
	}
	if (!Profile && !ConfigureFromAnchor(OutError)) return false;
	if (InContext.AttemptId == Context.AttemptId && InContext.Generation == Context.Generation && InContext.Phase == Context.Phase) return true;
	if (InContext.GameplayPackage != GameplayStreaming->GetWorldAssetPackageFName())
	{ OutError = TEXT("VoyageGameplayPackageMismatch"); return false; }
	Context = InContext; bActive = true; bBlocked = true; CompletedParticipants.Reset();
	UpdateAsyncGuard();
	if (!BuildParticipantOrder(OutError)) return false;
	if (Context.Phase == ESWVoyagePhase::Presentation) { PreparedParticipants.Reset(); ResumedParticipants.Reset(); bDestructiveStarted = false; }
	if (Context.Phase == ESWVoyagePhase::Unload)
	{
		USWRoomSnapshotSubsystem* Snapshot = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>();
		if (!Snapshot || !Snapshot->BeginVoyageDiscard(GameplayStreaming->GetLoadedLevel(), Context.Generation, OutError)) return false;
		ActorGeneration = Context.Generation; bDestructiveStarted = true;
		UpdateAsyncGuard();
		OldLevel = GameplayStreaming->GetLoadedLevel(); OldActors.Reset(); OldStaticGuids.Reset(); RequiredGcSerial = 0; bUnloadIssued = false;
		UNetDriver* Driver = GetWorld()->GetNetDriver();
		if (Context.bAuthority && (!Driver || !Driver->GuidCache.IsValid())) { OutError = TEXT("VoyageNetDriverMissing"); return false; }
		if (Driver)
			for (const FNetworkGUID& Guid : Driver->GetDestroyedStartupOrDormantActors(Context.GameplayPackage))
				if (Guid.IsValid() && Guid.IsStatic()) OldStaticGuids.Add(Guid.Value);
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			const FActorRegistration* Registration = Actors.Find(*It);
			if (It->GetLevel() == OldLevel || (Registration && (Registration->Lifetime == ESWVoyageActorLifetime::Voyage || Registration->Lifetime == ESWVoyageActorLifetime::PlayerLife || Registration->Lifetime == ESWVoyageActorLifetime::LocalPresentation)))
			{
				OldActors.Add(*It);
				if (Driver && Driver->GuidCache.IsValid())
				{
					const FNetworkGUID Guid = Driver->GuidCache->GetNetGUID(*It);
					if (Guid.IsValid() && Guid.IsStatic()) OldStaticGuids.Add(Guid.Value);
				}
			}
		}
		for (auto& Pair : Actors)
			if (Pair.Value.Lifetime == ESWVoyageActorLifetime::SharedService) Pair.Value.Generation = ActorGeneration;
		TArray<FGuid> OldCleanupIds;
		for (const auto& Pair : PresentationCleanupOwners)
			if (Pair.Value.Generation != ActorGeneration) OldCleanupIds.Add(Pair.Key);
		for (const FGuid& CleanupId : OldCleanupIds) UnregisterLocalPresentationCleanupOwner(CleanupId);
	}
	if (Context.Phase == ESWVoyagePhase::Restore)
	{
		bRestoreStageStarted = false; bRestoreStageCompleted = false;
		return BeginRestoreStage(ESWVoyageRestoreStage::AuthoredActors, OutError);
	}
	if (Context.Phase == ESWVoyagePhase::Load)
	{
		if (RequiredGcSerial && ObservedGcSerial < RequiredGcSerial) { OutError = TEXT("VoyageLoadBeforePurge"); return false; }
		GameplayStreaming->SetShouldBeLoaded(true); GameplayStreaming->SetShouldBeVisible(true);
	}
	if (Context.Phase == ESWVoyagePhase::Release)
	{
		bBlocked = false; bPreparationSpawnAllowed = false;
		UpdateAsyncGuard();
		for (const TWeakObjectPtr<UObject>& Object : OrderedParticipants)
			if (Object.IsValid() && !ResumedParticipants.Contains(Object))
			{ ISWVoyageResetParticipant::Execute_ResumeVoyage(Object.Get(), Context); ResumedParticipants.Add(Object); }
		RestoreLocalPause();
	}
	if (bBlocked) MaintainLocalPause(); return true;
}

ESWVoyageStepResult USWVoyageResetSubsystem::PollParticipants(FString& OutError)
{
	const bool bReverse = Context.Phase == ESWVoyagePhase::Quiesce || Context.Phase == ESWVoyagePhase::Unload;
	for (int32 Index = 0; Index < OrderedParticipants.Num(); ++Index)
	{
		const TWeakObjectPtr<UObject> Object = OrderedParticipants[bReverse ? OrderedParticipants.Num() - Index - 1 : Index];
		if (!Object.IsValid() || CompletedParticipants.Contains(Object)) continue;
		if (Context.Phase == ESWVoyagePhase::Restore && ISWVoyageResetParticipant::Execute_GetVoyageRestoreStage(Object.Get()) > Context.RestoreStage) continue;
		ESWVoyageStepResult Result = ESWVoyageStepResult::Succeeded;
		if (ISWVoyageResetParticipant::Execute_GetVoyagePolicy(Object.Get()) != ESWVoyagePolicy::Preserve)
		{
			switch (Context.Phase)
			{
			case ESWVoyagePhase::Quiesce: PreparedParticipants.Add(Object); Result = ISWVoyageResetParticipant::Execute_PrepareVoyageReset(Object.Get(), Context, OutError); break;
			case ESWVoyagePhase::Unload: Result = ISWVoyageResetParticipant::Execute_ResetVoyageTransientState(Object.Get(), Context, OutError); break;
			case ESWVoyagePhase::Restore:
			{
				FSWVoyageResetContext ObjectContext = Context;
				ObjectContext.RestoreStage = ISWVoyageResetParticipant::Execute_GetVoyageRestoreStage(Object.Get());
				if (!PreparedParticipants.Contains(Object))
				{
					const AActor* Owner = Cast<AActor>(Object.Get());
					if (const UActorComponent* Component = Cast<UActorComponent>(Object.Get())) Owner = Component->GetOwner();
					if (Owner && !IsCurrentGeneration(GetActorGeneration(Owner)))
					{ OutError = TEXT("VoyageLateParticipantGenerationInvalid:") + Object->GetPathName(); return ESWVoyageStepResult::Failed; }
					Result = ISWVoyageResetParticipant::Execute_PrepareVoyageReset(Object.Get(), ObjectContext, OutError);
					if (Result != ESWVoyageStepResult::Succeeded)
					{
						if (Result == ESWVoyageStepResult::Pending) OutError.Reset();
						else if (OutError.IsEmpty()) OutError = TEXT("VoyageLateParticipantPrepareFailed:") + Object->GetPathName();
						return Result;
					}
					PreparedParticipants.Add(Object);
				}
				Result = ISWVoyageResetParticipant::Execute_RestoreVoyageState(Object.Get(), ObjectContext, OutError); break;
			}
			case ESWVoyagePhase::ClientReady: Result = ISWVoyageResetParticipant::Execute_IsVoyageReady(Object.Get(), Context, OutError); break;
			default: break;
			}
		}
		if (Result != ESWVoyageStepResult::Succeeded)
		{
			if (Result == ESWVoyageStepResult::Pending) OutError.Reset();
			if (Result == ESWVoyageStepResult::Failed && OutError.IsEmpty()) OutError = TEXT("VoyageParticipantFailed:") + Object->GetPathName();
			return Result;
		}
		CompletedParticipants.Add(Object);
	}
	OutError.Reset(); return ESWVoyageStepResult::Succeeded;
}

ESWVoyageStepResult USWVoyageResetSubsystem::PollLocalPhase(FString& OutError)
{
	check(IsInGameThread()); OutError.Reset();
	if (!RegistrationFailure.IsEmpty()) { OutError = RegistrationFailure; return ESWVoyageStepResult::Failed; }
	if (!GameplayStreaming.IsValid()) { OutError = TEXT("VoyageStreamingMissing"); return ESWVoyageStepResult::Failed; }
	const ESWVoyageStepResult Result = PollParticipants(OutError);
	if (Result != ESWVoyageStepResult::Succeeded) return Result;
	if (Context.Phase == ESWVoyagePhase::Unload || Context.Phase == ESWVoyagePhase::Purge)
	{
		if (!bUnloadIssued)
		{
			for (const TWeakObjectPtr<AActor>& Old : OldActors)
				if (Old.IsValid() && Old->GetLevel() != OldLevel) Old->Destroy();
			GameplayStreaming->SetShouldBeVisible(false); GameplayStreaming->SetShouldBeLoaded(false); bUnloadIssued = true;
		}
		if (GameplayStreaming->GetLoadedLevel() || GameplayStreaming->IsLevelVisible()) return ESWVoyageStepResult::Pending;
		if (!RequiredGcSerial)
		{
			for (const TWeakObjectPtr<AActor>& Actor : OldActors) Actors.Remove(Actor);
			RequiredGcSerial = ObservedGcSerial + 1; GEngine->ForceGarbageCollection(true);
			return ESWVoyageStepResult::Pending;
		}
		if (ObservedGcSerial < RequiredGcSerial || OldLevel.IsValid()) return ESWVoyageStepResult::Pending;
		for (const TWeakObjectPtr<AActor>& Actor : OldActors) if (Actor.IsValid()) return ESWVoyageStepResult::Pending;
		if (UNetDriver* Driver = GetWorld()->GetNetDriver())
			for (const FNetworkGUID& Guid : Driver->GetDestroyedStartupOrDormantActors(Context.GameplayPackage))
				if (OldStaticGuids.Contains(Guid.Value)) { OutError = TEXT("VoyageOldStaticNetGuidRetained"); return ESWVoyageStepResult::Failed; }
	}
	if (Context.Phase == ESWVoyagePhase::Load)
	{
		ULevel* Level = GameplayStreaming->GetLoadedLevel();
		if (!Level || !GameplayStreaming->IsLevelVisible() || !Level->bIsVisible) return ESWVoyageStepResult::Pending;
		if (!AuditWorldActors(OutError)) return ESWVoyageStepResult::Failed;
	}
	if (Context.Phase == ESWVoyagePhase::Restore)
	{
		const ESWVoyageStepResult RestoreResult = PollRestoreStage(OutError);
		if (RestoreResult != ESWVoyageStepResult::Succeeded) return RestoreResult;
		if (!Context.bAuthority && Context.RestoreStage != ESWVoyageRestoreStage::Readiness)
		{
			if (!BeginRestoreStage(static_cast<ESWVoyageRestoreStage>(static_cast<uint8>(Context.RestoreStage) + 1), OutError)) return ESWVoyageStepResult::Failed;
			return ESWVoyageStepResult::Pending;
		}
	}
	return ESWVoyageStepResult::Succeeded;
}

bool USWVoyageResetSubsystem::BeginRestoreStage(ESWVoyageRestoreStage Stage, FString& OutError)
{
	check(IsInGameThread()); OutError.Reset();
	if (Context.Phase != ESWVoyagePhase::Restore) { OutError = TEXT("VoyageRestorePhaseRequired"); return false; }
	if (bRestoreStageStarted && Stage == Context.RestoreStage) return true;
	if ((!bRestoreStageStarted && Stage != ESWVoyageRestoreStage::AuthoredActors)
		|| (bRestoreStageStarted && (!bRestoreStageCompleted || static_cast<uint8>(Stage) != static_cast<uint8>(Context.RestoreStage) + 1)))
	{ OutError = TEXT("VoyageRestoreStageOrderInvalid"); return false; }
	if (!BuildParticipantOrder(OutError)) return false;
	Context.RestoreStage = Stage; bRestoreStageStarted = true; bRestoreStageCompleted = false;
	return true;
}

ESWVoyageStepResult USWVoyageResetSubsystem::PollRestoreStage(FString& OutError)
{
	check(IsInGameThread()); OutError.Reset();
	if (!bRestoreStageStarted || Context.Phase != ESWVoyagePhase::Restore)
	{ OutError = TEXT("VoyageRestoreStageNotStarted"); return ESWVoyageStepResult::Failed; }
	if (!BuildParticipantOrder(OutError)) return ESWVoyageStepResult::Failed;
	const ESWVoyageStepResult Result = PollParticipants(OutError);
	bRestoreStageCompleted = Result == ESWVoyageStepResult::Succeeded;
	return Result;
}

bool USWVoyageResetSubsystem::AuditWorldActors(FString& OutError)
{
	check(IsInGameThread()); OutError.Reset();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor) || Actor == LocalPauseSentinel || Actor->IsA<AWorldSettings>() || Actor->IsA<AController>()
			|| Actor->IsA<APlayerState>() || Actor->IsA<AGameModeBase>() || Actor->IsA<AGameStateBase>() || Actor->IsA<ABrush>()) continue;
		int32 Declared = ResolveVoyageLifetimeTag(Actor);
		if (Declared == -2) { OutError = TEXT("VoyageActorLifetimeAmbiguous:") + Actor->GetPathName(); return false; }
		const FActorRegistration* Existing = Actors.Find(Actor);
		if (Existing)
		{
			if (Declared != INDEX_NONE && Existing->Lifetime != static_cast<ESWVoyageActorLifetime>(Declared))
			{ OutError = TEXT("VoyageActorRegistrationConflict:") + Actor->GetPathName(); return false; }
			if (Existing->Generation != ActorGeneration && (Existing->Lifetime == ESWVoyageActorLifetime::Voyage || Existing->Lifetime == ESWVoyageActorLifetime::PlayerLife))
			{ OutError = TEXT("VoyageOldActorRetained:") + Actor->GetPathName(); return false; }
			continue;
		}
		if (Declared == INDEX_NONE && GameplayStreaming.IsValid() && Actor->GetLevel() == GameplayStreaming->GetLoadedLevel()
			&& USWRoomSnapshotComponent::IsAuthoredRoomActor(Actor)) Declared = static_cast<int32>(ESWVoyageActorLifetime::Voyage);
		if (Declared != INDEX_NONE)
		{
			if (!RegisterActor(Actor, static_cast<ESWVoyageActorLifetime>(Declared), ActorGeneration, OutError)) return false;
			continue;
		}
		const UClass* NativeClass = Actor->GetClass();
		while (NativeClass && !NativeClass->HasAnyClassFlags(CLASS_Native)) NativeClass = NativeClass->GetSuperClass();
		bool bPhysics = false;
		TInlineComponentArray<UPrimitiveComponent*> Primitives(Actor);
		for (UPrimitiveComponent* Primitive : Primitives) bPhysics |= Primitive && Primitive->IsSimulatingPhysics();
		if (Profile && (Actor->GetIsReplicated() || bPhysics || (NativeClass && Profile->ProjectScriptPackages.Contains(NativeClass->GetOutermost()->GetFName()))))
		{ OutError = TEXT("VoyagePersistentActorUndeclared:") + Actor->GetPathName(); return false; }
	}
	return true;
}
