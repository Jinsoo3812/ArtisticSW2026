#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SWCameraRevealAuthoringLibrary.generated.h"

class UMaterial;
class UMaterialInterface;
class UMaterialInstanceConstant;
class UMaterialParameterCollection;

UCLASS()
class CLASSFEATUREEDITOR_API USWCameraRevealAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "Camera|Reveal Authoring")
	static UMaterialParameterCollection* CreateParameters();
	UFUNCTION(BlueprintCallable, Category = "Camera|Reveal Authoring")
	static bool AddMaskedReveal(UMaterial* Material, UMaterialParameterCollection* Collection);
	UFUNCTION(BlueprintCallable, Category = "Camera|Reveal Authoring")
	static bool ConfigureInstance(UMaterialInstanceConstant* Material, float OriginalClipValue);
	UFUNCTION(BlueprintCallable, Category = "Camera|Reveal Authoring")
	static FString FinishMaterials(const TArray<UMaterialInterface*>& Materials);
};
