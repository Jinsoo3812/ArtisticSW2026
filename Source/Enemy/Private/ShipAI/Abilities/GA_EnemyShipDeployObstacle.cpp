#include "ShipAI/Abilities/GA_EnemyShipDeployObstacle.h"

#include "BaseGameplayTags.h"
#include "Cannon.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Ship.h"
#include "ShipAI/Abilities/EnemyShipObstacle.h"
#include "ShipAI/Abilities/EnemyShipObstacleProjectile.h"
#include "ShipAI/Abilities/GA_EnemyShipCannonVolley.h"
#include "ShipAI/Abilities/GA_EnemyShipLaunchTorpedo.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipArchetypeData.h"
#include "ShipAI/EnemyShipNavigationComponent.h"

UGA_EnemyShipDeployObstacle::UGA_EnemyShipDeployObstacle()
{
	SetNativeAbilityAndCooldownTags(
		GameplayAbility_EnemyShip_DeployObstacle,
		Cooldown_EnemyShip_DeployObstacle);
	CooldownDurationSeconds = 10.0f;
	ObstacleProjectileClass = AEnemyShipObstacleProjectile::StaticClass();
	ObstacleClass = AEnemyShipObstacle::StaticClass();
}

FGameplayTag UGA_EnemyShipDeployObstacle::GetDeployObstacleAbilityTag()
{
	return GameplayAbility_EnemyShip_DeployObstacle;
}

bool UGA_EnemyShipDeployObstacle::CanActivateAbility(
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

	AEnemyShip* Ship = ActorInfo ? Cast<AEnemyShip>(ActorInfo->AvatarActor.Get()) : nullptr;
	UEnemyShipNavigationComponent* Navigation = Ship ? Ship->GetNavigationComponent() : nullptr;
	AShip* Target = Navigation ? Navigation->GetTargetShip() : nullptr;
	const bool bValidTarget = IsValid(Target)
		&& !Target->IsEnemyShipForEffects()
		&& Target->ActorHasTag(TEXT("Player"))
		&& !Target->ActorHasTag(TEXT("Enemy"));
	if (!Ship || !Ship->HasAuthority() || Ship->IsDeathHandled() || !bValidTarget
		|| !Ship->EnemyShipArchetype
		|| !ObstacleProjectileClass || !ObstacleClass || !Ship->GetWorld())
	{
		return false;
	}

	Ship->RefreshMountedCannons();
	const FVector TargetShipLocation = Target->BuoyancyRoot
		? Target->BuoyancyRoot->GetComponentLocation()
		: Target->GetActorLocation();
	const FVector EnemyShipLocation = Ship->BuoyancyRoot
		? Ship->BuoyancyRoot->GetComponentLocation()
		: Ship->GetActorLocation();
	const ACannon* Cannon = UGA_EnemyShipLaunchTorpedo::SelectClosestCannon(Ship, TargetShipLocation);
	if (!Cannon)
	{
		return false;
	}

	const FVector TargetPoint = CalculateTargetPoint(
		EnemyShipLocation, TargetShipLocation, TargetLineAlpha, TargetWorldZ);
	FEnemyShipCannonAimProfile AimProfile = Ship->EnemyShipArchetype->CannonAimProfile;
	AimProfile.ProjectileFlightTime = FMath::Max(
		0.05f, AimProfile.ProjectileFlightTime / FMath::Max(0.01f, ProjectileSpeedMultiplier));
	FVector LaunchVelocity = FVector::ZeroVector;
	return UGA_EnemyShipCannonVolley::CalculateLaunchVelocity(
		Cannon->GetProjectileMuzzleTransform().GetLocation(),
		TargetPoint,
		FVector::ZeroVector,
		Ship->GetWorld()->GetGravityZ(),
		AimProfile,
		LaunchVelocity);
}

