#include "DeckAI/DeckWalkSurfaceSampler.h"

#include "DeckAI/DeckWalkGraph.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

FDeckWalkSurfaceSampler::FDeckWalkSurfaceSampler(const FTransform& InFrame,
	const FDeckWalkSamplingSettings& InSettings, const TArray<FDeckWalkFloorSource>& InSources,
	const TArray<TWeakObjectPtr<UPrimitiveComponent>>& InObstacles)
	: Frame(InFrame), Settings(InSettings), Sources(InSources), Obstacles(InObstacles)
{
}

bool FDeckWalkSurfaceSampler::HasClearance(const FVector& LocalFloor) const
{
	const FVector Center = Frame.TransformPosition(LocalFloor)
		+ Frame.GetUnitAxis(EAxis::Z) * (Settings.HalfHeight + Settings.Skin);
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Settings.Radius, Settings.HalfHeight);
	for (const auto& Obstacle : Obstacles)
	{
		// Keep the source floor in this query: its other triangles may form a ceiling.
		if (Obstacle.IsValid() && Obstacle->OverlapComponent(Center, Frame.GetRotation(), Shape)) return false;
	}
	return true;
}

bool FDeckWalkSurfaceSampler::TraceObstacle(const FVector& WorldStart, const FVector& WorldEnd, FHitResult& OutHit) const
{
	bool bHit = false;
	float BestTime = 1.0f;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(DeckWalkObstacleLOS), false);
	for (const auto& Obstacle : Obstacles)
	{
		FHitResult Hit;
		if (Obstacle.IsValid() && Obstacle->LineTraceComponent(Hit, WorldStart, WorldEnd, Query)
			&& Hit.Time <= BestTime)
		{
			OutHit = Hit; BestTime = Hit.Time; bHit = true;
		}
	}
	return bHit;
}

bool FDeckWalkSurfaceSampler::TraceNear(const FVector& LocalFloor, int32 Surface,
	float Above, float Below, FVector& OutFloor) const
{
	float Best = TNumericLimits<float>::Max();
	bool bFound = false;
	for (const FDeckWalkFloorSource& Source : Sources)
	{
		if (!Source.Component.IsValid() || Source.Surface != Surface) continue;
		const float Top = FMath::Min(LocalFloor.Z + Above, Source.MaxZ + 0.1f);
		const float Bottom = FMath::Max(LocalFloor.Z - Below, Source.MinZ - 0.1f);
		if (Top <= Bottom) continue;
		FHitResult Hit;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(DeckWalkSupport), Source.bTraceComplex);
		if (!Source.Component->LineTraceComponent(Hit,
			Frame.TransformPosition(FVector(LocalFloor.X, LocalFloor.Y, Top)),
			Frame.TransformPosition(FVector(LocalFloor.X, LocalFloor.Y, Bottom)), Query)
			|| Hit.bStartPenetrating
			|| FVector::DotProduct(Hit.ImpactNormal, Frame.GetUnitAxis(EAxis::Z))
				< FMath::Cos(FMath::DegreesToRadians(Settings.SlopeDegrees))) continue;
		const FVector Floor = Frame.InverseTransformPosition(Hit.ImpactPoint);
		if (Floor.Z < Source.MinZ || Floor.Z > Source.MaxZ) continue;
		const float Error = FMath::Abs(Floor.Z - LocalFloor.Z);
		if (Error < Best) { OutFloor = Floor; Best = Error; bFound = true; }
	}
	return bFound;
}

bool FDeckWalkSurfaceSampler::HasPassage(const FDeckWalkNode& A, const FDeckWalkNode& B) const
{
	if (FMath::Abs(A.Floor.Z - B.Floor.Z) > Settings.StepHeight) return false;
	// Require support along the edge so equal-height nodes cannot bridge a hole.
	FVector Previous = A.Floor;
	for (int32 I = 1; I <= 4; ++I)
	{
		const FVector Expected = FMath::Lerp(A.Floor, B.Floor, I / 4.0f);
		FVector Support;
		if ((!TraceNear(Expected, A.Surface, Settings.StepHeight + 2.0f,
			Settings.StepHeight + 2.0f, Support)
			&& !TraceNear(Expected, B.Surface, Settings.StepHeight + 2.0f,
				Settings.StepHeight + 2.0f, Support))
			|| FMath::Abs(Support.Z - Previous.Z) > Settings.StepHeight) return false;
		Previous = Support;
	}
	const FVector Up = Frame.GetUnitAxis(EAxis::Z);
	const FVector Start = Frame.TransformPosition(A.Floor) + Up * (Settings.HalfHeight + Settings.Skin);
	const FVector End = Frame.TransformPosition(B.Floor) + Up * (Settings.HalfHeight + Settings.Skin);
	const float Height = FMath::Max(A.Floor.Z, B.Floor.Z);
	const FVector RaisedStart = Start + Up * (Height - A.Floor.Z);
	const FVector RaisedEnd = End + Up * (Height - B.Floor.Z);
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Settings.Radius, Settings.HalfHeight);
	for (const auto& Obstacle : Obstacles)
	{
		if (!Obstacle.IsValid()) continue;
		FHitResult Hit;
		if (Obstacle->SweepComponent(Hit, RaisedStart, RaisedEnd, Frame.GetRotation(), Shape)
			|| (!Start.Equals(RaisedStart) && Obstacle->SweepComponent(Hit, Start, RaisedStart, Frame.GetRotation(), Shape))
			|| (!End.Equals(RaisedEnd) && Obstacle->SweepComponent(Hit, End, RaisedEnd, Frame.GetRotation(), Shape))) return false;
	}
	return true;
}

