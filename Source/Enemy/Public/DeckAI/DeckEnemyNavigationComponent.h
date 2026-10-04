#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DeckAI/DeckWalkTypes.h"
#include "DeckEnemyNavigationComponent.generated.h"

class ADeckEnemy;

/** Server goal selection/claims. Route owns locomotion; Combat owns attack and recovery state. */
UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
class ENEMY_API UDeckEnemyNavigationComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UDeckEnemyNavigationComponent();
	bool PlanTargetDistanceRoute(AActor* Target, float Distance, float ProjectionTolerance, float AngleDegrees = 0.0f);
	bool PlanRecoveryRoute(AActor* Target);
	bool PlanInvestigationRoute(const FVector& WorldPoint);
	bool ReplanIfTargetMoved(AActor* Target);
	void CancelCombatRoute();
	bool HasActiveRoute() const { return CombatGoal.NodeIndex != INDEX_NONE; }
	static FVector CalculateDistanceGoal(const FVector& PlayerFloor, const FVector& EnemyFloor,
		float Distance, float AngleDegrees = 0.0f);

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Deck AI|Navigation", meta = (ClampMin = "25.0", Units = "cm"))
	float TargetReplanDistance = 100.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Deck AI|Navigation", meta = (ClampMin = "0.05", Units = "s"))
	float MinimumReplanInterval = 0.35f;

private:
	ADeckEnemy* GetDeckEnemy() const;
	bool ClaimGoal(const FDeckWalkLocation& Goal);
	bool SelectNearGoal(const FDeckWalkLocation& Start, const FVector& Ideal, FName Surface,
		float Tolerance, bool bExcludePlayer, AActor* Player);
	FDeckWalkLocation CombatGoal;
	FDeckWalkLocation PlannedTargetFloor;
	TWeakObjectPtr<AActor> PlannedTarget;
	double NextAllowedReplanTime = 0.0;
	float PlannedDistance = 0.0f;
	float PlannedProjectionTolerance = 150.0f;
	float PlannedAngle = 0.0f;
	bool bRecoveryRoute = false;
	FVector BandCenter = FVector::ZeroVector;
	float BandDistance = 0.0f;
	bool bUseDistanceBand = false;
};
