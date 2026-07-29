#include "StoryConditionalSpawner.h"

#include "Components/SceneComponent.h"

AStoryConditionalSpawner::AStoryConditionalSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
}

void AStoryConditionalSpawner::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority())
	{
		return;
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UStoryFacadeSubsystem* Story =
			GameInstance->GetSubsystem<UStoryFacadeSubsystem>())
		{
			Story->OnStoryChanged.AddUniqueDynamic(
				this,
				&AStoryConditionalSpawner::HandleStoryChanged);
		}
	}
	RefreshFromStory();
}

void AStoryConditionalSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UStoryFacadeSubsystem* Story =
			GameInstance->GetSubsystem<UStoryFacadeSubsystem>())
		{
			Story->OnStoryChanged.RemoveDynamic(
				this,
				&AStoryConditionalSpawner::HandleStoryChanged);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AStoryConditionalSpawner::RefreshFromStory()
{
	if (!HasAuthority())
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	const UStoryFacadeSubsystem* Story = GameInstance
		? GameInstance->GetSubsystem<UStoryFacadeSubsystem>()
		: nullptr;
	if (!Story)
	{
		return;
	}

	const bool bStoppedByCompletion =
		bStopAfterStoryNode && Story->IsStoryNodeReached(StopAfterStoryNode);
	const bool bShouldExist =
		Story->IsStoryNodeReached(RequiredStoryNode) && !bStoppedByCompletion;

	if (bShouldExist && !SpawnedActor.IsValid())
	{
		UClass* ActorClass = SpawnedActorClass.LoadSynchronous();
		if (!ActorClass)
		{
			return;
		}

		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AActor* NewActor = GetWorld()->SpawnActor<AActor>(ActorClass, GetActorTransform(), Params);
		if (NewActor)
		{
			SpawnedActor = NewActor;
			NewActor->OnDestroyed.AddUniqueDynamic(this, &AStoryConditionalSpawner::HandleSpawnedActorDestroyed);
			OnActorSpawned.Broadcast(NewActor);
		}
	}
	else if (!bShouldExist && !bStoppedByCompletion && SpawnedActor.IsValid())
	{
		// Campaign reset can close a gate. A completed boss is left alive long
		// enough for its own death animation and configured drops to finish.
		SpawnedActor->Destroy();
		SpawnedActor.Reset();
	}
}

void AStoryConditionalSpawner::HandleStoryChanged()
{
	RefreshFromStory();
}

void AStoryConditionalSpawner::HandleSpawnedActorDestroyed(AActor* DestroyedActor)
{
	if (SpawnedActor.Get() == DestroyedActor)
	{
		SpawnedActor.Reset();
	}
	RefreshFromStory();
}
