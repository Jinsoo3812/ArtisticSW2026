#include "BossAI/ShipBossEnemy.h"

#include "AbilitySystemComponent.h"
#include "AI/BaseAIController.h"
#include "BaseGameplayTags.h"
#include "BossAI/ShipBossAIController.h"
#include "Components/BaseHealthComponent.h"
#include "GAS/Ability/Boss/GA_BossDashSlash.h"
#include "GAS/Ability/Boss/GA_BossKnockback.h"
#include "GAS/Ability/Boss/GA_BossVanish.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "ShipAI/EnemyShip.h"

AShipBossEnemy::AShipBossEnemy()
{
	// Boss damage feedback is intentionally stronger and must not leak into the
	// regular enemy defaults inherited by melee and ranged archetypes.
	GetHealthComponent()->SetDamageGameplayCueTag(GameplayCue_Boss_Hit);

	DashDamageVolume = CreateDefaultSubobject<USphereComponent>(TEXT("DashDamageVolume"));
	DashDamageVolume->SetupAttachment(GetRootComponent());
	DashDamageVolume->InitSphereRadius(120.0f);
	DashDamageVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DashDamageVolume->SetCollisionObjectType(ECC_WorldDynamic);
	DashDamageVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	DashDamageVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	DashDamageVolume->SetGenerateOverlapEvents(true);
	DashDamageVolume->SetCanEverAffectNavigation(false);

	AIControllerClass = AShipBossAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	bUseControllerRotationYaw = true;
	bAlwaysRelevant = true;
	bDestroyAfterDeathFinished = true;
	bEquipWeaponOnSpawn = true;
	DefaultWeaponTag = Item_EnemyWeapon_Sword;
	StartingAbilities.Add(UGA_BossKnockback::StaticClass());
	StartingAbilities.Add(UGA_BossVanish::StaticClass());
	StartingAbilities.Add(UGA_BossDashSlash::StaticClass());

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = 0.0f;
		Movement->bOrientRotationToMovement = false;
		Movement->bUseControllerDesiredRotation = true;
	}
}

void AShipBossEnemy::BeginPlay()
{
	InitialCapsuleCollision = GetCapsuleComponent()
		? GetCapsuleComponent()->GetCollisionEnabled()
		: ECollisionEnabled::QueryAndPhysics;
	Super::BeginPlay();

	BindHostShip();
	ApplyHiddenPresentation();
	if (HasAuthority())
	{
		TransitionBossAIState(FGameplayTag(), AI_State_Boss_Intro);
	}
}

void AShipBossEnemy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseSummonedDeckEnemies();
	UnbindHostShip();
	Super::EndPlay(EndPlayReason);
}

void AShipBossEnemy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShipBossEnemy, HostShip);
	DOREPLIFETIME(AShipBossEnemy, CurrentPointId);
	DOREPLIFETIME(AShipBossEnemy, PreviousPointId);
	DOREPLIFETIME(AShipBossEnemy, DestinationPointId);
	DOREPLIFETIME(AShipBossEnemy, bBossHidden);
}

bool AShipBossEnemy::InitializeBoss(AEnemyShip* InHostShip, int32 InitialPointId, AActor* InitialTarget)
{
	if (!HasAuthority() || !IsValid(InHostShip) || !CanEngageActor(InitialTarget))
	{
		return false;
	}

	UnbindHostShip();
	HostShip = InHostShip;
	CurrentPointId = InitialPointId;
	PreviousPointId = INDEX_NONE;
	DestinationPointId = INDEX_NONE;
	BindHostShip();
	SetBossCombatTarget(InitialTarget);

	if (UStaticMeshComponent* DeckMesh = HostShip->GetShipDeckMesh())
	{
		SetBase(DeckMesh);
	}
	TransitionBossAIState(AI_State_Boss_Intro, AI_State_Boss_Combat);
	ForceNetUpdate();
	return true;
}

void AShipBossEnemy::SetBossCombatTarget(AActor* NewTarget)
{
	if (!HasAuthority())
	{
		return;
	}

	BossCombatTarget = CanEngageActor(NewTarget) ? NewTarget : nullptr;
	if (ABaseAIController* BossController = Cast<ABaseAIController>(GetController()))
	{
		if (BossCombatTarget)
		{
			BossController->SetCombatTarget(BossCombatTarget);
		}
		else
		{
			BossController->ClearCombatTarget(false);
		}
	}
}

