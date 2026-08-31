#include "DeckAI/DeckEnemySpawnerComponent.h"

#include "AI/BaseAIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "Engine/World.h"
#include "Ship.h"
#include "ShipAI/EnemyShip.h"
#include "TimerManager.h"

UDeckEnemySpawnerComponent::UDeckEnemySpawnerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void UDeckEnemySpawnerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Shutdown();
	Super::EndPlay(EndPlayReason);
}

void UDeckEnemySpawnerComponent::ConfigureLegacyFallback(
	bool bInEnabled,
	TSubclassOf<ADeckEnemy> InEnemyClass,
	int32 InPoolSize,
	float InSightDelay,
	float InActivationInterval,
	int32 InMaxRetries,
	float InRetryInterval,
	int32 InRandomSeed)
{
	bLegacyEnabled = bInEnabled;
	LegacyEnemyClass = InEnemyClass;
	LegacyPoolSize = FMath::Clamp(InPoolSize, 1, 32);
	LegacySightDelay = FMath::Max(0.0f, InSightDelay);
	LegacyActivationInterval = FMath::Max(0.05f, InActivationInterval);
	LegacyMaxRetries = FMath::Clamp(InMaxRetries, 0, 5);
	LegacyRetryInterval = FMath::Max(0.05f, InRetryInterval);
	LegacyRandomSeed = InRandomSeed;
}

AEnemyShip* UDeckEnemySpawnerComponent::GetHostShip() const
{
	return Cast<AEnemyShip>(GetOwner());
}

bool UDeckEnemySpawnerComponent::IsEnabled() const
{
	return bEnableSpawning || bLegacyEnabled;
}

void UDeckEnemySpawnerComponent::BuildEffectiveComposition(
	TArray<FDeckEnemySpawnEntry>& OutComposition) const
{
	OutComposition.Reset();
	if (bEnableSpawning && !SpawnComposition.IsEmpty())
	{
		for (const FDeckEnemySpawnEntry& Entry : SpawnComposition)
		{
			if (Entry.EnemyClass && Entry.Count > 0)
			{
				OutComposition.Add(Entry);
			}
		}
		return;
	}

	if (bLegacyEnabled && LegacyEnemyClass && LegacyPoolSize > 0)
	{
		FDeckEnemySpawnEntry& Entry = OutComposition.AddDefaulted_GetRef();
		Entry.EnemyClass = LegacyEnemyClass;
		Entry.Count = LegacyPoolSize;
	}
}

void UDeckEnemySpawnerComponent::InitializeWaypoints()
{
	WaypointsById.Reset();
	SpawnWaypoints.Reset();
	PointRuntimeStates.Reset();

	AEnemyShip* Host = GetHostShip();
	if (!Host)
	{
		return;
	}

	TArray<UDeckWaypointComponent*> Components;
	Host->GetComponents<UDeckWaypointComponent>(Components);
	for (UDeckWaypointComponent* Waypoint : Components)
	{
		if (!IsValid(Waypoint))
		{
			continue;
		}

		const int32 WaypointId = Waypoint->GetWaypointId();
		if (WaypointsById.Contains(WaypointId))
		{
			UE_LOG(LogTemp, Error,
				TEXT("[DeckEnemySpawner] Duplicate WaypointId. Ship=%s WaypointId=%d Component=%s"),
				*GetNameSafe(Host), WaypointId, *GetNameSafe(Waypoint));
			continue;
		}

		if (Host->GetShipDeckMesh() && !Waypoint->IsAttachedTo(Host->GetShipDeckMesh()))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[DeckEnemySpawner] Waypoint is not attached below ShipDeckMesh. Ship=%s Waypoint=%s"),
				*GetNameSafe(Host), *GetNameSafe(Waypoint));
		}

		WaypointsById.Add(WaypointId, Waypoint);
		if (Waypoint->CanSpawnEnemy())
		{
			SpawnWaypoints.Add(Waypoint);
		}
	}

	SpawnWaypoints.Sort([](const UDeckWaypointComponent& Left, const UDeckWaypointComponent& Right)
	{
		return Left.GetWaypointId() < Right.GetWaypointId();
	});

	for (const TPair<int32, TObjectPtr<UDeckWaypointComponent>>& Pair : WaypointsById)
	{
		for (const int32 LinkedId : Pair.Value->GetLinkedWaypointIds())
		{
			if (!WaypointsById.Contains(LinkedId))
			{
				UE_LOG(LogTemp, Error,
					TEXT("[DeckEnemySpawner] Invalid waypoint link. Ship=%s WaypointId=%d LinkedId=%d"),
					*GetNameSafe(Host), Pair.Key, LinkedId);
			}
		}
	}
}

