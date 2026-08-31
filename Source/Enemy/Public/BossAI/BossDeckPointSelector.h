#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BossDeckPointSelector.generated.h"

class AEnemyShip;

UENUM(BlueprintType)
enum class EBossDestinationPurpose : uint8
{
	Vanish,
	Dash,
	/** Appended to preserve the serialized values of existing Vanish and Dash BT nodes. */
	Walk
};

/** Player-facing relation is independent from how the Boss travels to the point. */
UENUM(BlueprintType)
enum class EBossDestinationRelation : uint8
{
	BehindTarget = 0 UMETA(DisplayName = "Behind Target"),
	InFrontOfTarget = 1 UMETA(DisplayName = "In Front Of Target"),
	/** No target-facing half-plane filter. Useful for path-based attacks. */
	Any = 2 UMETA(DisplayName = "Any")
};

/** Small, deliberately conservative rule set shared by boss mobility abilities. */
USTRUCT(BlueprintType)
struct ENEMY_API FBossDestinationSelectionSettings
{
	GENERATED_BODY()

	/** 0 accepts the complete rear half-plane; lower values make the rear cone narrower. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Point", meta = (ClampMin = "-1.0", ClampMax = "0.0"))
	float MaximumRearDot = 0.0f;

	/** 0 accepts the complete front half-plane; higher values make the front cone narrower. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Point", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinimumFrontDot = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Point", meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumTravelDistance = 100.0f;

	/** Dash-only lower bound. Appended so existing Vanish authoring is unchanged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Point|Dash", meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumDashTravelDistance = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Point", meta = (ClampMin = "1.0", Units = "cm"))
	float DashHitCorridorRadius = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Point", meta = (ClampMin = "1.0", Units = "cm"))
	float MaximumDashDistance = 1200.0f;

	/** Optional deterministic scoring target. Zero keeps distance neutral. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Point|Dash", meta = (ClampMin = "0.0", Units = "cm"))
	float PreferredDashTravelDistance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Point|Dash")
	bool bRequireDashPathThroughTarget = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Point", meta = (ClampMin = "0.0", Units = "cm"))
	float IdealWalkRange = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Point")
	bool bCheckDestinationOccupancy = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Point")
	bool bCheckDashObstacles = true;
};

UCLASS()
class ENEMY_API UBossDeckPointSelector : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Selects an ID, never a world-space snapshot, so the point follows its moving ship. */
	UFUNCTION(BlueprintCallable, Category = "Boss|Point", meta = (AutoCreateRefTerm = "Settings"))
	static bool SelectDestinationPoint(
		AEnemyShip* HostShip,
		AActor* BossActor,
		AActor* TargetActor,
		EBossDestinationPurpose Purpose,
		EBossDestinationRelation Relation,
		const FBossDestinationSelectionSettings& Settings,
		int32& OutPointId);

	UFUNCTION(BlueprintPure, Category = "Boss|Point")
	static bool IsPointBehindTarget(
		const FVector& TargetLocation,
		const FVector& TargetForward,
		const FVector& PointLocation,
		const FVector& DeckUp,
		float MaximumRearDot = 0.0f);

	UFUNCTION(BlueprintPure, Category = "Boss|Point")
	static bool IsPointInFrontOfTarget(
		const FVector& TargetLocation,
		const FVector& TargetForward,
		const FVector& PointLocation,
		const FVector& DeckUp,
		float MinimumFrontDot = 0.0f);

	UFUNCTION(BlueprintPure, Category = "Boss|Point")
	static bool DoesSegmentPassTarget(
		const FVector& SegmentStart,
		const FVector& SegmentEnd,
		const FVector& TargetLocation,
		float CorridorRadius);

private:
	static bool SelectWalkDestinationPoint(
		AEnemyShip& HostShip,
		AActor& BossActor,
		AActor& TargetActor,
		const FBossDestinationSelectionSettings& Settings,
		int32& OutPointId);

	static bool IsDestinationClear(
		const AEnemyShip& HostShip,
		const AActor& BossActor,
		const FVector& Destination,
		const AActor* TargetActor);

	static bool IsDashSegmentClear(
		const AEnemyShip& HostShip,
		const AActor& BossActor,
		const AActor& TargetActor,
		const FVector& Destination,
		const FBossDestinationSelectionSettings& Settings);
};
