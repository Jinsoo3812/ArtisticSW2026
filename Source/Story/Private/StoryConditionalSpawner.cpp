#include "StoryConditionalSpawner.h"

#include "Components/SceneComponent.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "Room/SWRoomSnapshotSubsystem.h"
#include "Room/SWVoyageResetSubsystem.h"
#include "Room/SWVoyageSpawnLibrary.h"
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
	if (!USWVoyageSpawnLibrary::IsVoyageGameplayBlocked(this)
		&& !GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>()->IsRestoringSnapshot()) RefreshFromStory();
}

void AStoryConditionalSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(DeferredStoryRefreshTimer);
	ClearSpawnedActorBinding();
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
	USWVoyageResetSubsystem* Voyage = GetWorld() ? GetWorld()->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	if (Voyage && Voyage->IsGameplayBlocked() && !Voyage->IsPreparationSpawnAllowed()) return;
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
		bVoyageSpawnFailed = true;
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
			bVoyageSpawnFailed = true;
			return;
		}

		const FTransform Transform = GetActorTransform();
		AActor* NewActor = FSWVoyageSpawn::SpawnDeferred<AActor>(GetWorld(), ActorClass, Transform, this, nullptr,
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn, ESWVoyageActorLifetime::Voyage,
			Voyage ? Voyage->GetGeneration() : 0);
		if (NewActor && USWVoyageSpawnLibrary::FinishVoyageActorSpawn(NewActor, Transform) == NewActor && IsValid(NewActor))
		{
			SpawnedActor = NewActor;
			NewActor->OnDestroyed.AddUniqueDynamic(this, &AStoryConditionalSpawner::HandleSpawnedActorDestroyed);
			OnActorSpawned.Broadcast(NewActor);
		}
		else
		{
			bVoyageSpawnFailed = true;
			if (IsValid(NewActor)) NewActor->Destroy();
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
	if (USWVoyageSpawnLibrary::IsVoyageGameplayBlocked(this)) return;
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
	ClearSpawnedActorBinding();
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
	// Continue restores the recorded spawn outcome; it must not evaluate a new draw.
	USWVoyageResetSubsystem* Voyage = GetWorld() ? GetWorld()->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	if (!Voyage || !Voyage->IsActiveVoyageSession())
	{
		const int32 ExpectedGeneration = Voyage ? Voyage->GetGeneration() : 0;
		DeferredStoryRefreshTimer = GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, ExpectedGeneration]()
		{
			USWVoyageResetSubsystem* Current = GetWorld() ? GetWorld()->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
			if (Current && !Current->IsCurrentGeneration(ExpectedGeneration)) return;
			RefreshFromStory();
		}));
	}
	return true;
}

void AStoryConditionalSpawner::ClearSpawnedActorBinding()
{
	if (AActor* Actor = SpawnedActor.Get())
		Actor->OnDestroyed.RemoveDynamic(this, &AStoryConditionalSpawner::HandleSpawnedActorDestroyed);
	SpawnedActor.Reset();
}

FName AStoryConditionalSpawner::GetVoyageParticipantId_Implementation() const
{
	USWVoyageResetSubsystem* Voyage = GetWorld() ? GetWorld()->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	return Voyage ? Voyage->ResolveParticipantId(const_cast<AStoryConditionalSpawner*>(this)) : NAME_None;
}

ESWVoyageStepResult AStoryConditionalSpawner::PrepareVoyageReset_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	if (!bDeferredStoryRefreshPaused && GetWorldTimerManager().IsTimerActive(DeferredStoryRefreshTimer))
	{
		GetWorldTimerManager().PauseTimer(DeferredStoryRefreshTimer);
		bDeferredStoryRefreshPaused = true;
	}
	return ESWVoyageStepResult::Succeeded;
}

void AStoryConditionalSpawner::CancelVoyagePreparation_Implementation(const FSWVoyageResetContext& Context)
{
	if (bDeferredStoryRefreshPaused)
	{
		GetWorldTimerManager().UnPauseTimer(DeferredStoryRefreshTimer);
		bDeferredStoryRefreshPaused = false;
	}
}

ESWVoyageStepResult AStoryConditionalSpawner::ResetVoyageTransientState_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	GetWorldTimerManager().ClearTimer(DeferredStoryRefreshTimer);
	bDeferredStoryRefreshPaused = false;
	ClearSpawnedActorBinding();
	bSpawnOutcomeConsumed = false;
	bHasPendingRoomState = false;
	PendingRoomState = FSWRoomStorySpawnerState();
	bVoyageSpawnFailed = false;
	RestoredVoyageGeneration = INDEX_NONE;
	return ESWVoyageStepResult::Succeeded;
}

ESWVoyageStepResult AStoryConditionalSpawner::RestoreVoyageState_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	USWVoyageResetSubsystem* Voyage = GetWorld() ? GetWorld()->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	if (!Voyage || !Voyage->IsCurrentGeneration(Context.Generation))
	{
		OutError = TEXT("StorySpawnerGenerationMismatch");
		return ESWVoyageStepResult::Failed;
	}
	if (!Context.bAuthority || Context.bContinue) return ESWVoyageStepResult::Succeeded;
	if (RestoredVoyageGeneration != Context.Generation)
	{
		if (GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>()->IsRestoringSnapshot()
			|| (Voyage->IsGameplayBlocked() && !Voyage->IsPreparationSpawnAllowed())) return ESWVoyageStepResult::Pending;
		RestoredVoyageGeneration = Context.Generation;
		RefreshFromStory();
	}
	if (bVoyageSpawnFailed) OutError = TEXT("StorySpawnerRequiredStoryOrSpawnFailed");
	return bVoyageSpawnFailed ? ESWVoyageStepResult::Failed : ESWVoyageStepResult::Succeeded;
}

ESWVoyageStepResult AStoryConditionalSpawner::IsVoyageReady_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	USWVoyageResetSubsystem* Voyage = GetWorld() ? GetWorld()->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	if (!Voyage || !Voyage->IsCurrentGeneration(Context.Generation) || bVoyageSpawnFailed)
	{
		OutError = bVoyageSpawnFailed ? TEXT("StorySpawnerRequiredStoryOrSpawnFailed") : TEXT("StorySpawnerGenerationMismatch");
		return ESWVoyageStepResult::Failed;
	}
	if (SpawnedActor.IsValid() && !USWVoyageSpawnLibrary::IsActorFromCurrentVoyage(SpawnedActor.Get()))
	{
		OutError = TEXT("StorySpawnerSpawnedActorGenerationMismatch");
		return ESWVoyageStepResult::Failed;
	}
	return ESWVoyageStepResult::Succeeded;
}
