#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DeckAI/DeckNavigationTypes.h"
#include "DeckNavigationComponent.generated.h"

/** Ship-owned, non-replicated graph snapshot and path query service for deck characters. */
UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
class ENEMY_API UDeckNavigationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDeckNavigationComponent();

	void RebuildGraph();

	bool FindPathToAny(
		int32 StartPointId,
		const TMap<int32, float>& GoalSecondaryCosts,
		const AActor* Requester,
		FDeckNavigationPath& OutPath) const;

	bool GetPointLocalLocation(int32 PointId, FVector& OutLocalLocation) const;
	int32 GetGraphRevision() const { return GraphRevision; }
	int32 GetNodeCount() const { return NodesById.Num(); }

private:
	TMap<int32, FDeckNavigationNode> NodesById;
	int32 GraphRevision = 0;
};
