#include "DeckAI/DeckRangedEnemy.h"

#include "AbilitySystemComponent.h"
#include "AI/BaseAIController.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Components/BaseHealthComponent.h"
#include "Components/StatusComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DeckAI/DeckEnemyNavigationComponent.h"
#include "DeckAI/DeckEnemyCombatComponent.h"
#include "DeckAI/DeckCombatTargetResolverComponent.h"
#include "AI/EnemyAlarmComponent.h"
#include "DeckAI/DeckWalkRouteComponent.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "DeckAI/DeckEnemyCharacterMovementComponent.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipWeakeningWorldSubsystem.h"
#include "TimerManager.h"
#include "UI/EnemyHealthBarComponent.h"
#include "Weapon/BaseWeaponComponent.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "Room/SWRoomSnapshotSubsystem.h"

ADeckEnemy::ADeckEnemy(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UDeckEnemyCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	DeckEnemyNavigationComponent = CreateDefaultSubobject<UDeckEnemyNavigationComponent>(
		TEXT("DeckEnemyNavigationComponent"));
	DeckWalkRouteComponent = CreateDefaultSubobject<UDeckWalkRouteComponent>(TEXT("DeckWalkRouteComponent"));
	DeckCombatComponent = CreateDefaultSubobject<UDeckEnemyCombatComponent>(TEXT("DeckCombatComponent"));
	DeckTargetResolver = CreateDefaultSubobject<UDeckCombatTargetResolverComponent>(TEXT("DeckTargetResolver"));
	bAutoResolveHostShip = false;
	bDestroyWithHostShip = false;
	bDestroyAfterDeathFinished = false;
	bAlwaysRelevant = false;
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->bBaseOnAttachmentRoot = true;
	}
}

bool ADeckEnemy::CanMoveOnDeck() const
{
	const auto* Host = GetDeckHostShip();
	return HasAuthority() && !bAwaitingSnapshotCompletion && bPoolActive && !bDeathHandled
		&& IsValid(Host) && Host->CanDeployDeckEnemies();
}

AEnemyShip* ADeckEnemy::GetDeckHostShip() const
{
	return !HasAuthority() && PoolNetState.ActivationGeneration > 0
		? PoolNetState.Host.Get() : Cast<AEnemyShip>(GetHostShip());
}

void ADeckEnemy::SetHostShip(AShip* NewHostShip)
{
	if (GetHostShip() != NewHostShip && DeckEnemyNavigationComponent)
	{
		DeckEnemyNavigationComponent->CancelCombatRoute();
		if (DeckWalkRouteComponent) DeckWalkRouteComponent->ResetNavigationState();
	}
	Super::SetHostShip(NewHostShip);
	BindRuntimeHost();
}

void ADeckEnemy::BindRuntimeHost()
{
	if (BoundRuntimeHost.Get() == GetDeckHostShip()) return;
	if (auto* Previous = BoundRuntimeHost.Get())
		(HasAuthority() ? Previous->OnRuntimeStateChanged : Previous->OnRuntimePresentationChanged).Remove(RuntimeHostHandle);
	BoundRuntimeHost = GetDeckHostShip();
	if (auto* Host = BoundRuntimeHost.Get()) RuntimeHostHandle = (HasAuthority() ? Host->OnRuntimeStateChanged : Host->OnRuntimePresentationChanged)
		.AddUObject(this, &ADeckEnemy::HandleHostRuntimeStateChanged);
}

void ADeckEnemy::HandleHostRuntimeStateChanged(const FEnemyShipRuntimeState&, const FEnemyShipRuntimeState& Current)
{
	if (!HasAuthority()) { ReconcileClientPoolState(); return; }
	if (!bPoolActive) return;
	if (Current.IsActive()) ResumePoolAI();
	else
	{
		if (DeckCombatComponent) DeckCombatComponent->ResetCombat();
		if (DeckEnemyNavigationComponent) DeckEnemyNavigationComponent->CancelCombatRoute();
		if (DeckWalkRouteComponent) DeckWalkRouteComponent->ResetNavigationState();
		if (auto* ASC = GetAbilitySystemComponent()) ASC->CancelAllAbilities();
		StopDeckMovement();
		if (auto* AI = Cast<AAIController>(GetController()))
			if (auto* Brain = AI->GetBrainComponent()) Brain->StopLogic(TEXT("Deck host inactive"));
	}
	FlushNetDormancy();
	RefreshPoolNetState();
	ApplyPoolPresentationState();
	ForceNetUpdate();
}

