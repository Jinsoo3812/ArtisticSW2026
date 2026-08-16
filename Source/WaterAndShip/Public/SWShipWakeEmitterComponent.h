#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SWShipWakeTypes.h"
#include "SWShipWakeEmitterComponent.generated.h"

/** Publishes replicated hull state for the M3 GPU field and CPU buoyancy approximation. */
UCLASS(ClassGroup = (Water), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class WATERANDSHIP_API USWShipWakeEmitterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWShipWakeEmitterComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Hull", meta = (ClampMin = "100.0", Units = "cm"))
	float HullLengthCm = 2400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Hull", meta = (ClampMin = "50.0", Units = "cm"))
	float BeamWidthCm = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Hull", meta = (ClampMin = "1.0", Units = "cm"))
	float DraftCm = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Hull", meta = (ClampMin = "0.0", Units = "cm"))
	float SternOffsetCm = 900.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Emission", meta = (ClampMin = "0.01", Units = "s"))
	float MinimumEmissionInterval = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Emission", meta = (ClampMin = "10.0", Units = "cm"))
	float EmissionDistanceCm = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Emission", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MinimumSpeedCmPerSecond = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Wave", meta = (ClampMin = "0.0", Units = "cm"))
	float MaximumAmplitudeCm = 65.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Wave", meta = (ClampMin = "0.1", Units = "s"))
	float LifetimeSeconds = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Wave", meta = (ClampMin = "2.0"))
	float WakeLengthMultiplier = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Spectrum", meta = (ClampMin = "0.0"))
	float TransverseStrength = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Spectrum", meta = (ClampMin = "0.0"))
	float DivergentStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Spectrum", meta = (ClampMin = "0.0"))
	float SternStrength = 0.72f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Spectrum", meta = (ClampMin = "0.0", ClampMax = "6.283185", Units = "rad"))
	float SternPhaseOffsetRadians = 2.15f;

	/** Low-pass rate for wavelength/spectrum speed. Hull position still uses raw speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Spectrum", meta = (ClampMin = "0.1"))
	float SpectrumSpeedSmoothingRate = 2.5f;

private:
	void PublishWakeState(const FVector& OwnerLocation, const FVector2D& Forward, float HorizontalSpeed);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastUpdateWakeEvent(const FSWShipWakeEvent& Event);

	FVector2D LastEmissionPosition = FVector2D::ZeroVector;
	double LastEmissionServerTime = -DBL_MAX;
	float SmoothedSpectrumSpeed = 0.0f;
	bool bHasEmissionOrigin = false;
};
