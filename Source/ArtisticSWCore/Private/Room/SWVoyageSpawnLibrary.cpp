#include "Room/SWVoyageSpawnLibrary.h"
#include "Room/SWVoyageResetSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Containers/Ticker.h"

namespace
{
struct FDeferredSpawnRegistration
{
	TWeakObjectPtr<UWorld> World;
	ESWVoyageActorLifetime Lifetime = ESWVoyageActorLifetime::Voyage;
	int32 Generation = 0;
	FGuid PresentationCleanupId;
	ESWVoyageSpawnFinishState State = ESWVoyageSpawnFinishState::Deferred;
};
TMap<TWeakObjectPtr<AActor>, FDeferredSpawnRegistration> DeferredSpawns;
TSet<TWeakObjectPtr<UWorld>> ClosingWorlds;
FTSTicker::FDelegateHandle SpawnSweepHandle;
FDelegateHandle SpawnWorldCleanupHandle;

bool IsSpawnObjectValid(const UObject* Object)
{
	if (!IsValid(Object) || Object->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)) return false;
	const AActor* Actor = Cast<AActor>(Object);
	return !Actor || !Actor->IsActorBeingDestroyed();
}

void SweepSpawnTracking()
{
	check(IsInGameThread());
	for (auto It = DeferredSpawns.CreateIterator(); It; ++It)
		if (!It.Key().IsValid() || !It.Value().World.IsValid()) It.RemoveCurrent();
	for (auto It = ClosingWorlds.CreateIterator(); It; ++It)
		if (!It->IsValid()) It.RemoveCurrent();
}

void InstallSpawnTracking()
{
	if (!SpawnSweepHandle.IsValid())
		SpawnSweepHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
		{ SweepSpawnTracking(); return true; }), 1.f);
	if (!SpawnWorldCleanupHandle.IsValid())
		SpawnWorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddLambda([](UWorld* World, bool, bool)
		{
			check(IsInGameThread()); ClosingWorlds.Add(World);
			for (auto It = DeferredSpawns.CreateIterator(); It; ++It)
				if (It.Value().World.Get() == World) It.RemoveCurrent();
		});
}

bool ValidateTrackedContract(AActor* Actor, const FDeferredSpawnRegistration& Registration)
{
	if (!IsSpawnObjectValid(Actor) || !Registration.World.IsValid()
		|| Actor->GetWorld() != Registration.World.Get() || ClosingWorlds.Contains(Registration.World)) return false;
	UWorld* World = Registration.World.Get();
	USWVoyageResetSubsystem* Voyage = World->GetSubsystem<USWVoyageResetSubsystem>();
	if (!Voyage)
		return !World->IsGameWorld() && Registration.Lifetime == ESWVoyageActorLifetime::LocalPresentation
			&& Registration.Generation == 0 && !Registration.PresentationCleanupId.IsValid() && !Actor->GetIsReplicated();
	ESWVoyageActorLifetime Lifetime;
	FGuid CleanupId;
	if (!Voyage->GetActorLifetime(Actor, Lifetime) || !Voyage->GetActorPresentationCleanupId(Actor, CleanupId) || Lifetime != Registration.Lifetime
		|| Voyage->GetActorGeneration(Actor) != Registration.Generation) return false;
	if (Lifetime == ESWVoyageActorLifetime::LocalPresentation && Actor->GetIsReplicated()) return false;
	return CleanupId == Registration.PresentationCleanupId
		&& Voyage->CanSpawnVoyageActor(Lifetime, Registration.Generation, CleanupId);
}

ESWVoyageSpawnFinishState BeginTrackedFinish(AActor* Actor)
{
	FDeferredSpawnRegistration* Registration = DeferredSpawns.Find(Actor);
	if (!Registration) return ESWVoyageSpawnFinishState::Untracked;
	const ESWVoyageSpawnFinishState Previous = Registration->State;
	if (Previous == ESWVoyageSpawnFinishState::Deferred) Registration->State = ESWVoyageSpawnFinishState::Finishing;
	return Previous;
}

bool CompleteTrackedFinish(AActor* Actor)
{
	FDeferredSpawnRegistration* Registration = DeferredSpawns.Find(Actor);
	if (!Registration || Registration->State != ESWVoyageSpawnFinishState::Finishing) return false;
	Registration->State = ESWVoyageSpawnFinishState::Finished; return true;
}

void AbortTrackedSpawn(AActor* Actor)
{
	if (FDeferredSpawnRegistration* Registration = DeferredSpawns.Find(Actor)) Registration->State = ESWVoyageSpawnFinishState::Aborted;
}
}

