#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpawnRoute.generated.h"

class USplineComponent;

// Route 이동에 필요한 중심 경유점 정보를 한 곳에 모아 Enemy별 MoveTarget 계산의 기준으로 사용한다.
USTRUCT(BlueprintType)
struct FRouteWaypoint
{
	GENERATED_BODY()

	// RouteWaypoints 배열에서 이 경유점이 몇 번째인지 식별하기 위해 필요하다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Route|Waypoint")
	int32 WaypointIndex = INDEX_NONE;

	// Spline 위 어느 거리에서 만들어진 점인지 Debug와 Support Waypoint 생성 검증에 필요하다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Route|Waypoint")
	float DistanceAlongSpline = 0.0f;

	// 모든 Enemy가 공유하는 중심 목표점이며 실제 MoveTarget Offset의 기준점이다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Route|Waypoint")
	FVector CenterLocation = FVector::ZeroVector;

	// Waypoint 주변에서 앞뒤 Jitter를 계산하고 Debug 방향을 표시하기 위해 필요하다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Route|Waypoint")
	FVector ForwardVector = FVector::ForwardVector;

	// RouteWidth 기반 좌우 Offset을 계산하기 위해 필요하다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Route|Waypoint")
	FVector RightVector = FVector::RightVector;

	// 디자이너가 직접 찍은 Spline Point와 자동 생성된 보조점을 구분하기 위해 필요하다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Route|Waypoint")
	bool bDesignerPoint = true;

	// NavMesh 보정 성공 여부를 Debug와 레벨 검증에서 확인하기 위해 필요하다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Route|Waypoint")
	bool bProjectedToNavMesh = false;
};


UCLASS()
class ENEMY_API ASpawnRoute : public AActor
{
	GENERATED_BODY()

public:
	ASpawnRoute();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

public:
	// WaveData의 RouteId와 레벨에 배치된 Route Actor를 매칭하기 위해 필요하다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Route")
	FName RouteId = NAME_None;

	// 디자이너가 편집하는 Route 중심선이며 Waypoint 생성의 원본 데이터로 필요하다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Route")
	TObjectPtr<USplineComponent> RouteSpline;

	// Enemy별 MoveTarget을 중심선 좌우로 분산시키기 위해 필요하다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Movement", meta = (ClampMin = "0.0"))
	float RouteWidth = 200.0f;

	// 일반 Waypoint에 도착했다고 판단할 MoveTo 허용 반경으로 필요하다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Movement", meta = (ClampMin = "1.0"))
	float AcceptanceRadius = 180.0f;

	// Goal 지점은 일반 Waypoint보다 넓게 판정할 수 있어 별도 값으로 필요하다.
	// 실제 필요한 기능일지 고려 필요
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Movement", meta = (ClampMin = "1.0"))
	float GoalAcceptanceRadius = 240.0f;

	// 같은 Route에서 Spawn되는 Enemy가 시작점에 겹치지 않게 분산시키기 위해 필요하다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Spawn", meta = (ClampMin = "0.0"))
	float SpawnRadius = 100.0f;

	// Enemy별 목표점이 완전히 좌우로만 퍼지지 않게 작은 앞뒤 편차를 주기 위해 필요하다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Movement", meta = (ClampMin = "0.0"))
	float ForwardJitterRadius = 25.0f;

	// 디자이너 Spline Point 사이가 너무 길 때 곡선 추종용 Support Waypoint를 만들기 위해 필요하다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Generation", meta = (ClampMin = "0.0"))
	float MaxSegmentLength = 900.0f;

	// 긴 Segment에 보조 Waypoint를 자동 추가할지 선택하기 위해 필요하다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Generation")
	bool bGenerateSupportWaypointsForLongSegments = true;

	// Start와 Goal에서 Enemy 목표점이 과하게 퍼지는 것을 막기 위해 필요하다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Movement")
	bool bKeepStartAndGoalCentered = true;

	// Waypoint와 MoveTarget이 실제 AI 이동 가능한 NavMesh 위에 놓이도록 보정하기 위해 필요하다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Navigation")
	bool bProjectWaypointsToNavMesh = true;

	// NavMesh Projection이 주변 어느 범위까지 유효 위치를 찾을지 정하기 위해 필요하다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Navigation")
	FVector NavProjectionExtent = FVector(300.0f, 300.0f, 500.0f);

