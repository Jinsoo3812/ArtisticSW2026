#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "WaveSystem/Data/WaveSpawnTypes.h"
#include "Room/SWRoomStateAdapter.h"
#include "Room/SWVoyageResetParticipant.h"
#include "GroundEnemySpawner.generated.h"

USTRUCT()
struct FSWRoomGroundSpawnerState
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FSoftObjectPath CatalogPath;
	UPROPERTY(SaveGame) int32 EntryCount = 0;
	UPROPERTY(SaveGame) bool bConfiguredSpawnCommitted = false;
	UPROPERTY(SaveGame) TArray<FGuid> TrackedEnemyIds;
};

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
class ENEMY_API AGroundEnemySpawner : public AActor, public ISWRoomStateAdapter, public ISWVoyageResetParticipant
{
	GENERATED_BODY()

public:
	AGroundEnemySpawner();
	virtual ESWVoyagePolicy GetVoyagePolicy_Implementation() const override { return ESWVoyagePolicy::ResetParticipant; }
	virtual ESWVoyageRestoreStage GetVoyageRestoreStage_Implementation() const override { return ESWVoyageRestoreStage::InitialSpawners; }
	virtual FName GetVoyageParticipantId_Implementation() const override;
	virtual ESWVoyageStepResult PrepareVoyageReset_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual void CancelVoyagePreparation_Implementation(const FSWVoyageResetContext& Context) override;
	virtual ESWVoyageStepResult ResetVoyageTransientState_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult RestoreVoyageState_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult IsVoyageReady_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual void CaptureRoomDomains(TArray<FSWRoomDomainPart>& OutParts, TArray<FSWRoomCaptureIssue>& OutIssues) const override;
	virtual bool RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& OutError) override;
	virtual bool CompareRoomDomain(const FSWRoomDomainPart& Expected, const FSWRoomDomainPart& Actual,
		float TimeToleranceSeconds, TArray<FString>& OutFields) const override
	{ return FSWRoomStructCodec::Compare<FSWRoomGroundSpawnerState>(Expected, Actual, TimeToleranceSeconds, OutFields); }
	virtual bool FinalizeRoomRestore(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError) override;

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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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
	void ClearTrackedEnemyBindings();

	UFUNCTION()
	void HandleTrackedEnemyRemoved(ABaseEnemy* Enemy, EWaveEnemyRemoveReason Reason);

	UFUNCTION()
	void HandleTrackedEnemyDestroyed(AActor* DestroyedActor);

	TSet<TWeakObjectPtr<ABaseEnemy>> TrackedEnemies;
	bool bConfiguredSpawnCommitted = false;
	FSWRoomGroundSpawnerState PendingRoomState;
	bool bHasPendingRoomState = false;
	bool bConfiguredSpawnFailed = false;
	int32 RestoredVoyageGeneration = INDEX_NONE;
	FTimerHandle DeferredRoomSpawnTimer;
	bool bDeferredRoomSpawnPaused = false;
};
