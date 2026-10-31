#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SWCameraRevealComponent.generated.h"

class UMaterialParameterCollection;
class ABasePlayer;

UCLASS(ClassGroup = Camera, meta = (BlueprintSpawnableComponent))
class CLASSFEATURE_API USWCameraRevealComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	USWCameraRevealComponent();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Reveal")
	bool bEnabled = true;
	/** Normal gameplay requires an actual camera-to-body obstruction. Disable only for visual tuning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Reveal|Occlusion")
	bool bRequireOcclusion = true;
	/** Head, chest and upper-hip samples. Two avoids triggering for a low railing alone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Reveal|Occlusion", meta = (ClampMin = "1", ClampMax = "3"))
	int32 RequiredBlockedSamples = 2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Reveal|Occlusion", meta = (ClampMin = "0", ClampMax = "100", Units = "cm"))
	float TraceEndPadding = 15.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Reveal|Occlusion", meta = (ClampMin = "0", ClampMax = "2", Units = "s"))
	float OcclusionReleaseDelay = 0.1f;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Camera|Reveal|Occlusion")
	int32 BlockedSampleCount = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Camera|Reveal|Occlusion")
	bool bViewOccluded = false;
	/** 0: original surface. 1: completely clear at the center. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Reveal", meta = (ClampMin = "0", ClampMax = "1"))
	float VisibilityStrength = 0.97f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Reveal", meta = (ClampMin = "0", ClampMax = "1000", UIMax = "10"))
	float RadiusScale = 0.65f;
	/** 0 uses projected player size; 1 is a radius equal to viewport height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Reveal", meta = (ClampMin = "0", ClampMax = "8", UIMax = "2"))
	float FixedRadiusScreenHeight = 0.f;
	/** 0 gives a hard edge, 1 feathers the entire radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Reveal", meta = (ClampMin = "0", ClampMax = "1"))
	float FeatherRatio = 0.1f;
	/** Disable to reveal every supported surface inside the circle, regardless of depth. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Reveal")
	bool bLimitToPlayerDepth = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Reveal", meta = (ClampMin = "-100000", ClampMax = "100000", UIMin = "-500", UIMax = "500", Units = "cm"))
	float DepthOffset = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Reveal", meta = (ClampMin = "0", ClampMax = "100000", UIMax = "200", Units = "cm"))
	float DepthFeather = 5.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Reveal", meta = (ClampMin = "0", ClampMax = "30", UIMax = "2", Units = "s"))
	float TransitionSeconds = 0.1f;
	UPROPERTY(EditDefaultsOnly, Category = "Camera|Reveal")
	TSoftObjectPtr<UMaterialParameterCollection> Parameters;
	void UpdateView(float DeltaSeconds);
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> LoadedParameters;
	float Activation = 0.f;
	float OcclusionHoldRemaining = 0.f;
	bool IsPlayerOccluded(const ABasePlayer* Character, const FVector& CameraLocation);
	void ClearView();
};