void ADeckEnemy::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Initial replication can apply the inactive pool state before BeginPlay.
	// Cache authored collision before that state disables the components.
	InitialCapsuleCollision = GetCapsuleComponent()
		? GetCapsuleComponent()->GetCollisionEnabled()
		: ECollisionEnabled::QueryAndPhysics;
	InitialMeshCollision = GetMesh()
		? GetMesh()->GetCollisionEnabled()
		: ECollisionEnabled::QueryOnly;
}

void ADeckEnemy::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		if (auto* Room = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>())
		{
			PoolRestoreCompletedHandle = Room->OnRestoreCompleted.AddUObject(this, &ADeckEnemy::HandleRoomRestoreCompleted);
			if (Room->IsRestoringSnapshot())
			{
				bAwaitingSnapshotCompletion = true;
				StopDeckMovement();
				if (auto* AI = Cast<AAIController>(GetController()))
					if (auto* Brain = AI->GetBrainComponent()) Brain->StopLogic(TEXT("Deck pool snapshot restore"));
				ApplyPoolPresentationState();
				return;
			}
		}
	}

	if (HasAuthority() && bStartPooled)
	{
		DeactivateToPool();
	}
	else
	{
		ApplyPoolPresentationState();
		if (bPoolActive)
		{
			RestoreDeckMovementState();
		}
		else
		{
			StopDeckMovement();
		}
		if (!HasAuthority()) ReconcileClientPoolState();
	}
}