void UDeckEnemySpawnerComponent::InitializePool()
{
	AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority() || !IsEnabled() || !EnemyPool.IsEmpty())
	{
		return;
	}
	if (SpawnWaypoints.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DeckEnemySpawner] No spawn waypoint exists. Ship=%s"), *GetNameSafe(Host));
		return;
	}

	TArray<FDeckEnemySpawnEntry> Composition;
	BuildEffectiveComposition(Composition);
	if (Composition.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DeckEnemySpawner] Spawn composition is empty. Ship=%s"), *GetNameSafe(Host));
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	int32 PoolOrdinal = 0;
	for (const FDeckEnemySpawnEntry& Entry : Composition)
	{
		const int32 Count = FMath::Clamp(Entry.Count, 0, 32);
		for (int32 EntryIndex = 0; EntryIndex < Count; ++EntryIndex, ++PoolOrdinal)
		{
			UDeckWaypointComponent* InitialWaypoint = SpawnWaypoints[
				PoolOrdinal % SpawnWaypoints.Num()];
			const ADeckEnemy* EnemyCDO = Entry.EnemyClass
				? Entry.EnemyClass->GetDefaultObject<ADeckEnemy>()
				: nullptr;
			FTransform InitialTransform;
			if (!EnemyCDO || !ResolveEnemySpawnTransform(InitialWaypoint, *EnemyCDO, InitialTransform))
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[DeckEnemySpawner] No valid initial transform. Ship=%s Class=%s Index=%d"),
					*GetNameSafe(Host), *GetNameSafe(Entry.EnemyClass.Get()), EntryIndex);
				continue;
			}

			ADeckEnemy* PooledEnemy = World->SpawnActorDeferred<ADeckEnemy>(
				Entry.EnemyClass,
				InitialTransform,
				Host,
				nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!PooledEnemy)
			{
				UE_LOG(LogTemp, Error,
					TEXT("[DeckEnemySpawner] Failed to allocate pool actor. Ship=%s Class=%s Index=%d"),
					*GetNameSafe(Host), *GetNameSafe(Entry.EnemyClass.Get()), EntryIndex);
				continue;
			}

			PooledEnemy->PrepareForPool();
			PooledEnemy->FinishSpawning(InitialTransform);
			PooledEnemy->SetHostShip(Host);
			PooledEnemy->DeactivateToPool();
			EnemyPool.Add(PooledEnemy);
		}
	}
}

void UDeckEnemySpawnerComponent::Shutdown()
{
	CancelDeployment();
	for (ADeckEnemy* Enemy : EnemyPool)
	{
		if (IsValid(Enemy))
		{
			ReleaseAllPointsFor(Enemy);
			Enemy->Destroy();
		}
	}
	EnemyPool.Reset();
	AliveDeployedEnemies.Reset();
	PointRuntimeStates.Reset();
	DeploymentState = EDeckEnemyDeploymentState::Idle;
	bHasDeployedEnemy = false;
	bAllDeployedEnemiesDefeated = false;
}

void UDeckEnemySpawnerComponent::CancelDeployment()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SightDelayTimerHandle);
		World->GetTimerManager().ClearTimer(DeploymentTimerHandle);
	}
	DeploymentQueue.Reset();
	DeploymentTriggerShip.Reset();
	DeploymentInitialTarget.Reset();
	if (DeploymentState == EDeckEnemyDeploymentState::Preparing
		|| DeploymentState == EDeckEnemyDeploymentState::Deploying)
	{
		DeploymentState = EDeckEnemyDeploymentState::Failed;
	}
}

bool UDeckEnemySpawnerComponent::RequestDeployment(
	AShip* TriggeringPlayerShip,
	AActor* InitialCombatTarget)
{
	AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority() || Host->IsDeathHandled() || !IsEnabled()
		|| !IsValid(TriggeringPlayerShip)
		|| (InitialCombatTarget && !IsValid(InitialCombatTarget)))
	{
		return false;
	}
	if (DeploymentState == EDeckEnemyDeploymentState::Preparing
		|| DeploymentState == EDeckEnemyDeploymentState::Deploying
		|| DeploymentState == EDeckEnemyDeploymentState::Completed
		|| DeploymentState == EDeckEnemyDeploymentState::CompletedWithFailures)
	{
		return false;
	}

	if (EnemyPool.IsEmpty())
	{
		InitializePool();
	}
	if (EnemyPool.IsEmpty())
	{
		DeploymentState = EDeckEnemyDeploymentState::Failed;
		return false;
	}

	TArray<FDeckEnemySpawnEntry> Composition;
	BuildEffectiveComposition(Composition);
	DeploymentQueue.Reset();
	for (const FDeckEnemySpawnEntry& Entry : Composition)
	{
		for (int32 Index = 0; Index < FMath::Max(0, Entry.Count); ++Index)
		{
			DeploymentQueue.Add(Entry.EnemyClass);
		}
	}
	if (DeploymentQueue.IsEmpty())
	{
		DeploymentState = EDeckEnemyDeploymentState::Failed;
		return false;
	}

	DeploymentTriggerShip = TriggeringPlayerShip;
	DeploymentInitialTarget = InitialCombatTarget;
	AliveDeployedEnemies.Reset();
	bHasDeployedEnemy = false;
	bAllDeployedEnemiesDefeated = false;
	DeploymentQueueIndex = 0;
	CurrentRetryCount = 0;
	DeploymentFailureCount = 0;
	DeploymentState = EDeckEnemyDeploymentState::Preparing;

	const float EffectiveDelay = bEnableSpawning ? SightActivationDelay : LegacySightDelay;
	if (EffectiveDelay <= 0.0f)
	{
		BeginDeployment();
	}
	else
	{
		GetWorld()->GetTimerManager().SetTimer(
			SightDelayTimerHandle,
			this,
			&UDeckEnemySpawnerComponent::BeginDeployment,
			EffectiveDelay,
			false);
	}
	return true;
}

