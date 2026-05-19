#pragma once

#include "CoreMinimal.h"
#include "AITypes.h"
#include "Components/ActorComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "EnemyWaypointMoveComponent.generated.h"

class AAIController;
class ASpawnRoute;
class UPathFollowingComponent;

// Route 이동 작업의 진행 상태만 표현
UENUM(BlueprintType)
enum class ERouteFollowState : uint8
{
	Idle UMETA(DisplayName = "Idle"),
	Moving UMETA(DisplayName = "Moving"),
	Completed UMETA(DisplayName = "Completed"),
	Failed UMETA(DisplayName = "Failed"),
	Stopped UMETA(DisplayName = "Stopped")
};

// Enemy가 Route의 마지막 Waypoint에 도착했음을 Broadcast.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEnemyRouteGoalReachedSignature, AActor*, EnemyActor, ASpawnRoute*, Route);

// Enemy가 Route 이동을 실패했음을 Broadcast
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FEnemyRouteMoveFailedSignature, AActor*, EnemyActor, ASpawnRoute*, Route, int32, FailedWaypointIndex);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class ENEMY_API UEnemyWaypointMoveComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Tick 없이 Move 완료 이벤트만 사용하기 위해 컴포넌트 기본 설정을 초기화한다.
	UEnemyWaypointMoveComponent();

	// 컴포넌트 종료 시 AI Move 완료 Delegate를 해제해 파괴된 컴포넌트로 콜백이 들어오지 않게 한다.
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:

	// --------- Delegate
	
	// Route Goal 도착을 WaveSpawnManager의 제거 처리로 연결하기 위해 필요하다.
	UPROPERTY(BlueprintAssignable, Category = "Route|Event")
	FEnemyRouteGoalReachedSignature OnRouteGoalReached;

	// Move 요청 실패나 경로 중단을 WaveSpawnManager 또는 Debug UI로 전달하기 위해 필요하다.
	UPROPERTY(BlueprintAssignable, Category = "Route|Event")
	FEnemyRouteMoveFailedSignature OnRouteMoveFailed;

	// -----------

	
	// MoveTo 요청에서 NavMesh 경로 탐색을 사용할지 정하기 위해 필요하다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Move")
	bool bUsePathfinding = true;

	// 목적지까지 완전 경로가 없을 때 부분 경로라도 허용할지 정하기 위해 필요하다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Move")
	bool bAllowPartialPath = true;

	// AcceptanceRadius 판정에 Enemy 캡슐 반경을 포함해 너무 엄격한 도착 판정을 피하기 위해 필요하다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Move")
	bool bReachTestIncludesAgentRadius = true;

	// AcceptanceRadius 판정에 목표 반경을 포함할지 선택하기 위해 필요하다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Move")
	bool bReachTestIncludesGoalRadius = false;

	// ----------- 실패 시 재시도
	
	// AIController가 아직 없을 때 기본 Controller 생성을 한 번 시도해 Spawn 직후 이동 실패를 줄이기 위해 필요하다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|AI")
	bool bTrySpawnDefaultControllerIfMissing = true;

	// MoveTarget 이동 실패 시 같은 Waypoint의 CenterLocation으로 제한적으로 재시도하기 위해 필요하다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Recovery", meta = (ClampMin = "0", ClampMax = "3"))
	int32 MaxMoveRetryCount = 1;

public:
	// 특정 Route와 EnemySeed로 Waypoint 이동을 시작하기 위해 필요하다.
	UFUNCTION(BlueprintCallable, Category = "Route")
	bool StartRoute(ASpawnRoute* InRoute, int32 InEnemySeed);

	// 사망, Despawn, Wave 종료 등 외부 이벤트에서 현재 Route 이동을 중단하기 위해 필요하다.
	UFUNCTION(BlueprintCallable, Category = "Route")
	void StopRoute(bool bAbortMove = true);

	// 현재 컴포넌트가 Route 이동 중인지 외부 시스템이 확인하기 위해 필요하다.
	UFUNCTION(BlueprintPure, Category = "Route")
	bool IsRouteActive() const;

	// Route 이동 작업의 내부 진행 상태를 Debug와 Blueprint에서 확인하기 위해 필요하다.
	UFUNCTION(BlueprintPure, Category = "Route")
	ERouteFollowState GetRouteFollowState() const;

	// 현재 이동 중인 Route Actor를 Manager나 Debug 코드가 확인하기 위해 필요하다.
	UFUNCTION(BlueprintPure, Category = "Route")
	ASpawnRoute* GetActiveRoute() const;

	// Enemy별 deterministic MoveTarget 계산에 사용 중인 Seed를 확인하기 위해 필요하다.
	UFUNCTION(BlueprintPure, Category = "Route")
	int32 GetEnemySeed() const;

	// 현재 목표 Waypoint Index를 Debug와 테스트 검증에서 확인하기 위해 필요하다.
	UFUNCTION(BlueprintPure, Category = "Route")
	int32 GetCurrentWaypointIndex() const;

	// 현재 AIController에 요청한 실제 MoveTarget을 Debug Draw나 검증에서 확인하기 위해 필요하다.
	UFUNCTION(BlueprintPure, Category = "Route")
	FVector GetCurrentMoveTarget() const;