void ADeckEnemy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
		if (AEnemyShip* Host = GetDeckHostShip()) Host->NotifyCrewEnemyDeactivated(this);
	if (auto* Host = BoundRuntimeHost.Get())
		(HasAuthority() ? Host->OnRuntimeStateChanged : Host->OnRuntimePresentationChanged).Remove(RuntimeHostHandle);
	if (DeckTargetResolver) DeckTargetResolver->Reset();
	GetWorldTimerManager().ClearTimer(ReturnToPoolTimerHandle);
	GetWorldTimerManager().ClearTimer(PoolPresentationRetryHandle);
	GetWorldTimerManager().ClearTimer(PoolRestoreResumeTimerHandle);
	if (auto* Room = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>()) Room->OnRestoreCompleted.Remove(PoolRestoreCompletedHandle);
	if (HasAuthority())
	{
		if (DeckEnemyNavigationComponent)
		{
			DeckEnemyNavigationComponent->CancelCombatRoute();
		}
		if (AEnemyShip* Host = GetDeckHostShip())
		{
			Host->ReleaseAllDeckPointsFor(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void ADeckEnemy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADeckEnemy, PoolNetState);
}

void ADeckEnemy::PrepareForPool()
{
	bStartPooled = true;
	bPoolActive = false;
	RefreshPoolNetState();
}

bool ADeckEnemy::ActivateFromPool(AEnemyShip* Host, int32 PointId, int32 Seed, const FTransform* Transform)
{
	if (!PreparePoolActivation(Host, PointId, Seed, Transform)) return false;
	if (CommitPoolActivation()) return true;
	DeactivateToPool();
	return false;
}

bool ADeckEnemy::PreparePoolActivation(
	AEnemyShip* InHostShip,
	int32 InitialWaypointId,
	int32 RandomSeed,
	const FTransform* ReservedTransform)
{
	if (!HasAuthority() || bPoolActive || bPoolActivationPrepared || !IsValid(InHostShip) || !InHostShip->CanDeployDeckEnemies()
		|| !InHostShip->GetShipDeckMesh()
		|| !InHostShip->GetDeckWaypoint(InitialWaypointId))
	{
		return false;
	}
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f;
	FTransform AuthoritativeStartTransform;
	const bool bResolvedStart = ReservedTransform ? !ReservedTransform->ContainsNaN()
		: InHostShip->ResolveDeckCharacterTransform(
			InitialWaypointId, HalfHeight, AuthoritativeStartTransform);
	if (ReservedTransform) AuthoritativeStartTransform = *ReservedTransform;
	
	if (!bResolvedStart || AuthoritativeStartTransform.ContainsNaN())
	{
		return false;
	}

	FlushNetDormancy();
	SetNetDormancy(DORM_Awake);
	GetWorldTimerManager().ClearTimer(ReturnToPoolTimerHandle);

	ResetLocalDeathRagdoll();
	SetHostShip(InHostShip);
	InitialSpawnPointId = InitialWaypointId;
	DeckRandomStream.Initialize(RandomSeed);

	RestoreForPoolActivation();
	if (!IsBalanceReady())
	{
		DeactivateToPool();
		return false;
	}
	if (!ApplyAuthoritativeDeckStart(AuthoritativeStartTransform))
	{
		DeactivateToPool();
		return false;
	}
	bPoolActivationPrepared = true;
	StopDeckMovement(); // The movement base is prepared, but AI/collision remain unpublished.
	return true;
}

bool ADeckEnemy::CommitPoolActivation()
{
	AEnemyShip* Host = GetDeckHostShip();
	if (!HasAuthority() || !bPoolActivationPrepared || bPoolActive || !Host || !Host->CanDeployDeckEnemies()) return false;
	bPoolActivationPrepared = false;
	bPoolActive = true;
	RefreshPoolNetState(true);
	ApplyPoolPresentationState();
	RestoreDeckMovementState();
	Host->NotifyCrewEnemyReactivated(this);
	ResumePoolAI();
	ForceNetUpdate();
	return true;
}

void ADeckEnemy::ResumePoolAI()
{
	if (!HasAuthority() || bAwaitingSnapshotCompletion || !bPoolActive || bDeathHandled || !GetDeckHostShip() || !GetDeckHostShip()->CanDeployDeckEnemies()) return;
	if (!GetController()) SpawnDefaultController();
	if (auto* AI = Cast<AAIController>(GetController()))
	{
		if (auto* Base = Cast<ABaseAIController>(AI); Base && Base->GetBrainComponent()) Base->RefreshBehaviorRouting();
		else if (auto* Brain = AI->GetBrainComponent()) Brain->RestartLogic();
		if (auto* Base = Cast<ABaseAIController>(AI); Base && CombatTarget) Base->SetCombatTarget(CombatTarget);
	}
	RestoreDeckMovementState();
}

void ADeckEnemy::DeactivateToPool()
{
	if (!HasAuthority())
	{
		return;
	}

	SetNetDormancy(DORM_Awake);
	FlushNetDormancy();
	GetWorldTimerManager().ClearTimer(ReturnToPoolTimerHandle);
	if (DeckWalkRouteComponent) DeckWalkRouteComponent->ResetNavigationState();
	if (DeckCombatComponent) DeckCombatComponent->ResetCombat();
	if (DeckTargetResolver) DeckTargetResolver->Reset();
	if (AlarmComponent) AlarmComponent->ResetForReuse();
	ClearCombatTarget();
	if (DeckEnemyNavigationComponent)
	{
		DeckEnemyNavigationComponent->CancelCombatRoute();
	}
	if (AEnemyShip* Host = GetDeckHostShip())
	{
		Host->ReleaseAllDeckPointsFor(this);
		Host->NotifyCrewEnemyDeactivated(this);
	}

	if (AAIController* OwningAIController = Cast<AAIController>(GetController()))
	{
		OwningAIController->StopMovement();
		if (UBrainComponent* Brain = OwningAIController->GetBrainComponent())
		{
			Brain->StopLogic(TEXT("Deck enemy returned to pool"));
		}
	}
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->CancelAllAbilities();
	}
	if (UBaseWeaponComponent* BaseWeaponComponent = GetWeaponComponent())
	{
		BaseWeaponComponent->SuspendForOwnerPool();
	}
	if (UStatusComponent* Status = FindComponentByClass<UStatusComponent>()) Status->ClearStatuses();
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->RemoveActiveEffects(FGameplayEffectQuery());
	}
	ResetBalanceForReuse();
	NextAttackTime = 0.;
	StopDeckMovement();

	bPoolActive = false;
	bPoolActivationPrepared = false;
	ClearAuthoritativeDeckBase();
	InitialSpawnPointId = INDEX_NONE;
	RefreshPoolNetState();
	ApplyPoolPresentationState();
	ForceNetUpdate();
	SetNetDormancy(DORM_DormantAll);
}

