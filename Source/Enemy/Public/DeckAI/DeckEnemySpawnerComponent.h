#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DeckAI/DeckPointReservation.h"
#include "DeckEnemySpawnerComponent.generated.h"

class ADeckEnemy;
class AEnemyShip;
class AShip;
class UDeckWaypointComponent;

USTRUCT(BlueprintType)
struct ENEMY_API FDeckEnemySpawnEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck Enemy Spawn")
	TSubclassOf<ADeckEnemy> EnemyClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck Enemy Spawn", meta = (ClampMin = "0", ClampMax = "32"))
	int32 Count = 0;
};

UENUM(BlueprintType)
enum class EDeckEnemyDeploymentState : uint8
{
	Idle,
	Preparing,
	Deploying,
	Completed,
	CompletedWithFailures,
	Failed
};

/**
 * Server-authoritative owner of an EnemyShip's deck spawn lifecycle and point claims.
 *
 * Waypoint components and reservations are not replicated. The server commits a claim
 * before exposing replicated enemy point IDs, so clients only receive actor movement,
 * movement-base state, pool activity, and the committed IDs carried by each enemy.
 */
UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
class ENEMY_API UDeckEnemySpawnerComponent : public UActorComponent
{
	GENERATED_BODY()

#if WITH_DEV_AUTOMATION_TESTS
	friend class FDeckEnemySpawnerCompositionTest;
#endif

public:
	UDeckEnemySpawnerComponent();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Compatibility bridge for EnemyShip Blueprint defaults authored before this component existed. */
	void ConfigureLegacyFallback(
		bool bInEnabled,
		TSubclassOf<ADeckEnemy> InEnemyClass,
		int32 InPoolSize,
		float InSightDelay,
		float InActivationInterval,
		int32 InMaxRetries,
		float InRetryInterval,
		int32 InRandomSeed);

	void InitializeWaypoints();
	void InitializePool();
	void CancelDeployment();
	void ResetForNewEncounter();
	void Shutdown();

	bool RequestDeployment(AShip* TriggeringPlayerShip, AActor* InitialCombatTarget = nullptr);

	UFUNCTION(BlueprintPure, Category = "Deck Enemy Spawner")
	EDeckEnemyDeploymentState GetDeploymentState() const { return DeploymentState; }

	UFUNCTION(BlueprintPure, Category = "Deck Enemy Spawner")
	int32 GetPooledEnemyCount() const { return EnemyPool.Num(); }

	/** Number of pooled enemies that have not died, including enemies awaiting deployment. */
	UFUNCTION(BlueprintPure, Category = "Deck Enemy Spawner")
	int32 GetLivingPooledEnemyCount() const;

	/** Number of deployed enemies that have not reported death yet. Authority-owned state. */
	UFUNCTION(BlueprintPure, Category = "Deck Enemy Spawner")
	int32 GetAliveDeployedEnemyCount() const;

	/** Becomes true once a non-empty deployment has completed and every deployed enemy died. */
	UFUNCTION(BlueprintPure, Category = "Deck Enemy Spawner")
	bool AreAllDeployedEnemiesDefeated() const { return bAllDeployedEnemiesDefeated; }

	/** Called by an owned pooled enemy at authoritative death start. */
	void NotifyEnemyDefeated(ADeckEnemy* Enemy);

	UDeckWaypointComponent* GetWaypoint(int32 WaypointId) const;
	FVector GetWaypointWorldLocation(int32 WaypointId) const;
	void GetWaypointIds(TArray<int32>& OutWaypointIds, bool bRequireCombatPoint = false) const;
	void GetConnectedWaypointIds(int32 WaypointId, TArray<int32>& OutWaypointIds) const;
	int32 FindNearestWaypoint(const FVector& WorldLocation, bool bRequirePatrolPoint = true) const;

	bool ResolveFixedDeckAnchorTransform(
		int32 WaypointId,
		float CapsuleHalfHeight,
		FTransform& OutTransform) const;
	bool ResolveDeckCharacterTransform(
		int32 WaypointId,
		float CapsuleHalfHeight,
		FTransform& OutTransform) const;

	bool IsPointAvailable(int32 WaypointId, const AActor* Requester = nullptr) const;
	bool TryReservePoint(int32 WaypointId, AActor* Requester, FDeckPointReservation& OutReservation);
	bool TryReserveEnemySpawnPoint(
		const FDeckEnemySpawnRequest& Request,
		FDeckPointReservation& OutReservation);
	bool CommitPointReservation(const FDeckPointReservation& Reservation, AActor* Occupant);
	void ReleasePointReservation(FDeckPointReservation& Reservation);
	bool TryOccupyPoint(int32 WaypointId, AActor* Occupant);
	void ReleasePointOccupancy(int32 WaypointId, AActor* Occupant);
	bool IsCombatPointClaimAvailable(int32 WaypointId, const AActor* Requester = nullptr) const;
	bool TryClaimCombatPoint(int32 WaypointId, AActor* Requester);
	void ReleaseCombatPointClaim(int32 WaypointId, AActor* Requester);
	void ReleaseAllPointsFor(AActor* Actor);