void UGA_EnemyShipDeployObstacle::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AEnemyShip* Ship = ActorInfo ? Cast<AEnemyShip>(ActorInfo->AvatarActor.Get()) : nullptr;
	UEnemyShipNavigationComponent* Navigation = Ship ? Ship->GetNavigationComponent() : nullptr;
	AShip* Target = Navigation ? Navigation->GetTargetShip() : nullptr;
	const bool bValidTarget = IsValid(Target)
		&& !Target->IsEnemyShipForEffects()
		&& Target->ActorHasTag(TEXT("Player"))
		&& !Target->ActorHasTag(TEXT("Enemy"));
	if (!Ship || !Ship->HasAuthority() || Ship->IsDeathHandled() || !bValidTarget
		|| !Ship->EnemyShipArchetype
		|| !ObstacleProjectileClass || !ObstacleClass || !Ship->GetWorld())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	Ship->RefreshMountedCannons();
	const FVector TargetShipLocation = Target->BuoyancyRoot
		? Target->BuoyancyRoot->GetComponentLocation()
		: Target->GetActorLocation();
	const FVector EnemyShipLocation = Ship->BuoyancyRoot
		? Ship->BuoyancyRoot->GetComponentLocation()
		: Ship->GetActorLocation();
	ACannon* Cannon = UGA_EnemyShipLaunchTorpedo::SelectClosestCannon(Ship, TargetShipLocation);
	if (!Cannon)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const FTransform MuzzleTransform = Cannon->GetProjectileMuzzleTransform();
	const FVector TargetPoint = CalculateTargetPoint(
		EnemyShipLocation,
		TargetShipLocation,
		TargetLineAlpha,
		TargetWorldZ);
	FEnemyShipCannonAimProfile AimProfile = Ship->EnemyShipArchetype->CannonAimProfile;
	AimProfile.ProjectileFlightTime = FMath::Max(
		0.05f, AimProfile.ProjectileFlightTime / FMath::Max(0.01f, ProjectileSpeedMultiplier));
	FVector LaunchVelocity = FVector::ZeroVector;
	if (!UGA_EnemyShipCannonVolley::CalculateLaunchVelocity(
		MuzzleTransform.GetLocation(),
		TargetPoint,
		FVector::ZeroVector,
		Ship->GetWorld()->GetGravityZ(),
		AimProfile,
		LaunchVelocity))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	const float TravelSeconds = AimProfile.ProjectileFlightTime;

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
	AEnemyShipObstacleProjectile* Projectile = Ship->GetWorld()->SpawnActor<AEnemyShipObstacleProjectile>(
		ObstacleProjectileClass,
		MuzzleTransform.GetLocation(),
		LaunchVelocity.Rotation(),
		SpawnParameters);
	if (!Projectile)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	Projectile->InitializeObstacleProjectile(
		LaunchVelocity,
		TargetPoint,
		TravelSeconds,
		ObstacleClass,
		ObstacleSpawnRotationOffset);
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

FVector UGA_EnemyShipDeployObstacle::CalculateTargetPoint(
	const FVector& EnemyShipLocation,
	const FVector& PlayerShipLocation,
	float LineAlpha,
	float InTargetWorldZ)
{
	const FVector SegmentPoint = FMath::Lerp(
		EnemyShipLocation,
		PlayerShipLocation,
		FMath::Clamp(LineAlpha, 0.0f, 1.0f));
	return FVector(SegmentPoint.X, SegmentPoint.Y, InTargetWorldZ);
}

bool UGA_EnemyShipDeployObstacle::CalculateBallisticLaunchVelocity(
	const FVector& Start,
	const FVector& Target,
	float Speed,
	float GravityZ,
	bool bHighArc,
	FVector& OutVelocity,
	float& OutTravelSeconds)
{
	OutVelocity = FVector::ZeroVector;
	OutTravelSeconds = 0.0f;
	const float Gravity = -GravityZ;
	const FVector Delta = Target - Start;
	const FVector2D DeltaXY(Delta.X, Delta.Y);
	const float HorizontalDistance = DeltaXY.Size();
	if (Speed <= KINDA_SMALL_NUMBER || Gravity <= KINDA_SMALL_NUMBER || HorizontalDistance <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const double SpeedSquared = static_cast<double>(Speed) * Speed;
	const double Discriminant = SpeedSquared * SpeedSquared
		- static_cast<double>(Gravity) * (
			static_cast<double>(Gravity) * HorizontalDistance * HorizontalDistance
			+ 2.0 * Delta.Z * SpeedSquared);
	if (Discriminant < 0.0)
	{
		return false;
	}

	const double Root = FMath::Sqrt(Discriminant);
	const double TanTheta = (SpeedSquared + (bHighArc ? Root : -Root))
		/ (static_cast<double>(Gravity) * HorizontalDistance);
	const double CosTheta = 1.0 / FMath::Sqrt(1.0 + TanTheta * TanTheta);
	const double HorizontalSpeed = Speed * CosTheta;
	if (HorizontalSpeed <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector2D HorizontalDirection = DeltaXY / HorizontalDistance;
	OutTravelSeconds = HorizontalDistance / HorizontalSpeed;
	OutVelocity = FVector(
		HorizontalDirection.X * HorizontalSpeed,
		HorizontalDirection.Y * HorizontalSpeed,
		HorizontalSpeed * TanTheta);
	return !OutVelocity.ContainsNaN() && FMath::IsFinite(OutTravelSeconds) && OutTravelSeconds > 0.0f;
}