void UDeckEnemySpawnerComponent::BeginDeployment()
{
	AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority() || Host->IsDeathHandled()
		|| DeploymentQueue.IsEmpty() || EnemyPool.IsEmpty())
	{
		DeploymentState = EDeckEnemyDeploymentState::Failed;
		return;
	}
	DeploymentState = EDeckEnemyDeploymentState::Deploying;
	DeployNextEnemy();
}

ADeckEnemy* UDeckEnemySpawnerComponent::FindInactiveEnemy(
	TSubclassOf<ADeckEnemy> RequiredClass) const
{
	for (ADeckEnemy* Enemy : EnemyPool)
	{
		if (IsValid(Enemy) && !Enemy->IsPoolActive()
			&& (!RequiredClass || Enemy->GetClass() == RequiredClass.Get()))
		{
			return Enemy;
		}
	}
	return nullptr;
}

void UDeckEnemySpawnerComponent::DeployNextEnemy()
{
	GetWorld()->GetTimerManager().ClearTimer(DeploymentTimerHandle);
	AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority() || Host->IsDeathHandled()
		|| !DeploymentQueue.IsValidIndex(DeploymentQueueIndex))
	{
		FinishDeployment();
		return;
	}

	ADeckEnemy* Enemy = FindInactiveEnemy(DeploymentQueue[DeploymentQueueIndex]);
	if (!Enemy)
	{
		HandleDeploymentFailure();
		return;
	}

	FDeckEnemySpawnRequest Request;
	Request.Requester = Enemy;
	if (!SpawnWaypoints.IsEmpty())
	{
		Request.PreferredPointIds.Add(
			SpawnWaypoints[DeploymentQueueIndex % SpawnWaypoints.Num()]->GetWaypointId());
	}
	FDeckPointReservation Reservation;
	if (!TryReserveEnemySpawnPoint(Request, Reservation))
	{
		HandleDeploymentFailure();
		return;
	}

	ADeckEnemy* ActivatedEnemy = nullptr;
	if (!ActivateSpecificEnemyAtReservation(
		*Enemy, Reservation, DeploymentInitialTarget.Get(), ActivatedEnemy))
	{
		ReleasePointReservation(Reservation);
		HandleDeploymentFailure();
		return;
	}

	++DeploymentQueueIndex;
	CurrentRetryCount = 0;
	if (!DeploymentQueue.IsValidIndex(DeploymentQueueIndex))
	{
		FinishDeployment();
		return;
	}

	const float Interval = bEnableSpawning ? ActivationInterval : LegacyActivationInterval;
	GetWorld()->GetTimerManager().SetTimer(
		DeploymentTimerHandle,
		this,
		&UDeckEnemySpawnerComponent::DeployNextEnemy,
		FMath::Max(0.05f, Interval),
		false);
}

void UDeckEnemySpawnerComponent::HandleDeploymentFailure()
{
	++CurrentRetryCount;
	const int32 RetryLimit = bEnableSpawning ? MaxSpawnRetries : LegacyMaxRetries;
	if (CurrentRetryCount <= FMath::Max(0, RetryLimit))
	{
		const float Interval = bEnableSpawning ? SpawnRetryInterval : LegacyRetryInterval;
		GetWorld()->GetTimerManager().SetTimer(
			DeploymentTimerHandle,
			this,
			&UDeckEnemySpawnerComponent::DeployNextEnemy,
			FMath::Max(0.05f, Interval),
			false);
		return;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[DeckEnemySpawner] Deployment entry abandoned. Ship=%s QueueIndex=%d Class=%s"),
		*GetNameSafe(GetHostShip()), DeploymentQueueIndex,
		DeploymentQueue.IsValidIndex(DeploymentQueueIndex)
			? *GetNameSafe(DeploymentQueue[DeploymentQueueIndex].Get())
			: TEXT("None"));
	++DeploymentFailureCount;
	++DeploymentQueueIndex;
	CurrentRetryCount = 0;
	if (!DeploymentQueue.IsValidIndex(DeploymentQueueIndex))
	{
		FinishDeployment();
		return;
	}

	const float Interval = bEnableSpawning ? ActivationInterval : LegacyActivationInterval;
	GetWorld()->GetTimerManager().SetTimer(
		DeploymentTimerHandle,
		this,
		&UDeckEnemySpawnerComponent::DeployNextEnemy,
		FMath::Max(0.05f, Interval),
		false);
}

