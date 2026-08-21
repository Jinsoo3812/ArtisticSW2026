#include "ShipAI/Abilities/GA_EnemyShipCannonVolley.h"

#include "BaseGameplayTags.h"
#include "Cannon.h"
#include "Components/StaticMeshComponent.h"
#include "Ship.h"
#include "ShipAI/Abilities/EnemyShipSkillMath.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipNavigationComponent.h"

UGA_EnemyShipCannonVolley::UGA_EnemyShipCannonVolley()
{
	SetNativeAbilityTag(GameplayAbility_EnemyShip_CannonVolley);
	CooldownDurationSeconds = 0.0f;
}

bool UGA_EnemyShipCannonVolley::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	const AEnemyShip* Ship = ActorInfo ? Cast<AEnemyShip>(ActorInfo->AvatarActor.Get()) : nullptr;
	const UEnemyShipNavigationComponent* Navigation = Ship ? Ship->GetNavigationComponent() : nullptr;
	const AShip* Target = Navigation ? Navigation->GetTargetShip() : nullptr;
	if (!Ship || !Ship->HasAuthority() || !IsValidPlayerTarget(Target))
	{
		return false;
	}

	for (const ACannon* Cannon : Ship->GetMountedCannons())
	{
		FVector ShotDirection;
		if (IsValid(Cannon) && Cannon->CanFireCannon()
			&& BuildShotDirection(Cannon, Target, ShotDirection)
			&& Cannon->CanAimAtWorldDirection(ShotDirection))
		{
			return true;
		}
	}
	return false;
}

void UGA_EnemyShipCannonVolley::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AEnemyShip* Ship = ActorInfo ? Cast<AEnemyShip>(ActorInfo->AvatarActor.Get()) : nullptr;
	UEnemyShipNavigationComponent* Navigation = Ship ? Ship->GetNavigationComponent() : nullptr;
	AShip* Target = Navigation ? Navigation->GetTargetShip() : nullptr;
	if (!Ship || !Ship->HasAuthority() || !IsValidPlayerTarget(Target)
		|| !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	Ship->RefreshMountedCannons();
	TArray<ACannon*> Cannons;
	for (ACannon* Cannon : Ship->GetMountedCannons())
	{
		if (IsValid(Cannon))
		{
			Cannons.AddUnique(Cannon);
		}
	}
	const FVector TargetLocation = Target->BuoyancyRoot
		? Target->BuoyancyRoot->GetComponentLocation()
		: Target->GetActorLocation();
	Cannons.StableSort([TargetLocation](const ACannon& A, const ACannon& B)
	{
		return FVector::DistSquared2D(A.GetActorLocation(), TargetLocation)
			< FVector::DistSquared2D(B.GetActorLocation(), TargetLocation);
	});

	int32 FiredCount = 0;
	for (ACannon* Cannon : Cannons)
	{
		if (FiredCount >= FMath::Max(1, MaxCannonsPerVolley))
		{
			break;
		}

		FVector ShotDirection;
		if (Cannon->CanFireCannon()
			&& BuildShotDirection(Cannon, Target, ShotDirection)
			&& Cannon->FireAICannonAtDirection(ShotDirection))
		{
			++FiredCount;
		}
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, FiredCount == 0);
}

bool UGA_EnemyShipCannonVolley::BuildShotDirection(
	const ACannon* Cannon,
	const AShip* Target,
	FVector& OutDirection) const
{
	if (!Cannon || !Target)
	{
		return false;
	}

	const float Speed = Cannon->GetResolvedFiringStats().ProjectileSpeed;
	const UWorld* World = Cannon->GetWorld();
	const float Gravity = World ? FMath::Abs(World->GetGravityZ()) : 0.0f;
	const FVector Start = Cannon->GetProjectileMuzzleTransform().GetLocation();
	const FVector End = Target->BuoyancyRoot
		? Target->BuoyancyRoot->GetComponentLocation()
		: Target->GetActorLocation();
	FVector Velocity;
	float FlightTime = 0.0f;
	float SolvedAngle = 0.0f;
	if (!FEnemyShipSkillMath::SuggestBallisticVelocity(
		Start,
		End,
		Speed,
		Gravity,
		PreferredLaunchAngleDegrees,
		Velocity,
		FlightTime,
		SolvedAngle))
	{
		return false;
	}

	OutDirection = Velocity.GetSafeNormal();
	return !OutDirection.IsNearlyZero();
}

bool UGA_EnemyShipCannonVolley::IsValidPlayerTarget(const AShip* Candidate) const
{
	return IsValid(Candidate)
		&& !Candidate->IsEnemyShipForEffects()
		&& Candidate->ActorHasTag(TEXT("Player"))
		&& !Candidate->ActorHasTag(TEXT("Enemy"));
}
