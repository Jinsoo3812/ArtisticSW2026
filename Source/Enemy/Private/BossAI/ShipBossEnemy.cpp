#include "BossAI/ShipBossEnemy.h"

#include "AbilitySystemComponent.h"
#include "AI/BaseAIController.h"
#include "BaseGameplayTags.h"
#include "BossAI/ShipBossAIController.h"
#include "GAS/Ability/Boss/GA_BossDashSlash.h"
#include "GAS/Ability/Boss/GA_BossKnockback.h"
#include "GAS/Ability/Boss/GA_BossVanish.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "ShipAI/EnemyShip.h"

AShipBossEnemy::AShipBossEnemy()
{
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
	UnbindHostShip();
	Super::EndPlay(EndPlayReason);
}

void AShipBossEnemy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShipBossEnemy, HostShip);
	DOREPLIFETIME(AShipBossEnemy, CurrentPointId);
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
	CurrentPointId = DestinationPointId;
	DestinationPointId = INDEX_NONE;
	ForceNetUpdate();
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
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
		{
			ASC->CancelAllAbilities();
		}
		SetBossHidden(false);
		TransitionBossAIState(FGameplayTag(), AI_State_Boss_Dead);
	}
	Super::HandleDeath_Implementation();
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