void UDeckEnemySpawnerComponent::FinishDeployment()
{
	const int32 SuccessfulCount = DeploymentQueueIndex - DeploymentFailureCount;
	if (SuccessfulCount <= 0)
	{
		DeploymentState = EDeckEnemyDeploymentState::Failed;
	}
	else if (DeploymentFailureCount > 0)
	{
		DeploymentState = EDeckEnemyDeploymentState::CompletedWithFailures;
	}
	else
	{
		DeploymentState = EDeckEnemyDeploymentState::Completed;
	}
	EvaluateAllEnemiesDefeated();
}

int32 UDeckEnemySpawnerComponent::GetAliveDeployedEnemyCount() const
{
	int32 AliveCount = 0;
	for (const TWeakObjectPtr<ADeckEnemy>& Enemy : AliveDeployedEnemies)
	{
		AliveCount += Enemy.IsValid() ? 1 : 0;
	}
	return AliveCount;
}

void UDeckEnemySpawnerComponent::NotifyEnemyDefeated(ADeckEnemy* Enemy)
{
	AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority() || !IsValid(Enemy) || !EnemyPool.Contains(Enemy))
	{
		return;
	}

	AliveDeployedEnemies.Remove(Enemy);
	EvaluateAllEnemiesDefeated();
}

void UDeckEnemySpawnerComponent::EvaluateAllEnemiesDefeated()
{
	if (bAllDeployedEnemiesDefeated || !bHasDeployedEnemy
		|| (DeploymentState != EDeckEnemyDeploymentState::Completed
			&& DeploymentState != EDeckEnemyDeploymentState::CompletedWithFailures)
		|| GetAliveDeployedEnemyCount() > 0)
	{
		return;
	}

	bAllDeployedEnemiesDefeated = true;
	if (AEnemyShip* Host = GetHostShip())
	{
		Host->NotifyAllOwnedDeckEnemiesDefeated();
	}
}

UDeckWaypointComponent* UDeckEnemySpawnerComponent::GetWaypoint(int32 WaypointId) const
{
	const TObjectPtr<UDeckWaypointComponent>* Found = WaypointsById.Find(WaypointId);
	return Found ? Found->Get() : nullptr;
}

FVector UDeckEnemySpawnerComponent::GetWaypointWorldLocation(int32 WaypointId) const
{
	const UDeckWaypointComponent* Waypoint = GetWaypoint(WaypointId);
	return Waypoint ? Waypoint->GetComponentLocation()
		: (GetHostShip() ? GetHostShip()->GetActorLocation() : FVector::ZeroVector);
}

void UDeckEnemySpawnerComponent::GetWaypointIds(
	TArray<int32>& OutWaypointIds,
	bool bRequireCombatPoint) const
{
	OutWaypointIds.Reset();
	for (const TPair<int32, TObjectPtr<UDeckWaypointComponent>>& Pair : WaypointsById)
	{
		const UDeckWaypointComponent* Waypoint = Pair.Value;
		if (IsValid(Waypoint) && (!bRequireCombatPoint || Waypoint->CanUseInCombat()))
		{
			OutWaypointIds.Add(Pair.Key);
		}
	}
	OutWaypointIds.Sort();
}

void UDeckEnemySpawnerComponent::GetConnectedWaypointIds(
	int32 WaypointId,
	TArray<int32>& OutWaypointIds) const
{
	OutWaypointIds.Reset();
	if (const UDeckWaypointComponent* Waypoint = GetWaypoint(WaypointId))
	{
		for (const int32 LinkedId : Waypoint->GetLinkedWaypointIds())
		{
			if (WaypointsById.Contains(LinkedId))
			{
				OutWaypointIds.Add(LinkedId);
			}
		}
	}
}

int32 UDeckEnemySpawnerComponent::FindNearestWaypoint(
	const FVector& WorldLocation,
	bool bRequirePatrolPoint) const
{
	const AEnemyShip* Host = GetHostShip();
	const UStaticMeshComponent* DeckMesh = Host ? Host->GetShipDeckMesh() : nullptr;
	if (!DeckMesh)
	{
		return INDEX_NONE;
	}
	const FTransform DeckTransform = DeckMesh->GetComponentTransform();
	const FVector QueryLocal = DeckTransform.InverseTransformPosition(WorldLocation);
	int32 BestId = INDEX_NONE;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	for (const TPair<int32, TObjectPtr<UDeckWaypointComponent>>& Pair : WaypointsById)
	{
		const UDeckWaypointComponent* Waypoint = Pair.Value;
		if (!IsValid(Waypoint) || (bRequirePatrolPoint && !Waypoint->CanPatrol()))
		{
			continue;
		}
		const FVector PointLocal = DeckTransform.InverseTransformPosition(
			Waypoint->GetComponentLocation());
		const float DistanceSquared = FVector::DistSquared2D(QueryLocal, PointLocal);
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestId = Pair.Key;
		}
	}
	return BestId;
}

