#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SWRippleSettings.generated.h"

/**
 * Shared ripple capacity used by the authoritative FastArray, the CPU history
 * cache, and the client render texture.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Water Ripple"))
class ARTISTICSWCORE_API USWRippleSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	/**
	 * Maximum number of ripple events retained at once.
	 * This is a packaged project setting and must be identical on server and clients.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Capacity",
		meta = (ClampMin = "1", ClampMax = "256", UIMin = "1", UIMax = "256"))
	int32 MaxRippleCount = 32;

	int32 GetMaxRippleCount() const
	{
		return FMath::Clamp(MaxRippleCount, 1, 256);
	}
};