AActor* AShipBossEnemy::GetBossCombatTarget() const
{
	if (const ABaseAIController* BossController = Cast<ABaseAIController>(GetController()))
	{
		if (AActor* ControllerTarget = BossController->GetCombatTarget())
		{
			return ControllerTarget;
		}
	}
	return IsValid(BossCombatTarget) ? BossCombatTarget.Get() : nullptr;
}

void AShipBossEnemy::MarkDestinationReached()
{
	if (!HasAuthority() || DestinationPointId == INDEX_NONE)
	{
		return;
	}
	PreviousPointId = CurrentPointId;
	CurrentPointId = DestinationPointId;
	DestinationPointId = INDEX_NONE;
	ForceNetUpdate();
}

void AShipBossEnemy::OnDeckMoveFailed()
{
	if (!HasAuthority())
	{
		return;
	}

	DestinationPointId = INDEX_NONE;
	ForceNetUpdate();
}

bool AShipBossEnemy::CanMoveOnDeck() const
{
	return HasAuthority() && !bDeathHandled && !bBossHidden && IsValid(HostShip);
}

bool AShipBossEnemy::CanSummonDeckEnemy() const
{
	if (!HasAuthority() || bDeathHandled || !IsValid(HostShip) || !CanEngageActor(GetBossCombatTarget()))
	{
		return false;
	}
	if (const UWorld* World = GetWorld(); !World || World->GetTimeSeconds() < NextSummonAllowedTime)
	{
		return false;
	}

	int32 ActiveCount = 0;
	for (const TWeakObjectPtr<ADeckRangedEnemy>& EnemyPtr : SummonedDeckEnemies)
	{
		const ADeckRangedEnemy* Enemy = EnemyPtr.Get();
		if (Enemy && Enemy->IsPoolActive()
			&& Enemy->GetHealthComponent() && !Enemy->GetHealthComponent()->IsDead())
		{
			++ActiveCount;
		}
	}
	return ActiveCount < FMath::Max(1, MaxSummonedDeckEnemies);
}

bool AShipBossEnemy::TrySummonDeckEnemy(ADeckRangedEnemy*& OutEnemy)
{
	OutEnemy = nullptr;
	if (!CanSummonDeckEnemy())
	{
		return false;
	}

	AActor* Target = GetBossCombatTarget();
	UWorld* World = GetWorld();
	NextSummonAllowedTime = World->GetTimeSeconds() + FMath::Max(0.0f, SummonCooldown);
	SummonedDeckEnemies.RemoveAll([](const TWeakObjectPtr<ADeckRangedEnemy>& EnemyPtr)
	{
		const ADeckRangedEnemy* Enemy = EnemyPtr.Get();
		return !Enemy || !Enemy->IsPoolActive()
			|| (Enemy->GetHealthComponent() && Enemy->GetHealthComponent()->IsDead());
	});

	TArray<int32> LinkedIds;
	HostShip->GetConnectedDeckWaypointIds(CurrentPointId, LinkedIds);
	TArray<int32> CandidateIds;
	HostShip->GetDeckWaypointIds(CandidateIds);
	const FVector DeckUp = HostShip->GetShipDeckMesh()
		? HostShip->GetShipDeckMesh()->GetUpVector().GetSafeNormal()
		: FVector::UpVector;
	CandidateIds.RemoveAll([this, Target, DeckUp](const int32 PointId)
	{
		const UDeckWaypointComponent* Waypoint = HostShip->GetDeckWaypoint(PointId);
		if (!Waypoint || !Waypoint->CanSpawnEnemy() || !Waypoint->CanUseInCombat()
			|| PointId == CurrentPointId)
		{
			return true;
		}
		const FVector PointLocation = Waypoint->GetComponentLocation();
		const float TargetDistance = FVector::VectorPlaneProject(
			PointLocation - Target->GetActorLocation(), DeckUp).Size();
		const float BossDistance = FVector::VectorPlaneProject(
			PointLocation - GetActorLocation(), DeckUp).Size();
		return TargetDistance < FMath::Max(0.0f, MinimumSummonDistanceFromTarget)
			|| BossDistance < FMath::Max(0.0f, MinimumSummonDistanceFromBoss);
	});
	CandidateIds.Sort([this, Target, &LinkedIds](const int32 LeftId, const int32 RightId)
	{
		const bool bLeftLinked = LinkedIds.Contains(LeftId);
		const bool bRightLinked = LinkedIds.Contains(RightId);
		if (bLeftLinked != bRightLinked)
		{
			return bLeftLinked;
		}
		const float LeftDistance = FVector::DistSquared(
			HostShip->GetDeckWaypointWorldLocation(LeftId), Target->GetActorLocation());
		const float RightDistance = FVector::DistSquared(
			HostShip->GetDeckWaypointWorldLocation(RightId), Target->GetActorLocation());
		return !FMath::IsNearlyEqual(LeftDistance, RightDistance)
			? LeftDistance > RightDistance
			: LeftId < RightId;
	});

	for (const int32 CandidateId : CandidateIds)
	{
		if (HostShip->ActivateDeckEnemyAtPoint(CandidateId, Target, OutEnemy))
		{
			SummonedDeckEnemies.Add(OutEnemy);
			return true;
		}
	}
	return false;
}

