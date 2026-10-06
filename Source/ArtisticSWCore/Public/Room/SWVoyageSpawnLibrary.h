#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Room/SWVoyageResetTypes.h"
#include "Room/SWVoyageSpawnLibrary.inl"
#include "SWVoyageSpawnLibrary.generated.h"

class AActor;
class APawn;
class UWorld;

UCLASS()
class ARTISTICSWCORE_API USWVoyageSpawnLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="Voyage", meta=(WorldContext="WorldContextObject", DeterminesOutputType="Class"))
	static AActor* BeginVoyageActorSpawn(UObject* WorldContextObject, TSubclassOf<AActor> Class, FTransform Transform, AActor* Owner, APawn* Instigator,
		ESpawnActorCollisionHandlingMethod Collision, ESWVoyageActorLifetime Lifetime, int32 ExpectedGeneration);
	UFUNCTION(BlueprintCallable, Category="Voyage") static AActor* FinishVoyageActorSpawn(AActor* Actor, FTransform Transform);
	UFUNCTION(BlueprintCallable, Category="Voyage", meta=(WorldContext="WorldContextObject", DeterminesOutputType="Class"))
	static AActor* BeginVoyageLocalPresentationSpawn(UObject* WorldContextObject, TSubclassOf<AActor> Class, FTransform Transform,
		AActor* Owner, APawn* Instigator, ESpawnActorCollisionHandlingMethod Collision, int32 ExpectedGeneration, FGuid PresentationCleanupId);
	UFUNCTION(BlueprintPure, Category="Voyage") static int32 GetActorVoyageGeneration(AActor* Actor);
	UFUNCTION(BlueprintPure, Category="Voyage") static bool IsActorFromCurrentVoyage(AActor* Actor);
	UFUNCTION(BlueprintPure, Category="Voyage", meta=(WorldContext="WorldContextObject")) static bool IsVoyageGameplayBlocked(UObject* WorldContextObject);
	static bool IsActorVoyageGameplayBlocked(AActor* Actor);
};

