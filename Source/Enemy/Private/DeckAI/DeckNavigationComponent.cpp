#include "DeckAI/DeckNavigationComponent.h"

#include "Algo/Unique.h"
#include "Components/StaticMeshComponent.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "ShipAI/EnemyShip.h"

UDeckNavigationComponent::UDeckNavigationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void UDeckNavigationComponent::RebuildGraph()
{
	NodesById.Reset();
	AEnemyShip* Ship = Cast<AEnemyShip>(GetOwner());
	UStaticMeshComponent* DeckMesh = Ship ? Ship->GetShipDeckMesh() : nullptr;
	if (!Ship || !DeckMesh)
	{
		++GraphRevision;
		return;
	}

	TArray<int32> PointIds;
	Ship->GetDeckWaypointIds(PointIds, false);
	const FTransform DeckTransform = DeckMesh->GetComponentTransform();
	for (const int32 PointId : PointIds)
	{
		const UDeckWaypointComponent* Point = Ship->GetDeckWaypoint(PointId);
		if (!Point)
		{
			continue;
		}

		FDeckNavigationNode& Node = NodesById.Add(PointId);
		Node.PointId = PointId;
		Node.LocalLocation = DeckTransform.InverseTransformPosition(Point->GetComponentLocation());
		Node.LinkedPointIds = Point->GetLinkedWaypointIds();
		Node.LinkedPointIds.RemoveAll([Ship](int32 LinkedId)
		{
			return LinkedId == INDEX_NONE || !Ship->GetDeckWaypoint(LinkedId);
		});
		Node.LinkedPointIds.Sort();
		Node.LinkedPointIds.SetNum(Algo::Unique(Node.LinkedPointIds));
	}
	++GraphRevision;
}

bool UDeckNavigationComponent::FindPathToAny(
	int32 StartPointId,
	const TMap<int32, float>& GoalSecondaryCosts,
	const AActor* Requester,
	FDeckNavigationPath& OutPath) const
{
	const AEnemyShip* Ship = Cast<AEnemyShip>(GetOwner());
	if (!Ship || !Ship->HasAuthority() || !IsValid(Requester))
	{
		OutPath.Reset();
		return false;
	}
	if (const UDeckWalkAreaComponent* Area = Ship->GetDeckWalkAreaComponent();
		Area && Area->IsReady())
	{
		OutPath.Reset();
		float BestCost = TNumericLimits<float>::Max();
		for (const TPair<int32, float>& Goal : GoalSecondaryCosts)
		{
			const UDeckWaypointComponent* Point = Ship->GetDeckWaypoint(Goal.Key);
			if (!Point || (Goal.Key != StartPointId
				&& !Ship->IsDeckPointAvailable(Goal.Key, Requester))) continue;
			TArray<FDeckWalkLocation> LocalPath;
			if (!Area->FindPathToWaypoint(*Requester, *Point, LocalPath)) continue;
			float Cost = FMath::Max(0.0f, Goal.Value);
			for (int32 I = 1; I < LocalPath.Num(); ++I)
			{
				Cost += FVector::Dist(LocalPath[I - 1].LocalFloor, LocalPath[I].LocalFloor);
			}
			if (Cost >= BestCost) continue;
			BestCost = Cost;
			OutPath.PointIds = { StartPointId };
			if (Goal.Key != StartPointId) OutPath.PointIds.Add(Goal.Key);
			OutPath.GoalPointId = Goal.Key;
			OutPath.TravelCost = Cost;
		}
		return OutPath.IsValid();
	}

	TSet<int32> BlockedPointIds;
	for (const TPair<int32, FDeckNavigationNode>& Pair : NodesById)
	{
		if (Pair.Key != StartPointId && !Ship->IsDeckPointAvailable(Pair.Key, Requester))
		{
			BlockedPointIds.Add(Pair.Key);
		}
	}

	return FDeckGraphPathfinder::FindLowestCostPathToAny(
		NodesById,
		StartPointId,
		GoalSecondaryCosts,
		BlockedPointIds,
		OutPath);
}

bool UDeckNavigationComponent::GetPointLocalLocation(
	int32 PointId,
	FVector& OutLocalLocation) const
{
	if (const AEnemyShip* Ship = Cast<AEnemyShip>(GetOwner()))
	{
		if (const UDeckWalkAreaComponent* Area = Ship->GetDeckWalkAreaComponent();
			Area && Area->IsReady())
		{
			const UDeckWaypointComponent* Point = Ship->GetDeckWaypoint(PointId);
			FDeckWalkLocation Location;
			if (!Point || !Area->ResolveWaypoint(*Point, Location)) return false;
			OutLocalLocation = Location.LocalFloor;
			return true;
		}
	}
	const FDeckNavigationNode* Node = NodesById.Find(PointId);
	if (!Node)
	{
		return false;
	}
	OutLocalLocation = Node->LocalLocation;
	return true;
}
