#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CannonOperatorStationProfile.generated.h"

/** Model-space presentation anchors, independent of firing, damage and input tuning. */
UCLASS(BlueprintType)
class WATERANDSHIP_API UCannonOperatorStationProfile : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Station") FTransform Mount;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Station") FTransform Exit;
	/** Relative to Mount; Z identifies the deck rather than an ankle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Station") FVector LeftFoot = FVector(42,-28,-92);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Station") FVector RightFoot = FVector(26,28,-92);
	/** Contact-mesh-local fallback frame: X along fingers, Z out of the back of the hand. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Station") FTransform LeftPalm;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Station") FTransform RightPalm;
	/** Optional mesh sockets override the palm transforms above. Never binds to an untagged collider. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sockets") FName ContactSurfaceTag = TEXT("CannonOperatorContact");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sockets") FName LeftPalmSocket = TEXT("Operator_LeftPalm");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sockets") FName RightPalmSocket = TEXT("Operator_RightPalm");
};
