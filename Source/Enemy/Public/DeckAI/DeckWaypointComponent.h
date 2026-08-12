#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "DeckWaypointComponent.generated.h"

/**
 * Designer-authored point on a moving ship deck.
 *
 * The component is attached below ShipDeckMesh, so its relative transform is
 * static while its world transform follows the ship's network-physics motion.
 * It intentionally has no tick and no independent replication.
 */
UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
class ENEMY_API UDeckWaypointComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UDeckWaypointComponent();

	UFUNCTION(BlueprintPure, Category = "Deck AI|Waypoint")
	int32 GetWaypointId() const { return WaypointId; }

	const TArray<int32>& GetLinkedWaypointIds() const { return LinkedWaypointIds; }

	UFUNCTION(BlueprintPure, Category = "Deck AI|Waypoint")
	bool CanSpawnEnemy() const { return bCanSpawn; }

	UFUNCTION(BlueprintPure, Category = "Deck AI|Waypoint")
	bool CanPatrol() const { return bCanPatrol; }

	UFUNCTION(BlueprintPure, Category = "Deck AI|Waypoint")
	bool CanUseInCombat() const { return bCanUseInCombat; }

	float GetRandomWaitTime(FRandomStream& RandomStream) const;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck AI|Waypoint", meta = (ClampMin = "0"))
	int32 WaypointId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck AI|Waypoint")
	TArray<int32> LinkedWaypointIds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck AI|Usage")
	bool bCanSpawn = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck AI|Usage")
	bool bCanPatrol = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck AI|Usage")
	bool bCanUseInCombat = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck AI|Patrol", meta = (ClampMin = "0.0", Units = "s"))
	float MinWaitTime = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck AI|Patrol", meta = (ClampMin = "0.0", Units = "s"))
	float MaxWaitTime = 2.0f;
};
