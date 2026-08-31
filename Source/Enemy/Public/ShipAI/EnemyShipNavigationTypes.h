#pragma once

#include "CoreMinimal.h"
#include "EnemyShipNavigationTypes.generated.h"

UENUM(BlueprintType)
enum class ENavalCombatState : uint8
{
	Idle,
	Approach,
	Orbit,
	Retreat,
	Return
};

UENUM(BlueprintType)
enum class EEnemyShipSkillMovementPolicy : uint8
{
	ContinueNavigation,
	LockSteering,
	OverrideNavigation,
	StopMovement
};

UENUM(BlueprintType)
enum class EEnemyShipNavigationOverrideMode : uint8
{
	ControlInput,
	StopMovement
};

USTRUCT(BlueprintType)
struct ENEMY_API FEnemyShipNavigationProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation", meta = (ClampMin = "0.0", Units = "cm"))
	float DetectionDistance = 10000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation", meta = (ClampMin = "1.0", Units = "cm"))
	float IdealDistance = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation", meta = (ClampMin = "0.0", Units = "cm"))
	float OrbitTolerance = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation", meta = (ClampMin = "0.0", Units = "cm"))
	float DangerCloseDistance = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation", meta = (ClampMin = "0.0", Units = "cm"))
	float ReturnArrivalDistance = 800.0f;

	/** Starts returning only after the ship is farther than this planar distance from its Return Point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation", meta = (ClampMin = "0.0", Units = "cm"))
	float ReturnTriggerDistance = 800.0f;

	/** Multiplies forward propulsion while the navigation state is Return. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation", meta = (ClampMin = "0.0"))
	float ReturnPropulsionMultiplier = 1.0f;

	/** Time without a detected player ship before returning home. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation", meta = (ClampMin = "0.0", Units = "s"))
	float LostTargetReturnDelay = 10.0f;

	/** Cannon cooldown multiplier at zero ship health; interpolates linearly to 1 at full health. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "1.0"))
	float ZeroHealthCannonCooldownMultiplier = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	bool bOrbitClockwise = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation", meta = (ClampMin = "0.0"))
	float ForwardInputScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation", meta = (ClampMin = "0.0"))
	float TurnInputScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation", meta = (ClampMin = "1"))
	int32 MaxActiveCannons = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Avoidance", meta = (ClampMin = "0.02", Units = "s"))
	float AvoidanceDecisionInterval = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation|Avoidance", meta = (ClampMin = "0.0", Units = "cm"))
	float AvoidanceSafetyBuffer = 800.0f;
};

USTRUCT(BlueprintType)
struct ENEMY_API FEnemyShipNavigationContext
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	FVector ShipLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	FVector ShipForward = FVector::ForwardVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	FVector ShipRight = FVector::RightVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	bool bHasTarget = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	FVector TargetLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	bool bHasHome = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	FVector HomeLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	bool bReturnRequested = false;
};

USTRUCT(BlueprintType)
struct ENEMY_API FEnemyShipNavigationOutput
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Navigation")
	ENavalCombatState State = ENavalCombatState::Idle;

	UPROPERTY(BlueprintReadOnly, Category = "Navigation")
	FVector DesiredHeading = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly, Category = "Navigation")
	float MoveInput = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Navigation")
	float TurnInput = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Navigation")
	float TargetDistance = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Navigation")
	float HomeDistance = 0.0f;
};

USTRUCT(BlueprintType)
struct ENEMY_API FEnemyShipNavigationOverrideHandle
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Navigation")
	FGuid Id;

	bool IsValid() const { return Id.IsValid(); }
	void Reset() { Id.Invalidate(); }

	friend bool operator==(const FEnemyShipNavigationOverrideHandle& A, const FEnemyShipNavigationOverrideHandle& B)
	{
		return A.Id == B.Id;
	}
};

USTRUCT(BlueprintType)
struct ENEMY_API FEnemyShipNavigationOverrideRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	EEnemyShipNavigationOverrideMode Mode = EEnemyShipNavigationOverrideMode::ControlInput;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float MoveInput = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float TurnInput = 0.0f;

	/** Multiplies the owning ship's DT/ASC-backed forward propulsion multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation", meta = (ClampMin = "0.0"))
	float PropulsionMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation", meta = (ClampMin = "0.0"))
	float TurnMultiplier = 1.0f;
};

struct ENEMY_API FEnemyShipNavigationModel
{
	static FEnemyShipNavigationOutput Evaluate(
		ENavalCombatState CurrentState,
		const FEnemyShipNavigationProfile& Profile,
		const FEnemyShipNavigationContext& Context);
};
