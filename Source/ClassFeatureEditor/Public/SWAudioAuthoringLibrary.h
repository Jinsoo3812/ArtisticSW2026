#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SWAudioAuthoringLibrary.generated.h"

class USoundWave;
class USoundCue;
class UMaterial;
class UPhysicalMaterial;
class ACharacter;

/** Editor-only construction/inspection helpers used by author_world_audio.py. */
UCLASS()
class CLASSFEATUREEDITOR_API USWAudioAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "Audio|Authoring")
	static USoundCue* CreateRandomCue(const FString& PackagePath, const TArray<USoundWave*>& Waves);
	UFUNCTION(BlueprintCallable, Category = "Audio|Authoring")
	static USoundCue* CreateWaterImpactCue(const FString& PackagePath, const TArray<USoundWave*>& Attacks, USoundWave* Body, float Pitch);
	UFUNCTION(BlueprintCallable, Category = "Audio|Authoring")
	static USoundCue* CreateShipLoopCue(const FString& PackagePath, USoundWave* Wake, USoundWave* Sea);
	/** Diagnostic PIE setup; retain the JSON and restore the per-project settings afterwards. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Authoring")
	static FString ConfigureAudioPIE(int32 Clients);
	UFUNCTION(BlueprintCallable, Category = "Audio|Authoring")
	static void RestoreAudioPIE(const FString& Snapshot);
	UFUNCTION(BlueprintCallable, Category = "Audio|Authoring")
	static TArray<UWorld*> GetAudioPIEWorlds();
	UFUNCTION(BlueprintCallable, Category = "Audio|Authoring")
	static FString DescribeAudioWorld(UWorld* World);
	/** Run accepted-shot/cooldown checks on the actual game-world timer, not Slate's editor world. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Authoring")
	static bool QueueAudioCannonShot(AActor* Cannon, bool bAI);
	UFUNCTION(BlueprintCallable, Category = "Audio|Authoring")
	static bool QueueAudioProbeLeaks(AActor* Ship, int32 ActiveCount);
	/** Give the replicated fixture time to arrive before its fall crosses water. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Authoring")
	static bool QueueAudioProbeCharacter(UWorld* World, TSubclassOf<ACharacter> CharacterClass, FVector Location);
	UFUNCTION(BlueprintCallable, Category = "Audio|Authoring")
	static FVector FindAudioProbeCabinPoint(AActor* Ship);
	UFUNCTION(BlueprintCallable, Category = "Audio|Authoring")
	static FString InspectTerrain(UWorld* World);
	UFUNCTION(BlueprintCallable, Category = "Audio|Authoring")
	static TArray<FVector> BakeShorePoints(UWorld* World, float Spacing = 2000.f);
	UFUNCTION(BlueprintCallable, Category = "Audio|Authoring")
	static bool RebuildLandscapePhysics(UWorld* World);
	/** Adds collision-only weights from the existing sand paint and rendered grass mask. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Authoring")
	static bool ConfigureLandscapeSurfaces(UMaterial* Material, UPhysicalMaterial* Sand, UPhysicalMaterial* Grass, UPhysicalMaterial* Soil);
	UFUNCTION(BlueprintCallable, Category = "Audio|Authoring")
	static FString SampleTerrainSurfaces(UWorld* World, float Spacing = 6000.f);
};
