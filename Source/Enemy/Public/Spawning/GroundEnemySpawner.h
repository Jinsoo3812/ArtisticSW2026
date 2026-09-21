#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "WaveSystem/Data/WaveSpawnTypes.h"
#include "GroundEnemySpawner.generated.h"

class ABaseEnemy;
class UEnemySpawnCatalog;
class USceneComponent;

USTRUCT(BlueprintType)
struct ENEMY_API FGroundEnemySpawnEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn", meta = (Categories = "Enemy.Type"))
	FGameplayTag EnemyTypeTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn", meta = (ClampMin = "1"))
	int32 Count = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn", meta = (ClampMin = "0.01"))
	float HealthMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn", meta = (ClampMin = "0.01"))
	float SpeedMultiplier = 1.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnGroundEnemySpawnedSignature,
	ABaseEnemy*, Enemy,
	FGameplayTag, EnemyTypeTag);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnGroundEnemyRemovedSignature,
	ABaseEnemy*, Enemy);

/**
 * Server-authoritative level/test spawner for NavMesh ground enemies.
 * It resolves classes through UEnemySpawnCatalog, assigns territory before
 * BeginPlay, and leaves all behavior decisions to the spawned enemy's AI.
 */
UCLASS(Blueprintable)
class ENEMY_API AGroundEnemySpawner : public AActor
{
	GENERATED_BODY()

public:
	AGroundEnemySpawner();

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Enemy Spawn")
	int32 SpawnConfiguredEnemies();

	UFUNCTION(BlueprintPure, Category = "Enemy Spawn")
	int32 GetTrackedEnemyCount() const;

	UFUNCTION(BlueprintPure, Category = "Enemy Spawn")
	bool ValidateConfiguration() const;

	UPROPERTY(BlueprintAssignable, Category = "Enemy Spawn|Events")
	FOnGroundEnemySpawnedSignature OnEnemySpawned;

	UPROPERTY(BlueprintAssignable, Category = "Enemy Spawn|Events")
	FOnGroundEnemyRemovedSignature OnEnemyRemoved;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy Spawn")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn|Data")
	TObjectPtr<UEnemySpawnCatalog> SpawnCatalog;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn|Data", meta = (TitleProperty = "EnemyTypeTag"))
	TArray<FGroundEnemySpawnEntry> SpawnEntries;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn")
	bool bAutoSpawnOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn|Area", meta = (ClampMin = "0.0", Units = "cm"))
	float SpawnRadius = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn|Area", meta = (ClampMin = "0.0", Units = "cm"))
	float PatrolRadius = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn|Area", meta = (ClampMin = "0.0", Units = "cm"))
	float CombatRadius = 1600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn", meta = (ClampMin = "1", ClampMax = "100"))
	int32 MaxPlacementAttemptsPerEnemy = 8;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Spawn")
	ESpawnActorCollisionHandlingMethod SpawnCollisionPolicy =
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;

private:
	bool SpawnOneEnemy(const FGroundEnemySpawnEntry& Request, ABaseEnemy*& OutEnemy);
	bool FindSpawnTransform(TSubclassOf<ABaseEnemy> EnemyClass, FTransform& OutTransform) const;

	UFUNCTION()
	void HandleTrackedEnemyRemoved(ABaseEnemy* Enemy, EWaveEnemyRemoveReason Reason);

	UFUNCTION()
	void HandleTrackedEnemyDestroyed(AActor* DestroyedActor);

	TSet<TWeakObjectPtr<ABaseEnemy>> TrackedEnemies;
};
