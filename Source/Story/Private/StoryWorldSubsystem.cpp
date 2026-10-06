#include "StoryWorldSubsystem.h"

#include "EngineUtils.h"
#include "StorySubsystem.h"
#include "StoryStateReplicator.h"
#include "Room/SWVoyageResetSubsystem.h"
#include "Room/SWVoyageSpawnLibrary.h"

FName UStoryWorldSubsystem::GetVoyageParticipantId_Implementation() const
{
	USWVoyageResetSubsystem* Voyage = GetWorld() ? GetWorld()->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	return Voyage ? Voyage->ResolveParticipantId(const_cast<UStoryWorldSubsystem*>(this)) : NAME_None;
}

bool UStoryWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World
		&& (World->WorldType == EWorldType::Game
			|| World->WorldType == EWorldType::PIE
			|| World->WorldType == EWorldType::GamePreview);
}

void UStoryWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (InWorld.GetNetMode() == NM_Client)
	{
		return;
	}

	if (UGameInstance* GameInstance = InWorld.GetGameInstance())
	{
		if (UStorySubsystem* Story = GameInstance->GetSubsystem<UStorySubsystem>())
		{
			Story->InitializeConfiguredProgress();
		}
	}

	for (TActorIterator<AStoryStateReplicator> It(&InWorld); It; ++It)
	{
		Replicator = *It;
		return;
	}

	FActorSpawnParameters Params;
	Params.Name = TEXT("StoryStateReplicator");
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.OverrideLevel = InWorld.PersistentLevel;
	Params.bDeferConstruction = true;
	Replicator = InWorld.SpawnActor<AStoryStateReplicator>(
		AStoryStateReplicator::StaticClass(),
		FTransform::Identity,
		Params);
	if (Replicator)
	{
		const USWVoyageResetSubsystem* Voyage = InWorld.GetSubsystem<USWVoyageResetSubsystem>();
		FString Error;
		if (!FSWVoyageSpawn::RegisterDeferredActorSpawn(Replicator, ESWVoyageActorLifetime::SharedService,
			Voyage ? Voyage->GetGeneration() : 0, FGuid(), Error)
			|| USWVoyageSpawnLibrary::FinishVoyageActorSpawn(Replicator, FTransform::Identity) != Replicator)
		{
			UE_LOG(LogTemp, Error, TEXT("Story shared replicator creation failed: %s"), *Error);
			if (IsValid(Replicator)) Replicator->Destroy();
			Replicator = nullptr;
		}
	}
}
