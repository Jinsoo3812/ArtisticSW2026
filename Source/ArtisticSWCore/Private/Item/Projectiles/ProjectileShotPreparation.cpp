#include "Item/Projectiles/ProjectileShotPreparation.h"

#include "Item/Projectiles/ProjectileLaunchMath.h"
#include "Movement/MovementFrameVelocity.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/IConsoleManager.h"

double ProjectileShotPreparation::GetServerTime(const UWorld* World)
{
	if (!World) return 0.0;
	const AGameStateBase* State = World->GetGameState();
	return State ? State->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

bool ProjectileShotPreparation::Prepare(const AActor* Shooter, const FProjectileShotInput& Input,
	FProjectileShotSnapshot& OutShot)
{
	OutShot = FProjectileShotSnapshot();
	if (!IsValid(Shooter) || !Shooter->GetWorld() || !Input.ShotId.IsValid()
		|| Input.MuzzleTransform.ContainsNaN() || !Input.MuzzleTransform.GetRotation().IsNormalized()
		|| !FMath::IsFinite(Input.GravityZ) || !FMath::IsFinite(Input.AimServerTime)) return false;
	FVector Direction;
	if (!ProjectileLaunchMath::ResolveMuzzleDirection(Input.MuzzleTransform.GetLocation(),
		Input.AimPoint, Input.AimDirection, Direction)) return false;
	OutShot.Input = Input;
	OutShot.CommitServerTime = GetServerTime(Shooter->GetWorld());
	OutShot.CommitFrame = GFrameCounter;
	OutShot.bHasMotionSample = MovementFrameVelocity::TryGetActorPointVelocity(Shooter,
		Input.MuzzleTransform.GetLocation(), OutShot.ShooterVelocity);
	if (!OutShot.bHasMotionSample)
	{
		OutShot.ShooterVelocity = FVector::ZeroVector;
		if (Input.Profile.VelocityPolicy == EProjectileVelocityPolicy::PhysicalInheritance) return false;
	}
	FName Bone;
	if (const USceneComponent* Carrier = MovementFrameVelocity::GetCarrier(Shooter, Bone))
		OutShot.CarrierName = Carrier->GetFName();
	if (!ProjectileLaunchMath::BuildVelocity(Direction, Input.Speed, Input.Profile.VelocityPolicy,
		OutShot.ShooterVelocity, OutShot.WorldVelocity)) return false;
	OutShot.SpawnTransform = FTransform(OutShot.WorldVelocity.Rotation(), Input.MuzzleTransform.GetLocation());
	return true;
}

void ProjectileShotPreparation::DebugShot(const AActor* Shooter, const FProjectileShotSnapshot& Shot)
{
	const IConsoleVariable* Debug = IConsoleManager::Get().FindConsoleVariable(TEXT("sw.Projectile.DebugLaunch"));
	if (!Debug || Debug->GetInt() == 0) return;
	UE_LOG(LogTemp, Display, TEXT("[ProjectileShot] Id=%s Shooter=%s Role=%d Frame=%llu AimTime=%.3f CommitTime=%.3f Age=%.3f Policy=%s Carrier=%s MotionValid=%d Muzzle=%s Aim=%s ShooterV=%s WorldV=%s Gravity=%.2f"),
		*Shot.Input.ShotId.ToString(), *GetNameSafe(Shooter), Shooter ? int32(Shooter->GetLocalRole()) : -1,
		Shot.CommitFrame, Shot.Input.AimServerTime, Shot.CommitServerTime,
		Shot.CommitServerTime - Shot.Input.AimServerTime,
		Shot.Input.Profile.VelocityPolicy == EProjectileVelocityPolicy::WorldAim ? TEXT("WorldAim") : TEXT("PhysicalInheritance"),
		*Shot.CarrierName.ToString(), Shot.bHasMotionSample, *Shot.SpawnTransform.GetLocation().ToString(),
		*Shot.Input.AimPoint.ToString(), *Shot.ShooterVelocity.ToString(), *Shot.WorldVelocity.ToString(), Shot.Input.GravityZ);
}
