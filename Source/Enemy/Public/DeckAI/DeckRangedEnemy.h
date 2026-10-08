#pragma once

#include "CoreMinimal.h"
#include "DeckAI/DeckWaypointMovementInterface.h"
#include "Engine/EngineTypes.h"
#include "RangedEnemy/RangedEnemy.h"
#include "ShipAI/EnemyShipRuntimeState.h"
#include "DeckRangedEnemy.generated.h"

class AEnemyShip;
class UDeckEnemyNavigationComponent;
class UDeckWalkRouteComponent;
class UDeckEnemyCombatComponent;
class UDeckCombatTargetResolverComponent;

USTRUCT()
struct FDeckEnemyPoolNetState
{
	GENERATED_BODY()
	UPROPERTY() bool bActive = true;
	UPROPERTY() bool bDead = false;
	UPROPERTY() TObjectPtr<AEnemyShip> Host;
	UPROPERTY() int32 PointId = INDEX_NONE;
	UPROPERTY() uint32 ActivationGeneration = 0;
	UPROPERTY() uint32 Revision = 0;
};

USTRUCT()
struct FSWRoomDeckEnemyPoolState
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) bool bActive = false;
	UPROPERTY(SaveGame) FGuid HostId;
	UPROPERTY(SaveGame) int32 PointId = INDEX_NONE;
	UPROPERTY(SaveGame) uint32 ActivationGeneration = 0;
	UPROPERTY(SaveGame) float ReturnToPoolRemaining = 0.f;
};

UENUM(BlueprintType)
enum class EDeckEnemyCombatRole : uint8
{
	Melee,
	Ranged
};

