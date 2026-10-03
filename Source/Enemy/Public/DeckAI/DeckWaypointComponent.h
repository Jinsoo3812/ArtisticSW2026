#pragma once

#include "CoreMinimal.h"
#include "Components/SphereComponent.h"
#include "DeckWaypointComponent.generated.h"

/**
 * Manually authored spawn anchor on a moving ship deck.
 *
 * The component is attached below ShipDeckMesh, so its relative transform is
 * static while its world transform follows the ship's network-physics motion.
 * It intentionally has no tick and no independent replication.
 */
UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
class ENEMY_API UDeckWaypointComponent : public USphereComponent
{
	GENERATED_BODY()

public:
	UDeckWaypointComponent();

	UFUNCTION(BlueprintPure, Category = "Deck AI|Waypoint")
	int32 GetWaypointId() const { return WaypointId; }

	UFUNCTION(BlueprintPure, Category = "Deck AI|Waypoint")
	FName GetWalkSurfaceId() const { return WalkSurfaceId; }


	UFUNCTION(BlueprintPure, Category = "Deck AI|Waypoint")
	bool CanSpawnEnemy() const { return bCanSpawn; }



	/** Editor authoring helpers. Runtime code treats IDs as immutable. */
	void SetWaypointIdForAuthoring(int32 InWaypointId);
	void RefreshEditorVisualization();


#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	/** Required for spawn anchors on ships with multiple walk surfaces. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck AI|Waypoint")
	FName WalkSurfaceId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck AI|Waypoint", meta = (ClampMin = "0"))
	int32 WaypointId = 0;


	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck AI|Usage")
	bool bCanSpawn = false;





};
