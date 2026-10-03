#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DeckAI/DeckWalkTypes.h"
#include "DeckEnemyNavigationComponent.generated.h"

class ADeckEnemy;
enum class EReleaseLineOfSightRepositionState : uint8 { None, Pending, Moving };

/** Combat selection and destination claims. DeckWalkRoute owns movement; the ship owns geometry. */
UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
class ENEMY_API UDeckEnemyNavigationComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UDeckEnemyNavigationComponent();
	bool PlanCombatRoute(AActor* TargetActor, bool bRequireLineOfSight = true);
	bool ReplanIfTargetMoved(AActor* TargetActor, bool bRequireLineOfSight = true);
	void CancelCombatRoute();
	void RequestReleaseLineOfSightReposition(AActor* TargetActor);
	bool PrepareReleaseLineOfSightReposition(AActor* TargetActor);
	bool HasReleaseLineOfSightReposition(const AActor* TargetActor = nullptr) const;
	void CompleteReleaseLineOfSightReposition();
	bool HasActiveRoute() const { return CombatGoal.NodeIndex != INDEX_NONE; }
protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Deck AI|Combat Navigation", meta = (ClampMin = "25.0", Units = "cm"))
	float TargetReplanDistance = 200.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Deck AI|Combat Navigation", meta = (ClampMin = "0.05", Units = "s"))
	float MinimumReplanInterval = 0.35f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Deck AI|Combat Navigation", meta = (ClampMin = "0.0", Units = "cm"))
	float RangeSafetyMargin = 20.0f;
private:
	ADeckEnemy* GetDeckEnemy() const;
	bool HasCandidateLineOfSight(const FDeckWalkLocation& Location, const AActor& Target) const;
	void CancelRouteState();
	FDeckWalkLocation CombatGoal;
	FDeckWalkLocation PlannedTargetFloor;
	TWeakObjectPtr<AActor> PlannedTarget;
	double NextAllowedReplanTime = 0.0;
	EReleaseLineOfSightRepositionState ReleaseLineOfSightRepositionState = EReleaseLineOfSightRepositionState::None;
	TWeakObjectPtr<AActor> ReleaseLineOfSightRepositionTarget;
};
