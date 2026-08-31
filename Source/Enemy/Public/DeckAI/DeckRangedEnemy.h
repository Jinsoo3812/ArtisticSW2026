#pragma once

#include "CoreMinimal.h"
#include "DeckAI/DeckPointReservation.h"
#include "DeckAI/DeckWaypointMovementInterface.h"
#include "Engine/EngineTypes.h"
#include "RangedEnemy/RangedEnemy.h"
#include "DeckRangedEnemy.generated.h"

class AEnemyShip;
class UDeckEnemyNavigationComponent;

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

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void PrepareForPool();
	bool ActivateFromPool(AEnemyShip* InHostShip, int32 InitialWaypointId, int32 RandomSeed);
	void DeactivateToPool();

	UFUNCTION(BlueprintPure, Category = "Deck AI|Pool")
	bool IsPoolActive() const { return bPoolActive; }

	UFUNCTION(BlueprintPure, Category = "Deck AI|Pool")
	float GetReturnToPoolAfterDeathDelay() const { return ReturnToPoolAfterDeathDelay; }

	UFUNCTION(BlueprintPure, Category = "Deck AI|Combat")
	EDeckEnemyCombatRole GetDeckCombatRole() const { return DeckCombatRole; }

	float GetPreferredDeckCombatRange() const;

	UFUNCTION(BlueprintPure, Category = "Deck AI|Combat Navigation")
	UDeckEnemyNavigationComponent* GetDeckEnemyNavigationComponent() const
	{
		return DeckEnemyNavigationComponent;
	}

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
	virtual bool CanMoveOnDeck() const override;

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
	void StopDeckMovement();
	void RestoreDeckMovementState();
	void RestoreForPoolActivation();
	bool ApplyAuthoritativeDeckStart(const FTransform& AuthoritativeTransform);
	void ClearAuthoritativeDeckBase();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Deck AI|Combat")
	EDeckEnemyCombatRole DeckCombatRole = EDeckEnemyCombatRole::Ranged;

	UPROPERTY(ReplicatedUsing = OnRep_PoolActive, VisibleInstanceOnly, BlueprintReadOnly, Category = "Deck AI|Pool")
	bool bPoolActive = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck AI|Pool", meta = (ClampMin = "0.0", Units = "s"))
	float ReturnToPoolAfterDeathDelay = 1.5f;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Deck AI|Waypoint")
	int32 CurrentDeckWaypointId = INDEX_NONE;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Deck AI|Waypoint")
	int32 PreviousDeckWaypointId = INDEX_NONE;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Deck AI|Waypoint")
	int32 GoalDeckWaypointId = INDEX_NONE;

	/** Server-only route and final combat-point claim; route details are intentionally not replicated. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Deck AI|Combat Navigation")
	TObjectPtr<UDeckEnemyNavigationComponent> DeckEnemyNavigationComponent;

private:
	bool bStartPooled = false;
	FRandomStream DeckRandomStream;
	ECollisionEnabled::Type InitialCapsuleCollision = ECollisionEnabled::QueryAndPhysics;
	ECollisionEnabled::Type InitialMeshCollision = ECollisionEnabled::QueryOnly;
	FTimerHandle ReturnToPoolTimerHandle;
	FDeckPointReservation GoalPointReservation;
};

/** Asset-compatible wrapper for existing BP_DeckRangedEnemy assets. */
UCLASS(Blueprintable)
class ENEMY_API ADeckRangedEnemy : public ADeckEnemy
{
	GENERATED_BODY()
};
