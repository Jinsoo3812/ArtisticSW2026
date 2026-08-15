#pragma once

#include "CoreMinimal.h"
#include "AI/BaseAIController.h"
#include "BossAI/BossDeckPointSelector.h"
#include "ShipBossAIController.generated.h"

class AShipBossEnemy;

/** Native fallback decision loop; a configured Behavior Tree takes precedence automatically. */
UCLASS()
class ENEMY_API AShipBossAIController : public ABaseAIController
{
	GENERATED_BODY()

public:
	AShipBossAIController();

protected:
	virtual void OnPossess(APawn* PossessedPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void EvaluateBossCombat();
	bool TryActivateAbilityByTag(AShipBossEnemy& Boss, FGameplayTag AbilityTag, FGameplayTag CooldownTag);
	bool SelectAndActivateMobility(
		AShipBossEnemy& Boss,
		AActor& Target,
		EBossDestinationPurpose Purpose,
		FGameplayTag AbilityTag,
		FGameplayTag CooldownTag);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Decision", meta = (ClampMin = "0.05", Units = "s"))
	float DecisionInterval = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Decision", meta = (ClampMin = "0.0", Units = "cm"))
	float CloseCombatDistance = 280.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Decision", meta = (ClampMin = "0.0", Units = "cm"))
	float KnockbackDistance = 250.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Decision", meta = (ClampMin = "0.0", Units = "cm"))
	float FarCombatDistance = 900.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Point")
	FBossDestinationSelectionSettings PointSelectionSettings;

private:
	FTimerHandle DecisionTimerHandle;
};
