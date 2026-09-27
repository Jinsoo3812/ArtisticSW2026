#include "DeckAI/DeckNavigationTypes.h"

#include "Algo/Reverse.h"

bool FDeckGraphPathfinder::FindLowestCostPathToAny(
	const TMap<int32, FDeckNavigationNode>& Nodes,
	int32 StartPointId,
	const TMap<int32, float>& GoalSecondaryCosts,
	const TSet<int32>& BlockedPointIds,
	FDeckNavigationPath& OutPath)
{
	OutPath.Reset();
	if (!Nodes.Contains(StartPointId) || GoalSecondaryCosts.IsEmpty())
	{
		return false;
	}

	TMap<int32, float> Distances;
	TMap<int32, int32> Previous;
	TSet<int32> Visited;
	TArray<int32> OpenPointIds;
	Distances.Add(StartPointId, 0.0f);
	OpenPointIds.Add(StartPointId);

	while (!OpenPointIds.IsEmpty())
	{
		OpenPointIds.Sort([&Distances](int32 Left, int32 Right)
		{
			const float LeftCost = Distances.FindRef(Left);
			const float RightCost = Distances.FindRef(Right);
			return !FMath::IsNearlyEqual(LeftCost, RightCost)
				? LeftCost > RightCost
				: Left > Right;
		});
		const int32 CurrentId = OpenPointIds.Pop(EAllowShrinking::No);
		if (Visited.Contains(CurrentId))
		{
			continue;
		}
		Visited.Add(CurrentId);

		const FDeckNavigationNode* CurrentNode = Nodes.Find(CurrentId);
		if (!CurrentNode)
		{
			continue;
		}

		TArray<int32> SortedLinks = CurrentNode->LinkedPointIds;
		SortedLinks.Sort();
		for (const int32 LinkedId : SortedLinks)
		{
			const FDeckNavigationNode* LinkedNode = Nodes.Find(LinkedId);
			if (!LinkedNode || (LinkedId != StartPointId && BlockedPointIds.Contains(LinkedId)))
			{
				continue;
			}

			const float EdgeCost = FMath::Max(
				1.0f,
				FVector::Dist2D(CurrentNode->LocalLocation, LinkedNode->LocalLocation));
			const float CandidateCost = Distances.FindRef(CurrentId) + EdgeCost;
			const float* ExistingCost = Distances.Find(LinkedId);
			const int32 ExistingPrevious = Previous.FindRef(LinkedId);
			if (!ExistingCost || CandidateCost < *ExistingCost - KINDA_SMALL_NUMBER
				|| (FMath::IsNearlyEqual(CandidateCost, *ExistingCost) && CurrentId < ExistingPrevious))
			{
				Distances.Add(LinkedId, CandidateCost);
				Previous.Add(LinkedId, CurrentId);
				OpenPointIds.Add(LinkedId);
			}
		}
	}

	int32 BestGoalId = INDEX_NONE;
	float BestTravelCost = TNumericLimits<float>::Max();
	float BestSecondaryCost = TNumericLimits<float>::Max();
	for (const TPair<int32, float>& Goal : GoalSecondaryCosts)
	{
		const float* TravelCost = Distances.Find(Goal.Key);
		if (!TravelCost)
		{
			continue;
		}

		const bool bBetterTravel = *TravelCost < BestTravelCost - KINDA_SMALL_NUMBER;
		const bool bEqualTravel = FMath::IsNearlyEqual(*TravelCost, BestTravelCost);
		const bool bBetterSecondary = Goal.Value < BestSecondaryCost - KINDA_SMALL_NUMBER;
		const bool bEqualSecondary = FMath::IsNearlyEqual(Goal.Value, BestSecondaryCost);
		if (BestGoalId == INDEX_NONE || bBetterTravel
			|| (bEqualTravel && (bBetterSecondary
				|| (bEqualSecondary && Goal.Key < BestGoalId))))
		{
			BestGoalId = Goal.Key;
			BestTravelCost = *TravelCost;
			BestSecondaryCost = Goal.Value;
		}
	}

	if (BestGoalId == INDEX_NONE)
	{
		return false;
	}

	TArray<int32> ReversePath;
	int32 Cursor = BestGoalId;
	ReversePath.Add(Cursor);
	while (Cursor != StartPointId)
	{
		const int32* Parent = Previous.Find(Cursor);
		if (!Parent)
		{
			OutPath.Reset();
			return false;
		}
		Cursor = *Parent;
		ReversePath.Add(Cursor);
	}

	Algo::Reverse(ReversePath);
	OutPath.PointIds = MoveTemp(ReversePath);
	OutPath.GoalPointId = BestGoalId;
	OutPath.TravelCost = BestTravelCost;
	return true;
}