bool UDeckEnemySpawnerComponent::ResolveFixedDeckAnchorTransform(
	int32 WaypointId,
	float CapsuleHalfHeight,
	FTransform& OutTransform) const
{
	const AEnemyShip* Host = GetHostShip();
	const UDeckWaypointComponent* Waypoint = GetWaypoint(WaypointId);
	const UStaticMeshComponent* DeckMesh = Host ? Host->GetShipDeckMesh() : nullptr;
	if (!Waypoint || !DeckMesh)
	{
		return false;
	}

	const FTransform DeckTransform = DeckMesh->GetComponentTransform();
	const FVector LocalAnchor = DeckTransform.InverseTransformPosition(Waypoint->GetComponentLocation());
	const FVector LocalForward = DeckTransform.InverseTransformVectorNoScale(Waypoint->GetForwardVector());
	const FVector DeckUp = DeckMesh->GetUpVector().GetSafeNormal();
	FVector Forward = FVector::VectorPlaneProject(
		DeckTransform.TransformVectorNoScale(LocalForward), DeckUp).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = DeckMesh->GetForwardVector();
	}
	const FVector CharacterLocation = DeckTransform.TransformPosition(LocalAnchor)
		+ DeckUp * (FMath::Max(0.0f, CapsuleHalfHeight) + 2.0f);
	OutTransform = FTransform(FRotationMatrix::MakeFromXZ(Forward, DeckUp).ToQuat(), CharacterLocation);
	return !OutTransform.ContainsNaN();
}

bool UDeckEnemySpawnerComponent::ResolveDeckCharacterTransform(
	int32 WaypointId,
	float CapsuleHalfHeight,
	FTransform& OutTransform) const
{
	const AEnemyShip* Host = GetHostShip();
	const UDeckWaypointComponent* Waypoint = GetWaypoint(WaypointId);
	UStaticMeshComponent* DeckMesh = Host ? Host->GetShipDeckMesh() : nullptr;
	UWorld* World = GetWorld();
	if (!Waypoint || !DeckMesh || !World)
	{
		return false;
	}

	const FTransform DeckTransform = DeckMesh->GetComponentTransform();
	const FVector LocalAnchor = DeckTransform.InverseTransformPosition(Waypoint->GetComponentLocation());
	const FVector AnchorLocation = DeckTransform.TransformPosition(LocalAnchor);
	const FVector DeckUp = DeckMesh->GetUpVector().GetSafeNormal();
	const FVector TraceStart = AnchorLocation + DeckUp * 150.0f;
	const FVector TraceEnd = AnchorLocation - DeckUp * 250.0f;
	FCollisionObjectQueryParams ObjectQuery;
	ObjectQuery.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectQuery.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DeckCharacterTransform), false);
	TArray<FHitResult> Hits;
	World->LineTraceMultiByObjectType(Hits, TraceStart, TraceEnd, ObjectQuery, QueryParams);
	const FHitResult* DeckHit = Hits.FindByPredicate([DeckMesh](const FHitResult& Hit)
	{
		return Hit.GetComponent() == DeckMesh;
	});
	if (!DeckHit)
	{
		return false;
	}

	const FVector LocalForward = DeckTransform.InverseTransformVectorNoScale(Waypoint->GetForwardVector());
	FVector Forward = FVector::VectorPlaneProject(
		DeckTransform.TransformVectorNoScale(LocalForward), DeckUp).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = DeckMesh->GetForwardVector();
	}
	const FVector CharacterLocation = DeckHit->ImpactPoint
		+ DeckUp * (FMath::Max(0.0f, CapsuleHalfHeight) + 2.0f);
	OutTransform = FTransform(FRotationMatrix::MakeFromXZ(Forward, DeckUp).ToQuat(), CharacterLocation);
	return !OutTransform.ContainsNaN();
}

bool UDeckEnemySpawnerComponent::ResolveEnemySpawnTransform(
	const UDeckWaypointComponent* SpawnWaypoint,
	const ADeckEnemy& Enemy,
	FTransform& OutTransform) const
{
	if (!SpawnWaypoint)
	{
		return false;
	}
	const UCapsuleComponent* Capsule = Enemy.GetCapsuleComponent();
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f;
	return ResolveDeckCharacterTransform(SpawnWaypoint->GetWaypointId(), HalfHeight, OutTransform)
		|| ResolveFixedDeckAnchorTransform(SpawnWaypoint->GetWaypointId(), HalfHeight, OutTransform);
}

bool UDeckEnemySpawnerComponent::IsPointAvailable(int32 WaypointId, const AActor* Requester) const
{
	(void)Requester;
	const AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority() || !GetWaypoint(WaypointId))
	{
		return false;
	}
	const FDeckPointRuntimeState* State = PointRuntimeStates.Find(WaypointId);
	return !State || (!State->Occupant.IsValid() && !State->ReservedBy.IsValid());
}