bool FDeckWalkSurfaceSampler::BeginBuild(FDeckWalkBuildCursor& Cursor, FDeckWalkGraph& Graph) const
{
	Graph.Reset();
	Cursor = FDeckWalkBuildCursor();
	FBox& Bounds = Cursor.Bounds;
	for (const auto& Source : Sources)
	{
		const UStaticMeshComponent* Component = Source.Component.Get();
		if (!Component || !Component->GetStaticMesh()) continue;
		const FBox MeshBounds = Component->GetStaticMesh()->GetBoundingBox();
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector P(Corner & 1 ? MeshBounds.Max.X : MeshBounds.Min.X,
				Corner & 2 ? MeshBounds.Max.Y : MeshBounds.Min.Y,
				Corner & 4 ? MeshBounds.Max.Z : MeshBounds.Min.Z);
			Bounds += Frame.InverseTransformPosition(Component->GetComponentTransform().TransformPosition(P));
		}
	}
	if (!Bounds.IsValid) return false;
	const int32 Width = Cursor.Width = FMath::FloorToInt(Bounds.GetSize().X / Settings.CellSize) + 1;
	const int32 Height = Cursor.Height = FMath::FloorToInt(Bounds.GetSize().Y / Settings.CellSize) + 1;
	if (Width < 2 || Height < 2 || Width > 160 || Height > 160) return false;
	return true;
}

