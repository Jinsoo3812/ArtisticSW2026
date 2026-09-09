#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArrowImpactVisual.generated.h"

class AArrowProjectile;
class USceneComponent;
class UStaticMeshComponent;
struct FArrowImpactPresentationData;

/**
 * Client-local presentation left behind after an authoritative arrow impact.
 * It has no collision, tick, gameplay state, or replication cost.
 */
UCLASS(NotBlueprintable, NotPlaceable, Transient)
class ARTISTICSWCORE_API AArrowImpactVisual final : public AActor
{
	GENERATED_BODY()

public:
	AArrowImpactVisual();

	void InitializeFromProjectile(
		const AArrowProjectile& SourceProjectile,
		const FArrowImpactPresentationData& ImpactData,
		float EmbedDepth,
		float VisualLifeSpan);

private:
	UPROPERTY(VisibleAnywhere, Category = "Arrow|Impact")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Arrow|Impact")
	TObjectPtr<UStaticMeshComponent> ArrowMesh;
};
