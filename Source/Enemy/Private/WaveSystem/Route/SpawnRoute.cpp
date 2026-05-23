#include "WaveSystem/Route/SpawnRoute.h"

#include "Components/SplineComponent.h"
#include "DrawDebugHelpers.h"
#include "NavigationSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogSpawnRoute, Log, All);

ASpawnRoute::ASpawnRoute()
{
	PrimaryActorTick.bCanEverTick = false;

	// Route는 레벨 배치 데이터 Actor이므로 기본 Tick이 필요하지 않고, 서버의 Manager/Movement가 참조한다.
	RouteSpline = CreateDefaultSubobject<USplineComponent>(TEXT("RouteSpline"));
	SetRootComponent(RouteSpline);

	// Wave Route는 시작점에서 Goal로 흐르는 열린 경로이므로 기본값을 ClosedLoop=false로 둔다.
	RouteSpline->SetClosedLoop(false);
}

void ASpawnRoute::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 에디터에서 Spline을 수정할 때 RouteWaypoints가 즉시 갱신되도록 한다.
	GenerateWaypointsFromSpline();
}

void ASpawnRoute::BeginPlay()
{
	Super::BeginPlay();

	// 런타임 시작 시 최종 Spline 상태를 다시 반영해 서버 이동 데이터의 신뢰도를 보장한다.
	GenerateWaypointsFromSpline();

	if (RouteId.IsNone())
	{
		UE_LOG(
			LogSpawnRoute,
			Warning,
			TEXT("[SpawnRoute] RouteId is None. Actor=%s"),
			*GetNameSafe(this)
		);
	}
}

