#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Water/SWBuoyancyTypes.h"
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

/** Player-only vertical swimming state. Ships continue to use their own physics buoyancy. */
UENUM(BlueprintType)
enum class ESwimDepthMode : uint8
{
	Surface,
	Submerged
};

/** Snapshot consumed by animation code. It is built on the game thread, then copied to the AnimInstance proxy. */
USTRUCT(BlueprintType)
struct FSwimmingAnimationState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Swimming")
	bool bIsSwimming = false;

	UPROPERTY(BlueprintReadOnly, Category = "Swimming")
	bool bIsUnderwater = false;

	UPROPERTY(BlueprintReadOnly, Category = "Swimming")
	bool bDiveInputHeld = false;

	UPROPERTY(BlueprintReadOnly, Category = "Swimming")
	bool bAscendInputHeld = false;

	UPROPERTY(BlueprintReadOnly, Category = "Swimming")
	ESwimDepthMode DepthMode = ESwimDepthMode::Surface;

	UPROPERTY(BlueprintReadOnly, Category = "Swimming")
	float HorizontalSpeed = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Swimming")
	float VerticalSpeed = 0.0f;

	/** Local-space movement direction in degrees. Forward is 0, right is 90. */
	UPROPERTY(BlueprintReadOnly, Category = "Swimming")
	float Direction = 0.0f;
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class CLASSFEATURE_API USwimmingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USwimmingComponent();

	// Process the custom swimming movement physics
	void UpdateSwimmingMovement(float DeltaTime);

	/** Sets the requested vertical swim direction: -1 dives, +1 ascends. */
	void SetVerticalSwimInput(float InVerticalInput);

	/** Current vertical swim command captured by CMC saved moves. */
	float GetVerticalSwimInput() const { return VerticalSwimInput; }

	/** Restores the movement sub-state associated with a replayed CMC saved move. */
	void RestorePredictedDepthMode(ESwimDepthMode InDepthMode) { DepthMode = InDepthMode; }

	/** True while Ctrl or Space owns movement and horizontal swim input must be ignored. */
	bool HasVerticalSwimInput() const;

	/** True only for neutral underwater movement, where W follows camera pitch in 3D. */
	bool ShouldUseCameraDirectedUnderwaterMovement() const;

	UFUNCTION(BlueprintPure, Category = "Swimming")
	bool IsCustomSwimming() const;

	/** True when the top of the capsule is below the queried water surface. */
	UFUNCTION(BlueprintPure, Category = "Swimming")
	bool IsUnderwater() const { return bIsUnderwater; }

	UFUNCTION(BlueprintPure, Category = "Swimming")
	ESwimDepthMode GetDepthMode() const { return DepthMode; }

	/** Builds the authoritative animation snapshot for the owning character. */
	UFUNCTION(BlueprintPure, Category = "Swimming")
	FSwimmingAnimationState GetAnimationState() const;

	// Check transition conditions (entry/exit)
	void CheckWaterTransitions(float DeltaSeconds);

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Overlap delegates
	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnOverlapEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);



private:
	// Helper to calculate the water height at a given location (queries overlapping water bodies)
	bool GetWaterHeightAtLocation(
		const FVector& Location,
		float& OutWaterHeight,
		bool* bOutHadValidWaterBodyQuery = nullptr,
		float WaveTimeOffsetSeconds = 0.0f) const;

	/** Deterministic pose proxy derived only from CMC horizontal velocity. */
	float GetSurfacePostureBlend() const;

	/** Pontoon placement matching the upright idle and prone moving swim poses. */
	FVector GetSurfacePontoonOffset() const;

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

	/** Time allowed for a missing WaterBody query before swimming exits. Known dry results do not use this grace. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Water Detection",
		meta = (ClampMin = "0.0", Units = "s"))
	float WaterQueryFailureGraceTime = 0.2f;

	/** Radius of the virtual pontoon sphere used for buoyancy */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Buoyancy", meta = (ClampMin = "1.0", Units = "cm"))
	float PontoonRadius = 50.0f;

	/** Per-pontoon force multiplier, shared with ship and chest pontoons. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Buoyancy", meta = (ClampMin = "0.0"))
	float PontoonForceScale = 1.0f;

	/** Shared pontoon force settings used by ships, chests, and player surface swimming. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Buoyancy", meta = (ShowOnlyInnerProperties))
	FSWBuoyancyForceSettings BuoyancyForceSettings;

	/** If true, draws the buoyancy pontoon debug sphere and submersion status */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Buoyancy")
	bool bShowDebugPontoon = false;

	/** Max speed on the XY plane when swimming */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Movement")
	float MaxSwimSpeed = 300.0f;

	/** Horizontal swimming acceleration coefficient */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Movement")
	float SwimAcceleration = 1000.0f;

	/** Friction/drag coefficient for horizontal movement in water */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Movement")
	float SwimFriction = 4.0f;

	/** Acceleration applied by dive/ascend input while in custom swimming mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Movement")
	float VerticalSwimAcceleration = 1200.0f;

	/** Absolute Z velocity limit while swimming. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Movement")
	float MaxVerticalSwimSpeed = 350.0f;

	/** Surface ceiling used by Space ascent before pontoon buoyancy resumes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Surface", meta = (ClampMin = "0.0", Units = "cm"))
	float SurfaceTargetDepth = 50.0f;

	/** Pontoon offset while the surface-swim pose is upright and idle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Surface|Posture")
	FVector SurfaceIdlePontoonOffset = FVector(0.0f, 0.0f, 65.0f);

	/** Pontoon offset while the surface-swim pose is prone and moving. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Surface|Posture")
	FVector SurfaceMovingPontoonOffset = FVector::ZeroVector;

	/** Horizontal speed at which the moving-pose pontoon offset is fully applied. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Surface|Posture",
		meta = (ClampMin = "1.0", Units = "cm/s"))
	float SurfaceMovingPoseSpeed = 200.0f;

	/** Fraction of the local wave's vertical velocity inherited by surface swimming drag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Surface",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SurfaceWaterVelocityInfluence = 0.8f;

	/** Player-only scale applied symmetrically to the shared vertical drag coefficients. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Surface",
		meta = (ClampMin = "0.0"))
	float SurfaceVerticalDragScale = 2.0f;

	/** Drag that brings the player to a stop at the current depth when submerged and no key is held. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Submerged", meta = (ClampMin = "0.0"))
	float SubmergedVerticalDamping = 6.0f;

	/** Clearance above the wave surface required before returning to Surface mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Surface", meta = (ClampMin = "0.0", Units = "cm"))
	float SurfaceReentryHeadClearance = 15.0f;

	/** Water must cover the head by this amount before the underwater animation state begins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Water Detection", meta = (ClampMin = "0.0", Units = "cm"))
	float UnderwaterEntryHeadSubmersion = 10.0f;

	/** Head clearance required before the underwater animation state ends. Kept separate from entry to prevent wave flicker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swimming|Water Detection", meta = (ClampMin = "0.0", Units = "cm"))
	float UnderwaterExitHeadClearance = 15.0f;

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

	UPROPERTY(Transient)
	float WaterQueryFailureElapsed = 0.0f;

	float VerticalSwimInput = 0.0f;

	UPROPERTY(Replicated)
	bool bDiveInputHeld = false;

	UPROPERTY(Replicated)
	bool bAscendInputHeld = false;

	UPROPERTY(Transient)
	ESwimDepthMode DepthMode = ESwimDepthMode::Surface;

	UPROPERTY(Transient)
	bool bIsUnderwater = false;

	void UpdateDepthMode();
	void UpdateUnderwaterState();
};