void ADeckEnemy::ResetToFreshPoolState()
{
	if (!HasAuthority())
	{
		return;
	}
	DeactivateToPool();
	// Fresh inactive state must remain configurable. Applying spawn stats belongs to preparation only.
	bDeathHandled = false;
	bWaveRemoveNotified = false;
	bHasDropped = false;
	if (auto* Health = GetHealthComponent()) Health->ResetForReuse();
	ResetInactivePresentation(false);
	ResetBalanceForReuse();
	ApplyPoolPresentationState();
	ForceNetUpdate();
}

void ADeckEnemy::BeginFreeDeckMovement()
{
	if (!HasAuthority()) return;
	if (AEnemyShip* Host = GetDeckHostShip()) Host->ReleaseDeckPointOccupancy(InitialSpawnPointId, this);
}

bool ADeckEnemy::EvaluateAttackTarget(const AActor* Candidate, bool bRequireLineOfSight, FString& OutReason) const
{
	if (!EvaluateCombatTarget(Candidate, OutReason)) return false;
	const AEnemyShip* Ship = GetDeckHostShip();
	const UDeckWalkAreaComponent* Area = Ship ? Ship->GetDeckWalkAreaComponent() : nullptr;
	FDeckWalkLocation SelfFloor;
	FDeckTargetAnchor TargetFloor;
	if (!Area || !Area->ResolveActorOnDeck(*this, SelfFloor)
		|| !UDeckCombatTargetResolverComponent::ResolveFor(this, const_cast<AActor*>(Candidate), TargetFloor)
		|| !TargetFloor.HasCurrentEvidence())
	{
		OutReason = TEXT("NoWalkableCombatFloor");
		return false;
	}
	if (SelfFloor.SurfaceId != TargetFloor.SurfaceId)
	{
		OutReason = TEXT("DifferentCombatSurface");
		return false;
	}
	if (DeckCombatRole == EDeckEnemyCombatRole::Melee)
	{
		const UBaseWeaponComponent* Weapon = GetWeaponComponent();
		const float Range = Weapon && Weapon->IsWeaponEquipped() ? Weapon->GetCurrentAttackRange() : 0.0f;
		if (Range <= 0.0f || FVector::Distance(GetActorLocation(), Candidate->GetActorLocation()) > Range)
		{
			OutReason = TEXT("AboveMeleeWeaponRange"); return false;
		}
		if (bRequireLineOfSight && !DeckCombatComponent->HasClearAttackLine(const_cast<AActor*>(Candidate)))
		{
			OutReason = TEXT("LineOfSightBlocked"); return false;
		}
		OutReason = TEXT("Ready"); return true;
	}
	return Super::EvaluateAttackTarget(Candidate, bRequireLineOfSight, OutReason);
}

void ADeckEnemy::OnDeckMoveReached()
{
	if (DeckEnemyNavigationComponent) DeckEnemyNavigationComponent->CancelCombatRoute();
}

void ADeckEnemy::OnDeckMoveFailed()
{
	if (DeckEnemyNavigationComponent) DeckEnemyNavigationComponent->CancelCombatRoute();
}

void ADeckEnemy::HandleDeath_Implementation()
{
	if (HasAuthority())
	{
		FlushNetDormancy();
		RefreshPoolNetState();
		ForceNetUpdate();
	}
	else if (PoolNetState.ActivationGeneration > 0 && !PoolNetState.bDead)
	{
		// A health subobject may describe the previous activation until the actor snapshot catches up.
		PoolPresentationRetryHandle = GetWorldTimerManager().SetTimerForNextTick(this, &ADeckEnemy::ReconcileClientPoolState);
	}
	if (DeckTargetResolver) DeckTargetResolver->Reset();
	if (DeckCombatComponent) DeckCombatComponent->ResetCombat();
	if (AlarmComponent) AlarmComponent->ResetForReuse();
	if (DeckWalkRouteComponent) DeckWalkRouteComponent->ResetNavigationState();
	if (HasAuthority())
	{
		if (AEnemyShip* Host = GetDeckHostShip())
		{
			Host->NotifyOwnedDeckEnemyDefeated(this);
		}
	}
	if (DeckEnemyNavigationComponent)
	{
		DeckEnemyNavigationComponent->CancelCombatRoute();
	}
	StopDeckMovement();
}

