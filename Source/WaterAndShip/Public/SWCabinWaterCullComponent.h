#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SWCabinWaterCullComponent.generated.h"

class UMaterialParameterCollection;

UENUM(BlueprintType)
enum class ESWCabinCullDebugView : uint8
{
	Normal UMETA(DisplayName = "0 - Normal Culling"),
	BoundsHole UMETA(DisplayName = "1 - Cull Entire Local Bounds"),
	MaskOnly UMETA(DisplayName = "2 - Show Only Occupied Voxels")
};

/**
 * Explicit opt-in water exclusion for one ship. Attach this component to the
 * ship that owns the cabin; no world search or actor tag is involved.
 */
UCLASS(ClassGroup = (Water), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class WATERANDSHIP_API USWCabinWaterCullComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWCabinWaterCullComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cabin Water Cull")
	bool bWaterCullEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cabin Water Cull|Diagnostics")
	bool bDiagnosticLogging = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cabin Water Cull|Diagnostics", meta = (ClampMin = "0.25"))
	float DiagnosticLogInterval = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cabin Water Cull|Diagnostics")
	ESWCabinCullDebugView DebugView = ESWCabinCullDebugView::Normal;

private:
	void UploadDisabled();
	void UploadTransformIfChanged();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> WaterParameterCollection;

	FTransform LastUploadedTransform;
	bool bHasUploadedTransform = false;
	bool bUploadedDisabled = false;
	float DiagnosticLogAccumulator = 0.0f;
};
