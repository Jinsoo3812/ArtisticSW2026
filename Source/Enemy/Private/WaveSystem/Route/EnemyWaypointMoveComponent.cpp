#include "WaveSystem/Route/EnemyWaypointMoveComponent.h"

#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "Navigation/PathFollowingComponent.h"
#include "WaveSystem/Route/SpawnRoute.h"

DEFINE_LOG_CATEGORY_STATIC(LogEnemyWaypointMoveComponent, Log, All);

UEnemyWaypointMoveComponent::UEnemyWaypointMoveComponent()
{
	// Route 이동은 PathFollowing 완료 이벤트로만 진행하므로 Tick이 필요하지 않다.
	PrimaryComponentTick.bCanEverTick = false;
}

void UEnemyWaypointMoveComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// EndPlay 이후 PathFollowingComponent에서 이 컴포넌트로 콜백하지 않도록 한다.
	UnbindMoveFinishedDelegate();

	Super::EndPlay(EndPlayReason);
}

bool UEnemyWaypointMoveComponent::StartRoute(ASpawnRoute* InRoute, int32 InEnemySeed)
{
	// 새 Route를 시작하기 전에 기존 이동 요청과 Delegate를 정리해 중복 이동을 막는다.
	StopRoute(true);

	if (!InRoute)
	{
		UE_LOG(LogEnemyWaypointMoveComponent, Warning, TEXT("[EnemyWaypointMove] StartRoute failed: Route is null. Owner=%s"), *GetNameSafe(GetOwner()));
		return false;
	}

	AAIController* ResolvedAIController = ResolveAIController();
	if (!ResolvedAIController)
	{
		UE_LOG(LogEnemyWaypointMoveComponent, Warning, TEXT("[EnemyWaypointMove] StartRoute failed: AIController is missing. Owner=%s Route=%s"), *GetNameSafe(GetOwner()), *GetNameSafe(InRoute));
		return false;
	}

	ActiveRoute = InRoute;
	CachedAIController = ResolvedAIController;
	EnemySeed = InEnemySeed;
	CurrentWaypointIndex = ActiveRoute->GetFirstMoveWaypointIndex();
	CurrentMoveTarget = FVector::ZeroVector;
	CurrentMoveRequestId = FAIRequestID();
	CurrentWaypointRetryCount = 0;
	bHasReportedTerminalEvent = false;
	RouteFollowState = ERouteFollowState::Idle;

	if (!BindMoveFinishedDelegate())
	{
		UE_LOG(LogEnemyWaypointMoveComponent, Warning, TEXT("[EnemyWaypointMove] StartRoute failed: PathFollowingComponent is missing. Owner=%s Route=%s"), *GetNameSafe(GetOwner()), *GetNameSafe(ActiveRoute));
		ClearRuntimeData();
		RouteFollowState = ERouteFollowState::Failed;
		return false;
	}

	return MoveToCurrentWaypoint(false);
}

void UEnemyWaypointMoveComponent::StopRoute(bool bAbortMove)
{
	// 외부 중단은 실패나 Goal 이벤트로 보고하지 않고 조용히 Route 이동만 종료한다.
	UnbindMoveFinishedDelegate();

	if (bAbortMove && CachedAIController && RouteFollowState == ERouteFollowState::Moving)
	{
		CachedAIController->StopMovement();
	}

	ClearRuntimeData();
	RouteFollowState = ERouteFollowState::Stopped;
}

bool UEnemyWaypointMoveComponent::IsRouteActive() const
{
	// 실제 AI Move 요청을 진행 중인 상태만 Active로 본다.
	return RouteFollowState == ERouteFollowState::Moving;
}

ERouteFollowState UEnemyWaypointMoveComponent::GetRouteFollowState() const
{
	// 외부에서 Route 이동 작업 상태를 읽을 수 있게 한다.
	return RouteFollowState;
}

ASpawnRoute* UEnemyWaypointMoveComponent::GetActiveRoute() const
{
	// 현재 참조 중인 Route Provider를 반환한다.
	return ActiveRoute;
}

int32 UEnemyWaypointMoveComponent::GetEnemySeed() const
{
	// 현재 Route 이동에 사용 중인 deterministic Seed를 반환한다.
	return EnemySeed;
}

int32 UEnemyWaypointMoveComponent::GetCurrentWaypointIndex() const
{
	// 현재 진행 중인 Waypoint Index를 반환한다.
	return CurrentWaypointIndex;
}

FVector UEnemyWaypointMoveComponent::GetCurrentMoveTarget() const
{
	// 현재 AI MoveTo에 전달된 실제 목표 위치를 반환한다.
	return CurrentMoveTarget;
}