void ADeckEnemy::HandleDeathFinishedPresentation()
{
	Super::HandleDeathFinishedPresentation();

	if (!HasAuthority() || GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>()->IsRestoringSnapshot()) return;

	if (ReturnToPoolAfterDeathDelay <= 0.0f)
	{
		ReturnToPoolAfterDeath();
	}
	else
	{
		GetWorldTimerManager().SetTimer(
			ReturnToPoolTimerHandle,
			this,
			&ADeckEnemy::ReturnToPoolAfterDeath,
			ReturnToPoolAfterDeathDelay,
			false);
	}
}

void ADeckEnemy::OnRep_PoolActive()
{
	ReconcileClientPoolState();
}

void ADeckEnemy::RefreshPoolNetState(bool bNewActivation)
{
	if (!HasAuthority()) return;
	PoolNetState.bActive = bPoolActive;
	PoolNetState.bDead = bDeathHandled;
	PoolNetState.Host = Cast<AEnemyShip>(GetHostShip());
	PoolNetState.PointId = InitialSpawnPointId;
	if (bNewActivation) ++PoolNetState.ActivationGeneration;
	++PoolNetState.Revision;
}

void ADeckEnemy::OnRep_PoolNetState()
{
	bPoolActive = PoolNetState.bActive;
	InitialSpawnPointId = PoolNetState.PointId;
	PoolPresentationRetries = 0;
	ReconcileClientPoolState();
}

void ADeckEnemy::PostNetReceive()
{
	Super::PostNetReceive();
	if (!HasAuthority()) ReconcileClientPoolState();
}

void ADeckEnemy::HandleReplicatedHostShipChanged()
{
	BindRuntimeHost();
	ReconcileClientPoolState();
}

void ADeckEnemy::ReconcileClientPoolState()
{
	if (HasAuthority() || !HasActorBegunPlay()) return;
	BindRuntimeHost();
	const bool bHostReady = PoolNetState.ActivationGeneration == 0 || (GetDeckHostShip() && GetDeckHostShip()->GetShipDeckMesh());
	GetWorldTimerManager().ClearTimer(PoolPresentationRetryHandle);
	if (PoolNetState.ActivationGeneration > 0) bDeathHandled = PoolNetState.bDead;
	if (bPoolActive && bHostReady && !bDeathHandled
		&& (LastPresentedActivationGeneration != PoolNetState.ActivationGeneration || bLocalDeathRagdollApplied))
	{
		ResetInactivePresentation();
	}
	if (bPoolActive && bHostReady)
	{
		LastPresentedActivationGeneration = PoolNetState.ActivationGeneration;
		if (bDeathHandled) ApplyLocalDeathRagdoll();
	}
	ApplyPoolPresentationState();
	if (bPoolActive && bHostReady && !bLocalDeathRagdollApplied) RestoreDeckMovementState();
	else StopDeckMovement();
	if (bPoolActive && !bHostReady && PoolPresentationRetries < 50)
	{
		++PoolPresentationRetries;
		GetWorldTimerManager().SetTimer(PoolPresentationRetryHandle, this, &ADeckEnemy::ReconcileClientPoolState, 0.1f, false);
		if (PoolPresentationRetries == 50) UE_LOG(LogTemp, Warning, TEXT("[DeckEnemyPool] Enemy=%s Generation=%u Reason=HostReferencePending"), *GetName(), PoolNetState.ActivationGeneration);
	}
}

void ADeckEnemy::ReturnToPoolAfterDeath()
{
	DeactivateToPool();
}

