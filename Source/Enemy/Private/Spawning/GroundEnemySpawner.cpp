#include "Spawning/GroundEnemySpawner.h"

#include "AI/EnemyTerritoryComponent.h"
#include "BaseEnemy.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "NavigationSystem.h"
#include "Spawning/EnemySpawnCatalog.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "Room/SWRoomSnapshotSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogGroundEnemySpawner, Log, All);

AGroundEnemySpawner::AGroundEnemySpawner()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	CreateDefaultSubobject<USWRoomSnapshotComponent>(TEXT("RoomSnapshot"));

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AGroundEnemySpawner::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && bAutoSpawnOnBeginPlay
		&& !GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>()->IsRestoringSnapshot())
	{
		SpawnConfiguredEnemies();
	}
}

int32 AGroundEnemySpawner::SpawnConfiguredEnemies()
{
	if (!HasAuthority() || GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>()->IsRestoringSnapshot()
		|| !ValidateConfiguration())
	{
		return 0;
	}

	int32 SpawnedCount = 0;
	bConfiguredSpawnCommitted = true;
	for (const FGroundEnemySpawnEntry& Request : SpawnEntries)
	{
		for (int32 Index = 0; Index < Request.Count; ++Index)
		{
			ABaseEnemy* SpawnedEnemy = nullptr;
			if (SpawnOneEnemy(Request, SpawnedEnemy))
			{
				++SpawnedCount;
			}
		}
	}

	return SpawnedCount;
}

int32 AGroundEnemySpawner::GetTrackedEnemyCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<ABaseEnemy>& Enemy : TrackedEnemies)
	{
		Count += Enemy.IsValid() ? 1 : 0;
	}
	return Count;
}

bool AGroundEnemySpawner::ValidateConfiguration() const
{
	if (!SpawnCatalog || SpawnEntries.IsEmpty())
	{
		UE_LOG(LogGroundEnemySpawner, Error,
			TEXT("Spawner %s requires a catalog and at least one spawn entry."), *GetName());
		return false;
	}

	if (SpawnRadius > PatrolRadius || PatrolRadius > CombatRadius)
	{
		UE_LOG(LogGroundEnemySpawner, Error,
			TEXT("Spawner %s requires SpawnRadius <= PatrolRadius <= CombatRadius."), *GetName());
		return false;
	}

	for (const FGroundEnemySpawnEntry& Request : SpawnEntries)
	{
		FEnemySpawnCatalogEntry Definition;
		if (!Request.EnemyTypeTag.IsValid() || Request.Count <= 0
			|| !SpawnCatalog->FindDefinition(Request.EnemyTypeTag, Definition))
		{
			UE_LOG(LogGroundEnemySpawner, Error,
				TEXT("Spawner %s has an invalid or unmapped tag: %s."),
				*GetName(), *Request.EnemyTypeTag.ToString());
			return false;
		}

		const ABaseEnemy* EnemyCDO = Definition.EnemyClass->GetDefaultObject<ABaseEnemy>();
		if (EnemyCDO && EnemyCDO->GetEnemyTypeTag().IsValid()
			&& EnemyCDO->GetEnemyTypeTag() != Request.EnemyTypeTag)
		{
			UE_LOG(LogGroundEnemySpawner, Error,
				TEXT("Catalog tag %s does not match class %s tag %s."),
				*Request.EnemyTypeTag.ToString(),
				*GetNameSafe(Definition.EnemyClass.Get()),
				*EnemyCDO->GetEnemyTypeTag().ToString());
			return false;
		}
	}

	return true;
}

