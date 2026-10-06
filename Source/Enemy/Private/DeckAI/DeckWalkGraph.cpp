#include "DeckAI/DeckWalkGraph.h"

void FDeckWalkGraph::Reset()
{
	Nodes.Reset();
	Columns.Reset();
}

int32 FDeckWalkGraph::AddNode(const FDeckWalkNode& Node)
{
	const int32 Index = Nodes.Add(Node);
	Columns.FindOrAdd(Node.Column).Add(Index);
	return Index;
}

void FDeckWalkGraph::AddEdge(int32 A, int32 B)
{
	Nodes[A].Neighbors.AddUnique(B);
	Nodes[B].Neighbors.AddUnique(A);
}

int32 FDeckWalkGraph::LabelRegions(bool bCrossSurfaces)
{
	for (FDeckWalkNode& Node : Nodes) Node.Region = INDEX_NONE;
	int32 Region = 0;
	for (int32 Start = 0; Start < Nodes.Num(); ++Start)
	{
		if (!Nodes[Start].bEnabled || Nodes[Start].Region != INDEX_NONE) continue;
		TArray<int32> Queue = { Start };
		Nodes[Start].Region = Region;
		for (int32 Cursor = 0; Cursor < Queue.Num(); ++Cursor)
		{
			const FDeckWalkNode& Current = Nodes[Queue[Cursor]];
			for (const int32 Next : Current.Neighbors)
			{
				FDeckWalkNode& Neighbor = Nodes[Next];
				if (!Neighbor.bEnabled || Neighbor.Region != INDEX_NONE
					|| (!bCrossSurfaces && Neighbor.Surface != Current.Surface)) continue;
				Neighbor.Region = Region;
				Queue.Add(Next);
			}
		}
		++Region;
	}
	return Region;
}

bool FDeckWalkGraph::FindPath(int32 Start, int32 Goal, bool bCrossSurfaces,
	TArray<int32>& OutPath, const TArray<uint8>* AllowedNodes) const
{
	OutPath.Reset();
	if (!Nodes.IsValidIndex(Start) || !Nodes.IsValidIndex(Goal)
		|| !Nodes[Start].bEnabled || !Nodes[Goal].bEnabled
		|| Nodes[Start].Region != Nodes[Goal].Region) return false;
	TArray<float> Cost;
	TArray<int32> Parent;
	TArray<uint8> Closed;
	Cost.Init(TNumericLimits<float>::Max(), Nodes.Num());
	Parent.Init(INDEX_NONE, Nodes.Num());
	Closed.Init(0, Nodes.Num());
	TArray<int32> Open = { Start };
	Cost[Start] = 0.0f;
	while (!Open.IsEmpty())
	{
		int32 Best = 0;
		float Estimate = TNumericLimits<float>::Max();
		for (int32 I = 0; I < Open.Num(); ++I)
		{
			const float Candidate = Cost[Open[I]] + FVector::Dist(Nodes[Open[I]].Floor, Nodes[Goal].Floor);
			if (Candidate < Estimate) { Best = I; Estimate = Candidate; }
		}
		const int32 Current = Open[Best];
		Open.RemoveAtSwap(Best, 1, EAllowShrinking::No);
		if (Current == Goal) break;
		Closed[Current] = 1;
		for (const int32 Next : Nodes[Current].Neighbors)
		{
			if (Closed[Next] || !Nodes[Next].bEnabled
				|| (AllowedNodes && (!AllowedNodes->IsValidIndex(Next) || !(*AllowedNodes)[Next]))
				|| (!bCrossSurfaces && Nodes[Next].Surface != Nodes[Current].Surface)) continue;
			const float NewCost = Cost[Current] + FVector::Dist(Nodes[Current].Floor, Nodes[Next].Floor);
			if (NewCost >= Cost[Next]) continue;
			Cost[Next] = NewCost;
			Parent[Next] = Current;
			Open.AddUnique(Next);
		}
	}
	if (Start != Goal && Parent[Goal] == INDEX_NONE) return false;
	TArray<int32> Reverse;
	for (int32 Cursor = Goal; Cursor != INDEX_NONE; Cursor = Parent[Cursor])
	{
		Reverse.Add(Cursor);
		if (Cursor == Start) break;
	}
	for (int32 I = Reverse.Num() - 1; I >= 0; --I) OutPath.Add(Reverse[I]);
	return true;
}
