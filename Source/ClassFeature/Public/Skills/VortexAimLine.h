#pragma once

#include "CoreMinimal.h"
#include "Components/SplineMeshComponent.h"
#include "GameFramework/Actor.h"
#include "VortexAimLine.generated.h"

class UMaterialInterface;
class USceneComponent;
class USplineComponent;
class USplineMeshComponent;
class UStaticMesh;

UENUM(BlueprintType)
enum class EVortexAimLineForwardAxis : uint8
{
	Auto,
	X,
	Y,
	Z
};

/**
 * Local-only projectile trajectory visualization.
 * A Blueprint child only needs an aim-line static mesh and material.
 */
UCLASS(Blueprintable)
class CLASSFEATURE_API AVortexAimLine : public AActor
{
	GENERATED_BODY()

public:
	AVortexAimLine();

	UFUNCTION(BlueprintCallable, Category = "Vortex Aim Line")
	void SetTrajectory(const TArray<FVector>& WorldPoints);

	UFUNCTION(BlueprintCallable, Category = "Vortex Aim Line")
	void ClearTrajectory();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vortex Aim Line")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Stores the path. A spline component by itself is not visible in game. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vortex Aim Line")
	TObjectPtr<USplineComponent> TrajectorySpline;

	/** Mesh deformed between every adjacent trajectory point. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vortex Aim Line")
	TObjectPtr<UStaticMesh> AimLineMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vortex Aim Line")
	TObjectPtr<UMaterialInterface> AimLineMaterial;

	/** Auto selects the longest local bounds axis of AimLineMesh. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vortex Aim Line")
	EVortexAimLineForwardAxis ForwardAxis = EVortexAimLineForwardAxis::Auto;

	/** Multiplies the mesh cross-section without affecting segment length. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vortex Aim Line", meta = (ClampMin = "0.001"))
	float WidthScale = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vortex Aim Line")
	bool bSmoothTrajectory = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vortex Aim Line", meta = (ClampMin = "1", ClampMax = "128"))
	int32 MaxSegments = 64;

private:
	USplineMeshComponent* GetOrCreateSegment(int32 SegmentIndex);
	TEnumAsByte<ESplineMeshAxis::Type> ResolveForwardAxis() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USplineMeshComponent>> SegmentMeshes;
};
