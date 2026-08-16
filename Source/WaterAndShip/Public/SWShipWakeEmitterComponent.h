#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SWShipWakeTypes.h"
#include "SWShipWakeEmitterComponent.generated.h"

/** Samples a replicated ship path and emits server-authored Kelvin wake packets. */
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Hull", meta = (ClampMin = "0.0", Units = "cm"))
	float SternOffsetCm = 900.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Emission", meta = (ClampMin = "0.01", Units = "s"))
	float MinimumEmissionInterval = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Emission", meta = (ClampMin = "10.0", Units = "cm"))
	float EmissionDistanceCm = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Emission", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MinimumSpeedCmPerSecond = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Wave", meta = (ClampMin = "0.0", Units = "cm"))
	float MaximumAmplitudeCm = 42.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Wave", meta = (ClampMin = "0.1", Units = "s"))
	float LifetimeSeconds = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Wave", meta = (ClampMin = "5.0", ClampMax = "35.0", Units = "deg"))
	float KelvinHalfAngleDegrees = 19.47f;

private:
	void EmitWakePacket(const FVector& OwnerLocation, const FVector2D& Forward, float HorizontalSpeed);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastAddWakeEvent(const FSWShipWakeEvent& Event);

	FVector2D LastEmissionPosition = FVector2D::ZeroVector;
	double LastEmissionServerTime = -DBL_MAX;
	int32 LocalSequence = 0;
	bool bHasEmissionOrigin = false;
};
