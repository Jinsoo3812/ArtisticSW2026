#pragma once

#include "CoreMinimal.h"
#include "DeckAI/DeckPointReservation.h"
#include "DeckAI/DeckWaypointMovementInterface.h"
#include "Engine/EngineTypes.h"
#include "RangedEnemy/RangedEnemy.h"
#include "DeckRangedEnemy.generated.h"

class AEnemyShip;

/** Minimal moving-deck RangedEnemy with a server-owned pooled lifetime. */
UCLASS(Blueprintable)
class ENEMY_API ADeckRangedEnemy : public ARangedEnemy, public IDeckWaypointMovementInterface
{
	GENERATED_BODY()

public:
	ADeckRangedEnemy();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Called before FinishSpawning for actors allocated into an EnemyShip pool. */
	void PrepareForPool();

	bool ActivateFromPool(AEnemyShip* InHostShip, int32 InitialWaypointId, int32 RandomSeed);
	void DeactivateToPool();

	UFUNCTION(BlueprintPure, Category = "Deck AI|Pool")
	bool IsPoolActive() const { return bPoolActive; }

	UFUNCTION(BlueprintPure, Category = "Deck AI|Pool")
	float GetReturnToPoolAfterDeathDelay() const { return ReturnToPoolAfterDeathDelay; }

	UFUNCTION(BlueprintPure, Category = "Deck AI|Waypoint")
	int32 GetCurrentDeckWaypointId() const { return CurrentDeckWaypointId; }

	UFUNCTION(BlueprintPure, Category = "Deck AI|Waypoint")
	int32 GetPreviousDeckWaypointId() const { return PreviousDeckWaypointId; }

	UFUNCTION(BlueprintPure, Category = "Deck AI|Waypoint")
	int32 GetGoalDeckWaypointId() const { return GoalDeckWaypointId; }

	bool TrySetGoalDeckWaypointId(int32 NewGoalWaypointId);
	void SetGoalDeckWaypointId(int32 NewGoalWaypointId) { TrySetGoalDeckWaypointId(NewGoalWaypointId); }
	void MarkGoalDeckWaypointReached();
	FRandomStream& GetDeckRandomStream() { return DeckRandomStream; }

	virtual AEnemyShip* GetDeckHostShip() const override;
	virtual int32 GetCurrentDeckPointId() const override { return CurrentDeckWaypointId; }
	virtual int32 GetGoalDeckPointId() const override { return GoalDeckWaypointId; }
	virtual void OnDeckPointReached() override { MarkGoalDeckWaypointReached(); }
	virtual void OnDeckMoveFailed() override;
	/** Deck ranged enemies are fixed emplacements. Only the boss uses live deck movement. */
	virtual bool CanMoveOnDeck() const override { return false; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void HandleDeath_Implementation() override;
	virtual void HandleDeathFinishedPresentation() override;

	UFUNCTION()
	void OnRep_PoolActive();

	UFUNCTION()
	void ReturnToPoolAfterDeath();

	void ApplyPoolPresentationState();
	void ApplyFixedMovementState();
	void RestoreForPoolActivation();
	bool ApplyAuthoritativeDeckAnchor(const FTransform& AuthoritativeTransform);
	void ClearAuthoritativeDeckAnchor();

protected:
	UPROPERTY(ReplicatedUsing = OnRep_PoolActive, VisibleInstanceOnly, BlueprintReadOnly, Category = "Deck AI|Pool")
	bool bPoolActive = true;

	/** Seconds a corpse remains visible after ragdoll before the server returns it to this pool. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck AI|Pool", meta = (ClampMin = "0.0", Units = "s"))
	float ReturnToPoolAfterDeathDelay = 1.5f;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Deck AI|Waypoint")
	int32 CurrentDeckWaypointId = INDEX_NONE;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Deck AI|Waypoint")
	int32 PreviousDeckWaypointId = INDEX_NONE;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Deck AI|Waypoint")
	int32 GoalDeckWaypointId = INDEX_NONE;

private:
	bool bStartPooled = false;
	FRandomStream DeckRandomStream;
	ECollisionEnabled::Type InitialCapsuleCollision = ECollisionEnabled::QueryAndPhysics;
	ECollisionEnabled::Type InitialMeshCollision = ECollisionEnabled::QueryOnly;
	FTimerHandle ReturnToPoolTimerHandle;
	FDeckPointReservation GoalPointReservation;
};