bool AGroundEnemySpawner::SpawnOneEnemy(
	const FGroundEnemySpawnEntry& Request,
	ABaseEnemy*& OutEnemy)
{
	OutEnemy = nullptr;

	FEnemySpawnCatalogEntry Definition;
	if (!SpawnCatalog || !SpawnCatalog->FindDefinition(Request.EnemyTypeTag, Definition))
	{
		return false;
	}

	FTransform SpawnTransform;
	if (!FindSpawnTransform(Definition.EnemyClass, SpawnTransform))
	{
		UE_LOG(LogGroundEnemySpawner, Warning,
			TEXT("Spawner %s found no NavMesh placement for %s."),
			*GetName(), *Request.EnemyTypeTag.ToString());
		return false;
	}

	UWorld* World = GetWorld();
	ABaseEnemy* Enemy = World ? World->SpawnActorDeferred<ABaseEnemy>(
		Definition.EnemyClass,
		SpawnTransform,
		this,
		nullptr,
		SpawnCollisionPolicy) : nullptr;

	if (!Enemy)
	{
		return false;
	}

	if (!Enemy->ConfigureSpawnTypeTag(Request.EnemyTypeTag)
		|| !Enemy->ConfigureSpawnBalance(
			Definition.StatsRow,
			Request.HealthMultiplier,
			Request.SpeedMultiplier))
	{
		Enemy->Destroy();
		return false;
	}

	if (UEnemyTerritoryComponent* Territory = Enemy->GetTerritoryComponent())
	{
		Territory->InitializeTerritory(GetActorLocation(), PatrolRadius, CombatRadius);
	}

	Enemy->FinishSpawning(SpawnTransform);
	if (!IsValid(Enemy) || !Enemy->IsBalanceReady())
	{
		if (IsValid(Enemy))
		{
			Enemy->Destroy();
		}
		return false;
	}

	if (!Enemy->GetController())
	{
		Enemy->SpawnDefaultController();
	}

	TrackedEnemies.Add(Enemy);
	Enemy->OnBaseEnemyDeathNotified.AddUniqueDynamic(
		this, &AGroundEnemySpawner::HandleTrackedEnemyRemoved);
	Enemy->OnDestroyed.AddUniqueDynamic(
		this, &AGroundEnemySpawner::HandleTrackedEnemyDestroyed);
	OnEnemySpawned.Broadcast(Enemy, Request.EnemyTypeTag);
	OutEnemy = Enemy;
	return true;
}

bool AGroundEnemySpawner::FindSpawnTransform(
	TSubclassOf<ABaseEnemy> EnemyClass,
	FTransform& OutTransform) const
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavigationSystem = World
		? UNavigationSystemV1::GetCurrent(World)
		: nullptr;
	const ABaseEnemy* EnemyCDO = EnemyClass
		? EnemyClass->GetDefaultObject<ABaseEnemy>()
		: nullptr;
	if (!NavigationSystem || !EnemyCDO)
	{
		return false;
	}

	const UCapsuleComponent* Capsule = EnemyCDO->GetCapsuleComponent();
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.0f;
	for (int32 Attempt = 0; Attempt < MaxPlacementAttemptsPerEnemy; ++Attempt)
	{
		FNavLocation NavLocation;
		if (NavigationSystem->GetRandomReachablePointInRadius(
			GetActorLocation(), SpawnRadius, NavLocation))
		{
			const FVector ActorLocation = NavLocation.Location + FVector(0.0f, 0.0f, HalfHeight);
			OutTransform = FTransform(GetActorRotation(), ActorLocation);
			return true;
		}
	}

	return false;
}

void AGroundEnemySpawner::HandleTrackedEnemyRemoved(
	ABaseEnemy* Enemy,
	EWaveEnemyRemoveReason Reason)
{
	if (!Enemy || TrackedEnemies.Remove(Enemy) == 0)
	{
		return;
	}

	Enemy->OnBaseEnemyDeathNotified.RemoveDynamic(
		this, &AGroundEnemySpawner::HandleTrackedEnemyRemoved);
	OnEnemyRemoved.Broadcast(Enemy);
}

void AGroundEnemySpawner::HandleTrackedEnemyDestroyed(AActor* DestroyedActor)
{
	ABaseEnemy* Enemy = Cast<ABaseEnemy>(DestroyedActor);
	if (!Enemy || TrackedEnemies.Remove(Enemy) == 0)
	{
		return;
	}

	OnEnemyRemoved.Broadcast(Enemy);
}

