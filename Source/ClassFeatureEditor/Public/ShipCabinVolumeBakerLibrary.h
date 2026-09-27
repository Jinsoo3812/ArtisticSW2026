#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ShipCabinVolumeBakerLibrary.generated.h"

USTRUCT(BlueprintType)
struct FSWCabinBakeResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Cabin Bake")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Cabin Bake")
	bool bLeakedToExterior = false;

	UPROPERTY(BlueprintReadOnly, Category = "Cabin Bake")
	int32 FilledVoxelCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Cabin Bake")
	int32 DebugInstanceCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Cabin Bake")
	FString Message;
};

/** Editor-only cabin volume baker. The tagged point light is a flood-fill seed, not a camera. */
UCLASS()
class CLASSFEATUREEDITOR_API UShipCabinVolumeBakerLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Builds a triangle shell from SM_Ship and SW_CabinBarrier actors, then performs a
	 * six-neighbour flood fill from SW_CabinSeed. A successful fill is visualised by
	 * a coalesced instanced-cube actor named SW_CabinVolume_Debug.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArtisticSW|Editor|Ship")
	static FSWCabinBakeResult BakeTaggedCabinDebug(
		float VoxelSize = 30.0f,
		float SurfaceThickness = 35.0f,
		float BoundsPadding = 60.0f);
};
