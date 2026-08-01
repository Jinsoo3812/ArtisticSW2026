#pragma once

#include "CoreMinimal.h"
#include "AI/BaseAIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "RangedEnemyAIController.generated.h"

/** Server-authoritative sight and fire loop for the stationary RangedEnemy MVP. */
UCLASS(Blueprintable)
class ENEMY_API ARangedEnemyAIController : public ABaseAIController
{
	GENERATED_BODY()

public:
	ARangedEnemyAIController();

	UFUNCTION(BlueprintPure, Category = "Ranged Enemy|AI")
	bool IsValidRangedTarget(const AActor* Candidate) const;

protected:
	virtual void OnPossess(APawn* PossessedPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void OnRangedTargetPerceptionUpdated(AActor* SeenTarget, FAIStimulus Stimulus);

	void UpdateRangedCombat();
	AActor* SelectBestPerceivedTarget();
	void ConfigureRangedSight();

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged Enemy|Perception", meta = (ClampMin = "0.0"))
	float RangedSightRadius = 3000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged Enemy|Perception", meta = (ClampMin = "0.0"))
	float RangedLoseSightRadius = 3500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged Enemy|Perception", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float RangedPeripheralVisionDegrees = 80.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged Enemy|Perception", meta = (ClampMin = "0.0"))
	float StimulusMaxAgeSeconds = 2.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged Enemy|AI", meta = (ClampMin = "0.05"))
	float CombatUpdateInterval = 0.1f;

	FTimerHandle CombatUpdateTimerHandle;
};