void UDeckEnemySpawnerComponent::PrunePointRuntimeState()
{
	for (auto It = PointRuntimeStates.CreateIterator(); It; ++It)
	{
		FDeckPointRuntimeState& State = It.Value();
		if (!State.Occupant.IsValid())
		{
			State.Occupant.Reset();
		}
		if (!State.ReservedBy.IsValid())
		{
			State.ReservedBy.Reset();
			State.ReservationSerial = 0;
		}
		if (!State.CombatClaimedBy.IsValid())
		{
			State.CombatClaimedBy.Reset();
		}
		if (!State.Occupant.IsValid() && !State.ReservedBy.IsValid()
			&& !State.CombatClaimedBy.IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

bool UDeckEnemySpawnerComponent::TryReservePoint(
	int32 WaypointId,
	AActor* Requester,
	FDeckPointReservation& OutReservation)
{
	OutReservation.Reset();
	AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority() || !IsValid(Requester) || !GetWaypoint(WaypointId))
	{
		return false;
	}
	PrunePointRuntimeState();
	FDeckPointRuntimeState& State = PointRuntimeStates.FindOrAdd(WaypointId);
	if ((State.Occupant.IsValid() && State.Occupant.Get() != Requester)
		|| (State.ReservedBy.IsValid() && State.ReservedBy.Get() != Requester))
	{
		return false;
	}
	if (State.ReservedBy.Get() == Requester && State.ReservationSerial != 0)
	{
		OutReservation.PointId = WaypointId;
		OutReservation.Serial = State.ReservationSerial;
		OutReservation.Requester = Requester;
		return true;
	}

	uint32 Serial = NextReservationSerial++;
	if (Serial == 0)
	{
		Serial = NextReservationSerial++;
	}
	State.ReservedBy = Requester;
	State.ReservationSerial = Serial;
	OutReservation.PointId = WaypointId;
	OutReservation.Serial = Serial;
	OutReservation.Requester = Requester;
	return true;
}

bool UDeckEnemySpawnerComponent::TryReserveEnemySpawnPoint(
	const FDeckEnemySpawnRequest& Request,
	FDeckPointReservation& OutReservation)
{
	OutReservation.Reset();
	AEnemyShip* Host = GetHostShip();
	AActor* Requester = Request.Requester.Get();
	UStaticMeshComponent* DeckMesh = Host ? Host->GetShipDeckMesh() : nullptr;
	if (!Host || !Host->HasAuthority() || !IsValid(Requester) || !DeckMesh)
	{
		return false;
	}

	PrunePointRuntimeState();
	const FTransform DeckTransform = DeckMesh->GetComponentTransform();
	const FVector RequesterLocal = DeckTransform.InverseTransformPosition(Requester->GetActorLocation());
	const AActor* Target = Request.Target.Get();
	const FVector TargetLocal = Target
		? DeckTransform.InverseTransformPosition(Target->GetActorLocation())
		: FVector::ZeroVector;
	TArray<int32> Candidates;
	for (const UDeckWaypointComponent* Waypoint : SpawnWaypoints)
	{
		if (!Waypoint || !Waypoint->CanUseInCombat())
		{
			continue;
		}
		TArray<int32> ConnectedIds;
		GetConnectedWaypointIds(Waypoint->GetWaypointId(), ConnectedIds);
		const int32 PointId = Waypoint->GetWaypointId();
		if (ConnectedIds.IsEmpty() || PointId == Request.ExcludedPointId
			|| !IsPointAvailable(PointId, Requester))
		{
			continue;
		}
		const FVector PointLocal = DeckTransform.InverseTransformPosition(Waypoint->GetComponentLocation());
		const float RequesterDistance = FVector::Dist2D(PointLocal, RequesterLocal);
		const float TargetDistance = Target
			? FVector::Dist2D(PointLocal, TargetLocal)
			: TNumericLimits<float>::Max();
		if (RequesterDistance < FMath::Max(0.0f, Request.MinimumDistanceFromRequester)
			|| TargetDistance < FMath::Max(0.0f, Request.MinimumDistanceFromTarget))
		{
			continue;
		}
		Candidates.Add(PointId);
	}

	Candidates.Sort([this, &Request, Target, DeckTransform](int32 LeftId, int32 RightId)
	{
		const bool bLeftPreferred = Request.PreferredPointIds.Contains(LeftId);
		const bool bRightPreferred = Request.PreferredPointIds.Contains(RightId);
		if (bLeftPreferred != bRightPreferred)
		{
			return bLeftPreferred;
		}
		if (Target)
		{
			const FVector TargetLocal = DeckTransform.InverseTransformPosition(Target->GetActorLocation());
			const FVector LeftLocal = DeckTransform.InverseTransformPosition(GetWaypointWorldLocation(LeftId));
			const FVector RightLocal = DeckTransform.InverseTransformPosition(GetWaypointWorldLocation(RightId));
			const float LeftDistance = FVector::DistSquared2D(LeftLocal, TargetLocal);
			const float RightDistance = FVector::DistSquared2D(RightLocal, TargetLocal);
			if (!FMath::IsNearlyEqual(LeftDistance, RightDistance))
			{
				return LeftDistance > RightDistance;
			}
		}
		return LeftId < RightId;
	});

	for (const int32 PointId : Candidates)
	{
		if (TryReservePoint(PointId, Requester, OutReservation))
		{
			return true;
		}
	}
	return false;
}

bool UDeckEnemySpawnerComponent::CommitPointReservation(
	const FDeckPointReservation& Reservation,
	AActor* Occupant)
{
	AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority() || !Reservation.IsValid() || !IsValid(Occupant))
	{
		return false;
	}
	FDeckPointRuntimeState* State = PointRuntimeStates.Find(Reservation.PointId);
	if (!State || State->ReservedBy != Reservation.Requester
		|| State->ReservationSerial != Reservation.Serial
		|| (State->Occupant.IsValid() && State->Occupant.Get() != Occupant))
	{
		return false;
	}
	State->Occupant = Occupant;
	State->ReservedBy.Reset();
	State->ReservationSerial = 0;
	return true;
}

void UDeckEnemySpawnerComponent::ReleasePointReservation(FDeckPointReservation& Reservation)
{
	AEnemyShip* Host = GetHostShip();
	if (Host && Host->HasAuthority() && Reservation.PointId != INDEX_NONE)
	{
		if (FDeckPointRuntimeState* State = PointRuntimeStates.Find(Reservation.PointId);
			State && State->ReservedBy == Reservation.Requester
			&& State->ReservationSerial == Reservation.Serial)
		{
			State->ReservedBy.Reset();
			State->ReservationSerial = 0;
			if (!State->Occupant.IsValid() && !State->CombatClaimedBy.IsValid())
			{
				PointRuntimeStates.Remove(Reservation.PointId);
			}
		}
	}
	Reservation.Reset();
}

bool UDeckEnemySpawnerComponent::TryOccupyPoint(int32 WaypointId, AActor* Occupant)
{
	AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority() || !IsValid(Occupant) || !GetWaypoint(WaypointId))
	{
		return false;
	}
	PrunePointRuntimeState();
	FDeckPointRuntimeState& State = PointRuntimeStates.FindOrAdd(WaypointId);
	if ((State.Occupant.IsValid() && State.Occupant.Get() != Occupant)
		|| (State.ReservedBy.IsValid() && State.ReservedBy.Get() != Occupant))
	{
		return false;
	}
	State.Occupant = Occupant;
	if (State.ReservedBy.Get() == Occupant)
	{
		State.ReservedBy.Reset();
		State.ReservationSerial = 0;
	}
	return true;
}

