#pragma once

#include "CoreMinimal.h"
#include "Components/SphereComponent.h"
#include "DeckWaypointComponent.generated.h"

/**
 * Designer-authored point on a moving ship deck.
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

	const TArray<int32>& GetLinkedWaypointIds() const { return LinkedWaypointIds; }

	UFUNCTION(BlueprintPure, Category = "Deck AI|Waypoint")
	bool CanSpawnEnemy() const { return bCanSpawn; }

	UFUNCTION(BlueprintPure, Category = "Deck AI|Waypoint")
	bool CanPatrol() const { return bCanPatrol; }

	UFUNCTION(BlueprintPure, Category = "Deck AI|Waypoint")
	bool CanUseInCombat() const { return bCanUseInCombat; }

	UFUNCTION(BlueprintPure, Category = "Deck AI|Generation")
	bool WasGeneratedFromDeckMesh() const { return bGeneratedFromDeckMesh; }

	UFUNCTION(BlueprintPure, Category = "Deck AI|Generation")
	bool IsGeneratedLocationLocked() const { return bLockGeneratedLocation; }

	int32 GetGeneratedGridX() const { return GeneratedGridX; }
	int32 GetGeneratedGridY() const { return GeneratedGridY; }

	/** Used by the ship's editor generator. Existing usage flags are intentionally not changed on regeneration. */
	void InitializeGeneratedWaypoint(
		int32 InWaypointId,
		int32 InGridX,
		int32 InGridY,
		bool bInCanSpawn,
		bool bInCanPatrol,
		bool bInCanUseInCombat);

	void SetGeneratedLinks(const TArray<int32>& InLinkedWaypointIds);
	void RefreshEditorVisualization();

	float GetRandomWaitTime(FRandomStream& RandomStream) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

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

	/** Generated points remain ordinary editable components. Enable this after hand-moving a point to preserve its location on regeneration. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck AI|Generation", meta = (EditCondition = "bGeneratedFromDeckMesh"))
	bool bLockGeneratedLocation = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deck AI|Generation")
	bool bGeneratedFromDeckMesh = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deck AI|Generation")
	int32 GeneratedGridX = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deck AI|Generation")
	int32 GeneratedGridY = INDEX_NONE;
};
