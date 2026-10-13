#pragma once

#include "CoreMinimal.h"
#include "DeckAI/DeckWalkTypes.h"
#include "DeckAI/DeckWalkGraph.h"

class UStaticMeshComponent;
class UPrimitiveComponent;
class FDeckWalkGraph;
struct FDeckWalkNode;

struct FDeckWalkFloorSource
{
	TWeakObjectPtr<UStaticMeshComponent> Component;
	int32 Surface = INDEX_NONE;
	bool bTraceComplex = true;
	float MinZ = 0.0f;
	float MaxZ = 0.0f;
	bool bUseReferenceHeight = false;
	float ReferenceZ = 0.0f;
};

struct FDeckWalkSamplingSettings
{
	float CellSize = 75.0f;
	float Radius = 45.0f;
	float HalfHeight = 100.0f;
	float StepHeight = 45.0f;
	float SlopeDegrees = 40.0f;
	float Skin = 5.0f;
};

/** Cursor lives with the owner; one sampling column or one edge node per step. */
struct FDeckWalkBuildCursor
{
	FBox Bounds = FBox(ForceInit);
	int32 Width = 0, Height = 0, Source = 0, Column = 0, Candidate = 0, Edge = 0;
	uint8 Phase = 0;
	bool bFailed = false;
	bool bColumnStarted = false;
	int32 Probe = 0;
	float ProbeZ = 0.0f;
	TMap<FIntVector, FDeckWalkNode> ReferenceCandidates;
	TArray<FDeckWalkNode> Candidates;
};

/** Physics adapter: samples stacked floors, tests headroom and supported walking edges. */
class FDeckWalkSurfaceSampler
{
public:
	FDeckWalkSurfaceSampler(const FTransform& InFrame, const FDeckWalkSamplingSettings& InSettings,
		const TArray<FDeckWalkFloorSource>& InSources,
		const TArray<TWeakObjectPtr<UPrimitiveComponent>>& InObstacles);
	bool Build(const TArray<FDeckWalkSurfaceSettings>& Surfaces,
		const TArray<FDeckWalkSurfaceConnection>& Connections, FDeckWalkGraph& OutGraph) const;
	bool BeginBuild(FDeckWalkBuildCursor& Cursor, FDeckWalkGraph& Graph) const;
	bool StepBuild(FDeckWalkBuildCursor& Cursor, const TArray<FDeckWalkSurfaceSettings>& Surfaces,
		const TArray<FDeckWalkSurfaceConnection>& Connections, FDeckWalkGraph& Graph) const;
	bool HasClearance(const FVector& LocalFloor) const;
	bool TraceObstacle(const FVector& WorldStart, const FVector& WorldEnd, FHitResult& OutHit) const;
	bool TraceNear(const FVector& LocalFloor, int32 Surface, float Above, float Below,
		FVector& OutFloor) const;

private:
	bool HasPassage(const FDeckWalkNode& A, const FDeckWalkNode& B) const;
	FTransform Frame;
	FDeckWalkSamplingSettings Settings;
	const TArray<FDeckWalkFloorSource>& Sources;
	const TArray<TWeakObjectPtr<UPrimitiveComponent>>& Obstacles;
};
