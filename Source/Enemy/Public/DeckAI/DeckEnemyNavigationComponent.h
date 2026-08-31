#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DeckAI/DeckNavigationTypes.h"
#include "DeckEnemyNavigationComponent.generated.h"

class ADeckEnemy;

/**
 * Per-enemy combat route state. It translates weapon/target constraints into graph
 * goals, owns the final combat-point claim, and asks the enemy to reserve one hop.
 */
UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
class ENEMY_API UDeckEnemyNavigationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDeckEnemyNavigationComponent();

	bool PlanCombatRoute(AActor* TargetActor, bool bRequireLineOfSight = true);
	bool PrepareNextHop();
	bool HandlePointReached();
	bool ReplanIfTargetMoved(AActor* TargetActor, bool bRequireLineOfSight = true);
	void CancelCombatRoute();

	bool HasActiveRoute() const { return Route.IsValid(); }
	bool IsAtCombatGoal() const;
	int32 GetCombatGoalPointId() const { return ClaimedCombatPointId; }
	const FDeckNavigationPath& GetRoute() const { return Route; }

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Deck AI|Combat Navigation",
		meta = (ClampMin = "25.0", Units = "cm"))
	float TargetReplanDistance = 200.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Deck AI|Combat Navigation",
		meta = (ClampMin = "0.05", Units = "s"))
	float MinimumReplanInterval = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Deck AI|Combat Navigation",
		meta = (ClampMin = "0.0", Units = "cm"))
	float RangeSafetyMargin = 20.0f;

private:
	ADeckEnemy* GetDeckEnemy() const;
	bool HasCandidateLineOfSight(int32 PointId, const AActor& TargetActor) const;
	bool BuildCombatGoals(
		AActor& TargetActor,
		bool bRequireLineOfSight,
		TMap<int32, float>& OutGoalSecondaryCosts,
		FVector& OutTargetLocalLocation) const;
	void ReleaseCombatClaim();

	FDeckNavigationPath Route;
	int32 RouteCursor = 0;
	int32 ClaimedCombatPointId = INDEX_NONE;
	int32 PlannedGraphRevision = INDEX_NONE;
	FVector PlannedTargetLocalLocation = FVector::ZeroVector;
	TWeakObjectPtr<AActor> PlannedTarget;
	double NextAllowedReplanTime = 0.0;
};
