#include "DeckAI/DeckRangedEnemy.h"

#include "AbilitySystemComponent.h"
#include "AI/BaseAIController.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Components/BaseHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DeckAI/DeckEnemyNavigationComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "ShipAI/EnemyShip.h"
#include "TimerManager.h"
#include "UI/EnemyHealthBarComponent.h"
#include "Weapon/BaseWeaponComponent.h"

ADeckEnemy::ADeckEnemy()
{
	DeckEnemyNavigationComponent = CreateDefaultSubobject<UDeckEnemyNavigationComponent>(
		TEXT("DeckEnemyNavigationComponent"));
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
		GoalPointReservation.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

void ADeckEnemy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADeckEnemy, bPoolActive);
	DOREPLIFETIME(ADeckEnemy, CurrentDeckWaypointId);
	DOREPLIFETIME(ADeckEnemy, PreviousDeckWaypointId);
	DOREPLIFETIME(ADeckEnemy, GoalDeckWaypointId);
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
		InitialWaypointId, HalfHeight, AuthoritativeStartTransform)
		|| InHostShip->ResolveFixedDeckAnchorTransform(
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
	CurrentDeckWaypointId = InitialWaypointId;
	PreviousDeckWaypointId = INDEX_NONE;
	GoalDeckWaypointId = INDEX_NONE;
	DeckRandomStream.Initialize(RandomSeed);

	RestoreForPoolActivation();
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
	ClearCombatTarget();
	if (DeckEnemyNavigationComponent)
	{
		DeckEnemyNavigationComponent->CancelCombatRoute();
	}
	if (AEnemyShip* Host = GetDeckHostShip())
	{
		Host->ReleaseDeckPointReservation(GoalPointReservation);
		Host->ReleaseAllDeckPointsFor(this);
	}
	else
	{
		GoalPointReservation.Reset();
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
	StopDeckMovement();

	bPoolActive = false;
	ClearAuthoritativeDeckBase();
	CurrentDeckWaypointId = INDEX_NONE;
	PreviousDeckWaypointId = INDEX_NONE;
	GoalDeckWaypointId = INDEX_NONE;
	ApplyPoolPresentationState();
	ForceNetUpdate();
	SetNetDormancy(DORM_DormantAll);
}

void ADeckEnemy::MarkGoalDeckWaypointReached()
{
	if (GoalDeckWaypointId == INDEX_NONE)
	{
		return;
	}

	AEnemyShip* Host = GetDeckHostShip();
	if (!Host || !Host->CommitDeckPointReservation(GoalPointReservation, this))
	{
		OnDeckMoveFailed();
		return;
	}
	GoalPointReservation.Reset();
	Host->ReleaseDeckPointOccupancy(CurrentDeckWaypointId, this);
	PreviousDeckWaypointId = CurrentDeckWaypointId;
	CurrentDeckWaypointId = GoalDeckWaypointId;
	GoalDeckWaypointId = INDEX_NONE;
	ForceNetUpdate();
}

bool ADeckEnemy::TrySetGoalDeckWaypointId(int32 NewGoalWaypointId)
{
	AEnemyShip* Host = GetDeckHostShip();
	if (!HasAuthority() || !Host || !bPoolActive)
	{
		return false;
	}
	if (NewGoalWaypointId == GoalDeckWaypointId && GoalPointReservation.IsValid())
	{
		return true;
	}
	Host->ReleaseDeckPointReservation(GoalPointReservation);
	GoalDeckWaypointId = INDEX_NONE;
	if (NewGoalWaypointId == INDEX_NONE)
	{
		ForceNetUpdate();
		return true;
	}
	if (NewGoalWaypointId == CurrentDeckWaypointId
		|| !Host->TryReserveDeckPoint(NewGoalWaypointId, this, GoalPointReservation))
	{
		ForceNetUpdate();
		return false;
	}
	GoalDeckWaypointId = NewGoalWaypointId;
	ForceNetUpdate();
	return true;
}

void ADeckEnemy::OnDeckMoveFailed()
{
	if (AEnemyShip* Host = GetDeckHostShip())
	{
		Host->ReleaseDeckPointReservation(GoalPointReservation);
	}
	else
	{
		GoalPointReservation.Reset();
	}
	GoalDeckWaypointId = INDEX_NONE;
	ForceNetUpdate();
}

void ADeckEnemy::HandleDeath_Implementation()
{
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
			Movement->SetBase(Host->GetShipDeckMesh());
		}
	}
}

void ADeckEnemy::RestoreForPoolActivation()
{
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
		|| CurrentDeckWaypointId == INDEX_NONE
		|| !Host->GetDeckWaypoint(CurrentDeckWaypointId))
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
		Movement->SetBase(DeckMesh);
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