void ASpawnRoute::GenerateWaypointsFromSpline()
{
	RouteWaypoints.Reset();
	FailedProjectionPoints.Reset();

	if (!RouteSpline)
	{
		UE_LOG(
			LogSpawnRoute,
			Warning,
			TEXT("[SpawnRoute] RouteSpline is null. Actor=%s RouteId=%s"),
			*GetNameSafe(this),
			*RouteId.ToString()
		);

		return;
	}

	const int32 NumSplinePoints = RouteSpline->GetNumberOfSplinePoints();
	const float SplineLength = RouteSpline->GetSplineLength();

	if (NumSplinePoints <= 0 || SplineLength <= KINDA_SMALL_NUMBER)
	{
		FRouteWaypoint FallbackWaypoint;
		FallbackWaypoint.WaypointIndex = 0;
		FallbackWaypoint.DistanceAlongSpline = 0.0f;
		FallbackWaypoint.CenterLocation = RouteSpline->GetComponentLocation();
		FallbackWaypoint.ForwardVector = GetActorForwardVector().GetSafeNormal();
		FallbackWaypoint.RightVector = GetActorRightVector().GetSafeNormal();
		FallbackWaypoint.bDesignerPoint = true;
		FallbackWaypoint.bProjectedToNavMesh = false;

		if (FallbackWaypoint.ForwardVector.IsNearlyZero())
		{
			FallbackWaypoint.ForwardVector = FVector::ForwardVector;
		}

		if (FallbackWaypoint.RightVector.IsNearlyZero())
		{
			FallbackWaypoint.RightVector = FVector::RightVector;
		}

		if (bProjectWaypointsToNavMesh)
		{
			FVector ProjectedLocation = FallbackWaypoint.CenterLocation;

			if (ProjectPointToNavMesh(FallbackWaypoint.CenterLocation, ProjectedLocation))
			{
				FallbackWaypoint.CenterLocation = ProjectedLocation;
				FallbackWaypoint.bProjectedToNavMesh = true;
			}
			else
			{
				FailedProjectionPoints.Add(FallbackWaypoint.CenterLocation);

				UE_LOG(
					LogSpawnRoute,
					Warning,
					TEXT("[SpawnRoute] Failed to project fallback waypoint to NavMesh. Actor=%s RouteId=%s Location=%s"),
					*GetNameSafe(this),
					*RouteId.ToString(),
					*FallbackWaypoint.CenterLocation.ToString()
				);
			}
		}

		RouteWaypoints.Add(FallbackWaypoint);
		return;
	}

	for (int32 PointIndex = 0; PointIndex < NumSplinePoints; ++PointIndex)
	{
		const float DesignerDistance = RouteSpline->GetDistanceAlongSplineAtSplinePoint(PointIndex);
		AddRouteWaypointAtDistance(DesignerDistance, true);

		const bool bHasNextPoint = PointIndex + 1 < NumSplinePoints;
		if (!bHasNextPoint || !bGenerateSupportWaypointsForLongSegments || MaxSegmentLength <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const float NextDesignerDistance = RouteSpline->GetDistanceAlongSplineAtSplinePoint(PointIndex + 1);
		const float SegmentLength = NextDesignerDistance - DesignerDistance;

		if (SegmentLength <= MaxSegmentLength)
		{
			continue;
		}

		const int32 SupportPointCount = FMath::FloorToInt(SegmentLength / MaxSegmentLength);

		for (int32 SupportIndex = 1; SupportIndex <= SupportPointCount; ++SupportIndex)
		{
			const float SupportDistance = DesignerDistance + MaxSegmentLength * static_cast<float>(SupportIndex);

			if (SupportDistance >= NextDesignerDistance - KINDA_SMALL_NUMBER)
			{
				break;
			}

			AddRouteWaypointAtDistance(SupportDistance, false);
		}
	}

	ReindexRouteWaypoints();
}

FTransform ASpawnRoute::GetSpawnTransform(float OverrideSpawnRadius) const
{
	if (!RouteSpline)
	{
		return GetActorTransform();
	}

	const float FinalSpawnRadius = OverrideSpawnRadius >= 0.0f
		? OverrideSpawnRadius
		: SpawnRadius;

	FVector SpawnLocation = RouteSpline->GetLocationAtDistanceAlongSpline(
		0.0f,
		ESplineCoordinateSpace::World
	);

	if (FinalSpawnRadius > 0.0f)
	{
		const FVector2D RandomOffset2D = FMath::RandPointInCircle(FinalSpawnRadius);
		SpawnLocation += FVector(RandomOffset2D.X, RandomOffset2D.Y, 0.0f);
	}

	if (bProjectWaypointsToNavMesh)
	{
		FVector ProjectedLocation = SpawnLocation;

		if (ProjectPointToNavMesh(SpawnLocation, ProjectedLocation))
		{
			SpawnLocation = ProjectedLocation;
		}
		else
		{
			UE_LOG(
				LogSpawnRoute,
				Warning,
				TEXT("[SpawnRoute] Failed to project spawn location to NavMesh. Actor=%s RouteId=%s Location=%s"),
				*GetNameSafe(this),
				*RouteId.ToString(),
				*SpawnLocation.ToString()
			);
		}
	}

	const FRotator SpawnRotation = RouteSpline->GetRotationAtDistanceAlongSpline(
		0.0f,
		ESplineCoordinateSpace::World
	);

	return FTransform(SpawnRotation, SpawnLocation, FVector::OneVector);
}

const TArray<FRouteWaypoint>& ASpawnRoute::GetRouteWaypointsRef() const
{
	return RouteWaypoints;
}

TArray<FRouteWaypoint> ASpawnRoute::GetRouteWaypoints() const
{
	return RouteWaypoints;
}

float ASpawnRoute::GetAcceptanceRadius() const
{
	return AcceptanceRadius;
}

float ASpawnRoute::GetGoalAcceptanceRadius() const
{
	return GoalAcceptanceRadius;
}

int32 ASpawnRoute::GetFirstMoveWaypointIndex() const
{
	return RouteWaypoints.Num() >= 2 ? 1 : 0;
}

bool ASpawnRoute::IsGoalWaypointIndex(int32 WaypointIndex) const
{
	return RouteWaypoints.IsValidIndex(WaypointIndex) && WaypointIndex == RouteWaypoints.Num() - 1;
}

bool ASpawnRoute::GetMoveTargetForWaypoint(int32 WaypointIndex, int32 EnemySeed, FVector& OutMoveTarget) const
{
	if (!RouteWaypoints.IsValidIndex(WaypointIndex))
	{
		UE_LOG(
			LogSpawnRoute,
			Warning,
			TEXT("[SpawnRoute] Invalid WaypointIndex. Actor=%s RouteId=%s WaypointIndex=%d NumWaypoints=%d"),
			*GetNameSafe(this),
			*RouteId.ToString(),
			WaypointIndex,
			RouteWaypoints.Num()
		);

		OutMoveTarget = GetActorLocation();
		return false;
	}

	const FRouteWaypoint& Waypoint = RouteWaypoints[WaypointIndex];
	const bool bIsStart = WaypointIndex == 0;
	const bool bIsGoal = IsGoalWaypointIndex(WaypointIndex);

	const float CenterScale = (bKeepStartAndGoalCentered && (bIsStart || bIsGoal)) ? 0.25f : 1.0f;
	const float EffectiveRouteWidth = FMath::Max(0.0f, RouteWidth) * CenterScale;
	const float EffectiveForwardJitterRadius = FMath::Max(0.0f, ForwardJitterRadius) * CenterScale;

	FRandomStream RandomStream(MakeCombinedSeed(EnemySeed, WaypointIndex));

	const float LateralOffset = EffectiveRouteWidth > 0.0f
		? RandomStream.FRandRange(-EffectiveRouteWidth, EffectiveRouteWidth)
		: 0.0f;

	const float ForwardOffset = EffectiveForwardJitterRadius > 0.0f
		? RandomStream.FRandRange(-EffectiveForwardJitterRadius, EffectiveForwardJitterRadius)
		: 0.0f;

	const FVector CandidateLocation = Waypoint.CenterLocation
		+ Waypoint.RightVector * LateralOffset
		+ Waypoint.ForwardVector * ForwardOffset;

	if (ProjectMoveTargetWithFallback(Waypoint, CandidateLocation, OutMoveTarget))
	{
		return true;
	}

	OutMoveTarget = Waypoint.CenterLocation;
	return true;
}

void ASpawnRoute::DrawDebugRoute(float Duration) const
{
	const UWorld* World = GetWorld();

	if (!World || !RouteSpline)
	{
		return;
	}

	const float SplineLength = RouteSpline->GetSplineLength();
	const FVector LabelLocation = GetActorLocation() + FVector(0.0f, 0.0f, 160.0f);

	DrawDebugString(
		World,
		LabelLocation,
		FString::Printf(TEXT("RouteId: %s | Waypoints: %d"), *RouteId.ToString(), RouteWaypoints.Num()),
		nullptr,
		FColor::White,
		Duration,
		true
	);

	if (SplineLength > KINDA_SMALL_NUMBER)
	{
		const int32 SegmentCount = FMath::Max(1, FMath::CeilToInt(SplineLength / 250.0f));

		FVector PreviousLocation = RouteSpline->GetLocationAtDistanceAlongSpline(
			0.0f,
			ESplineCoordinateSpace::World
		);

		for (int32 SegmentIndex = 1; SegmentIndex <= SegmentCount; ++SegmentIndex)
		{
			const float Alpha = static_cast<float>(SegmentIndex) / static_cast<float>(SegmentCount);
			const float Distance = FMath::Clamp(SplineLength * Alpha, 0.0f, SplineLength);

			const FVector CurrentLocation = RouteSpline->GetLocationAtDistanceAlongSpline(
				Distance,
				ESplineCoordinateSpace::World
			);

			DrawDebugLine(
				World,
				PreviousLocation,
				CurrentLocation,
				FColor::Cyan,
				false,
				Duration,
				0,
				3.0f
			);

			PreviousLocation = CurrentLocation;
		}
	}

	for (const FRouteWaypoint& Waypoint : RouteWaypoints)
	{
		const bool bIsStart = Waypoint.WaypointIndex == 0;
		const bool bIsGoal = IsGoalWaypointIndex(Waypoint.WaypointIndex);

		const FColor WaypointColor = bIsStart
			? FColor::Green
			: bIsGoal
				? FColor::Blue
				: Waypoint.bDesignerPoint ? FColor::Purple : FColor::Orange;

		const float SphereRadius = bIsStart ? 60.0f : bIsGoal ? 70.0f : 35.0f;

		DrawDebugSphere(
			World,
			Waypoint.CenterLocation,
			SphereRadius,
			16,
			WaypointColor,
			false,
			Duration,
			0,
			3.0f
		);

		DrawDebugString(
			World,
			Waypoint.CenterLocation + FVector(0.0f, 0.0f, 75.0f),
			FString::Printf(
				TEXT("%d %s%s"),
				Waypoint.WaypointIndex,
				Waypoint.bDesignerPoint ? TEXT("D") : TEXT("S"),
				Waypoint.bProjectedToNavMesh ? TEXT(" Nav") : TEXT("")
			),
			nullptr,
			FColor::White,
			Duration,
			true
		);

		DrawDebugDirectionalArrow(
			World,
			Waypoint.CenterLocation,
			Waypoint.CenterLocation + Waypoint.ForwardVector * 160.0f,
			40.0f,
			FColor::Yellow,
			false,
			Duration,
			0,
			3.0f
		);

		DrawDebugDirectionalArrow(
			World,
			Waypoint.CenterLocation,
			Waypoint.CenterLocation + Waypoint.RightVector * 120.0f,
			35.0f,
			FColor::Magenta,
			false,
			Duration,
			0,
			2.0f
		);

		const float DebugRouteWidth = bKeepStartAndGoalCentered && (bIsStart || bIsGoal)
			? RouteWidth * 0.25f
			: RouteWidth;

		if (DebugRouteWidth > 0.0f)
		{
			const FVector LeftLocation = Waypoint.CenterLocation - Waypoint.RightVector * DebugRouteWidth;
			const FVector RightLocation = Waypoint.CenterLocation + Waypoint.RightVector * DebugRouteWidth;

			DrawDebugLine(
				World,
				LeftLocation,
				RightLocation,
				FColor::Silver,
				false,
				Duration,
				0,
				1.5f
			);

			DrawDebugSphere(World, LeftLocation, 12.0f, 8, FColor::Silver, false, Duration, 0, 1.5f);
			DrawDebugSphere(World, RightLocation, 12.0f, 8, FColor::Silver, false, Duration, 0, 1.5f);
		}

		for (int32 SampleIndex = 0; SampleIndex < 3; ++SampleIndex)
		{
			FVector SampleMoveTarget = FVector::ZeroVector;
			const int32 SampleSeed = 1000 + SampleIndex;

			if (GetMoveTargetForWaypoint(Waypoint.WaypointIndex, SampleSeed, SampleMoveTarget))
			{
				DrawDebugSphere(
					World,
					SampleMoveTarget,
					16.0f,
					8,
					FColor::White,
					false,
					Duration,
					0,
					1.5f
				);
			}
		}
	}

	for (const FVector& FailedLocation : FailedProjectionPoints)
	{
		DrawDebugSphere(
			World,
			FailedLocation,
			55.0f,
			12,
			FColor::Red,
			false,
			Duration,
			0,
			4.0f
		);

		DrawDebugString(
			World,
			FailedLocation + FVector(0.0f, 0.0f, 80.0f),
			TEXT("Nav Projection Failed"),
			nullptr,
			FColor::Red,
			Duration,
			true
		);
	}
}

FRouteWaypoint ASpawnRoute::MakeRouteWaypointAtDistance(float DistanceAlongSpline, bool bDesignerPoint)
{
	FRouteWaypoint Waypoint;
	Waypoint.WaypointIndex = RouteWaypoints.Num();
	Waypoint.DistanceAlongSpline = FMath::Max(0.0f, DistanceAlongSpline);
	Waypoint.bDesignerPoint = bDesignerPoint;

	if (!RouteSpline)
	{
		Waypoint.CenterLocation = GetActorLocation();
		Waypoint.ForwardVector = GetActorForwardVector().GetSafeNormal();
		Waypoint.RightVector = GetActorRightVector().GetSafeNormal();
		return Waypoint;
	}

	const float SplineLength = RouteSpline->GetSplineLength();
	Waypoint.DistanceAlongSpline = FMath::Clamp(Waypoint.DistanceAlongSpline, 0.0f, SplineLength);

	const FVector RawLocation = RouteSpline->GetLocationAtDistanceAlongSpline(
		Waypoint.DistanceAlongSpline,
		ESplineCoordinateSpace::World
	);

	FVector Forward = RouteSpline->GetDirectionAtDistanceAlongSpline(
		Waypoint.DistanceAlongSpline,
		ESplineCoordinateSpace::World
	).GetSafeNormal();

	if (Forward.IsNearlyZero())
	{
		Forward = GetActorForwardVector().GetSafeNormal();
	}

	if (Forward.IsNearlyZero())
	{
		Forward = FVector::ForwardVector;
	}

	FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();

	if (Right.IsNearlyZero())
	{
		Right = GetActorRightVector().GetSafeNormal();
	}

	if (Right.IsNearlyZero())
	{
		Right = FVector::RightVector;
	}

	Waypoint.CenterLocation = RawLocation;
	Waypoint.ForwardVector = Forward;
	Waypoint.RightVector = Right;
	Waypoint.bProjectedToNavMesh = false;

	if (bProjectWaypointsToNavMesh)
	{
		FVector ProjectedLocation = RawLocation;

		if (ProjectPointToNavMesh(RawLocation, ProjectedLocation))
		{
			Waypoint.CenterLocation = ProjectedLocation;
			Waypoint.bProjectedToNavMesh = true;
		}
		else
		{
			FailedProjectionPoints.Add(RawLocation);

			UE_LOG(
				LogSpawnRoute,
				Warning,
				TEXT("[SpawnRoute] Failed to project route waypoint to NavMesh. Actor=%s RouteId=%s Distance=%.2f Location=%s"),
				*GetNameSafe(this),
				*RouteId.ToString(),
				Waypoint.DistanceAlongSpline,
				*RawLocation.ToString()
			);
		}
	}

	return Waypoint;
}

void ASpawnRoute::AddRouteWaypointAtDistance(float DistanceAlongSpline, bool bDesignerPoint)
{
	RouteWaypoints.Add(MakeRouteWaypointAtDistance(DistanceAlongSpline, bDesignerPoint));
}

void ASpawnRoute::ReindexRouteWaypoints()
{
	for (int32 Index = 0; Index < RouteWaypoints.Num(); ++Index)
	{
		RouteWaypoints[Index].WaypointIndex = Index;
	}
}

bool ASpawnRoute::ProjectPointToNavMesh(const FVector& InPoint, FVector& OutPoint) const
{
	UWorld* World = GetWorld();

	if (!World)
	{
		return false;
	}

	const UNavigationSystemV1* NavigationSystem = UNavigationSystemV1::GetCurrent(World);

	if (!NavigationSystem)
	{
		return false;
	}

	FNavLocation ProjectedNavLocation;

	const bool bProjected = NavigationSystem->ProjectPointToNavigation(
		InPoint,
		ProjectedNavLocation,
		NavProjectionExtent
	);

	if (!bProjected)
	{
		return false;
	}

	OutPoint = ProjectedNavLocation.Location;
	return true;
}

bool ASpawnRoute::ProjectMoveTargetWithFallback(const FRouteWaypoint& Waypoint, const FVector& CandidateLocation, FVector& OutMoveTarget) const
{
	if (!bProjectWaypointsToNavMesh)
	{
		OutMoveTarget = CandidateLocation;
		return true;
	}

	FVector ProjectedLocation = CandidateLocation;

	if (ProjectPointToNavMesh(CandidateLocation, ProjectedLocation))
	{
		OutMoveTarget = ProjectedLocation;
		return true;
	}

	const FVector HalfOffsetLocation = Waypoint.CenterLocation + (CandidateLocation - Waypoint.CenterLocation) * 0.5f;

	if (ProjectPointToNavMesh(HalfOffsetLocation, ProjectedLocation))
	{
		OutMoveTarget = ProjectedLocation;
		return true;
	}

	if (ProjectPointToNavMesh(Waypoint.CenterLocation, ProjectedLocation))
	{
		OutMoveTarget = ProjectedLocation;
		return true;
	}

	return false;
}

int32 ASpawnRoute::MakeCombinedSeed(int32 EnemySeed, int32 WaypointIndex) const
{
	const uint32 RouteHash = RouteId.IsNone()
		? GetTypeHash(GetFName())
		: GetTypeHash(RouteId);

	const uint32 SeedHash = HashCombine(static_cast<uint32>(EnemySeed), static_cast<uint32>(WaypointIndex));
	const uint32 CombinedHash = HashCombine(SeedHash, RouteHash);

	return static_cast<int32>(CombinedHash);
}