bool AShipBossEnemy::ResolvePointTransform(int32 PointId, FTransform& OutTransform) const
{
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f;
	if (!HostShip || !HostShip->ResolveDeckCharacterTransform(PointId, HalfHeight, OutTransform))
	{
		OutTransform = FTransform::Identity;
		return false;
	}
	return true;
}

bool AShipBossEnemy::TransitionBossAIState(FGameplayTag ExpectedState, FGameplayTag NewState)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!HasAuthority() || !ASC || !IsExclusiveBossAIState(NewState))
	{
		return false;
	}
	if (ExpectedState.IsValid() && !ASC->HasMatchingGameplayTag(ExpectedState))
	{
		return false;
	}

	ASC->RemoveLooseGameplayTag(AI_State_Boss_Intro);
	ASC->RemoveLooseGameplayTag(AI_State_Boss_Combat);
	ASC->RemoveLooseGameplayTag(AI_State_Boss_Dead);
	ASC->AddLooseGameplayTag(NewState);
	return true;
}

void AShipBossEnemy::SetBossHidden(bool bInHidden)
{
	if (!HasAuthority() || bBossHidden == bInHidden)
	{
		return;
	}
	bBossHidden = bInHidden;
	ApplyHiddenPresentation();
	ForceNetUpdate();
}

void AShipBossEnemy::HandleDeath_Implementation()
{
	if (HasAuthority())
	{
		ReleaseSummonedDeckEnemies();
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
		{
			ASC->CancelAllAbilities();
		}
		SetBossHidden(false);
		TransitionBossAIState(FGameplayTag(), AI_State_Boss_Dead);
	}
	Super::HandleDeath_Implementation();
}

void AShipBossEnemy::ReleaseSummonedDeckEnemies()
{
	if (HasAuthority())
	{
		for (const TWeakObjectPtr<ADeckRangedEnemy>& EnemyPtr : SummonedDeckEnemies)
		{
			if (ADeckRangedEnemy* Enemy = EnemyPtr.Get(); Enemy && Enemy->IsPoolActive())
			{
				Enemy->DeactivateToPool();
			}
		}
	}
	SummonedDeckEnemies.Reset();
}

void AShipBossEnemy::OnRep_HostShip()
{
	BindHostShip();
	if (HostShip && HostShip->GetShipDeckMesh())
	{
		SetBase(HostShip->GetShipDeckMesh());
	}
}

void AShipBossEnemy::OnRep_BossHidden()
{
	ApplyHiddenPresentation();
}

void AShipBossEnemy::HandleHostShipDestroyed(AActor* DestroyedActor)
{
	if (HasAuthority() && DestroyedActor == HostShip && !IsActorBeingDestroyed())
	{
		Destroy();
	}
}

void AShipBossEnemy::BindHostShip()
{
	if (HostShip)
	{
		HostShip->OnDestroyed.AddUniqueDynamic(this, &AShipBossEnemy::HandleHostShipDestroyed);
	}
}

void AShipBossEnemy::UnbindHostShip()
{
	if (HostShip)
	{
		HostShip->OnDestroyed.RemoveDynamic(this, &AShipBossEnemy::HandleHostShipDestroyed);
	}
}

void AShipBossEnemy::ApplyHiddenPresentation()
{
	SetActorHiddenInGame(bBossHidden);
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(bBossHidden ? ECollisionEnabled::NoCollision : InitialCapsuleCollision);
	}

	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors);
	for (AActor* AttachedActor : AttachedActors)
	{
		if (AttachedActor)
		{
			AttachedActor->SetActorHiddenInGame(bBossHidden);
		}
	}
}

bool AShipBossEnemy::IsExclusiveBossAIState(FGameplayTag StateTag) const
{
	return StateTag == AI_State_Boss_Intro
		|| StateTag == AI_State_Boss_Combat
		|| StateTag == AI_State_Boss_Dead;
}