/** Common moving-deck enemy used by melee and ranged Blueprint variants. */
UCLASS(Blueprintable)
class ENEMY_API ADeckEnemy : public ARangedEnemy, public IDeckWaypointMovementInterface
{
	GENERATED_BODY()

public:
	ADeckEnemy();
	virtual void CaptureRoomDomains(TArray<FSWRoomDomainPart>& OutParts, TArray<FSWRoomCaptureIssue>& OutIssues) const override;
	virtual bool RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& OutError) override;
	virtual bool FinalizeRoomRestore(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError) override;
	virtual bool AllowsMigratedRoomDomain(const FSWRoomDomainPart& Added, const TArray<FSWRoomDomainPart>& Previous) const override;
	virtual bool CompareRoomDomain(const FSWRoomDomainPart& Expected, const FSWRoomDomainPart& Actual,
		float TimeToleranceSeconds, TArray<FString>& OutFields) const override;
	virtual void PostNetReceive() override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void PrepareForPool();
	bool ActivateFromPool(AEnemyShip* InHostShip, int32 InitialWaypointId, int32 RandomSeed,
		const FTransform* ReservedTransform = nullptr);
	void DeactivateToPool();
	void ResetToFreshPoolState();
	bool PreparePoolActivation(AEnemyShip* Host, int32 PointId, int32 RandomSeed, const FTransform* Transform);
	bool CommitPoolActivation();
	void RestoreLegacyPoolActivity(bool bActive);
	void SetHostShip(AShip* NewHostShip);

	UFUNCTION(BlueprintPure, Category = "Deck AI|Pool")
	bool IsPoolActive() const { return bPoolActive; }
	bool IsPoolDeathHandled() const { return bDeathHandled; }

	UFUNCTION(BlueprintPure, Category = "Deck AI|Pool")
	float GetReturnToPoolAfterDeathDelay() const { return ReturnToPoolAfterDeathDelay; }

	UFUNCTION(BlueprintPure, Category = "Deck AI|Combat")
	EDeckEnemyCombatRole GetDeckCombatRole() const { return DeckCombatRole; }

	UFUNCTION(BlueprintPure, Category = "Deck AI|Combat Navigation")
	UDeckEnemyNavigationComponent* GetDeckEnemyNavigationComponent() const
	{
		return DeckEnemyNavigationComponent;
	}
	UDeckWalkRouteComponent* GetDeckWalkRouteComponent() const { return DeckWalkRouteComponent; }
	UDeckEnemyCombatComponent* GetDeckCombatComponent() const { return DeckCombatComponent; }

	UFUNCTION(BlueprintPure, Category = "Deck AI|Spawn")
	int32 GetInitialSpawnPointId() const { return InitialSpawnPointId; }
	void BeginFreeDeckMovement();
	FRandomStream& GetDeckRandomStream() { return DeckRandomStream; }

	virtual AEnemyShip* GetDeckHostShip() const override;
	virtual void OnDeckMoveReached() override;
	virtual void OnDeckMoveFailed() override;
	virtual bool CanMoveOnDeck() const override;

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void HandleDeath_Implementation() override;
	virtual void HandleDeathFinishedPresentation() override;
	virtual bool EvaluateAttackTarget(const AActor* Candidate, bool bRequireLineOfSight, FString& OutReason) const override;

	UFUNCTION()
	void OnRep_PoolActive();
	UFUNCTION() void OnRep_PoolNetState();
	virtual void HandleReplicatedHostShipChanged() override;
	void RefreshPoolNetState(bool bNewActivation = false);
	void ReconcileClientPoolState();
	void HandleRoomRestoreCompleted();
	void ResumePoolAI();
	void ResetInactivePresentation(bool bRestoreWeapon = true);
	void BindRuntimeHost();
	void HandleHostRuntimeStateChanged(const FEnemyShipRuntimeState& Previous, const FEnemyShipRuntimeState& Current);

	UFUNCTION()
	void ReturnToPoolAfterDeath();

	void ApplyPoolPresentationState();
	void StopDeckMovement();
	void RestoreDeckMovementState();
	void RestoreForPoolActivation();
	bool ApplyAuthoritativeDeckStart(const FTransform& AuthoritativeTransform);
	void ClearAuthoritativeDeckBase();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Deck AI|Combat")
	EDeckEnemyCombatRole DeckCombatRole = EDeckEnemyCombatRole::Ranged;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Deck AI|Pool")
	bool bPoolActive = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck AI|Pool", meta = (ClampMin = "0.0", Units = "s"))
	float ReturnToPoolAfterDeathDelay = 1.5f;

	/** Spawn provenance only. Free movement never changes this ID. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Deck AI|Spawn")
	int32 InitialSpawnPointId = INDEX_NONE;

	/** Server-only route and final combat-point claim; route details are intentionally not replicated. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deck AI|Combat Navigation")
	TObjectPtr<UDeckEnemyNavigationComponent> DeckEnemyNavigationComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deck AI|Walk Area")
	TObjectPtr<UDeckWalkRouteComponent> DeckWalkRouteComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deck AI|Combat")
	TObjectPtr<UDeckEnemyCombatComponent> DeckCombatComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deck AI|Tracking")
	TObjectPtr<UDeckCombatTargetResolverComponent> DeckTargetResolver;

private:
	bool bStartPooled = false;
	FRandomStream DeckRandomStream;
	ECollisionEnabled::Type InitialCapsuleCollision = ECollisionEnabled::QueryAndPhysics;
	ECollisionEnabled::Type InitialMeshCollision = ECollisionEnabled::QueryOnly;
	FTimerHandle ReturnToPoolTimerHandle;
	UPROPERTY(ReplicatedUsing = OnRep_PoolNetState)
	FDeckEnemyPoolNetState PoolNetState;
	bool bPoolActivationPrepared = false;
	bool bRestoredPoolState = false;
	bool bAwaitingSnapshotCompletion = false;
	FTimerHandle PoolRestoreResumeTimerHandle;
	FSWRoomDeckEnemyPoolState PendingPoolRestore;
	FDelegateHandle PoolRestoreCompletedHandle;
	FDelegateHandle RuntimeHostHandle;
	TWeakObjectPtr<AEnemyShip> BoundRuntimeHost;
	FTimerHandle PoolPresentationRetryHandle;
	int32 PoolPresentationRetries = 0;
	uint32 LastPresentedActivationGeneration = 0;
};

/** Asset-compatible wrapper for existing BP_DeckRangedEnemy assets. */
UCLASS(Blueprintable)
class ENEMY_API ADeckRangedEnemy : public ADeckEnemy
{
	GENERATED_BODY()
};
