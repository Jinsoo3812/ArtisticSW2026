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
#include "DeckAI/DeckWalkRouteComponent.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipWeakeningWorldSubsystem.h"
#include "TimerManager.h"
#include "UI/EnemyHealthBarComponent.h"
#include "Weapon/BaseWeaponComponent.h"

ADeckEnemy::ADeckEnemy()
{
	DeckEnemyNavigationComponent = CreateDefaultSubobject<UDeckEnemyNavigationComponent>(
		TEXT("DeckEnemyNavigationComponent"));
	DeckWalkRouteComponent = CreateDefaultSubobject<UDeckWalkRouteComponent>(TEXT("DeckWalkRouteComponent"));
	bAutoResolveHostShip = false;
	bDestroyWithHostShip = false;
	bDestroyAfterDeathFinished = false;
	bAlwaysRelevant = false;
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->bBaseOnAttachmentRoot = true;
	}
}

float ADeckEnemy::GetPreferredDeckCombatRange() const
{
	return DeckCombatRole == EDeckEnemyCombatRole::Melee
		? 0.0f
		: (GetMinAttackRange() + GetMaxAttackRange()) * 0.5f;
}

void ADeckEnemy::HandleRangedReleaseLineOfSightBlocked(AActor* TargetActor)
{
	if (HasAuthority() && DeckCombatRole == EDeckEnemyCombatRole::Ranged
		&& DeckEnemyNavigationComponent)
	{
		DeckEnemyNavigationComponent->RequestReleaseLineOfSightReposition(TargetActor);
	}
}

bool ADeckEnemy::CanMoveOnDeck() const
{
	return HasAuthority() && bPoolActive && !bDeathHandled && IsValid(GetDeckHostShip());
}

AEnemyShip* ADeckEnemy::GetDeckHostShip() const
{
	return Cast<AEnemyShip>(GetHostShip());
}

void ADeckEnemy::BeginPlay()
{
	InitialCapsuleCollision = GetCapsuleComponent()
		? GetCapsuleComponent()->GetCollisionEnabled()
		: ECollisionEnabled::QueryAndPhysics;
	InitialMeshCollision = GetMesh()
		? GetMesh()->GetCollisionEnabled()
		: ECollisionEnabled::QueryOnly;

	Super::BeginPlay();

	if (HasAuthority() && bStartPooled)
	{
		DeactivateToPool();
	}
	else
	{
		RestoreDeckMovementState();
		ApplyPoolPresentationState();
	}
}

void ADeckEnemy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ReturnToPoolTimerHandle);
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
	DOREPLIFETIME(ADeckEnemy, bPoolActive);
	DOREPLIFETIME(ADeckEnemy, InitialSpawnPointId);
}

void ADeckEnemy::PrepareForPool()
{
	bStartPooled = true;
	bPoolActive = false;
}

bool ADeckEnemy::ActivateFromPool(
	AEnemyShip* InHostShip,
	int32 InitialWaypointId,
	int32 RandomSeed)
{
	if (!HasAuthority() || bPoolActive || !IsValid(InHostShip)
		|| !InHostShip->GetShipDeckMesh()
		|| !InHostShip->GetDeckWaypoint(InitialWaypointId))
	{
		return false;
	}
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f;
	FTransform AuthoritativeStartTransform;
	const bool bResolvedStart = InHostShip->ResolveDeckCharacterTransform(
		InitialWaypointId, HalfHeight, AuthoritativeStartTransform);
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
	bPoolActive = true;
	ApplyPoolPresentationState();

	if (!GetController())
	{
		SpawnDefaultController();
	}
	if (AAIController* OwningAIController = Cast<AAIController>(GetController()))
	{
		if (ABaseAIController* BaseAIController = Cast<ABaseAIController>(OwningAIController);
			BaseAIController && BaseAIController->GetBrainComponent())
		{
			BaseAIController->RefreshBehaviorRouting();
		}
		else if (UBrainComponent* Brain = OwningAIController->GetBrainComponent())
		{
			Brain->RestartLogic();
		}
	}
	// Possession/Restart may replace the movement mode. Reassert the live deck
	// movement base after controller initialization.
	RestoreDeckMovementState();

	InHostShip->NotifyCrewEnemyReactivated(this);
	ForceNetUpdate();
	return true;
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
	if (DeckWalkRouteComponent) DeckWalkRouteComponent->ClearGoal();
	ClearCombatTarget();
	if (DeckEnemyNavigationComponent)
	{
		DeckEnemyNavigationComponent->CancelCombatRoute();
	}
	if (AEnemyShip* Host = GetDeckHostShip())
	{
		Host->ReleaseAllDeckPointsFor(this);
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
	ClearAuthoritativeDeckBase();
	InitialSpawnPointId = INDEX_NONE;
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
	RestoreForPoolActivation();
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
	const AEnemyShip* Ship = GetDeckHostShip();
	const UDeckWalkAreaComponent* Area = Ship ? Ship->GetDeckWalkAreaComponent() : nullptr;
	FDeckWalkLocation SelfFloor, TargetFloor;
	if (!Area || !Area->ResolveActorOnDeck(*this, SelfFloor)
		|| !Candidate || !Area->ResolveActorOnDeck(*Candidate, TargetFloor))
	{
		OutReason = TEXT("NoWalkableCombatFloor");
		return false;
	}
	if (SelfFloor.SurfaceId != TargetFloor.SurfaceId)
	{
		OutReason = TEXT("DifferentCombatSurface");
		return false;
	}
	return Super::EvaluateAttackTarget(Candidate, bRequireLineOfSight, OutReason);
}

void ADeckEnemy::OnDeckMoveReached()
{
	if (DeckEnemyNavigationComponent) DeckEnemyNavigationComponent->CompleteReleaseLineOfSightReposition();
}

void ADeckEnemy::OnDeckMoveFailed()
{
	if (DeckEnemyNavigationComponent) DeckEnemyNavigationComponent->CancelCombatRoute();
}

void ADeckEnemy::HandleDeath_Implementation()
{
	if (DeckWalkRouteComponent) DeckWalkRouteComponent->ClearGoal();
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

	if (!HasAuthority())
	{
		return;
	}

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
	if (bPoolActive)
	{
		ResetLocalDeathRagdoll();
	}
	if (!bPoolActive)
	{
		StopDeckMovement();
	}
	ApplyPoolPresentationState();
}

void ADeckEnemy::ReturnToPoolAfterDeath()
{
	DeactivateToPool();
}

void ADeckEnemy::ApplyPoolPresentationState()
{
	const bool bPresent = bPoolActive;
	SetActorHiddenInGame(!bPresent);
	SetActorEnableCollision(bPresent);
	SetActorTickEnabled(bPresent);

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(bPresent ? InitialCapsuleCollision : ECollisionEnabled::NoCollision);
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
		CharacterMesh->SetCollisionEnabled(bPresent ? InitialMeshCollision : ECollisionEnabled::NoCollision);
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
	if (!bPoolActive)
	{
		return;
	}
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
	if (USkeletalMeshComponent* CharacterMesh = GetMesh())
	{
		ResetLocalDeathRagdoll();
	}
	StopDeckMovement();
	if (UBaseWeaponComponent* BaseWeaponComponent = GetWeaponComponent())
	{
		BaseWeaponComponent->RestoreFromOwnerPool();
	}
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