void ADeckEnemy::ApplyPoolPresentationState()
{
	const auto* Room = HasAuthority() && GetWorld() ? GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>() : nullptr;
	const bool bHostReady = HasAuthority() || PoolNetState.ActivationGeneration == 0 || (GetDeckHostShip() && GetDeckHostShip()->GetShipDeckMesh());
	const auto* Host = GetDeckHostShip();
	const bool bHostDormant = Host && (Host->GetRuntimeStateSnapshot().Phase == EEnemyShipRuntimePhase::Dormant
		|| Host->GetRuntimeStateSnapshot().Phase == EEnemyShipRuntimePhase::Restoring);
	const bool bPresent = bPoolActive && bHostReady && !bHostDormant && !(Room && Room->IsRestoringSnapshot());
	SetActorHiddenInGame(!bPresent);
	SetActorEnableCollision(bPresent);
	SetActorTickEnabled(bPresent);

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(bPresent && !bLocalDeathRagdollApplied ? InitialCapsuleCollision : ECollisionEnabled::NoCollision);
	}
	if (USkeletalMeshComponent* CharacterMesh = GetMesh())
	{
		// Pooling hides the actor, not the mesh component. Active presentation
		// defensively restores visibility because an authored default or a prior
		// presentation path may have left the component's bVisible flag disabled.
		if (bPresent)
		{
			CharacterMesh->SetVisibility(true, true);
		}
		if (!bPresent || !bLocalDeathRagdollApplied) CharacterMesh->SetCollisionEnabled(bPresent ? InitialMeshCollision : ECollisionEnabled::NoCollision);
	}
	if (EnemyHealthBarComponent)
	{
		EnemyHealthBarComponent->SetOwnerPresentationActive(bPresent);
	}
}

void ADeckEnemy::StopDeckMovement()
{
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
}

void ADeckEnemy::RestoreDeckMovementState()
{
	if (!bPoolActive || bDeathHandled || bLocalDeathRagdollApplied) return;
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Walking);
		Movement->bForceNextFloorCheck = true;
		if (AEnemyShip* Host = GetDeckHostShip(); Host && Host->GetShipDeckMesh())
		{
			if (const UDeckWalkAreaComponent* Area = Host->GetDeckWalkAreaComponent(); Area && Area->IsReady())
			{
				Movement->SetBase(Area->GetMovementBase(*this));
			}
		}
	}
}

void ADeckEnemy::RestoreForPoolActivation()
{
	if (GetDeckHostShip() && GetWorld())
	{
		if (UEnemyShipWeakeningWorldSubsystem* Weakening = GetWorld()->GetSubsystem<UEnemyShipWeakeningWorldSubsystem>())
		{
			Weakening->BeforeMemberBaseStatsReset(this);
		}
	}
	bDeathHandled = false;
	bWaveRemoveNotified = false;
	bHasDropped = false;
	if (EnemyHealthBarComponent)
	{
		EnemyHealthBarComponent->ResetRevealState();
	}

	if (UBaseHealthComponent* BaseHealth = GetHealthComponent())
	{
		BaseHealth->ResetForReuse();
	}
	// Changing MaxHealth can clamp Health downward. Do not interpret a new pool
	// configuration as combat damage or a death event.
	if (GetHealthComponent()) GetHealthComponent()->UninitializeFromAbilitySystem();
	const bool bAppliedBalance = ApplyBaseStatsForSpawn();
	if (GetHealthComponent()) GetHealthComponent()->InitializeWithAbilitySystem(GetAbilitySystemComponent());
	if (bAppliedBalance && GetWorld())
	{
		if (UEnemyShipWeakeningWorldSubsystem* Weakening = GetWorld()->GetSubsystem<UEnemyShipWeakeningWorldSubsystem>())
		{
			Weakening->AfterMemberBaseStatsReset(this);
		}
	}
	if (!bAppliedBalance) return;
	ResetInactivePresentation();
	StopDeckMovement();
}

void ADeckEnemy::ResetInactivePresentation(bool bRestoreWeapon)
{
	ResetLocalDeathRagdoll();
	if (EnemyHealthBarComponent) EnemyHealthBarComponent->ResetRevealState();
	if (bRestoreWeapon)
		if (UBaseWeaponComponent* Weapon = GetWeaponComponent()) Weapon->RestoreFromOwnerPool();
}

