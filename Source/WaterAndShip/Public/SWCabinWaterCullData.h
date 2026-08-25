#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SWCabinWaterCullData.generated.h"

class UVolumeTexture;

/** Static ship-local occupancy data baked by the editor cabin-volume tool. */
UCLASS(BlueprintType)
class WATERANDSHIP_API USWCabinWaterCullData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cabin Water Cull")
	TObjectPtr<UVolumeTexture> MaskTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cabin Water Cull")
	FVector LocalBoundsMin = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cabin Water Cull")
	FVector LocalBoundsMax = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cabin Water Cull")
	FIntVector Resolution = FIntVector::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cabin Water Cull")
	float VoxelSizeCm = 0.0f;
};
