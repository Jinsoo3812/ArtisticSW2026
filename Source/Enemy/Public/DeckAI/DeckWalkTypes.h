#pragma once

#include "CoreMinimal.h"
#include "DeckWalkTypes.generated.h"

UENUM(BlueprintType)
enum class EDeckWalkHeightMode : uint8
{
	LocalRange UMETA(DisplayName = "Local Height Range"),
	WaypointReference UMETA(DisplayName = "Waypoint Reference Height")
};

/** Authoring only. Component names are resolved on this ship, never on the visual asset. */
USTRUCT(BlueprintType)
struct ENEMY_API FDeckWalkSurfaceSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck Walk")
	FName SurfaceId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck Walk")
	TArray<FName> FloorComponentNames;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck Walk")
	bool bTraceComplex = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck Walk")
	EDeckWalkHeightMode HeightMode = EDeckWalkHeightMode::LocalRange;
	/** Heights use the common DeckMesh_Complex frame, regardless of the source component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck Walk", meta = (Units = "cm", EditCondition = "HeightMode == EDeckWalkHeightMode::LocalRange", EditConditionHides))
	float MinimumFloorZ = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck Walk", meta = (Units = "cm", EditCondition = "HeightMode == EDeckWalkHeightMode::LocalRange", EditConditionHides))
	float MaximumFloorZ = 900.0f;
	/** Only the reference point's ship-local Z defines the band; its XY does not limit coverage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck Walk", meta = (ClampMin = "0", EditCondition = "HeightMode == EDeckWalkHeightMode::WaypointReference", EditConditionHides))
	int32 HeightReferencePointId = INDEX_NONE;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck Walk", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "HeightMode == EDeckWalkHeightMode::WaypointReference", EditConditionHides))
	float HeightBelowReference = 75.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck Walk", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "HeightMode == EDeckWalkHeightMode::WaypointReference", EditConditionHides))
	float HeightAboveReference = 75.0f;
	/** Optional region filter only. An empty array retains all sufficiently large valid regions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck Walk")
	TArray<int32> SeedPointIds;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck Walk")
	bool bRequired = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck Walk")
	FColor DebugColor = FColor::Green;
};

/** Allows adjacent, physically supported walking edges only; never a teleport/jump link. */
USTRUCT(BlueprintType)
struct ENEMY_API FDeckWalkSurfaceConnection
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck Walk")
	FName FromSurface;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck Walk")
	FName ToSurface;
};

/** Query handle. A rebuild invalidates all previous handles. */
USTRUCT(BlueprintType)
struct ENEMY_API FDeckWalkLocation
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deck Walk")
	int32 NodeIndex = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deck Walk")
	int32 Revision = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deck Walk")
	FName SurfaceId;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deck Walk")
	FVector LocalFloor = FVector::ZeroVector;
};
