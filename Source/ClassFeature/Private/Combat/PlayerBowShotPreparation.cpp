#include "Combat/PlayerBowShotPreparation.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Item/Projectiles/ProjectileShotPreparation.h"
#include "Movement/MovementFrameVelocity.h"

namespace
{
	TAutoConsoleVariable<float> CVarPlayerBowGravityScale(TEXT("sw.PlayerBow.GravityScale"), -1.0f,
		TEXT("Player bow new shots only: -1 uses BP authored gravity; 0 disables gravity for aim diagnosis."), ECVF_Cheat);
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
		|| Input.MuzzleTransform.ContainsNaN() || Input.AimPoint.ContainsNaN()
		|| !FMath::IsFinite(Input.Speed) || Input.Speed <= 0.0
		|| !FMath::IsFinite(Input.GravityZ) || !FMath::IsFinite(Input.AimServerTime)) return false;
	const FVector Direction = (Input.AimPoint - Input.MuzzleTransform.GetLocation()).GetSafeNormal();
	if (Direction.IsNearlyZero()) return false; // Resolver must supply a distinct forward point.
	OutShot.Input = Input;
	OutShot.Input.Profile.VelocityPolicy = EProjectileVelocityPolicy::WorldAim;
	OutShot.WorldVelocity = Direction * Input.Speed;
	OutShot.SpawnTransform = FTransform(Direction.Rotation(), Input.MuzzleTransform.GetLocation());
	OutShot.CommitServerTime = ProjectileShotPreparation::GetServerTime(Shooter->GetWorld());
	OutShot.CommitFrame = GFrameCounter;
	// Motion is diagnostic metadata only; it never changes Player bow initial velocity.
	OutShot.bHasMotionSample = MovementFrameVelocity::TryGetActorPointVelocity(Shooter,
		Input.MuzzleTransform.GetLocation(), OutShot.ShooterVelocity);
	if (!OutShot.bHasMotionSample) OutShot.ShooterVelocity = FVector::ZeroVector;
	FName Bone;
	if (const USceneComponent* Carrier = MovementFrameVelocity::GetCarrier(Shooter, Bone))
		OutShot.CarrierName = Carrier->GetFName();
	return true;
}
