#include "DeckAI/DeckRangedEnemy.h"

#include "AbilitySystemComponent.h"
#include "AI/BaseAIController.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Components/BaseHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "ShipAI/EnemyShip.h"
#include "TimerManager.h"
#include "UI/EnemyHealthBarComponent.h"
#include "Weapon/BaseWeaponComponent.h"

ADeckRangedEnemy::ADeckRangedEnemy()
{
	bAutoResolveHostShip = false;
	bDestroyWithHostShip = false;
	bDestroyAfterDeathFinished = false;
	bAlwaysRelevant = false;
}

AEnemyShip* ADeckRangedEnemy::GetDeckHostShip() const
{
	return Cast<AEnemyShip>(GetHostShip());
}

void ADeckRangedEnemy::BeginPlay()
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
		ApplyFixedMovementState();
		ApplyPoolPresentationState();
	}
}

void ADeckRangedEnemy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ReturnToPoolTimerHandle);
	if (HasAuthority())
	{
		if (AEnemyShip* Host = GetDeckHostShip())
		{
			Host->ReleaseAllDeckPointsFor(this);
		}
		GoalPointReservation.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

void ADeckRangedEnemy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADeckRangedEnemy, bPoolActive);
	DOREPLIFETIME(ADeckRangedEnemy, CurrentDeckWaypointId);
	DOREPLIFETIME(ADeckRangedEnemy, PreviousDeckWaypointId);
	DOREPLIFETIME(ADeckRangedEnemy, GoalDeckWaypointId);
}

void ADeckRangedEnemy::PrepareForPool()
{
	bStartPooled = true;
	bPoolActive = false;
}

bool ADeckRangedEnemy::ActivateFromPool(
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
	FTransform AuthoritativeAnchorTransform;
	if (!InHostShip->ResolveFixedDeckAnchorTransform(
		InitialWaypointId, HalfHeight, AuthoritativeAnchorTransform)
		|| AuthoritativeAnchorTransform.ContainsNaN())
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
	if (!ApplyAuthoritativeDeckAnchor(AuthoritativeAnchorTransform))
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
		if (ABaseAIController* BaseAIController = Cast<ABaseAIController>(OwningAIController))
		{
			BaseAIController->RefreshBehaviorRouting();
		}
		else if (UBrainComponent* Brain = OwningAIController->GetBrainComponent())
		{
			Brain->RestartLogic();
		}
	}
	// Possession/Restart can restore CharacterMovement's default mode after the
	// anchor was applied. Reassert MOVE_None after all controller setup so an AI
	// startup path can never put this fixed emplacement back into Falling.
	ApplyFixedMovementState();

	ForceNetUpdate();
	return true;
}

void ADeckRangedEnemy::DeactivateToPool()
{
	if (!HasAuthority())
	{
		return;
	}

	SetNetDormancy(DORM_Awake);
	FlushNetDormancy();
	GetWorldTimerManager().ClearTimer(ReturnToPoolTimerHandle);
	ClearCombatTarget();
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
	ApplyFixedMovementState();

	bPoolActive = false;
	ClearAuthoritativeDeckAnchor();
	CurrentDeckWaypointId = INDEX_NONE;
	PreviousDeckWaypointId = INDEX_NONE;
	GoalDeckWaypointId = INDEX_NONE;
	ApplyPoolPresentationState();
	ForceNetUpdate();
	SetNetDormancy(DORM_DormantAll);
}

void ADeckRangedEnemy::MarkGoalDeckWaypointReached()
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

bool ADeckRangedEnemy::TrySetGoalDeckWaypointId(int32 NewGoalWaypointId)
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

void ADeckRangedEnemy::OnDeckMoveFailed()
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

void ADeckRangedEnemy::HandleDeath_Implementation()
{
	ApplyFixedMovementState();
}

void ADeckRangedEnemy::HandleDeathFinishedPresentation()
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
			&ADeckRangedEnemy::ReturnToPoolAfterDeath,
			ReturnToPoolAfterDeathDelay,
			false);
	}
}

void ADeckRangedEnemy::OnRep_PoolActive()
{
	if (bPoolActive)
	{
		ResetLocalDeathRagdoll();
	}
	ApplyFixedMovementState();
	ApplyPoolPresentationState();
}

void ADeckRangedEnemy::ReturnToPoolAfterDeath()
{
	DeactivateToPool();
}

void ADeckRangedEnemy::ApplyPoolPresentationState()
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

void ADeckRangedEnemy::ApplyFixedMovementState()
{
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
}

void ADeckRangedEnemy::RestoreForPoolActivation()
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
	ApplyFixedMovementState();
	if (UBaseWeaponComponent* BaseWeaponComponent = GetWeaponComponent())
	{
		BaseWeaponComponent->RestoreFromOwnerPool();
	}
}

bool ADeckRangedEnemy::ApplyAuthoritativeDeckAnchor(const FTransform& AuthoritativeTransform)
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

	ApplyFixedMovementState();
	SetBase(nullptr);
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetActorTransform(AuthoritativeTransform, false, nullptr, ETeleportType::TeleportPhysics);
	AttachToComponent(DeckMesh, FAttachmentTransformRules::KeepWorldTransform);
	return GetRootComponent()
		&& GetRootComponent()->GetAttachParent() == DeckMesh;
}

void ADeckRangedEnemy::ClearAuthoritativeDeckAnchor()
{
	if (!HasAuthority())
	{
		return;
	}

	ApplyFixedMovementState();
	SetBase(nullptr);
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
}
