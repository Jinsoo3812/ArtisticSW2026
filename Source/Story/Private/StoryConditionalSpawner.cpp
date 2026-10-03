#include "StoryConditionalSpawner.h"

#include "Components/SceneComponent.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "Room/SWRoomSnapshotSubsystem.h"
#include "TimerManager.h"

AStoryConditionalSpawner::AStoryConditionalSpawner()
{
	CreateDefaultSubobject<USWRoomSnapshotComponent>(TEXT("RoomSnapshot"));
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
	if (!GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>()->IsRestoringSnapshot()) RefreshFromStory();
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
	if (!HasAuthority() || GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>()->IsRestoringSnapshot())
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

	if (bShouldExist && !bSpawnOutcomeConsumed && !SpawnedActor.IsValid())
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
		bSpawnOutcomeConsumed = true;
	}
	RefreshFromStory();
}

void AStoryConditionalSpawner::CaptureRoomDomains(TArray<FSWRoomDomainPart>& OutParts, TArray<FSWRoomCaptureIssue>& OutIssues) const
{
	FSWRoomStorySpawnerState State;
	State.SpawnedActorClass = FSoftClassPath(SpawnedActorClass.ToString());
	State.bSpawnOutcomeConsumed = bSpawnOutcomeConsumed;
	if (SpawnedActor.IsValid())
	{
		if (const USWRoomSnapshotComponent* Id = SpawnedActor->FindComponentByClass<USWRoomSnapshotComponent>())
			State.SpawnedActorId = Id->StableId;
		if (!State.SpawnedActorId.IsValid())
		{
			FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
			Issue.Domain = TEXT("Spawner");
			Issue.FieldKey = TEXT("SpawnedActorId");
			Issue.Reason = FString::Printf(TEXT("Story spawned actor has no stable ID: %s"), *SpawnedActor->GetPathName());
		}
	}
	FSWRoomDomainPart& Part = OutParts.AddDefaulted_GetRef();
	Part.Domain = ESWRoomDomain::Spawner;
	Part.Version = 1;
	if (!FSWRoomStructCodec::Write(State, Part.Bytes))
	{
		OutParts.Pop();
		FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
		Issue.Domain = TEXT("Spawner");
		Issue.FieldKey = TEXT("StoryState");
		Issue.Reason = TEXT("Story spawner serialization failed");
	}
}

bool AStoryConditionalSpawner::RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& OutError)
{
	FSWRoomStorySpawnerState State;
	if (Part.Domain != ESWRoomDomain::Spawner || Part.Version != 1 || !FSWRoomStructCodec::Read(Part.Bytes, State)
		|| State.SpawnedActorClass != FSoftClassPath(SpawnedActorClass.ToString()))
	{
		OutError = TEXT("Story spawner definition changed or state invalid");
		return false;
	}
	bSpawnOutcomeConsumed = State.bSpawnOutcomeConsumed;
	SpawnedActor.Reset();
	PendingRoomState = State;
	bHasPendingRoomState = true;
	return true;
}

bool AStoryConditionalSpawner::FinalizeRoomRestore(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError)
{
	if (!bHasPendingRoomState) return true;
	bHasPendingRoomState = false;
	if (PendingRoomState.SpawnedActorId.IsValid())
	{
		AActor* const* Found = RegisteredActors.Find(PendingRoomState.SpawnedActorId);
		if (!Found)
		{
			OutError = FString::Printf(TEXT("Story spawned actor missing: %s"), *PendingRoomState.SpawnedActorId.ToString());
			return false;
		}
		SpawnedActor = *Found;
		(*Found)->OnDestroyed.AddUniqueDynamic(this, &AStoryConditionalSpawner::HandleSpawnedActorDestroyed);
	}
	GetWorldTimerManager().SetTimerForNextTick(this, &AStoryConditionalSpawner::RefreshFromStory);
	return true;
}