AAIController* UEnemyWaypointMoveComponent::ResolveAIController() const
{
	// Route 이동은 AIController의 PathFollowing이 필요하므로 Owner가 Pawn인지 먼저 확인한다.
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn)
	{
		return nullptr;
	}

	AAIController* AIController = Cast<AAIController>(OwnerPawn->GetController());
	if (AIController)
	{
		return AIController;
	}

	if (!bTrySpawnDefaultControllerIfMissing)
	{
		return nullptr;
	}

	OwnerPawn->SpawnDefaultController();
	return Cast<AAIController>(OwnerPawn->GetController());
}

bool UEnemyWaypointMoveComponent::BindMoveFinishedDelegate()
{
	// PathFollowingComponent의 OnRequestFinished를 사용해 Tick 없이 Move 완료를 받는다.
	UnbindMoveFinishedDelegate();

	if (!CachedAIController)
	{
		return false;
	}

	CachedPathFollowingComponent = CachedAIController->GetPathFollowingComponent();
	if (!CachedPathFollowingComponent)
	{
		return false;
	}

	MoveFinishedDelegateHandle = CachedPathFollowingComponent->OnRequestFinished.AddUObject(this, &UEnemyWaypointMoveComponent::HandleMoveFinished);
	return MoveFinishedDelegateHandle.IsValid();
}

void UEnemyWaypointMoveComponent::UnbindMoveFinishedDelegate()
{
	// 바인딩된 Delegate가 있으면 제거해 중복 콜백과 파괴 후 콜백을 방지한다.
	if (CachedPathFollowingComponent && MoveFinishedDelegateHandle.IsValid())
	{
		CachedPathFollowingComponent->OnRequestFinished.Remove(MoveFinishedDelegateHandle);
	}

	MoveFinishedDelegateHandle.Reset();
	CachedPathFollowingComponent = nullptr;
}

bool UEnemyWaypointMoveComponent::MoveToCurrentWaypoint(bool bUseCenterLocationFallback)
{
	if (!ActiveRoute)
	{
		ReportMoveFailed(TEXT("ActiveRoute is null"));
		return false;
	}

	if (!CachedAIController)
	{
		ReportMoveFailed(TEXT("CachedAIController is null"));
		return false;
	}

	FVector DesiredMoveTarget = FVector::ZeroVector;
	const bool bHasMoveTarget = bUseCenterLocationFallback
		? GetCurrentWaypointCenterLocation(DesiredMoveTarget)
		: ActiveRoute->GetMoveTargetForWaypoint(CurrentWaypointIndex, EnemySeed, DesiredMoveTarget);

	if (!bHasMoveTarget)
	{
		ReportMoveFailed(TEXT("Failed to get MoveTarget for current waypoint"));
		return false;
	}

	CurrentMoveTarget = DesiredMoveTarget;

	FAIMoveRequest MoveRequest;
	MoveRequest.SetGoalLocation(CurrentMoveTarget);
	MoveRequest.SetAcceptanceRadius(GetAcceptanceRadiusForCurrentWaypoint());
	MoveRequest.SetUsePathfinding(bUsePathfinding);
	MoveRequest.SetAllowPartialPath(bAllowPartialPath);
	MoveRequest.SetProjectGoalLocation(false);
	MoveRequest.SetReachTestIncludesAgentRadius(bReachTestIncludesAgentRadius);
	MoveRequest.SetReachTestIncludesGoalRadius(bReachTestIncludesGoalRadius);

	const FPathFollowingRequestResult RequestResult = CachedAIController->MoveTo(MoveRequest);

	if (RequestResult.Code == EPathFollowingRequestResult::RequestSuccessful)
	{
		CurrentMoveRequestId = RequestResult.MoveId;
		RouteFollowState = ERouteFollowState::Moving;
		return true;
	}

	if (RequestResult.Code == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		CurrentMoveRequestId = FAIRequestID();
		HandleWaypointArrived();
		return true;
	}

	return TryRetryCurrentWaypoint(TEXT("MoveTo request failed"));
}

void UEnemyWaypointMoveComponent::HandleWaypointArrived()
{
	if (!ActiveRoute)
	{
		ReportMoveFailed(TEXT("Arrived callback received but ActiveRoute is null"));
		return;
	}

	CurrentWaypointRetryCount = 0;

	if (ActiveRoute->IsGoalWaypointIndex(CurrentWaypointIndex))
	{
		CompleteRoute();
		return;
	}

	++CurrentWaypointIndex;
	MoveToCurrentWaypoint(false);
}

