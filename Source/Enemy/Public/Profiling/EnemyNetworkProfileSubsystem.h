#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EnemyNetworkProfileSubsystem.generated.h"

class ABaseEnemy;

/**
 * Command-line-only deterministic enemy scenario for network and EQS captures.
 * It never exists in normal play unless -EnemyNetProfile is supplied.
 */
UCLASS()
class ENEMY_API UEnemyNetworkProfileSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	void TrySpawnScenario();
	bool FindScenarioAnchor(FVector& OutAnchor) const;
	FVector BuildSpawnPoint(int32 EnemyIndex, const FVector& Anchor) const;
	TSubclassOf<ABaseEnemy> LoadEnemyClass(const TCHAR* ObjectPath) const;

	FTimerHandle SpawnTimerHandle;
	TArray<TWeakObjectPtr<ABaseEnemy>> SpawnedEnemies;
	int32 EnemyCount = 7;
	int32 Seed = 100;
	float SpawnDelaySeconds = 3.0f;
	float AnchorWaitStartSeconds = 0.0f;
	bool bCombatMode = true;
	bool bScenarioSpawned = false;
};
