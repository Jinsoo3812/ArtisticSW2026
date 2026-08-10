#include "ShipAI/Abilities/GA_EnemyShipLaunchTorpedo.h"

#include "BaseGameplayTags.h"
#include "Cannon.h"
#include "Engine/World.h"
#include "Ship.h"
#include "ShipAI/Abilities/EnemyShipSkillMath.h"
#include "ShipAI/Abilities/EnemyShipTorpedo.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipNavigationComponent.h"

UGA_EnemyShipLaunchTorpedo::UGA_EnemyShipLaunchTorpedo()
{
	SetNativeAbilityAndCooldownTags(
		GameplayAbility_EnemyShip_LaunchTorpedo,
		Cooldown_EnemyShip_LaunchTorpedo);
	CooldownDurationSeconds = 6.0f;
	TorpedoClass = AEnemyShipTorpedo::StaticClass();
}

void UGA_EnemyShipLaunchTorpedo::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AEnemyShip* Ship = ActorInfo ? Cast<AEnemyShip>(ActorInfo->AvatarActor.Get()) : nullptr;
	UEnemyShipNavigationComponent* Navigation = Ship ? Ship->GetNavigationComponent() : nullptr;
	AShip* Target = Navigation ? Navigation->GetTargetShip() : nullptr;
	if (!Ship || !Ship->HasAuthority() || Ship->IsDeathHandled() || !IsValidPlayerTarget(Target)
		|| !TorpedoClass || !Ship->GetWorld())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	Ship->RefreshMountedCannons();
	const FVector TargetShipLocation = Target->BuoyancyRoot
		? Target->BuoyancyRoot->GetComponentLocation()
		: Target->GetActorLocation();
	ACannon* Cannon = SelectClosestCannon(Ship, TargetShipLocation);
	if (!Cannon)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const FCannonResolvedFiringStats FiringStats = Cannon->GetResolvedFiringStats();
	const FTransform MuzzleTransform = Cannon->GetProjectileMuzzleTransform();
	FVector TargetForward = Target->GetActorForwardVector();
	TargetForward.Z = 0.0f;
	TargetForward.Normalize();
	const FVector TargetPoint = TargetShipLocation
		+ TargetForward * (TargetForwardOffsetMeters * 100.0f);

	FVector LaunchVelocity;
	float FlightTime = 0.0f;
	float SolvedAngle = 0.0f;
	const float GravityMagnitude = FMath::Abs(Ship->GetWorld()->GetGravityZ());
	if (!FEnemyShipSkillMath::SuggestBallisticVelocity(
		MuzzleTransform.GetLocation(),
		TargetPoint,
		FiringStats.ProjectileSpeed,
		GravityMagnitude,
		PreferredLaunchAngleDegrees,
		LaunchVelocity,
		FlightTime,
		SolvedAngle))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const FVector LocalLaunchDirection = Cannon->GetActorTransform()
		.InverseTransformVectorNoScale(LaunchVelocity.GetSafeNormal());
	const FRotator AimRotation = LocalLaunchDirection.Rotation();
	Cannon->SetAIAimRotation(AimRotation.Pitch, AimRotation.Yaw);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = Ship;
	SpawnParameters.Instigator = Ship;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AEnemyShipTorpedo* Torpedo = Ship->GetWorld()->SpawnActor<AEnemyShipTorpedo>(
		TorpedoClass,
		MuzzleTransform.GetLocation(),
		LaunchVelocity.Rotation(),
		SpawnParameters);
	if (!Torpedo)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const float SnapshotDamage = FMath::Max(0.0f, FiringStats.Damage)
		* FMath::Max(0.0f, TorpedoDamageMultiplier);
	Torpedo->InitializeTorpedo(
		Ship,
		Target,
		SnapshotDamage,
		FiringStats.ProjectileSpeed,
		TargetPoint,
		ImpactTolerance,
		FMath::Min(FMath::Max(0.1f, MaximumFlightSeconds), FMath::Max(0.1f, FlightTime + 1.0f)));

	UE_LOG(LogTemp, Log,
		TEXT("[EnemyShipTorpedo] Ship=%s Cannon=%s Target=%s Damage=%.2f Speed=%.2f Angle=%.2f Flight=%.2f"),
		*GetNameSafe(Ship), *GetNameSafe(Cannon), *GetNameSafe(Target), SnapshotDamage,
		FiringStats.ProjectileSpeed, SolvedAngle, FlightTime);

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

ACannon* UGA_EnemyShipLaunchTorpedo::SelectClosestCannon(
	const AEnemyShip* Ship,
	const FVector& TargetLocation)
{
	if (!Ship)
	{
		return nullptr;
	}

	ACannon* Closest = nullptr;
	float ClosestDistanceSquared = TNumericLimits<float>::Max();
	for (ACannon* Cannon : Ship->GetMountedCannons())
	{
		if (!IsValid(Cannon))
		{
			continue;
		}
		const float DistanceSquared = FVector::DistSquared2D(Cannon->GetActorLocation(), TargetLocation);
		if (DistanceSquared < ClosestDistanceSquared)
		{
			ClosestDistanceSquared = DistanceSquared;
			Closest = Cannon;
		}
	}
	return Closest;
}

bool UGA_EnemyShipLaunchTorpedo::IsValidPlayerTarget(const AShip* Candidate) const
{
	return IsValid(Candidate)
		&& !Candidate->IsEnemyShipForEffects()
		&& Candidate->ActorHasTag(TEXT("Player"))
		&& !Candidate->ActorHasTag(TEXT("Enemy"));
}