bool ADeckEnemy::ApplyAuthoritativeDeckStart(const FTransform& AuthoritativeTransform)
{
	if (!HasAuthority() || AuthoritativeTransform.ContainsNaN())
	{
		return false;
	}

	AEnemyShip* Host = GetDeckHostShip();
	UStaticMeshComponent* DeckMesh = Host ? Host->GetShipDeckMesh() : nullptr;
	if (!IsValid(Host) || !IsValid(DeckMesh)
		|| InitialSpawnPointId == INDEX_NONE
		|| !Host->GetDeckWaypoint(InitialSpawnPointId))
	{
		return false;
	}

	StopDeckMovement();
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->SetBase(nullptr);
	}
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetActorTransform(AuthoritativeTransform, false, nullptr, ETeleportType::TeleportPhysics);
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Walking);
		const UDeckWalkAreaComponent* Area = Host->GetDeckWalkAreaComponent();
		Movement->SetBase(Area && Area->IsReady() ? Area->GetMovementBase(*this) : DeckMesh);
		Movement->bForceNextFloorCheck = true;
		// SetBase can defer parts of based-movement bookkeeping until the next
		// movement update. The validated Host/Deck/Point contract is sufficient here.
		return true;
	}
	return false;
}

void ADeckEnemy::ClearAuthoritativeDeckBase()
{
	if (!HasAuthority())
	{
		return;
	}

	StopDeckMovement();
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->SetBase(nullptr);
	}
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
}


void ADeckEnemy::CaptureRoomDomains(TArray<FSWRoomDomainPart>& OutParts, TArray<FSWRoomCaptureIssue>& OutIssues) const
{
	Super::CaptureRoomDomains(OutParts, OutIssues);
	FSWRoomDeckEnemyPoolState State;
	State.bActive = bPoolActive; State.PointId = InitialSpawnPointId;
	State.ActivationGeneration = PoolNetState.ActivationGeneration;
	State.ReturnToPoolRemaining = FMath::Max(0.f, GetWorldTimerManager().GetTimerRemaining(ReturnToPoolTimerHandle));
	if (const auto* Id = GetDeckHostShip() ? GetDeckHostShip()->FindComponentByClass<USWRoomSnapshotComponent>() : nullptr) State.HostId = Id->StableId;
	if (!State.HostId.IsValid())
	{
		FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
		Issue.Domain = TEXT("Spawner"); Issue.FieldKey = TEXT("DeckHost"); Issue.Reason = TEXT("Deck host has no stable ID");
	}
	FSWRoomDomainPart& Part = OutParts.AddDefaulted_GetRef();
	Part.Domain = ESWRoomDomain::Spawner; Part.Version = 1;
	if (!FSWRoomStructCodec::Write(State, Part.Bytes))
	{
		OutParts.Pop(); FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
		Issue.Domain = TEXT("Spawner"); Issue.FieldKey = TEXT("DeckPool"); Issue.Reason = TEXT("Deck pool serialization failed");
	}
}

bool ADeckEnemy::RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& OutError)
{
	if (Part.Domain != ESWRoomDomain::Spawner)
	{
		if (Part.Domain == ESWRoomDomain::Enemy)
		{
			bRestoredPoolState = false;
			bAwaitingSnapshotCompletion = true;
			if (DeckEnemyNavigationComponent) DeckEnemyNavigationComponent->CancelCombatRoute();
			if (DeckWalkRouteComponent) DeckWalkRouteComponent->ResetNavigationState();
			GetWorldTimerManager().ClearTimer(ReturnToPoolTimerHandle);
			GetWorldTimerManager().ClearTimer(PoolRestoreResumeTimerHandle);
			StopDeckMovement();
			if (auto* AI = Cast<AAIController>(GetController()))
				if (auto* Brain = AI->GetBrainComponent()) Brain->StopLogic(TEXT("Deck pool snapshot restore"));
			ApplyPoolPresentationState();
		}
		return Super::RestoreRoomDomain(Part, OutError);
	}
	FSWRoomDeckEnemyPoolState State;
	if (Part.Version != 1 || !FSWRoomStructCodec::Read(Part.Bytes, State) || !State.HostId.IsValid()
		|| !FMath::IsFinite(State.ReturnToPoolRemaining) || State.ReturnToPoolRemaining < 0.f || State.PointId < INDEX_NONE)
	{ OutError = TEXT("Invalid deck enemy pool snapshot"); return false; }
	GetWorldTimerManager().ClearTimer(ReturnToPoolTimerHandle);
	PendingPoolRestore = State;
	bRestoredPoolState = true;
	bPoolActive = State.bActive;
	InitialSpawnPointId = State.PointId;
	PoolNetState.ActivationGeneration = State.ActivationGeneration;
	bPoolActivationPrepared = false;
	return true;
}

