#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BossDeckPointSelector.generated.h"

class AEnemyShip;

UENUM(BlueprintType)
enum class EBossDestinationPurpose : uint8
{
	Vanish,
	Dash
};

/** Small, deliberately conservative rule set shared by boss mobility abilities. */
USTRUCT(BlueprintType)
struct ENEMY_API FBossDestinationSelectionSettings
{
	GENERATED_BODY()

	/** 0 accepts the complete rear half-plane; lower values make the rear cone narrower. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Point", meta = (ClampMin = "-1.0", ClampMax = "0.0"))
	float MaximumRearDot = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Point", meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumTravelDistance = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Point", meta = (ClampMin = "1.0", Units = "cm"))
	float DashHitCorridorRadius = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Point", meta = (ClampMin = "1.0", Units = "cm"))
	float MaximumDashDistance = 1200.0f;

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
	static bool DoesSegmentPassTarget(
		const FVector& SegmentStart,
		const FVector& SegmentEnd,
		const FVector& TargetLocation,
		float CorridorRadius);

private:
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
