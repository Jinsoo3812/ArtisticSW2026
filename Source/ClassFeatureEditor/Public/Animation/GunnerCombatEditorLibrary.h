#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GunnerCombatEditorLibrary.generated.h"
class UBehaviorTree;
class UNiagaraSystem;

UCLASS()
class CLASSFEATUREEDITOR_API UGunnerCombatEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Attach continuous focus to every task, preserving the existing graph layout and decorators. */
	UFUNCTION(BlueprintCallable, Category="Enemy|Authoring")
	static int32 AddTaskFocus(UBehaviorTree* Tree);
	UFUNCTION(BlueprintCallable, Category="Enemy|Authoring")
	static bool AuditTaskFocus(UBehaviorTree* Tree);
	UFUNCTION(BlueprintCallable, Category="Enemy|Authoring")
	static bool SimplifyMuzzleFlash(UNiagaraSystem* System);
	UFUNCTION(BlueprintCallable, Category="Enemy|Authoring")
	static bool ConfigureGunnerSummonTree(UBehaviorTree* Tree, float SummonInterval = 20.f);
	UFUNCTION(BlueprintCallable, Category="Enemy|Authoring")
	static bool AuditGunnerSummonTree(UBehaviorTree* Tree, float SummonInterval = 20.f);
};
