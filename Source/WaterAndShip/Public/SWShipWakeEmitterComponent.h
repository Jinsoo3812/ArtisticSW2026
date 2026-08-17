#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SWShipWakeTypes.h"
#include "SWShipWakeEmitterComponent.generated.h"

/** Distance/turn resampled M7 source. Each emitted tangent remains immutable. */
UCLASS(ClassGroup = (Water), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class WATERANDSHIP_API USWShipWakeEmitterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USWShipWakeEmitterComponent();
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Source", meta = (Units = "cm", MakeEditWidget = "true"))
	FVector KelvinApexLocalOffset = FVector(1500.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Source", meta = (Units = "deg"))
	float KelvinDirectionYawDegrees = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Emission", meta = (ClampMin = "1.0", Units = "cm"))
	float EmissionDistanceCm = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Emission", meta = (ClampMin = "1.0", ClampMax = "45.0", Units = "deg"))
	float MaximumTurnAngleDegrees = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Emission", meta = (ClampMin = "0.01", Units = "s"))
	float MaximumEmissionInterval = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Emission", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaximumCatchUpEvents = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Emission", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MinimumSpeedCmPerSecond = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Wave", meta = (ClampMin = "0.0", Units = "cm"))
	float MaximumAmplitudeCm = 65.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Wave", meta = (ClampMin = "1.0", Units = "cm/s"))
	float PropagationSpeedCmPerSecond = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Wave", meta = (ClampMin = "0.0"))
	float DecayRate = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Shape", meta = (ClampMin = "100.0", Units = "cm"))
	float WakeLengthCm = 16000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Shape", meta = (ClampMin = "100.0", Units = "cm"))
	float WakeHalfWidthCm = 6000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Shape", meta = (ClampMin = "10.0", Units = "cm"))
	float EnvelopeWidthCm = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Shape")
	ESWKelvinFroudeProfile FroudeProfile = ESWKelvinFroudeProfile::Fr_0_50;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Wave", meta = (ClampMin = "0.0", Units = "s"))
	float FadeInSeconds = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Wake|Network")
	bool bEnableClientPrediction = true;

private:
	void ResolveKelvinFrame(FVector2D& OutApex, FVector2D& OutForward) const;
	void EmitResampledSegment(const FVector2D& Apex, const FVector2D& Forward,
		float HorizontalSpeed, double ServerTime, bool bPredicted);

	FVector2D LastSamplePosition = FVector2D::ZeroVector;
	FVector2D LastSampleForward = FVector2D(1.0, 0.0);
	double LastSampleServerTime = 0.0;
	bool bHasSample = false;
};
