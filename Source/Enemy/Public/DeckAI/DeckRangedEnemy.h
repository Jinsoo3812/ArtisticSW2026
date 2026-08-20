#pragma once

#include "CoreMinimal.h"
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

	bool ActivateFromPool(AEnemyShip* InHostShip, const FTransform& SpawnTransform, int32 InitialWaypointId, int32 RandomSeed);
	void DeactivateToPool();

	UFUNCTION(BlueprintPure, Category = "Deck AI|Pool")
	bool IsPoolActive() const { return bPoolActive; }

	UFUNCTION(BlueprintPure, Category = "Deck AI|Waypoint")
	int32 GetCurrentDeckWaypointId() const { return CurrentDeckWaypointId; }

	UFUNCTION(BlueprintPure, Category = "Deck AI|Waypoint")
	int32 GetPreviousDeckWaypointId() const { return PreviousDeckWaypointId; }

	UFUNCTION(BlueprintPure, Category = "Deck AI|Waypoint")
	int32 GetGoalDeckWaypointId() const { return GoalDeckWaypointId; }

	void SetGoalDeckWaypointId(int32 NewGoalWaypointId) { GoalDeckWaypointId = NewGoalWaypointId; }
	void MarkGoalDeckWaypointReached();
	FRandomStream& GetDeckRandomStream() { return DeckRandomStream; }

	virtual AEnemyShip* GetDeckHostShip() const override;
	virtual int32 GetCurrentDeckPointId() const override { return CurrentDeckWaypointId; }
	virtual int32 GetGoalDeckPointId() const override { return GoalDeckWaypointId; }
	virtual void OnDeckPointReached() override { MarkGoalDeckWaypointReached(); }
	virtual void OnDeckMoveFailed() override { GoalDeckWaypointId = INDEX_NONE; }
	virtual bool CanMoveOnDeck() const override { return bPoolActive && GetHostShip() != nullptr; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void HandleDeath_Implementation() override;

	UFUNCTION()
	void OnRep_PoolActive();

	UFUNCTION()
	void ReturnToPoolAfterDeath();

	void ApplyPoolPresentationState();
	void RestoreForPoolActivation();

protected:
	UPROPERTY(ReplicatedUsing = OnRep_PoolActive, VisibleInstanceOnly, BlueprintReadOnly, Category = "Deck AI|Pool")
	bool bPoolActive = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Deck AI|Pool", meta = (ClampMin = "0.0", Units = "s"))
	float ReturnToPoolAfterDeathDelay = 1.5f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Deck AI|Waypoint")
	int32 CurrentDeckWaypointId = INDEX_NONE;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Deck AI|Waypoint")
	int32 PreviousDeckWaypointId = INDEX_NONE;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Deck AI|Waypoint")
	int32 GoalDeckWaypointId = INDEX_NONE;

private:
	bool bStartPooled = false;
	FRandomStream DeckRandomStream;
	ECollisionEnabled::Type InitialCapsuleCollision = ECollisionEnabled::QueryAndPhysics;
	ECollisionEnabled::Type InitialMeshCollision = ECollisionEnabled::QueryOnly;
	FTimerHandle ReturnToPoolTimerHandle;
};