void AGroundEnemySpawner::CaptureRoomDomains(TArray<FSWRoomDomainPart>& OutParts, TArray<FSWRoomCaptureIssue>& OutIssues) const
{
	FSWRoomGroundSpawnerState State;
	State.CatalogPath = SpawnCatalog ? FSoftObjectPath(SpawnCatalog) : FSoftObjectPath();
	State.EntryCount = SpawnEntries.Num();
	State.bConfiguredSpawnCommitted = bConfiguredSpawnCommitted;
	for (const TWeakObjectPtr<ABaseEnemy>& Enemy : TrackedEnemies)
	{
		if (!Enemy.IsValid()) continue;
		if (const USWRoomSnapshotComponent* Id = Enemy->FindComponentByClass<USWRoomSnapshotComponent>(); Id && Id->StableId.IsValid())
			State.TrackedEnemyIds.Add(Id->StableId);
		else
		{
			FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
			Issue.Domain = TEXT("Spawner");
			Issue.FieldKey = FName(*(TEXT("TrackedEnemy:") + Enemy->GetPathName()));
			Issue.Reason = FString::Printf(TEXT("Tracked enemy has no stable ID: %s"), *Enemy->GetPathName());
		}
	}
	State.TrackedEnemyIds.Sort();
	FSWRoomDomainPart& Part = OutParts.AddDefaulted_GetRef();
	Part.Domain = ESWRoomDomain::Spawner;
	Part.Version = 1;
	if (!FSWRoomStructCodec::Write(State, Part.Bytes))
	{
		OutParts.Pop();
		FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
		Issue.Domain = TEXT("Spawner");
		Issue.FieldKey = TEXT("State");
		Issue.Reason = TEXT("Ground spawner state serialization failed");
	}
}

bool AGroundEnemySpawner::RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& OutError)
{
	FSWRoomGroundSpawnerState State;
	if (Part.Domain != ESWRoomDomain::Spawner || Part.Version != 1 || !FSWRoomStructCodec::Read(Part.Bytes, State))
	{
		OutError = TEXT("Unsupported ground spawner state");
		return false;
	}
	if (State.EntryCount != SpawnEntries.Num() || State.CatalogPath != (SpawnCatalog ? FSoftObjectPath(SpawnCatalog) : FSoftObjectPath()))
	{
		UE_LOG(LogGroundEnemySpawner, Warning,
			TEXT("Flow=RoomRestore Result=Partial Spawner=%s Domain=Spawner Field=Definition Reason=ChangedCatalogOrEntries"),
			*GetPathName());
		bConfiguredSpawnCommitted = !State.TrackedEnemyIds.IsEmpty();
		TrackedEnemies.Reset();
		PendingRoomState = MoveTemp(State);
		bHasPendingRoomState = true;
		return true;
	}
	bConfiguredSpawnCommitted = State.bConfiguredSpawnCommitted;
	TrackedEnemies.Reset();
	PendingRoomState = MoveTemp(State);
	bHasPendingRoomState = true;
	return true;
}

bool AGroundEnemySpawner::FinalizeRoomRestore(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError)
{
	if (!bHasPendingRoomState) return true;
	bHasPendingRoomState = false;
	for (const FGuid& Id : PendingRoomState.TrackedEnemyIds)
	{
		AActor* const* Found = RegisteredActors.Find(Id);
		ABaseEnemy* Enemy = Found ? Cast<ABaseEnemy>(*Found) : nullptr;
		if (!Enemy)
		{
			OutError = FString::Printf(TEXT("Ground spawner tracked enemy missing: %s"), *Id.ToString());
			return false;
		}
		TrackedEnemies.Add(Enemy);
		Enemy->OnBaseEnemyDeathNotified.AddUniqueDynamic(this, &AGroundEnemySpawner::HandleTrackedEnemyRemoved);
		Enemy->OnDestroyed.AddUniqueDynamic(this, &AGroundEnemySpawner::HandleTrackedEnemyDestroyed);
	}
	if (!bConfiguredSpawnCommitted && bAutoSpawnOnBeginPlay && GetWorld())
		GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			SpawnConfiguredEnemies();
		}));
	return true;
}
