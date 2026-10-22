#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SamuraiDashRecoveryComponent.generated.h"

class AShipBossEnemy;
class UBehaviorTree;

/** Samurai-only tactical policy. Does not select points, move, cancel abilities or recover deck support. */
UCLASS(ClassGroup = AI, meta = (BlueprintSpawnableComponent))
class ENEMY_API USamuraiDashRecoveryComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	USamuraiDashRecoveryComponent();
	bool IsEnabled() const;
	bool RefreshContext(AActor* Target);
	bool CanTryDash() const;
	bool NeedsReposition() const { return bNeedsReposition; }
	bool CanStartReposition() const;
	bool RecordPointFailure(AActor* Target, const TCHAR* Reason);
	void RecordDashSuccess();
	bool BeginReposition();
	void FinishReposition(bool bReached, bool bInterrupted);
	void ResetPolicy();
	int32 GetFailureCount() const { return DashPointFailureCount; }
	float GetSelectionRetryInterval() const { return FMath::Max(0.02f, SelectionRetryInterval); }
	float GetMaximumSelectionTime() const { return FMath::Max(0.02f, MaximumSelectionTime); }

	/** Exact Combat subtree opt-in; Rogue and other bosses remain inactive. Set explicitly for a Samurai tree copy. */
	UPROPERTY(EditDefaultsOnly, Category = "Samurai|Dash Recovery")
	TSoftObjectPtr<UBehaviorTree> SamuraiCombatTree;
	UPROPERTY(EditDefaultsOnly, Category = "Samurai|Dash Recovery", meta = (ClampMin = "1"))
	int32 MaximumPointFailures = 3;
	UPROPERTY(EditDefaultsOnly, Category = "Samurai|Dash Recovery", meta = (ClampMin = "0.02", Units = "s"))
	float SelectionRetryInterval = 0.25f;
	UPROPERTY(EditDefaultsOnly, Category = "Samurai|Dash Recovery", meta = (ClampMin = "0.02", Units = "s"))
	float MaximumSelectionTime = 0.75f;
	UPROPERTY(EditDefaultsOnly, Category = "Samurai|Dash Recovery", meta = (ClampMin = "0.1", Units = "s"))
	float FailedMoveRetryDelay = 1.f;

private:
	AShipBossEnemy* GetBoss() const;
	TWeakObjectPtr<AActor> ObservedTarget;
	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Samurai|Dash Recovery|Runtime")
	int32 DashPointFailureCount = 0;
	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Samurai|Dash Recovery|Runtime")
	bool bNeedsReposition = false;
	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Samurai|Dash Recovery|Runtime")
	bool bRepositioning = false;
	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Samurai|Dash Recovery|Runtime")
	double DashSelectionBlockedUntil = 0.0;
};
