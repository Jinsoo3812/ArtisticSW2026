#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SwimmingComponent.generated.h"

class ACharacter;
class UCharacterMovementComponent;
class UCapsuleComponent;
class UWaterBodyComponent;

UENUM(BlueprintType)
enum class ECustomMovementMode : uint8
{
	CMOVE_None = 0,
	CMOVE_Swimming = 1
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class CLASSFEATURE_API USwimmingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USwimmingComponent();

	// Process the custom swimming movement physics
	void UpdateSwimmingMovement(float DeltaTime);

	// Check transition conditions (entry/exit)
	void CheckWaterTransitions();

protected:
	virtual void BeginPlay() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Overlap delegates
	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnOverlapEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);



private:
	// Helper to calculate the water height at a given location (queries overlapping water bodies)
	bool GetWaterHeightAtLocation(const FVector& Location, float& OutWaterHeight) const;

	// Initialize overlapping water bodies on startup
	void InitializeOverlaps();

protected:
	/** Distance above the feet the water surface must reach to trigger swimming */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Water Detection")
	float SwimEntryOffset = 20.0f;

	/** Minimum downward velocity (cm/s) to trigger a water entry ripple */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Water Detection")
	float MinEntryVelocityThreshold = 100.0f;

	/** Distance below the feet the water surface must reach to exit swimming */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Water Detection")
	float SwimExitOffset = 10.0f;

	/** Radius of the virtual pontoon sphere used for buoyancy */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Buoyancy")
	float PontoonRadius = 50.0f;

	/** Offset of the pontoon from the actor location */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Buoyancy")
	FVector PontoonOffset = FVector(0.0f, 0.0f, -50.0f);

	/** If true, draws the buoyancy pontoon debug sphere and submersion status */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Buoyancy")
	bool bShowDebugPontoon = false;

	/** Buoyancy coefficient (multiplier for buoyant force) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Buoyancy")
	float BuoyancyCoefficient = 0.2f;

	/** Linear damping coefficient for vertical water drag (1st order) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Buoyancy")
	float BuoyancyDamp = 1000.0f;

	/** Quadratic damping coefficient for vertical water drag (2nd order) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Buoyancy")
	float BuoyancyDamp2 = 1.0f;

	/** Max buoyant force applied in the upward direction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Buoyancy")
	float MaxBuoyantForce = 5000000.0f;

	/** Max speed on the XY plane when swimming */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Movement")
	float MaxSwimSpeed = 300.0f;

	/** Horizontal swimming acceleration coefficient */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Movement")
	float SwimAcceleration = 1000.0f;

	/** Friction/drag coefficient for horizontal movement in water */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Movement")
	float SwimFriction = 4.0f;

private:
	UPROPERTY(Transient)
	TObjectPtr<ACharacter> OwnerCharacter;

	UPROPERTY(Transient)
	TObjectPtr<UCharacterMovementComponent> CharacterMovement;

	UPROPERTY(Transient)
	TObjectPtr<UCapsuleComponent> CapsuleComponent;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UWaterBodyComponent>> OverlappingWaterBodies;

	UPROPERTY(Transient)
	TWeakObjectPtr<UWaterBodyComponent> LastActiveWaterBody;

	UPROPERTY(Transient)
	float LastLoggedTime = -1.0f;
};