bool FDeckWalkSurfaceSampler::StepBuild(FDeckWalkBuildCursor& Cursor,
	const TArray<FDeckWalkSurfaceSettings>& Surfaces,
	const TArray<FDeckWalkSurfaceConnection>& Connections, FDeckWalkGraph& OutGraph) const
{
	if (Cursor.bFailed || Cursor.Phase == 3) return true;
	auto& ReferenceCandidates = Cursor.ReferenceCandidates;
	const float NormalDot = FMath::Cos(FMath::DegreesToRadians(Settings.SlopeDegrees));
	const auto AddWalkableNode = [&](const FDeckWalkNode& Node)
	{
		if (const TArray<int32>* Existing = OutGraph.Columns.Find(Node.Column))
		{
			for (int32 Id : *Existing)
			{
				if (FMath::Abs(OutGraph.Nodes[Id].Floor.Z - Node.Floor.Z) > 3.0f) continue;
				if (OutGraph.Nodes[Id].Surface != Node.Surface)
				{
					/* Diagnostics muted after pooling/crew verification.
					UE_LOG(LogTemp, Error, TEXT("[DeckWalk] Surface height bands overlap at %s"), *Node.Floor.ToString()); */ {}
					return false;
				}
				return true;
			}
		}
		if (HasClearance(Node.Floor)) OutGraph.AddNode(Node);
		return true;
	};

	if (Cursor.Phase == 0)
	{
		if (Cursor.Source >= Sources.Num())
		{
			ReferenceCandidates.GenerateValueArray(Cursor.Candidates);
			ReferenceCandidates.Reset(); Cursor.Phase = 1; return false;
		}
		const int32 SourceIndex = Cursor.Source;
		const auto& Source = Sources[SourceIndex];
		UStaticMeshComponent* Component = Source.Component.Get();
		if (!Component) { ++Cursor.Source; Cursor.Column = 0; Cursor.bColumnStarted = false; return false; }
		const int32 X = Cursor.Column % Cursor.Width, Y = Cursor.Column / Cursor.Width;
		const FIntPoint Column(X, Y);
		const float PX = Cursor.Bounds.Min.X + X * Settings.CellSize;
		const float PY = Cursor.Bounds.Min.Y + Y * Settings.CellSize;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(DeckWalkLayerSample), Source.bTraceComplex);
		const auto FinishColumn = [&]()
		{
			Cursor.bColumnStarted = false;
			if (++Cursor.Column >= Cursor.Width * Cursor.Height) { Cursor.Column = 0; ++Cursor.Source; }
		};
		if (!Cursor.bColumnStarted) { Cursor.bColumnStarted = true; Cursor.ProbeZ = Source.MaxZ + 0.1f; Cursor.Probe = 0; }
		if (Cursor.Probe++ >= 2048 || Cursor.ProbeZ <= Source.MinZ) { FinishColumn(); return false; }
					FHitResult Hit;
					if (!Component->LineTraceComponent(Hit,
						Frame.TransformPosition(FVector(PX, PY, Cursor.ProbeZ)),
						Frame.TransformPosition(FVector(PX, PY, Source.MinZ - 0.1f)), Query)) { FinishColumn(); return false; }
					const FVector Floor = Frame.InverseTransformPosition(Hit.ImpactPoint);
					Cursor.ProbeZ = FMath::Min(Cursor.ProbeZ - 2.0f, static_cast<float>(Floor.Z) - 2.0f);
					if (Hit.bStartPenetrating || Floor.Z < Source.MinZ || Floor.Z > Source.MaxZ
						|| FVector::DotProduct(Hit.ImpactNormal, Frame.GetUnitAxis(EAxis::Z)) < NormalDot) return false;
					// An explicit reference band owns its height range. A broad legacy range
					// must not claim the additional deck before its reference surface is built.
					if (!Source.bUseReferenceHeight && Sources.ContainsByPredicate(
						[&](const FDeckWalkFloorSource& Other)
						{
							return Other.Surface != Source.Surface && Other.bUseReferenceHeight
								&& Floor.Z >= Other.MinZ && Floor.Z <= Other.MaxZ;
						})) return false;
					FDeckWalkNode Node;
					Node.Floor = Floor; Node.Column = Column;
					Node.Surface = Source.Surface; Node.Source = SourceIndex;
					if (Source.bUseReferenceHeight)
					{
						const FIntVector Key(X, Y, Source.Surface);
						const FDeckWalkNode* Previous = ReferenceCandidates.Find(Key);
						if (!Previous || FMath::Abs(Floor.Z - Source.ReferenceZ)
							< FMath::Abs(Previous->Floor.Z - Source.ReferenceZ))
						{
							ReferenceCandidates.Add(Key, Node);
						}
					}
					else if (!AddWalkableNode(Node)) { Cursor.bFailed = true; return true; }
		return false;
	}
	if (Cursor.Phase == 1)
	{
		if (Cursor.Candidate < Cursor.Candidates.Num())
		{
			if (!AddWalkableNode(Cursor.Candidates[Cursor.Candidate++])) { Cursor.bFailed = true; return true; }
			return false;
		}
		Cursor.Candidates.Reset(); Cursor.Phase = 2; return false;
	}
	if (Cursor.Edge < OutGraph.Nodes.Num())
	{
		const int32 A = Cursor.Edge++;
		const FDeckWalkNode& Node = OutGraph.Nodes[A];
		for (const FIntPoint Offset : { FIntPoint(1, 0), FIntPoint(0, 1) })
		{
			const auto* Neighbors = OutGraph.Columns.Find(Node.Column + Offset);
			if (!Neighbors) continue;
			for (const int32 B : *Neighbors)
			{
				const FDeckWalkNode& Other = OutGraph.Nodes[B];
				const bool bAllowed = Node.Surface == Other.Surface || Connections.ContainsByPredicate(
					[&](const FDeckWalkSurfaceConnection& Link)
					{
						const FName From = Surfaces[Node.Surface].SurfaceId;
						const FName To = Surfaces[Other.Surface].SurfaceId;
						return (Link.FromSurface == From && Link.ToSurface == To)
							|| (Link.FromSurface == To && Link.ToSurface == From);
					});
				if (bAllowed && HasPassage(Node, Other)) OutGraph.AddEdge(A, B);
			}
		}
		return false;
	}
	Cursor.Phase = 3;
	Cursor.bFailed = OutGraph.Nodes.IsEmpty();
	return true;
}

bool FDeckWalkSurfaceSampler::Build(const TArray<FDeckWalkSurfaceSettings>& Surfaces,
	const TArray<FDeckWalkSurfaceConnection>& Connections, FDeckWalkGraph& OutGraph) const
{
	FDeckWalkBuildCursor Cursor;
	if (!BeginBuild(Cursor, OutGraph)) return false;
	while (!StepBuild(Cursor, Surfaces, Connections, OutGraph)) {}
	return !Cursor.bFailed;
}
