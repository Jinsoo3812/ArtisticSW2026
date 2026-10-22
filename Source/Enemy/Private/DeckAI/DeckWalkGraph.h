#pragma once

#include "CoreMinimal.h"

/** Geometry-free graph. Owns connectivity, regions and A*, not physics or actor state. */
struct FDeckWalkNode
{
	FVector Floor = FVector::ZeroVector;
	int32 Surface = INDEX_NONE;
	int32 Source = INDEX_NONE;
	int32 Region = INDEX_NONE;
	FIntPoint Column = FIntPoint::ZeroValue;
	TArray<int32> Neighbors;
	bool bEnabled = true;
};

class FDeckWalkGraph
{
public:
	TArray<FDeckWalkNode> Nodes;
	TMap<FIntPoint, TArray<int32>> Columns;
	void Reset();
	int32 AddNode(const FDeckWalkNode& Node);
	void AddEdge(int32 A, int32 B);
	int32 LabelRegions(bool bCrossSurfaces);
	bool FindPath(int32 Start, int32 Goal, bool bCrossSurfaces, TArray<int32>& OutPath,
		const TArray<uint8>* AllowedNodes = nullptr,
		const TFunction<bool(int32, int32)>* AllowedEdges = nullptr) const;
};
