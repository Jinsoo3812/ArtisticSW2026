#include "DeckAI/DeckRangedEnemy.h"

#include "AbilitySystemComponent.h"
#include "AI/BaseAIController.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Components/BaseHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "ShipAI/EnemyShip.h"
#include "TimerManager.h"

ADeckRangedEnemy::ADeckRangedEnemy()
{
	bAutoResolveHostShip = false;
	bDestroyWithHostShip = false;
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
		ApplyPoolPresentationState();
	}
}

void ADeckRangedEnemy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ReturnToPoolTimerHandle);
	Super::EndPlay(EndPlayReason);
}

void ADeckRangedEnemy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADeckRangedEnemy, bPoolActive);
}

void ADeckRangedEnemy::PrepareForPool()
{
	bStartPooled = true;
	bPoolActive = false;
}

bool ADeckRangedEnemy::ActivateFromPool(
	AEnemyShip* InHostShip,
	const FTransform& SpawnTransform,
	int32 InitialWaypointId,
	int32 RandomSeed)
{
	if (!HasAuthority() || bPoolActive || !IsValid(InHostShip))
	{
		return false;
	}

	SetNetDormancy(DORM_Awake);
	FlushNetDormancy();
	GetWorldTimerManager().ClearTimer(ReturnToPoolTimerHandle);

	SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);
	SetHostShip(InHostShip);
	CurrentDeckWaypointId = InitialWaypointId;
	PreviousDeckWaypointId = INDEX_NONE;
	GoalDeckWaypointId = INDEX_NONE;
	DeckRandomStream.Initialize(RandomSeed);

	RestoreForPoolActivation();
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

	if (InHostShip->ShipDeckMesh)
	{
		SetBase(InHostShip->ShipDeckMesh);
	}

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

	bPoolActive = false;
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

	PreviousDeckWaypointId = CurrentDeckWaypointId;
	CurrentDeckWaypointId = GoalDeckWaypointId;
	GoalDeckWaypointId = INDEX_NONE;
}

void ADeckRangedEnemy::HandleDeath_Implementation()
{
	if (!HasAuthority())
	{
		return;
	}

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->DisableMovement();
		Movement->StopMovementImmediately();
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
	ApplyPoolPresentationState();
}

void ADeckRangedEnemy::ReturnToPoolAfterDeath()
{
	DeactivateToPool();
}

void ADeckRangedEnemy::ApplyPoolPresentationState()
{
	SetActorHiddenInGame(!bPoolActive);
	SetActorEnableCollision(bPoolActive);
	SetActorTickEnabled(bPoolActive);

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(bPoolActive ? InitialCapsuleCollision : ECollisionEnabled::NoCollision);
	}
	if (USkeletalMeshComponent* CharacterMesh = GetMesh())
	{
		CharacterMesh->SetVisibility(bPoolActive, true);
		CharacterMesh->SetCollisionEnabled(bPoolActive ? InitialMeshCollision : ECollisionEnabled::NoCollision);
	}
	if (HealthBarWidgetComponent)
	{
		HealthBarWidgetComponent->SetVisibility(
			bPoolActive && HealthBarVisibilityPolicy == EEnemyHealthBarVisibilityPolicy::AlwaysVisible);
	}
}

void ADeckRangedEnemy::RestoreForPoolActivation()
{
	bDeathHandled = false;
	bWaveRemoveNotified = false;
	bHasDropped = false;

	if (UBaseHealthComponent* BaseHealth = GetHealthComponent())
	{
		BaseHealth->ResetForReuse();
	}
	if (USkeletalMeshComponent* CharacterMesh = GetMesh())
	{
		CharacterMesh->SetSimulatePhysics(false);
		CharacterMesh->SetAllBodiesSimulatePhysics(false);
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->SetMovementMode(MOVE_Walking);
	}
}