	// Spline Point와 Support Point로 만든 공유 중심 경유점 배열이다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Route|Waypoint")
	TArray<FRouteWaypoint> RouteWaypoints;

	// NavMesh Projection 실패 지점을 Debug Draw와 레벨 검증에서 표시하기 위해 필요하다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Route|Navigation")
	TArray<FVector> FailedProjectionPoints;

public:
	// Spline Point를 기준으로 RouteWaypoints를 다시 만들어 에디터와 런타임 Route 데이터를 동기화한다.
	UFUNCTION(BlueprintCallable, Category = "Route")
	void GenerateWaypointsFromSpline();

	// AWaveSpawnManager가 이 Route의 시작점 근처 Spawn Transform을 얻기 위해 필요하다.
	UFUNCTION(BlueprintCallable, Category = "Route")
	FTransform GetSpawnTransform(float OverrideSpawnRadius = -1.0f) const;

	// C++ 전용
	const TArray<FRouteWaypoint>& GetRouteWaypointsRef() const;
	
	// 이동 컴포넌트가 공유 중심 경유점 목록을 읽기 위해 필요하다.
	UFUNCTION(BlueprintPure, Category = "Route")
	TArray<FRouteWaypoint> GetRouteWaypoints() const;

	// 이동 컴포넌트가 일반 Waypoint MoveTo 허용 반경을 읽기 위해 필요하다.
	UFUNCTION(BlueprintPure, Category = "Route")
	float GetAcceptanceRadius() const;

	// 이동 컴포넌트가 Goal Waypoint MoveTo 허용 반경을 읽기 위해 필요하다.
	UFUNCTION(BlueprintPure, Category = "Route")
	float GetGoalAcceptanceRadius() const;

	// Spawn 직후 Start 지점을 건너뛰고 첫 이동 목표를 정하기 위해 필요하다.
	UFUNCTION(BlueprintPure, Category = "Route")
	int32 GetFirstMoveWaypointIndex() const;

	// 이동 컴포넌트가 현재 Waypoint가 Goal인지 판단하기 위해 필요하다.
	UFUNCTION(BlueprintPure, Category = "Route")
	bool IsGoalWaypointIndex(int32 WaypointIndex) const;

	// EnemySeed 기반으로 특정 Enemy만의 실제 MoveTo 목표점을 만들기 위해 필요하다.
	UFUNCTION(BlueprintCallable, Category = "Route")
	bool GetMoveTargetForWaypoint(int32 WaypointIndex, int32 EnemySeed, FVector& OutMoveTarget) const;

	// Route 생성 결과, 방향 벡터, RouteWidth, MoveTarget 샘플을 화면에서 검증하기 위해 필요하다.
	UFUNCTION(BlueprintCallable, Category = "Route|Debug")
	void DrawDebugRoute(float Duration = 5.0f) const;

private:
	// Spline의 특정 거리에서 중심 Waypoint 데이터를 만들기 위해 필요하다.
	FRouteWaypoint MakeRouteWaypointAtDistance(float DistanceAlongSpline, bool bDesignerPoint);

	// 생성된 Waypoint를 배열에 추가하는 공통 경로를 만들기 위해 필요하다.
	void AddRouteWaypointAtDistance(float DistanceAlongSpline, bool bDesignerPoint);

	// Waypoint 배열 수정 후 인덱스를 실제 배열 순서와 일치시키기 위해 필요하다.
	void ReindexRouteWaypoints();

	// 후보 위치를 NavMesh 위의 유효 위치로 보정하기 위해 필요하다.
	bool ProjectPointToNavMesh(const FVector& InPoint, FVector& OutPoint) const;

	// MoveTarget Projection 실패 시 Offset을 줄여 재시도하기 위해 필요하다.
	bool ProjectMoveTargetWithFallback(const FRouteWaypoint& Waypoint, const FVector& CandidateLocation, FVector& OutMoveTarget) const;

	// EnemySeed, WaypointIndex, RouteId를 섞어 재현 가능한 난수를 만들기 위해 필요하다.
	int32 MakeCombinedSeed(int32 EnemySeed, int32 WaypointIndex) const;
};