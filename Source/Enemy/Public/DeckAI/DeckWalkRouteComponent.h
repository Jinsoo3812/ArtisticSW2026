#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DeckAI/DeckWalkTypes.h"
#include "DeckWalkRouteComponent.generated.h"

class UDeckWalkAreaComponent;
class UPrimitiveComponent;

enum class EDeckWalkRouteTick : uint8
{
	Moving,
	Reached,
	Failed,
	Blocked
};

enum class EDeckWalkBlockReason : uint8 { None, Player, Pawn, Geometry, Penetration, NoProgress };

/** Per-character goal and path state. AI tasks choose goals; the ship owns the area. */
UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
class ENEMY_API UDeckWalkRouteComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDeckWalkRouteComponent();

	bool SetLocationGoal(const FDeckWalkLocation& Goal);
	bool SetLocationGoalInDistanceBand(const FDeckWalkLocation& Goal, const FVector& Center, float Distance, float Tolerance);
	bool SetActorGoal(AActor* MovingTarget);
	bool SetPatrolGoal(FRandomStream& Random);
	void ClearGoal();
	/** Clear only at host/pool/snapshot boundaries; ordinary cancellation retains obstacle memory. */
	void ResetNavigationState();
	bool BeginGoalSelection();
	void RecordGoalSelectionFailure();
	bool IsGoalSelectionDelayed() const;
	bool HasPendingMovementBlock() const { return bHasPendingImpact && PendingImpactGeneration == RouteGeneration; }
	bool IsGoalRecentlyBlocked(const FDeckWalkLocation& Goal) const;
	bool IsTraversalAllowed(const FDeckWalkLocation& From, const FDeckWalkLocation& To) const { return CanTraverse(From, To); }
	FVector GetPreferredEscapeDirection() const;
	void ObserveMovementImpact(const FHitResult& Hit, const FVector& MoveDelta);
	bool IsOrdinaryWalking() const;
	void StopWalkingMovement();
	bool TryGetSafePenetrationAdjustment(const FHitResult& Hit, FVector& OutAdjustment) const;
	EDeckWalkBlockReason GetLastBlockReason() const { return LastBlockReason; }
	bool HasGoal() const { return bHasGoal; }
	const FDeckWalkLocation& GetGoal() const { return LocalGoal; }
	bool IsTrackingActor() const { return bTrackTarget; }
	EDeckWalkRouteTick TickRoute(float DeltaSeconds, float AcceptanceRadius,
		float ProgressTimeout, float MaximumMoveTime, float MoveSpeed, float MinimumProgressDistance);

private:
	UDeckWalkAreaComponent* GetArea() const;
	bool Replan(const FDeckWalkLocation& Goal, bool bCrossSurfaces = true);
	bool PlanActorGoal(AActor* Target);
	void AcceptPath(TArray<FDeckWalkLocation>&& Path);
	bool InstallPath(TArray<FDeckWalkLocation>&& Path, const FDeckWalkLocation& Start, const FDeckWalkLocation& Goal);
	bool CanTraverse(const FDeckWalkLocation& From, const FDeckWalkLocation& To) const;
	bool IsMovementSuspended() const;
	float GetGoalSelectionDelay() const;
	void RefreshAvoidanceContext();
	EDeckWalkRouteTick BlockRoute(const FHitResult& Hit, const FDeckWalkLocation& Current,
		const FVector& LocalDirection, EDeckWalkBlockReason Reason);

	UPROPERTY(EditDefaultsOnly, Category = "Deck AI|Collision")
	bool bAvoidMovementCollisions = true;
	UPROPERTY(EditDefaultsOnly, Category = "Deck AI|Collision", meta = (ClampMin = "0.1", Units = "s"))
	float BlockMemorySeconds = 1.25f;
	UPROPERTY(EditDefaultsOnly, Category = "Deck AI|Collision", meta = (ClampMin = "0.05", Units = "s"))
	float CollisionReplanInterval = 0.25f;
	UPROPERTY(EditDefaultsOnly, Category = "Deck AI|Collision", meta = (ClampMin = "0.0", Units = "cm"))
	float CollisionLookAhead = 60.f;
	UPROPERTY(EditDefaultsOnly, Category = "Deck AI|Collision")
	bool bDrawCollisionDebug = false;
	struct FBlockedMove
	{
		FDeckWalkLocation Goal;
		FName ContactSurface;
		TWeakObjectPtr<UPrimitiveComponent> Blocker;
		FVector LocalBlockerOrigin = FVector::ZeroVector;
		bool bHadBlocker = false;
		FVector LocalContact = FVector::ZeroVector;
		FVector EscapeDirection = FVector::ZeroVector;
		double ExpiresAt = 0.;
	};
	TArray<FBlockedMove> BlockedMoves;
	TWeakObjectPtr<UDeckWalkAreaComponent> AvoidanceArea;
	int32 AvoidanceRevision = INDEX_NONE;
	int32 RepeatedAttempts = 0;
	double NextGoalSelectionTime = 0.;
	FVector ProgressAnchor = FVector::ZeroVector;
	bool bHasProgressAnchor = false;
	FHitResult PendingImpact;
	uint32 RouteGeneration = 0;
	uint32 PendingImpactGeneration = 0;
	bool bHasPendingImpact = false;
	EDeckWalkBlockReason LastBlockReason = EDeckWalkBlockReason::None;
	double PathQueryBudgetTime = -1.;
	int32 RemainingPathCollisionQueries = 512;

	TArray<FDeckWalkLocation> LocalPath;
	FDeckWalkLocation LocalGoal;
	TWeakObjectPtr<AActor> TargetActor;
	int32 PathCursor = 0;
	float ElapsedTime = 0.0f;
	float TimeSinceProgress = 0.0f;
	float ProgressDistance = TNumericLimits<float>::Max();
	float EstimatedMoveTime = 0.0f;
	bool bHasGoal = false;
	bool bTrackTarget = false;
	double NextActorReplanTime = 0.;
};
