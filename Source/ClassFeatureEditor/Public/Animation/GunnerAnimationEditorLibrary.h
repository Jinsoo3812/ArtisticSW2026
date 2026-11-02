#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GunnerAnimationEditorLibrary.generated.h"

class UAnimBlueprint;
class UAnimSequence;
class UAnimMontage;

/** Explicit, editor-only authoring of the gunner's dedicated animation assets. */
UCLASS()
class CLASSFEATUREEDITOR_API UGunnerAnimationEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="Animation|Gunner")
	static bool ConfigureCombatMontage(UAnimMontage* Montage, UAnimSequence* Aim, UAnimSequence* Fire, float FireDuration = 0.f);
	UFUNCTION(BlueprintCallable, Category="Animation|Gunner")
	static bool ConnectCombatSlot(UAnimBlueprint* Blueprint);
	UFUNCTION(BlueprintCallable, Category="Animation|Gunner")
	static bool AuditCombatAssets(UAnimBlueprint* Blueprint, UAnimMontage* Montage);
};
