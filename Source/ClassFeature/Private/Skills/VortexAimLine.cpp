#include "Skills/VortexAimLine.h"

#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

AVortexAimLine::AVortexAimLine()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetActorEnableCollision(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	TrajectorySpline = CreateDefaultSubobject<USplineComponent>(TEXT("TrajectorySpline"));
	TrajectorySpline->SetupAttachment(SceneRoot);
	TrajectorySpline->SetClosedLoop(false);
	TrajectorySpline->SetHiddenInGame(true);
}

void AVortexAimLine::SetTrajectory(const TArray<FVector>& WorldPoints)
{
	if (!TrajectorySpline || !AimLineMesh || WorldPoints.Num() < 2)
	{
		if (!bLoggedConfiguration)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[VortexPipeline][AimLineActor] Cannot render Actor=%s Spline=%s "
					"Mesh=%s Material=%s PointCount=%d."),
				*GetPathNameSafe(this),
				*GetPathNameSafe(TrajectorySpline),
				*GetPathNameSafe(AimLineMesh),
				*GetPathNameSafe(AimLineMaterial),
				WorldPoints.Num());
			bLoggedConfiguration = true;
		}
		ClearTrajectory();
		return;
	}

	const int32 SourcePointCount = WorldPoints.Num();
	const int32 PointCount =
		FMath::Min(SourcePointCount, FMath::Max(2, MaxSegments + 1));
	TArray<FVector> LocalPoints;
	LocalPoints.Reserve(PointCount);
	const FTransform ActorTransform = GetActorTransform();
	for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
	{
		// Preserve the complete arc when limiting component count. In particular,
		// point 0 remains exactly the socket position and the last point remains
		// the end of the prediction.
		const float SourcePosition = PointCount > 1
			? static_cast<float>(PointIndex)
				* static_cast<float>(SourcePointCount - 1)
				/ static_cast<float>(PointCount - 1)
			: 0.0f;
		const int32 LowerIndex = FMath::FloorToInt(SourcePosition);
		const int32 UpperIndex = FMath::Min(LowerIndex + 1, SourcePointCount - 1);
		const float Alpha = SourcePosition - static_cast<float>(LowerIndex);
		const FVector ResampledWorldPoint =
			FMath::Lerp(WorldPoints[LowerIndex], WorldPoints[UpperIndex], Alpha);
		LocalPoints.Add(ActorTransform.InverseTransformPosition(ResampledWorldPoint));
	}

	TrajectorySpline->SetSplinePoints(LocalPoints, ESplineCoordinateSpace::Local, false);
	for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
	{
		TrajectorySpline->SetSplinePointType(
			PointIndex,
			bSmoothTrajectory ? ESplinePointType::CurveClamped : ESplinePointType::Linear,
			false);
	}
	TrajectorySpline->UpdateSpline();

	const int32 SegmentCount = PointCount - 1;
	const ESplineMeshAxis::Type ResolvedAxis = ResolveForwardAxis().GetValue();
	const FVector2D CrossSectionScale(
		FMath::Max(0.001f, WidthScale),
		FMath::Max(0.001f, WidthScale));

	for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
	{
		USplineMeshComponent* Segment = GetOrCreateSegment(SegmentIndex);
		if (!Segment)
		{
			continue;
		}

		const FVector StartPosition = TrajectorySpline->GetLocationAtSplinePoint(
			SegmentIndex, ESplineCoordinateSpace::Local);
		const FVector EndPosition = TrajectorySpline->GetLocationAtSplinePoint(
			SegmentIndex + 1, ESplineCoordinateSpace::Local);
		FVector StartTangent;
		FVector EndTangent;
		if (bSmoothTrajectory)
		{
			StartTangent = TrajectorySpline->GetTangentAtSplinePoint(
				SegmentIndex, ESplineCoordinateSpace::Local);
			EndTangent = TrajectorySpline->GetTangentAtSplinePoint(
				SegmentIndex + 1, ESplineCoordinateSpace::Local);
		}
		else
		{
			StartTangent = EndPosition - StartPosition;
			EndTangent = StartTangent;
		}

		Segment->SetStaticMesh(AimLineMesh);
		Segment->SetForwardAxis(ResolvedAxis, false);
		Segment->SetStartScale(CrossSectionScale, false);
		Segment->SetEndScale(CrossSectionScale, false);
		Segment->SetStartAndEnd(StartPosition, StartTangent, EndPosition, EndTangent, true);
		if (AimLineMaterial)
		{
			Segment->SetMaterial(0, AimLineMaterial);
		}
		Segment->SetVisibility(true);
		Segment->SetHiddenInGame(false);
	}

	for (int32 SegmentIndex = SegmentCount; SegmentIndex < SegmentMeshes.Num(); ++SegmentIndex)
	{
		if (SegmentMeshes[SegmentIndex])
		{
			SegmentMeshes[SegmentIndex]->SetVisibility(false);
			SegmentMeshes[SegmentIndex]->SetHiddenInGame(true);
		}
	}

	if (!bLoggedConfiguration)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[VortexPipeline][AimLineActor] READY Actor=%s Mesh=%s Material=%s "
				"Points=%d Segments=%d Axis=%d Width=%.2f First=%s Last=%s."),
			*GetPathNameSafe(this),
			*GetPathNameSafe(AimLineMesh),
			*GetPathNameSafe(AimLineMaterial),
			PointCount,
			SegmentCount,
			static_cast<int32>(ResolvedAxis),
			WidthScale,
			*WorldPoints[0].ToCompactString(),
			*WorldPoints[PointCount - 1].ToCompactString());
		bLoggedConfiguration = true;
	}
}

