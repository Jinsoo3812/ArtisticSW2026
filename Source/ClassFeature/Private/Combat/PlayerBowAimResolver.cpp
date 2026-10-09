#include "Combat/PlayerBowAimResolver.h"

#include "CollisionChannels.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Item/Projectiles/ArrowCollisionQuery.h"

namespace
{
	bool IsUsableAimHit(const FHitResult& Hit, const FVector& Muzzle, const FVector& Forward)
	{
		return Hit.bBlockingHit && !Hit.bStartPenetrating && Hit.Time > 0.0f
			&& !Hit.ImpactPoint.ContainsNaN()
			&& FVector::DotProduct(Hit.ImpactPoint - Muzzle, Forward) > 0.0;
	}

	void LogAimHit(const FGuid& Id, const TCHAR* Query, const FHitResult& Hit, bool bUsable)
	{
		const UPrimitiveComponent* Component = Hit.GetComponent();
		UE_LOG(LogTemp, Display, TEXT("[PlayerBowAimHit] Id=%s Query=%s Actor=%s Component=%s Profile=%s Blocking=%d Time=%.6f StartPenetrating=%d Usable=%d Point=%s"),
			*Id.ToString(), Query, *GetNameSafe(Hit.GetActor()), *GetNameSafe(Component),
			Component ? *Component->GetCollisionProfileName().ToString() : TEXT("None"),
			Hit.bBlockingHit, Hit.Time, Hit.bStartPenetrating, bUsable, *Hit.ImpactPoint.ToString());
	}
}

bool PlayerBowAimResolver::Resolve(const UWorld* World, const AActor* Shooter, const AActor* Weapon,
	const FVector& Muzzle, const FVector& ViewOrigin, const FVector& ViewDirection,
	double TraceDistance, const FGuid& ShotId, FVector& OutAimPoint, FVector& OutViewDirection)
{
	OutAimPoint = FVector::ZeroVector;
	OutViewDirection = ViewDirection.GetSafeNormal();
	if (!World || Muzzle.ContainsNaN() || ViewOrigin.ContainsNaN() || ViewDirection.ContainsNaN()
		|| OutViewDirection.IsNearlyZero() || !FMath::IsFinite(TraceDistance) || TraceDistance <= 0.0) return false;

	// Clip only target selection to the muzzle's forward plane. Flight still starts at
	// the socket and sweeps all real obstacles, including walls overlapping the socket.
	const double MuzzleDepth = FVector::DotProduct(Muzzle - ViewOrigin, OutViewDirection);
	const double StartDepth = FMath::Max(0.0, MuzzleDepth) + 1.0;
	const FVector Start = ViewOrigin + OutViewDirection * StartDepth;
	const FVector End = ViewOrigin + OutViewDirection * FMath::Max(TraceDistance, StartDepth + 1.0);
	OutAimPoint = End;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PlayerBowAim), false, Shooter);
	if (Weapon) Params.AddIgnoredActor(Weapon);

	FHitResult AimHit(1.0f), ObstacleHit(1.0f);
	ArrowCollisionQuery::TraceAimTarget(World, Start, End, Params, AimHit);
	ArrowCollisionQuery::TraceObstacles(World, Start, End, Params, ObstacleHit);
	const bool bAimUsable = IsUsableAimHit(AimHit, Muzzle, OutViewDirection);
	const bool bObstacleUsable = IsUsableAimHit(ObstacleHit, Muzzle, OutViewDirection);
	const TCHAR* Selection = TEXT("FarPoint");
	if (bAimUsable) { OutAimPoint = AimHit.ImpactPoint; Selection = TEXT("WeaponAim"); }
	if (bObstacleUsable && (!bAimUsable || ObstacleHit.Time < AimHit.Time))
	{
		OutAimPoint = ObstacleHit.ImpactPoint;
		Selection = TEXT("ArrowObstacle");
	}

	const IConsoleVariable* Debug = IConsoleManager::Get().FindConsoleVariable(TEXT("sw.Projectile.DebugLaunch"));
	if (ShotId.IsValid() && Debug && Debug->GetInt() != 0)
	{
		LogAimHit(ShotId, TEXT("WeaponAim"), AimHit, bAimUsable);
		LogAimHit(ShotId, TEXT("ArrowObstacle"), ObstacleHit, bObstacleUsable);
		UE_LOG(LogTemp, Display, TEXT("[PlayerBowAim] Id=%s Selection=%s Muzzle=%s Camera=%s TraceStart=%s FinalAim=%s"),
			*ShotId.ToString(), Selection, *Muzzle.ToString(), *ViewOrigin.ToString(), *Start.ToString(), *OutAimPoint.ToString());
		DrawDebugLine(World, Start, End, FColor::Blue, false, 3.0f, 0, 1.0f);
		DrawDebugLine(World, Muzzle, OutAimPoint, FColor::White, false, 3.0f, 0, 1.0f);
		DrawDebugPoint(World, OutAimPoint, 12.0f, FColor::White, false, 3.0f);
	}
	return true;
}
