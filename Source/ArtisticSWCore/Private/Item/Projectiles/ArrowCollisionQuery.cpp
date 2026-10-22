#include "Item/Projectiles/ArrowCollisionQuery.h"

#include "Item/Projectiles/ArrowProjectile.h"
#include "Item/Projectiles/ProjectileLaunchTypes.h"
#include "CollisionChannels.h"
#include "Components/CombatHurtboxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Components/BrushComponent.h"
#include "PCGVolume.h"

namespace
{
	// Single queries stop at the first blocker. Retry from the same origin after
	// excluding ONLY bounds components, so walls behind them and initial overlaps remain visible.
	template <typename QueryType>
	bool QueryPastPCGBounds(const FCollisionQueryParams& Params, FHitResult& OutHit, QueryType Query)
	{
		FCollisionQueryParams FilteredParams = Params;
		while (Query(FilteredParams, OutHit))
		{
			if (!ArrowCollisionQuery::IsPCGVolumeBounds(OutHit.GetComponent())) return true;
			FilteredParams.AddIgnoredComponent(OutHit.GetComponent());
			OutHit = FHitResult(1.0f);
		}
		return false;
	}
}

bool ArrowCollisionQuery::IsPCGVolumeBounds(const UPrimitiveComponent* Component)
{
	if (!Component) return false;
	if (Component->GetCollisionProfileName() == TEXT("PCGVolumeBounds")) return true;
	const APCGVolume* Volume = Cast<APCGVolume>(Component->GetOwner());
	return Volume && Component == Volume->GetBrushComponent();
}

bool ArrowCollisionQuery::TraceObstacles(const UWorld* World, const FVector& Start, const FVector& End,
	const FCollisionQueryParams& Params, FHitResult& OutHit)
{
	OutHit = FHitResult(1.0f);
	return World && QueryPastPCGBounds(Params, OutHit, [&](const FCollisionQueryParams& FilteredParams, FHitResult& Hit)
	{
		return World->LineTraceSingleByProfile(Hit, Start, End, TEXT("ArrowObstacle"), FilteredParams);
	});
}

bool ArrowCollisionQuery::TraceAimTarget(const UWorld* World, const FVector& Start, const FVector& End,
	const FCollisionQueryParams& Params, FHitResult& OutHit)
{
	OutHit = FHitResult(1.0f);
	return World && QueryPastPCGBounds(Params, OutHit, [&](const FCollisionQueryParams& FilteredParams, FHitResult& Hit)
	{
		return World->LineTraceSingleByChannel(Hit, Start, End, ECC_WeaponAim, FilteredParams);
	});
}

bool ArrowCollisionQuery::SweepObstacles(const UWorld* World, const FVector& Start, const FVector& End,
	const FQuat& Rotation, const FVector& HalfExtent, const FCollisionQueryParams& Params, FHitResult& OutHit)
{
	OutHit = FHitResult(1.0f);
	if (!World || Start.ContainsNaN() || End.ContainsNaN() || Rotation.ContainsNaN() || HalfExtent.ContainsNaN())
	{
		OutHit.bBlockingHit = true;
		OutHit.Time = 0.0f;
		return true;
	}
	return QueryPastPCGBounds(Params, OutHit, [&](const FCollisionQueryParams& FilteredParams, FHitResult& Hit)
	{
		return World->SweepSingleByProfile(Hit, Start, End, Rotation, TEXT("ArrowObstacle"),
			FCollisionShape::MakeBox(HalfExtent.ComponentMax(FVector(0.1))), FilteredParams);
	});
}

bool ArrowCollisionQuery::IsAimObstructed(const UWorld* World, const FProjectileShotSnapshot& Shot,
	const FVector& HalfExtent, const FCollisionQueryParams& Params, FHitResult& OutHit)
{
	const FVector Start = Shot.SpawnTransform.GetLocation();
	const double Distance = FVector::Distance(Start, Shot.Input.AimPoint);
	const FVector End = Start + Shot.WorldVelocity.GetSafeNormal() * Distance;
	if (!SweepObstacles(World, Start, End, Shot.SpawnTransform.GetRotation(), HalfExtent, Params, OutHit)) return false;
	// A surface chosen by the camera is an intended impact, not intervening cover.
	return OutHit.bStartPenetrating || OutHit.Time * Distance + FMath::Max(10.0, HalfExtent.Size()) < Distance;
}

bool ArrowCollisionQuery::SweepFlight(const AArrowProjectile& Arrow, const FVector& Start, const FVector& End,
	const FQuat& Rotation, FHitResult& OutHit)
{
	FCollisionQueryParams Params = Arrow.MakeFlightQueryParams();
	const FVector ObstacleExtent = Arrow.GetObstacleCollisionHalfExtent();
	const bool bObstacle = SweepObstacles(Arrow.GetWorld(), Start, End, Rotation, ObstacleExtent, Params, OutHit);
	const float ObstacleTime = bObstacle ? OutHit.Time : 1.0f;
	if (!Arrow.GetWorld() || (bObstacle && ObstacleTime <= 0.0f)) return bObstacle;

	FCollisionObjectQueryParams Characters;
	Characters.AddObjectTypesToQuery(ECC_CombatHurtbox);
	Characters.AddObjectTypesToQuery(ECC_Pawn); // Explicit MovementCapsule hurtbox mode also remains supported.
	TArray<FHitResult> Hits;
	Arrow.GetWorld()->SweepMultiByObjectType(Hits, Start, End, Rotation, Characters,
		FCollisionShape::MakeBox(Arrow.GetCollisionHalfExtent()), Params);
	Hits.Sort([](const FHitResult& A, const FHitResult& B) { return A.Time < B.Time; });
	for (const FHitResult& Hit : Hits)
	{
		// Obstacles win ties. Team/actor checks happen before selecting the first candidate.
		if (bObstacle && Hit.Time >= ObstacleTime - UE_KINDA_SMALL_NUMBER) break;
		const AActor* Target = Hit.GetActor();
		const UPrimitiveComponent* Surface = Hit.GetComponent();
		if (!Surface || Surface->GetCollisionResponseToChannel(ECC_Arrow) != ECR_Block
			|| !Cast<ACharacter>(Target) || !Arrow.IsValidDamageTarget(Target)
			|| !UCombatHurtboxComponent::IsValidHitSurface(Target, Hit)) continue;

		// An enlarged hit volume may reach through cover before the arrow center reaches
		// the wall. Validate that contact once with the same narrow obstacle policy.
		FCollisionQueryParams ContactParams = Params;
		ContactParams.AddIgnoredActor(Target);
		FHitResult Cover;
		const FVector ContactCenter = FMath::Lerp(Start, End, Hit.Time);
		if (SweepObstacles(Arrow.GetWorld(), ContactCenter, Hit.ImpactPoint, Rotation, ObstacleExtent, ContactParams, Cover)) continue;
		OutHit = Hit;
		OutHit.bBlockingHit = true;
		return true;
	}
	return bObstacle;
}
