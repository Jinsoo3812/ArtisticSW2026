#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "WeaponFeedbackLibrary.generated.h"

class USkeletalMeshComponent;
class UWeaponFeedbackComponent;

UCLASS()
class ARTISTICSWCORE_API UWeaponFeedbackLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Resolves a feedback component on the mesh owner or its visible attached weapon actor. */
	UFUNCTION(BlueprintPure, Category = "Weapon Feedback")
	static UWeaponFeedbackComponent* FindWeaponFeedbackComponent(USkeletalMeshComponent* MeshComponent);
};
