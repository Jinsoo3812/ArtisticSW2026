#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DeckAI/DeckWalkTypes.h"
#include "DeckWalkRouteComponent.generated.h"

class UDeckWalkAreaComponent;

enum class EDeckWalkRouteTick : uint8
{
	Moving,
	Reached,
	Failed
};

/** Per-character goal and path state. AI tasks choose goals; the ship owns the area. */
UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
class ENEMY_API UDeckWalkRouteComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDeckWalkRouteComponent();

	bool SetPointGoal(int32 PointId, bool bCommitPoint = true);
	bool SetActorGoal(AActor* MovingTarget);
	bool SetPatrolGoal(FRandomStream& Random);
	void ClearGoal();
	bool HasGoal() const { return bHasGoal; }
	int32 GetPointGoalId() const { return PointGoalId; }
	EDeckWalkRouteTick TickRoute(float DeltaSeconds, float AcceptanceRadius,
		float ProgressTimeout, float MaximumMoveTime, float MoveSpeed);

private:
	UDeckWalkAreaComponent* GetArea() const;
	bool Replan(const FDeckWalkLocation& Goal);
	void AcceptPath(TArray<FDeckWalkLocation>&& Path);

	TArray<FDeckWalkLocation> LocalPath;
	FDeckWalkLocation LocalGoal;
	TWeakObjectPtr<AActor> TargetActor;
	int32 PathCursor = 0;
	int32 PointGoalId = INDEX_NONE;
	float ElapsedTime = 0.0f;
	float TimeSinceProgress = 0.0f;
	float ProgressDistance = TNumericLimits<float>::Max();
	float EstimatedMoveTime = 0.0f;
	bool bHasGoal = false;
	bool bTrackTarget = false;
};