bool ADeckEnemy::CompareRoomDomain(const FSWRoomDomainPart& Expected, const FSWRoomDomainPart& Actual,
	float TimeToleranceSeconds, TArray<FString>& OutFields) const
{
	return Expected.Domain == ESWRoomDomain::Spawner ? FSWRoomStructCodec::Compare<FSWRoomDeckEnemyPoolState>(Expected, Actual, TimeToleranceSeconds, OutFields)
		: Super::CompareRoomDomain(Expected, Actual, TimeToleranceSeconds, OutFields);
}

bool ADeckEnemy::FinalizeRoomRestore(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError)
{
	if (!Super::FinalizeRoomRestore(RegisteredActors, OutError)) return false;
	if (bRestoredPoolState)
	{
		AActor* const* Actor = RegisteredActors.Find(PendingPoolRestore.HostId);
		AEnemyShip* Host = Actor ? Cast<AEnemyShip>(*Actor) : nullptr;
		if (!Host || (InitialSpawnPointId != INDEX_NONE && !Host->GetDeckWaypoint(InitialSpawnPointId)))
		{ OutError = TEXT("Restored deck enemy host/point missing"); return false; }
		SetHostShip(Host);
	}
	// Preserve saved health/death/effects. Completion performs presentation/AI only.
	RefreshPoolNetState();
	return true;
}

bool ADeckEnemy::AllowsMigratedRoomDomain(const FSWRoomDomainPart& Added, const TArray<FSWRoomDomainPart>& Previous) const
{
	return Added.Domain == ESWRoomDomain::Spawner && Added.Version == 1
		&& Previous.ContainsByPredicate([](const FSWRoomDomainPart& Part) { return Part.Domain == ESWRoomDomain::Enemy && Part.Version == 1; })
		&& !Previous.ContainsByPredicate([](const FSWRoomDomainPart& Part) { return Part.Domain == ESWRoomDomain::Spawner; });
}

void ADeckEnemy::RestoreLegacyPoolActivity(bool bActive)
{
	if (!HasAuthority() || bRestoredPoolState) return;
	bPoolActive = bActive;
	if (bActive && PoolNetState.ActivationGeneration == 0) PoolNetState.ActivationGeneration = 1;
	RefreshPoolNetState();
}

void ADeckEnemy::HandleRoomRestoreCompleted()
{
	if (!HasAuthority() || !bAwaitingSnapshotCompletion) return;
	RefreshPoolNetState();
	ApplyPoolPresentationState();
	if (bPoolActive && bDeathHandled)
	{
		const float Remaining = bRestoredPoolState ? PendingPoolRestore.ReturnToPoolRemaining : ReturnToPoolAfterDeathDelay;
		GetWorldTimerManager().SetTimer(ReturnToPoolTimerHandle, this, &ADeckEnemy::ReturnToPoolAfterDeath, FMath::Max(0.01f, Remaining), false);
	}
	else if (bPoolActive)
	{
		// Do not run behavior tasks inside the global restore audit or another actor's Finalize callback.
		PoolRestoreResumeTimerHandle = GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]
		{
			bAwaitingSnapshotCompletion = false;
			ResumePoolAI();
		}));
	}
	else StopDeckMovement();
	if (!bPoolActive || bDeathHandled) bAwaitingSnapshotCompletion = false;
	ForceNetUpdate();
}
