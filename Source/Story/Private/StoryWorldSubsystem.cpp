#include "StoryWorldSubsystem.h"

#include "EngineUtils.h"
#include "StorySubsystem.h"
#include "StoryStateReplicator.h"

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
	Replicator = InWorld.SpawnActor<AStoryStateReplicator>(
		AStoryStateReplicator::StaticClass(),
		FTransform::Identity,
		Params);
}