	bool ActivateEnemyAtPoint(
		int32 SpawnPointId,
		AActor* InitialTarget,
		ADeckEnemy*& OutEnemy);
	bool ActivateEnemyAtReservation(
		FDeckPointReservation& Reservation,
		AActor* InitialTarget,
		ADeckEnemy*& OutEnemy);

protected:
	/** New source of truth. Entries are deployed in array order. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Deck Enemy Spawner")
	bool bEnableSpawning = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Deck Enemy Spawner", meta = (TitleProperty = "EnemyClass"))
	TArray<FDeckEnemySpawnEntry> SpawnComposition;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Deck Enemy Spawner|Timing", meta = (ClampMin = "0.0", Units = "s"))
	float SightActivationDelay = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Deck Enemy Spawner|Timing", meta = (ClampMin = "0.05", Units = "s"))
	float ActivationInterval = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Deck Enemy Spawner|Retry", meta = (ClampMin = "0", ClampMax = "5"))
	int32 MaxSpawnRetries = 3;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Deck Enemy Spawner|Retry", meta = (ClampMin = "0.05", Units = "s"))
	float SpawnRetryInterval = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Deck Enemy Spawner")
	int32 RandomSeed = 1337;

private:
	struct FDeckPointRuntimeState
	{
		TWeakObjectPtr<AActor> Occupant;
		TWeakObjectPtr<AActor> ReservedBy;
		TWeakObjectPtr<AActor> CombatClaimedBy;
		uint32 ReservationSerial = 0;
	};

	AEnemyShip* GetHostShip() const;
	bool IsEnabled() const;
	void BuildEffectiveComposition(TArray<FDeckEnemySpawnEntry>& OutComposition) const;
	ADeckEnemy* FindInactiveEnemy(TSubclassOf<ADeckEnemy> RequiredClass) const;
	bool ActivateSpecificEnemyAtReservation(
		ADeckEnemy& Enemy,
		FDeckPointReservation& Reservation,
		AActor* InitialTarget,
		ADeckEnemy*& OutEnemy);
	bool ResolveEnemySpawnTransform(
		const UDeckWaypointComponent* SpawnWaypoint,
		const ADeckEnemy& Enemy,
		FTransform& OutTransform) const;
	void PrunePointRuntimeState();
	void BeginDeployment();
	void DeployNextEnemy();
	void HandleDeploymentFailure();
	void FinishDeployment();
	void EvaluateAllEnemiesDefeated();

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UDeckWaypointComponent>> WaypointsById;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDeckWaypointComponent>> SpawnWaypoints;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ADeckEnemy>> EnemyPool;

	TMap<int32, FDeckPointRuntimeState> PointRuntimeStates;
	TArray<TSubclassOf<ADeckEnemy>> DeploymentQueue;
	TSet<TWeakObjectPtr<ADeckEnemy>> AliveDeployedEnemies;
	TWeakObjectPtr<AShip> DeploymentTriggerShip;
	TWeakObjectPtr<AActor> DeploymentInitialTarget;
	uint32 NextReservationSerial = 1;
	int32 ActivationSerial = 0;
	int32 DeploymentQueueIndex = 0;
	int32 CurrentRetryCount = 0;
	int32 DeploymentFailureCount = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Deck Enemy Spawner", meta = (AllowPrivateAccess = "true"))
	EDeckEnemyDeploymentState DeploymentState = EDeckEnemyDeploymentState::Idle;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Deck Enemy Spawner", meta = (AllowPrivateAccess = "true"))
	bool bAllDeployedEnemiesDefeated = false;

	bool bHasDeployedEnemy = false;

	UPROPERTY(Transient)
	bool bLegacyEnabled = false;

	UPROPERTY(Transient)
	TSubclassOf<ADeckEnemy> LegacyEnemyClass;

	int32 LegacyPoolSize = 0;
	float LegacySightDelay = 0.25f;
	float LegacyActivationInterval = 0.35f;
	int32 LegacyMaxRetries = 3;
	float LegacyRetryInterval = 0.5f;
	int32 LegacyRandomSeed = 1337;

	FTimerHandle SightDelayTimerHandle;
	FTimerHandle DeploymentTimerHandle;
};
