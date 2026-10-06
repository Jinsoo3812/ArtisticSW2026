#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR
class AEnemyShip;
class ABaseEnemy;
class UDeckWalkAreaComponent;
class UDeckWaypointComponent;
struct FDataTableRowHandle;

struct FDeckSpawnAnchorValidationResult
{
	int32 PointCount = 0;
	int32 AnchorCount = 0;
	int32 ErrorCount = 0;
	bool bCheckedRuntimeFloor = false;
	FString ToSummary() const;
};

/** Read-only editor validation. Never renumbers, links, adds or removes ship components. */
class FDeckSpawnAnchorValidator
{
public:
	static FDeckSpawnAnchorValidationResult Validate(AEnemyShip& Context);

private:
	explicit FDeckSpawnAnchorValidator(AEnemyShip& InShip) : Ship(InShip) {}
	void CollectPoints();
	void ValidateWalkArea();
	void ValidateSpawnPlan();
	void ValidateBossSpawn();
	UDeckWaypointComponent* ValidateReference(int32 PointId, const FString& Source, bool bRequireSpawn);
	void ValidateCapsule(const UDeckWaypointComponent& Point, const ABaseEnemy& Enemy, const FString& Source);
	void ValidateStats(const FDataTableRowHandle& Override, const ABaseEnemy& Enemy, const FString& Source);
	void Error(const FString& Message);

	AEnemyShip& Ship;
	FDeckSpawnAnchorValidationResult Result;
	TMap<int32, UDeckWaypointComponent*> PointsById;
	TSet<int32> AnchorIds;
};
#endif
