#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SWShipCameraAuthoringLibrary.generated.h"

class UStaticMesh;

/** Editor-only, audited geometry extraction; never modifies the gameplay source. */
UCLASS()
class CLASSFEATUREEDITOR_API USWShipCameraAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "Camera|Authoring")
	static UStaticMesh* BuildCameraProxy(UStaticMesh* Source, const FString& PackagePath,
		const TArray<FVector>& BoundsMin, const TArray<FVector>& BoundsMax, int32 ExpectedRemovedTriangles);
};
