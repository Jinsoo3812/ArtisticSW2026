#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SWCannonOperatorAuthoringLibrary.generated.h"
class UAnimBlueprint;
class APlayerController;
class APawn;
class ACannon;
class UStaticMesh;
class USkeletalMesh;
class ACharacter;
UCLASS()
class CLASSFEATUREEDITOR_API USWCannonOperatorAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Derive visual parts from connected geometry. Source mesh/materials/UVs are preserved. */
	UFUNCTION(BlueprintCallable, Category="Cannon|Authoring")
	static UStaticMesh* BuildOperatorCannonPart(UStaticMesh* Source, const FString& PackagePath, bool bBarrel);
	/** Add initial sockets only; preserve all existing designer edits. */
	UFUNCTION(BlueprintCallable, Category="Cannon|Authoring")
	static void EnsureOperatorContactSockets(UStaticMesh* Mesh, const FTransform& LeftPalm, const FTransform& RightPalm);
	UFUNCTION(BlueprintCallable, Category="Cannon|Validation")
	static FString DescribeOperatorRig(USkeletalMesh* Mesh);
	/** Sole or palm distance from the authored physical contact, in world centimeters. */
	UFUNCTION(BlueprintCallable, Category="Cannon|Validation")
	static double GetOperatorContactError(ACharacter* Operator, int32 Index);
	UFUNCTION(BlueprintCallable, Category="Camera|Authoring")
	static bool InstallOperatorOverlay(UAnimBlueprint* Blueprint);
	/** Read-only PIE diagnostics, kept in the editor module. */
	UFUNCTION(BlueprintCallable, Category="Cannon|Validation")
	static FString DescribeOperatorController(APlayerController* Controller);
	/** Defer to a game-world tick: Python's editor execution guard forces nested RPCs local. */
	UFUNCTION(BlueprintCallable, Category="Cannon|Validation")
	static bool QueueOperatorBoard(APawn* Operator, ACannon* Cannon);
	UFUNCTION(BlueprintCallable, Category="Cannon|Validation")
	static bool QueueOperatorDestroy(ACannon* Cannon);
};