bool FSWVoyageSpawn::RegisterDeferredActorSpawn(AActor* Actor, ESWVoyageActorLifetime Lifetime,
	int32 ExpectedGeneration, const FGuid& PresentationCleanupId, FString& OutError)
{
	check(IsInGameThread()); OutError.Reset();
	UWorld* World = IsSpawnObjectValid(Actor) ? Actor->GetWorld() : nullptr;
	if (!IsSpawnObjectValid(World) || ClosingWorlds.Contains(World))
	{ OutError = TEXT("VoyageDeferredSpawnWorldInvalid"); return false; }
	if (const FDeferredSpawnRegistration* Existing = DeferredSpawns.Find(Actor))
	{
		if (Existing->World.Get() != World || Existing->Lifetime != Lifetime || Existing->Generation != ExpectedGeneration
			|| Existing->PresentationCleanupId != PresentationCleanupId || Existing->State != ESWVoyageSpawnFinishState::Deferred)
		{ OutError = TEXT("VoyageDeferredSpawnRegistrationConflict"); return false; }
	}
	USWVoyageResetSubsystem* Voyage = World->GetSubsystem<USWVoyageResetSubsystem>();
	if (Voyage)
	{
		if (!Voyage->CanSpawnVoyageActor(Lifetime, ExpectedGeneration, PresentationCleanupId)
			|| (Lifetime == ESWVoyageActorLifetime::LocalPresentation && Actor->GetIsReplicated()))
		{ OutError = TEXT("VoyageDeferredSpawnContractInvalid"); return false; }
		if (!Voyage->RegisterActor(Actor, Lifetime, ExpectedGeneration, PresentationCleanupId, OutError)) return false;
	}
	else if (World->IsGameWorld() || Lifetime != ESWVoyageActorLifetime::LocalPresentation || ExpectedGeneration != 0
		|| PresentationCleanupId.IsValid() || Actor->GetIsReplicated())
	{ OutError = TEXT("VoyageDeferredPreviewContractInvalid"); return false; }
	FDeferredSpawnRegistration Registration;
	Registration.World = World; Registration.Lifetime = Lifetime; Registration.Generation = ExpectedGeneration;
	Registration.PresentationCleanupId = PresentationCleanupId;
	DeferredSpawns.Add(Actor, Registration); InstallSpawnTracking(); return true;
}

ESWVoyageSpawnFinishState FSWVoyageSpawn::GetActorSpawnFinishState(const AActor* Actor)
{
	check(IsInGameThread());
	if (!IsSpawnObjectValid(Actor)) return ESWVoyageSpawnFinishState::Untracked;
	const FDeferredSpawnRegistration* Registration = DeferredSpawns.Find(TWeakObjectPtr<AActor>(const_cast<AActor*>(Actor)));
	return Registration ? Registration->State : ESWVoyageSpawnFinishState::Untracked;
}

void FSWVoyageSpawn::ShutdownSpawnTracking()
{
	check(IsInGameThread());
	FTSTicker::GetCoreTicker().RemoveTicker(SpawnSweepHandle); SpawnSweepHandle.Reset();
	FWorldDelegates::OnWorldCleanup.Remove(SpawnWorldCleanupHandle); SpawnWorldCleanupHandle.Reset();
	DeferredSpawns.Empty(); ClosingWorlds.Empty();
}

AActor* FSWVoyageSpawn::BeginSpawn(UWorld* World, UClass* Class, const FTransform& Transform, AActor* Owner, APawn* Instigator,
	ESpawnActorCollisionHandlingMethod Collision, ESWVoyageActorLifetime Lifetime, int32 ExpectedGeneration)
{
	return BeginSpawn(World, Class, Transform, Owner, Instigator, Collision, Lifetime, ExpectedGeneration, FGuid());
}

AActor* FSWVoyageSpawn::BeginSpawn(UWorld* World, UClass* Class, const FTransform& Transform, AActor* Owner, APawn* Instigator,
	ESpawnActorCollisionHandlingMethod Collision, ESWVoyageActorLifetime Lifetime, int32 ExpectedGeneration, const FGuid& PresentationCleanupId)
{
	check(IsInGameThread());
	if (!IsSpawnObjectValid(World) || ClosingWorlds.Contains(World) || !Class || !Class->IsChildOf(AActor::StaticClass()) || Transform.ContainsNaN()) return nullptr;
	USWVoyageResetSubsystem* Voyage = World->GetSubsystem<USWVoyageResetSubsystem>();
	if (Voyage ? !Voyage->CanSpawnVoyageActor(Lifetime, ExpectedGeneration, PresentationCleanupId)
		: (World->IsGameWorld() || Lifetime != ESWVoyageActorLifetime::LocalPresentation || ExpectedGeneration != 0)) return nullptr;
	FActorSpawnParameters Parameters;
	Parameters.Owner = Owner; Parameters.Instigator = Instigator; Parameters.SpawnCollisionHandlingOverride = Collision;
	Parameters.bDeferConstruction = true;
	ULevelStreaming* Gameplay = Voyage ? Voyage->GetGameplayStreamingLevel() : nullptr;
	if (Voyage && Voyage->IsActiveVoyageSession() && Lifetime == ESWVoyageActorLifetime::Voyage)
	{
		Parameters.OverrideLevel = Gameplay ? Gameplay->GetLoadedLevel() : nullptr;
		if (!Parameters.OverrideLevel) return nullptr;
	}
	else if (Voyage && Voyage->IsActiveVoyageSession()
		&& (Lifetime == ESWVoyageActorLifetime::PlayerLife || Lifetime == ESWVoyageActorLifetime::SharedService || Lifetime == ESWVoyageActorLifetime::LocalPresentation))
		Parameters.OverrideLevel = World->PersistentLevel;
	AActor* Actor = World->SpawnActor<AActor>(Class, Transform, Parameters);
	const TWeakObjectPtr<AActor> ActorWeak = Actor;
	if (!IsSpawnObjectValid(ActorWeak.Get())) return nullptr;
	FString Error;
	if (!RegisterDeferredActorSpawn(ActorWeak.Get(), Lifetime, ExpectedGeneration, PresentationCleanupId, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("Voyage BeginSpawn failed: %s"), *Error);
		if (IsSpawnObjectValid(ActorWeak.Get())) ActorWeak->Destroy();
		return nullptr;
	}
	return ActorWeak.Get();
}

