#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SpringArmComponent.h"
#include "SWCameraSpringArmComponent.generated.h"

class UPrimitiveComponent;

/** View-only obstacle selection. Never changes collision responses or actor visibility. */
UCLASS(ClassGroup = Camera, meta = (BlueprintSpawnableComponent))
class CLASSFEATURE_API USWCameraSpringArmComponent : public USpringArmComponent
{

	GENERATED_BODY()

public:
	/** Emergency rollback to the engine's unfiltered camera collision. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CameraCollision|Policy")
	bool bEnableObstaclePolicy = true;

	/** Characters, corpses and attached equipment do not shorten the boom by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CameraCollision|Policy")
	bool bIgnoreCharacters = true;

	/** Additional sweeps per update, only after the native sweep hits something.
	 * Exhaustion keeps the last obstacle blocking, rather than revealing geometry behind it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CameraCollision|Policy", meta = (ClampMin = "1", ClampMax = "32"))
	int32 MaxPolicySweeps = 8;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "CameraCollision|Diagnostics")
	TWeakObjectPtr<UPrimitiveComponent> LastBlockingComponent;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "CameraCollision|Diagnostics")
	int32 LastIgnoredComponentCount = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "CameraCollision|Diagnostics")
	bool bLastSweepBudgetExhausted = false;

	/** Component tags take precedence over actor/attachment tags. Block beats Ignore
	 * at the same level. Supported tags: SW.Camera.Block and SW.Camera.Ignore. */
	static bool ShouldIgnoreCameraComponent(const UPrimitiveComponent* Component, bool bIgnoreCharacterObstacles);

protected:
	virtual void UpdateDesiredArmLocation(bool bDoTrace, bool bDoLocationLag, bool bDoRotationLag, float DeltaTime) override;
	virtual FVector BlendLocations(const FVector& DesiredArmLocation, const FVector& TraceHitLocation,
		bool bHitSomething, float DeltaTime) override;
};