void UDeckEnemySpawnerComponent::ReleasePointOccupancy(int32 WaypointId, AActor* Occupant)
{
	AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority() || !IsValid(Occupant))
	{
		return;
	}
	if (FDeckPointRuntimeState* State = PointRuntimeStates.Find(WaypointId);
		State && State->Occupant.Get() == Occupant)
	{
		State->Occupant.Reset();
		if (!State->ReservedBy.IsValid() && !State->CombatClaimedBy.IsValid())
		{
			PointRuntimeStates.Remove(WaypointId);
		}
	}
}

bool UDeckEnemySpawnerComponent::IsCombatPointClaimAvailable(
	int32 WaypointId,
	const AActor* Requester) const
{
	const AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority() || !GetWaypoint(WaypointId))
	{
		return false;
	}
	const FDeckPointRuntimeState* State = PointRuntimeStates.Find(WaypointId);
	return !State || !State->CombatClaimedBy.IsValid()
		|| State->CombatClaimedBy.Get() == Requester;
}

bool UDeckEnemySpawnerComponent::TryClaimCombatPoint(int32 WaypointId, AActor* Requester)
{
	AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority() || !IsValid(Requester) || !GetWaypoint(WaypointId))
	{
		return false;
	}
	PrunePointRuntimeState();
	FDeckPointRuntimeState& State = PointRuntimeStates.FindOrAdd(WaypointId);
	if (State.CombatClaimedBy.IsValid() && State.CombatClaimedBy.Get() != Requester)
	{
		return false;
	}
	State.CombatClaimedBy = Requester;
	return true;
}

void UDeckEnemySpawnerComponent::ReleaseCombatPointClaim(int32 WaypointId, AActor* Requester)
{
	AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority() || WaypointId == INDEX_NONE || !Requester)
	{
		return;
	}
	if (FDeckPointRuntimeState* State = PointRuntimeStates.Find(WaypointId);
		State && State->CombatClaimedBy.Get() == Requester)
	{
		State->CombatClaimedBy.Reset();
		if (!State->Occupant.IsValid() && !State->ReservedBy.IsValid())
		{
			PointRuntimeStates.Remove(WaypointId);
		}
	}
}