AActor* USWVoyageSpawnLibrary::BeginVoyageActorSpawn(UObject* WorldContextObject, TSubclassOf<AActor> Class, FTransform Transform,
	AActor* Owner, APawn* Instigator, ESpawnActorCollisionHandlingMethod Collision, ESWVoyageActorLifetime Lifetime, int32 ExpectedGeneration)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return FSWVoyageSpawn::BeginSpawn(World, Class.Get(), Transform, Owner, Instigator, Collision, Lifetime, ExpectedGeneration);
}

AActor* USWVoyageSpawnLibrary::FinishVoyageActorSpawn(AActor* Actor, FTransform Transform)
{
	check(IsInGameThread());
	if (!IsSpawnObjectValid(Actor)) return nullptr;
	FDeferredSpawnRegistration Registration;
	{
		const FDeferredSpawnRegistration* Found = DeferredSpawns.Find(Actor);
		if (!Found)
		{
			UE_LOG(LogTemp, Error, TEXT("Voyage Finish rejected untracked Actor: %s"), *Actor->GetPathName()); return nullptr;
		}
		Registration = *Found;
	}
	if (Registration.State == ESWVoyageSpawnFinishState::Finishing || Registration.State == ESWVoyageSpawnFinishState::Aborted) return nullptr;
	const TWeakObjectPtr<AActor> ActorWeak = Actor;
	const auto Abort = [&ActorWeak]()
	{
		if (AActor* Current = ActorWeak.Get())
		{
			AbortTrackedSpawn(Current);
			if (IsSpawnObjectValid(Current)) Current->Destroy();
		}
	};
	if (Transform.ContainsNaN() || !ValidateTrackedContract(Actor, Registration)) { Abort(); return nullptr; }
	if (Registration.State == ESWVoyageSpawnFinishState::Finished) return Actor;
	if (BeginTrackedFinish(Actor) != ESWVoyageSpawnFinishState::Deferred) return nullptr;
	Actor->FinishSpawning(Transform);
	AActor* Current = ActorWeak.Get();
	if (!IsSpawnObjectValid(Current) || FSWVoyageSpawn::GetActorSpawnFinishState(Current) != ESWVoyageSpawnFinishState::Finishing
		|| !ValidateTrackedContract(Current, Registration) || !CompleteTrackedFinish(Current))
	{ Abort(); return nullptr; }
	return Current;
}

AActor* USWVoyageSpawnLibrary::BeginVoyageLocalPresentationSpawn(UObject* WorldContextObject, TSubclassOf<AActor> Class,
	FTransform Transform, AActor* Owner, APawn* Instigator, ESpawnActorCollisionHandlingMethod Collision,
	int32 ExpectedGeneration, FGuid PresentationCleanupId)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return FSWVoyageSpawn::BeginSpawn(World, Class.Get(), Transform, Owner, Instigator, Collision,
		ESWVoyageActorLifetime::LocalPresentation, ExpectedGeneration, PresentationCleanupId);
}
int32 USWVoyageSpawnLibrary::GetActorVoyageGeneration(AActor* Actor)
{
	USWVoyageResetSubsystem* Voyage = IsValid(Actor) && Actor->GetWorld() ? Actor->GetWorld()->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	return Voyage ? Voyage->GetActorGeneration(Actor) : -1;
}
bool USWVoyageSpawnLibrary::IsActorFromCurrentVoyage(AActor* Actor)
{
	const int32 Generation = GetActorVoyageGeneration(Actor);
	USWVoyageResetSubsystem* Voyage = IsValid(Actor) && Actor->GetWorld() ? Actor->GetWorld()->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	return Generation >= 0 && Voyage && Voyage->IsCurrentGeneration(Generation);
}
bool USWVoyageSpawnLibrary::IsVoyageGameplayBlocked(UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	USWVoyageResetSubsystem* Voyage = World ? World->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	return Voyage && Voyage->IsGameplayBlocked();
}

bool USWVoyageSpawnLibrary::IsActorVoyageGameplayBlocked(AActor* Actor)
{
	USWVoyageResetSubsystem* Voyage = IsValid(Actor) && Actor->GetWorld() ? Actor->GetWorld()->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	return Voyage && Voyage->IsActiveVoyageSession()
		&& (Voyage->IsGameplayBlocked() || !Voyage->IsCurrentGeneration(Voyage->GetActorGeneration(Actor)));
}