private:
	// Owner Pawn에서 AIController를 찾거나 필요 시 기본 Controller 생성을 시도하기 위해 필요하다.
	AAIController* ResolveAIController() const;

	// AIController의 PathFollowing 완료 Delegate에 연결해 Tick 없이 다음 Waypoint로 진행하기 위해 필요하다.
	bool BindMoveFinishedDelegate();

	// Stop, Fail, Complete, EndPlay 시 Delegate 중복 호출과 dangling callback을 막기 위해 필요하다.
	void UnbindMoveFinishedDelegate();

	// 현재 Waypoint의 MoveTarget을 계산하고 AIController에 Move 요청을 보내기 위해 필요하다.
	bool MoveToCurrentWaypoint(bool bUseCenterLocationFallback);

	// 현재 Waypoint 도착 후 Goal 판정 또는 다음 Waypoint 진행을 처리하기 위해 필요하다.
	void HandleWaypointArrived();

	// 마지막 Waypoint 도착을 한 번만 보고하고 Route 이동을 Completed 상태로 끝내기 위해 필요하다.
	void CompleteRoute();

	// Move 요청 또는 완료 실패 시 CenterLocation 재시도를 제한적으로 수행하기 위해 필요하다.
	bool TryRetryCurrentWaypoint(const TCHAR* FailureContext);

	// 더 이상 복구할 수 없는 실패를 한 번만 보고하고 Route 이동을 Failed 상태로 끝내기 위해 필요하다.
	void ReportMoveFailed(const TCHAR* FailureContext);

	// Goal Waypoint에는 GoalAcceptanceRadius, 일반 Waypoint에는 AcceptanceRadius를 적용하기 위해 필요하다.
	float GetAcceptanceRadiusForCurrentWaypoint() const;

	// MoveTarget 실패 복구 시 Route 중심 Waypoint 위치를 얻기 위해 필요하다.
	bool GetCurrentWaypointCenterLocation(FVector& OutCenterLocation) const;

	// 새 Route 시작이나 Stop 처리 시 런타임 이동 데이터를 초기화하기 위해 필요하다.
	void ClearRuntimeData();

	// PathFollowingComponent가 Move 요청을 끝냈을 때 성공/실패에 따라 다음 처리를 하기 위해 필요하다.
	void HandleMoveFinished(FAIRequestID RequestID, const FPathFollowingResult& Result);

private:
	// 현재 이동에 사용 중인 Route Provider를 보관하기 위해 필요하다.
	UPROPERTY(Transient)
	TObjectPtr<ASpawnRoute> ActiveRoute = nullptr;

	// MoveTo 요청을 실행하는 AIController를 캐싱해 반복 Cast와 null 검사를 줄이기 위해 필요하다.
	UPROPERTY(Transient)
	TObjectPtr<AAIController> CachedAIController = nullptr;

	// Move 완료 Delegate를 해제할 대상 PathFollowingComponent를 보관하기 위해 필요하다.
	UPROPERTY(Transient)
	TObjectPtr<UPathFollowingComponent> CachedPathFollowingComponent = nullptr;

	// Route 이동 작업의 현재 단계만 표현하기 위해 필요하다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Route|Runtime", meta = (AllowPrivateAccess = "true"))
	ERouteFollowState RouteFollowState = ERouteFollowState::Idle;

	// SpawnRoute의 deterministic MoveTarget 계산에 전달하기 위해 필요하다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Route|Runtime", meta = (AllowPrivateAccess = "true"))
	int32 EnemySeed = 0;

	// 현재 이동 중인 Waypoint Index를 도착 처리와 다음 Waypoint 계산에 사용하기 위해 필요하다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Route|Runtime", meta = (AllowPrivateAccess = "true"))
	int32 CurrentWaypointIndex = INDEX_NONE;

	// 현재 AIController에 전달한 실제 목표점을 Debug와 실패 복구에 사용하기 위해 필요하다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Route|Runtime", meta = (AllowPrivateAccess = "true"))
	FVector CurrentMoveTarget = FVector::ZeroVector;

	// 현재 MoveTo 요청 ID만 처리하고 다른 시스템의 Move 완료 이벤트는 무시하기 위해 필요하다.
	FAIRequestID CurrentMoveRequestId;

	// PathFollowing 완료 Delegate를 중복 바인딩하지 않기 위해 필요하다.
	FDelegateHandle MoveFinishedDelegateHandle;

	// 같은 Waypoint에서 무한 재시도하지 않도록 현재 재시도 횟수를 기록하기 위해 필요하다.
	int32 CurrentWaypointRetryCount = 0;

	// GoalReached 또는 MoveFailed가 중복 Broadcast되지 않게 막기 위해 필요하다.
	bool bHasReportedTerminalEvent = false;
};