void UDeckEnemySpawnerComponent::ReleaseAllPointsFor(AActor* Actor)
{
	AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority() || !Actor)
	{
		return;
	}
	for (auto It = PointRuntimeStates.CreateIterator(); It; ++It)
	{
		FDeckPointRuntimeState& State = It.Value();
		if (State.Occupant.Get() == Actor)
		{
			State.Occupant.Reset();
		}
		if (State.ReservedBy.Get() == Actor)
		{
			State.ReservedBy.Reset();
			State.ReservationSerial = 0;
		}
		if (State.CombatClaimedBy.Get() == Actor)
		{
			State.CombatClaimedBy.Reset();
		}
		if (!State.Occupant.IsValid() && !State.ReservedBy.IsValid()
			&& !State.CombatClaimedBy.IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

bool UDeckEnemySpawnerComponent::ActivateEnemyAtPoint(
	int32 SpawnPointId,
	AActor* InitialTarget,
	ADeckEnemy*& OutEnemy)
{
	OutEnemy = nullptr;
	if (EnemyPool.IsEmpty())
	{
		InitializePool();
	}
	ADeckEnemy* Enemy = FindInactiveEnemy(nullptr);
	if (!Enemy)
	{
		return false;
	}
	FDeckPointReservation Reservation;
	if (!TryReservePoint(SpawnPointId, Enemy, Reservation))
	{
		return false;
	}
	return ActivateSpecificEnemyAtReservation(*Enemy, Reservation, InitialTarget, OutEnemy);
}

bool UDeckEnemySpawnerComponent::ActivateEnemyAtReservation(
	FDeckPointReservation& Reservation,
	AActor* InitialTarget,
	ADeckEnemy*& OutEnemy)
{
	OutEnemy = nullptr;
	if (EnemyPool.IsEmpty())
	{
		InitializePool();
	}
	ADeckEnemy* Enemy = FindInactiveEnemy(nullptr);
	if (!Enemy)
	{
		ReleasePointReservation(Reservation);
		return false;
	}
	return ActivateSpecificEnemyAtReservation(*Enemy, Reservation, InitialTarget, OutEnemy);
}

bool UDeckEnemySpawnerComponent::ActivateSpecificEnemyAtReservation(
	ADeckEnemy& Enemy,
	FDeckPointReservation& Reservation,
	AActor* InitialTarget,
	ADeckEnemy*& OutEnemy)
{
	OutEnemy = nullptr;
	AEnemyShip* Host = GetHostShip();
	if (!Host || !Host->HasAuthority() || Host->IsDeathHandled() || !Reservation.IsValid()
		|| Enemy.IsPoolActive()
		|| (InitialTarget && !Enemy.IsValidCombatTarget(InitialTarget)))
	{
		ReleasePointReservation(Reservation);
		return false;
	}

	UDeckWaypointComponent* SpawnWaypoint = GetWaypoint(Reservation.PointId);
	if (!SpawnWaypoint || !SpawnWaypoint->CanSpawnEnemy() || !SpawnWaypoint->CanUseInCombat())
	{
		ReleasePointReservation(Reservation);
		return false;
	}

	FTransform SpawnTransform;
	if (!ResolveEnemySpawnTransform(SpawnWaypoint, Enemy, SpawnTransform) || !GetWorld())
	{
		ReleasePointReservation(Reservation);
		return false;
	}

	const UCapsuleComponent* Capsule = Enemy.GetCapsuleComponent();
	const float Radius = Capsule ? Capsule->GetScaledCapsuleRadius() : 42.0f;
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.0f;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DeckEnemySpawnClearance), false, &Enemy);
	QueryParams.AddIgnoredActor(Host);
	if (GetWorld()->OverlapBlockingTestByChannel(
		SpawnTransform.GetLocation(),
		SpawnTransform.GetRotation(),
		ECC_Pawn,
		FCollisionShape::MakeCapsule(Radius, HalfHeight),
		QueryParams))
	{
		ReleasePointReservation(Reservation);
		return false;
	}

	const int32 EffectiveSeed = bEnableSpawning ? RandomSeed : LegacyRandomSeed;
	const int32 Seed = static_cast<int32>(HashCombine(
		static_cast<uint32>(EffectiveSeed),
		HashCombine(GetTypeHash(Host->GetFName()), static_cast<uint32>(ActivationSerial++))));
	if (!Enemy.ActivateFromPool(Host, Reservation.PointId, Seed))
	{
		ReleasePointReservation(Reservation);
		return false;
	}
	if (!CommitPointReservation(Reservation, &Enemy))
	{
		Enemy.DeactivateToPool();
		ReleasePointReservation(Reservation);
		return false;
	}
	Reservation.Reset();
	AliveDeployedEnemies.Add(&Enemy);
	bHasDeployedEnemy = true;
	bAllDeployedEnemiesDefeated = false;

	if (InitialTarget)
	{
		Enemy.SetCombatTarget(InitialTarget);
		if (ABaseAIController* Controller = Cast<ABaseAIController>(Enemy.GetController()))
		{
			Controller->SetCombatTarget(InitialTarget);
		}
	}
	OutEnemy = &Enemy;
	return true;
}
