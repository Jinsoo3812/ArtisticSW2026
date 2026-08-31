#include "DeckAI/DeckNavigationComponent.h"

#include "Algo/Unique.h"
#include "Components/StaticMeshComponent.h"
#include "DeckAI/DeckWaypointComponent.h"
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
	const FDeckNavigationNode* Node = NodesById.Find(PointId);
	if (!Node)
	{
		return false;
	}
	OutLocalLocation = Node->LocalLocation;
	return true;
}