void UEnemyWaypointMoveComponent::CompleteRoute()
{
	if (bHasReportedTerminalEvent)
	{
		return;
	}

	bHasReportedTerminalEvent = true;
	RouteFollowState = ERouteFollowState::Completed;
	CurrentMoveRequestId = FAIRequestID();

	UnbindMoveFinishedDelegate();

	AActor* OwnerActor = GetOwner();
	ASpawnRoute* CompletedRoute = ActiveRoute;

	UE_LOG(LogEnemyWaypointMoveComponent, Log, TEXT("[EnemyWaypointMove] Route completed. Owner=%s Route=%s Seed=%d"), *GetNameSafe(OwnerActor), *GetNameSafe(CompletedRoute), EnemySeed);

	OnRouteGoalReached.Broadcast(OwnerActor, CompletedRoute);
}

bool UEnemyWaypointMoveComponent::TryRetryCurrentWaypoint(const TCHAR* FailureContext)
{
	if (CurrentWaypointRetryCount >= MaxMoveRetryCount)
	{
		ReportMoveFailed(FailureContext);
		return false;
	}

	++CurrentWaypointRetryCount;

	UE_LOG(
		LogEnemyWaypointMoveComponent,
		Warning,
		TEXT("[EnemyWaypointMove] Retry current waypoint with center target. Owner=%s Route=%s WaypointIndex=%d Retry=%d/%d Context=%s"),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(ActiveRoute),
		CurrentWaypointIndex,
		CurrentWaypointRetryCount,
		MaxMoveRetryCount,
		FailureContext ? FailureContext : TEXT("Unknown")
	);

	return MoveToCurrentWaypoint(true);
}

void UEnemyWaypointMoveComponent::ReportMoveFailed(const TCHAR* FailureContext)
{
	if (bHasReportedTerminalEvent)
	{
		return;
	}

	bHasReportedTerminalEvent = true;
	RouteFollowState = ERouteFollowState::Failed;
	CurrentMoveRequestId = FAIRequestID();

	UnbindMoveFinishedDelegate();

	AActor* OwnerActor = GetOwner();
	ASpawnRoute* FailedRoute = ActiveRoute;
	const int32 FailedWaypointIndex = CurrentWaypointIndex;

	UE_LOG(
		LogEnemyWaypointMoveComponent,
		Warning,
		TEXT("[EnemyWaypointMove] Route move failed. Owner=%s Route=%s WaypointIndex=%d Seed=%d Context=%s"),
		*GetNameSafe(OwnerActor),
		*GetNameSafe(FailedRoute),
		FailedWaypointIndex,
		EnemySeed,
		FailureContext ? FailureContext : TEXT("Unknown")
	);

	OnRouteMoveFailed.Broadcast(OwnerActor, FailedRoute, FailedWaypointIndex);
}

float UEnemyWaypointMoveComponent::GetAcceptanceRadiusForCurrentWaypoint() const
{
	if (!ActiveRoute)
	{
		return 5.0f;
	}

	return ActiveRoute->IsGoalWaypointIndex(CurrentWaypointIndex)
		? ActiveRoute->GetGoalAcceptanceRadius()
		: ActiveRoute->GetAcceptanceRadius();
}

bool UEnemyWaypointMoveComponent::GetCurrentWaypointCenterLocation(FVector& OutCenterLocation) const
{
	if (!ActiveRoute)
	{
		OutCenterLocation = FVector::ZeroVector;
		return false;
	}

	const TArray<FRouteWaypoint>& RouteWaypoints = ActiveRoute->GetRouteWaypointsRef();
	if (!RouteWaypoints.IsValidIndex(CurrentWaypointIndex))
	{
		OutCenterLocation = FVector::ZeroVector;
		return false;
	}

	OutCenterLocation = RouteWaypoints[CurrentWaypointIndex].CenterLocation;
	return true;
}

void UEnemyWaypointMoveComponent::ClearRuntimeData()
{
	ActiveRoute = nullptr;
	CachedAIController = nullptr;
	CachedPathFollowingComponent = nullptr;
	EnemySeed = 0;
	CurrentWaypointIndex = INDEX_NONE;
	CurrentMoveTarget = FVector::ZeroVector;
	CurrentMoveRequestId = FAIRequestID();
	CurrentWaypointRetryCount = 0;
	bHasReportedTerminalEvent = false;
}

void UEnemyWaypointMoveComponent::HandleMoveFinished(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	if (RouteFollowState != ERouteFollowState::Moving)
	{
		return;
	}

	if (CurrentMoveRequestId.IsValid() && RequestID != CurrentMoveRequestId)
	{
		return;
	}

	CurrentMoveRequestId = FAIRequestID();

	if (Result.Code == EPathFollowingResult::Success)
	{
		HandleWaypointArrived();
		return;
	}

	TryRetryCurrentWaypoint(TEXT("Move finished without success"));
}