void AVortexAimLine::ClearTrajectory()
{
	if (TrajectorySpline)
	{
		TrajectorySpline->ClearSplinePoints(false);
		TrajectorySpline->UpdateSpline();
	}
	for (USplineMeshComponent* Segment : SegmentMeshes)
	{
		if (Segment)
		{
			Segment->SetVisibility(false);
			Segment->SetHiddenInGame(true);
		}
	}
}

USplineMeshComponent* AVortexAimLine::GetOrCreateSegment(int32 SegmentIndex)
{
	if (SegmentMeshes.IsValidIndex(SegmentIndex))
	{
		return SegmentMeshes[SegmentIndex];
	}

	while (SegmentMeshes.Num() <= SegmentIndex)
	{
		USplineMeshComponent* Segment = NewObject<USplineMeshComponent>(this);
		if (!Segment)
		{
			return nullptr;
		}
		AddInstanceComponent(Segment);
		Segment->SetupAttachment(TrajectorySpline);
		Segment->SetMobility(EComponentMobility::Movable);
		Segment->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Segment->SetGenerateOverlapEvents(false);
		Segment->SetCastShadow(false);
		Segment->TranslucencySortPriority = 20;
		Segment->RegisterComponent();
		SegmentMeshes.Add(Segment);
	}

	return SegmentMeshes[SegmentIndex];
}

TEnumAsByte<ESplineMeshAxis::Type> AVortexAimLine::ResolveForwardAxis() const
{
	switch (ForwardAxis)
	{
	case EVortexAimLineForwardAxis::X:
		return ESplineMeshAxis::X;
	case EVortexAimLineForwardAxis::Y:
		return ESplineMeshAxis::Y;
	case EVortexAimLineForwardAxis::Z:
		return ESplineMeshAxis::Z;
	case EVortexAimLineForwardAxis::Auto:
	default:
		break;
	}

	if (!AimLineMesh)
	{
		return ESplineMeshAxis::X;
	}

	const FVector Extent = AimLineMesh->GetBounds().BoxExtent;
	if (Extent.Y >= Extent.X && Extent.Y >= Extent.Z)
	{
		return ESplineMeshAxis::Y;
	}
	if (Extent.Z >= Extent.X && Extent.Z >= Extent.Y)
	{
		return ESplineMeshAxis::Z;
	}
	return ESplineMeshAxis::X;
}
