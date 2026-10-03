#include "Item/Projectiles/ProjectileShotPreparation.h"

#include "Item/Projectiles/ArrowProjectile.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarProjectileDebugLaunch(TEXT("sw.Projectile.DebugLaunch"), 0,
		TEXT("Log committed shot velocity/timing; inherited ship or diagnostic shooter velocity (cyan), world launch (yellow), character box (green), obstacle box (orange)."), ECVF_Cheat);
}

double ProjectileShotPreparation::GetServerTime(const UWorld* World)
{
	if (!World) return 0.0;
	const AGameStateBase* State = World->GetGameState();
	return State ? State->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

void ProjectileShotPreparation::DebugShot(const AArrowProjectile& Arrow, const FProjectileShotSnapshot& Shot)
{
	const IConsoleVariable* Debug = IConsoleManager::Get().FindConsoleVariable(TEXT("sw.Projectile.DebugLaunch"));
	if (!Debug || Debug->GetInt() == 0 || !Arrow.GetWorld()) return;
	const AActor* Shooter = Arrow.GetInstigator();
	UE_LOG(LogTemp, Display, TEXT("[ProjectileShot] Id=%s Shooter=%s Role=%d Frame=%llu AimTime=%.3f CommitTime=%.3f Age=%.3f Policy=%s Carrier=%s MotionValid=%d Muzzle=%s Aim=%s FireSpeed=%.2f InheritedV=%s ShooterV=%s WorldV=%s Gravity=%.2f"),
		*Shot.Input.ShotId.ToString(), *GetNameSafe(Shooter), Shooter ? int32(Shooter->GetLocalRole()) : -1,
		Shot.CommitFrame, Shot.Input.AimServerTime, Shot.CommitServerTime,
		Shot.CommitServerTime - Shot.Input.AimServerTime,
		Shot.Input.Profile.VelocityPolicy == EProjectileVelocityPolicy::WorldAim ? TEXT("WorldAim") : TEXT("PhysicalInheritance"),
		*Shot.CarrierName.ToString(), Shot.bHasMotionSample, *Shot.SpawnTransform.GetLocation().ToString(),
		*Shot.Input.AimPoint.ToString(), Shot.Input.Speed, *Shot.InheritedVelocity.ToString(),
		*Shot.ShooterVelocity.ToString(), *Shot.WorldVelocity.ToString(), Shot.Input.GravityZ);

	const UWorld* World = Arrow.GetWorld();
	const FVector Origin = Shot.SpawnTransform.GetLocation();
	const FVector MotionVelocity = Shot.Input.Profile.VelocityPolicy == EProjectileVelocityPolicy::PhysicalInheritance
		? Shot.InheritedVelocity : Shot.ShooterVelocity;
	DrawDebugLine(World, Origin, Origin + MotionVelocity * 0.15, FColor::Cyan, false, 3.0f, 0, 2.0f);
	DrawDebugLine(World, Origin, Origin + Shot.WorldVelocity * 0.15, FColor::Yellow, false, 3.0f, 0, 2.0f);
	DrawDebugBox(World, Origin, Arrow.GetCollisionHalfExtent(), Shot.SpawnTransform.GetRotation(), FColor::Green, false, 3.0f);
	DrawDebugBox(World, Origin, Arrow.GetObstacleCollisionHalfExtent(), Shot.SpawnTransform.GetRotation(), FColor::Orange, false, 3.0f);
	DrawDebugPoint(World, Shot.Input.AimPoint, 12.0f, FColor::White, false, 3.0f);
	FVector Previous = Origin;
	for (int32 Index = 1; Index <= 30; ++Index)
	{
		const double Time = Index / 30.0;
		const FVector Next = Origin + Shot.WorldVelocity * Time + FVector(0, 0, 0.5 * Shot.Input.GravityZ * Time * Time);
		DrawDebugLine(World, Previous, Next, FColor::Magenta, false, 3.0f);
		Previous = Next;
	}
}
