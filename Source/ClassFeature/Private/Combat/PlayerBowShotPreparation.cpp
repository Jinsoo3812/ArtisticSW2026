#include "Combat/PlayerBowShotPreparation.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Item/Projectiles/ProjectileShotPreparation.h"
#include "Movement/MovementFrameVelocity.h"
#include "Ship.h"

namespace
{
	TAutoConsoleVariable<float> CVarPlayerBowGravityScale(TEXT("sw.PlayerBow.GravityScale"), -1.0f,
		TEXT("Player bow new shots only: -1 uses BP authored gravity; 0 disables gravity for aim diagnosis."), ECVF_Cheat);

	const AShip* GetSupportedShip(const AActor* Shooter)
	{
		FName Bone;
		// Only current support or attachment transfers ship motion; Owner/HostShip can be stale.
		for (const USceneComponent* Component = MovementFrameVelocity::GetCarrier(Shooter, Bone);
			Component; Component = Component->GetAttachParent())
		{
			if (const AShip* Ship = Cast<AShip>(Component->GetOwner())) return Ship;
		}
		return nullptr;
	}
}

double PlayerBowShotPreparation::GetGravityScale(double AuthoredScale)
{
	const float Override = CVarPlayerBowGravityScale.GetValueOnGameThread();
	return FMath::IsFinite(Override) && Override >= 0.0f ? Override : AuthoredScale;
}

bool PlayerBowShotPreparation::Prepare(const AActor* Shooter, const FProjectileShotInput& Input,
	FProjectileShotSnapshot& OutShot)
{
	OutShot = FProjectileShotSnapshot();
	if (!IsValid(Shooter) || !Shooter->GetWorld() || !Input.ShotId.IsValid()
		|| Input.MuzzleTransform.ContainsNaN() || !Input.MuzzleTransform.GetRotation().IsNormalized()
		|| Input.AimPoint.ContainsNaN()
		|| !FMath::IsFinite(Input.Speed) || Input.Speed <= 0.0
		|| !FMath::IsFinite(Input.GravityZ) || !FMath::IsFinite(Input.AimServerTime)) return false;
	const FVector Direction = (Input.AimPoint - Input.MuzzleTransform.GetLocation()).GetSafeNormal();
	if (Direction.IsNearlyZero()) return false; // Resolver must supply a distinct forward point.
	FProjectileShotSnapshot Shot;
	Shot.Input = Input;
	Shot.Input.AimDirection = Direction; // Direction of the shot relative to the ship, before inheritance.
	Shot.Input.Profile.VelocityPolicy = EProjectileVelocityPolicy::PhysicalInheritance;
	if (const AShip* Ship = GetSupportedShip(Shooter))
	{
		// Sample the actual Chaos body at the socket, including angular motion. Do not add walking velocity.
		if (!Ship->TryGetMovementFrameVelocityAtPoint(Input.MuzzleTransform.GetLocation(), Shot.InheritedVelocity))
		{
			return false;
		}
		Shot.bHasMotionSample = true;
		Shot.CarrierName = Ship->GetFName();
	}
	// Commit once. No normalization, speed cap, or further carrier sampling after this sum.
	Shot.WorldVelocity = Direction * Input.Speed + Shot.InheritedVelocity;
	if (Shot.WorldVelocity.ContainsNaN()) return false;
	// Opposite ship and release velocities can cancel exactly; gravity still acts on that arrow.
	const FVector Facing = Shot.WorldVelocity.IsNearlyZero() ? Direction : Shot.WorldVelocity;
	Shot.SpawnTransform = FTransform(Facing.Rotation(), Input.MuzzleTransform.GetLocation());
	Shot.CommitServerTime = ProjectileShotPreparation::GetServerTime(Shooter->GetWorld());
	Shot.CommitFrame = GFrameCounter;
	OutShot = Shot;
	return true;
}
