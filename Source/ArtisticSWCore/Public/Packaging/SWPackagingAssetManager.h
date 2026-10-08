#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetManager.h"
#include "SWPackagingAssetManager.generated.h"

UCLASS()
class ARTISTICSWCORE_API USWPackagingAssetManager : public UAssetManager
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	virtual void ModifyCook(TConstArrayView<const ITargetPlatform*> TargetPlatforms,
		TArray<FName>& PackagesToCook, TArray<FName>& PackagesToNeverCook) override;
#endif
};
