#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DeckAI/DeckWalkTypes.h"
#include "DeckWalkAreaComponent.generated.h"

class UStaticMeshComponent;
class UPrimitiveComponent;
class UDeckWaypointComponent;
class ACharacter;
struct FDeckWalkRuntime;
struct FDeckWalkRuntimeDeleter
{
	void operator()(FDeckWalkRuntime* Runtime) const;
};

/** Ship-owned query facade. Physics sampling and graph search have separate adapters. */
UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
class ENEMY_API UDeckWalkAreaComponent : public UActorComponent
{
	GENERATED_BODY()
#if WITH_EDITOR
	friend class FDeckSpawnAnchorValidator;
#endif
#if WITH_DEV_AUTOMATION_TESTS
	friend class FDeckEnemySpawnerCompositionTest;
	friend class FDeckFixedAnchorLifecycleTest;
#endif
public:
	UDeckWalkAreaComponent();
	virtual ~UDeckWalkAreaComponent() override;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Ship|Deck Walk")
	void Rebuild();
	UFUNCTION(BlueprintPure, Category = "Ship|Deck Walk")
	bool IsReady() const { return bReady; }
	UFUNCTION(BlueprintPure, Category = "Ship|Deck Walk")
	int32 GetSurfaceNodeCount(FName SurfaceId) const;
	/** Server-side build snapshot; false before resolution or on clients. */
	UFUNCTION(BlueprintPure, Category = "Ship|Deck Walk")
	bool GetSurfaceHeightRange(FName SurfaceId, float& OutMinimumZ, float& OutMaximumZ) const;
	UFUNCTION(BlueprintPure, Category = "Ship|Deck Walk")
	bool GetActorSurface(AActor* Actor, FName& OutSurfaceId) const;
	UFUNCTION(BlueprintCallable, Category = "Ship|Deck Walk")
	void DrawWalkArea(float Duration = 0.15f, FName SurfaceFilter = NAME_None, bool bDrawConnections = false) const;

	bool ResolveWaypoint(const UDeckWaypointComponent& Point, FDeckWalkLocation& Out) const;
	bool ResolveActorOnDeck(const AActor& Actor, FDeckWalkLocation& Out) const;
	int32 GetRevision() const { return Revision; }
	bool IsTrackingSupport(const UPrimitiveComponent* Component) const;
	/** Returns tracking geometry, not a walk handle; clearance is checked at the selected movement goal. */
	bool ResolveTrackingProjection(const FVector& LocalSupport, FName PreferredSurface, float XYTolerance,
		float MaximumHeight, FVector& OutCenter, FName& OutSurface, FName& OutReason) const;
	bool ResolveLocalFloor(const FVector& LocalFloor, FName SurfaceId, FDeckWalkLocation& Out) const;
	/** Keeps the requested XY after validating floor, clearance and its connection to the graph. */
	bool ResolvePreciseLocalFloor(const FVector& LocalFloor, FName SurfaceId, FDeckWalkLocation& Out) const;
	bool ResolveSpawnTransform(const UDeckWaypointComponent& Point, float CapsuleHalfHeight, FTransform& OutTransform) const;
	bool FindPath(const FDeckWalkLocation& Start, const FDeckWalkLocation& Goal,
		TArray<FDeckWalkLocation>& OutPath, bool bCrossSurfaces = true) const;
	bool FindPathInDistanceBand(const FDeckWalkLocation& Start, const FDeckWalkLocation& Goal,
		const FVector& Center, float Distance, float Tolerance, TArray<FDeckWalkLocation>& OutPath) const;
	bool PickPatrolPath(const AActor& Actor, FRandomStream& Random, TArray<FDeckWalkLocation>& OutPath) const;
	bool IsLocationValid(const FDeckWalkLocation& Location) const;
	void GetReachableLocations(const FDeckWalkLocation& Start, TArray<FDeckWalkLocation>& Out, bool bCrossSurfaces = true) const;
	bool ResolveLocationTransform(const FDeckWalkLocation& Location, const ACharacter& Character, FTransform& Out) const;
	bool IsLocationAvailable(const FDeckWalkLocation& Location, const ACharacter& Requester) const;
	bool TryClaimLocation(const FDeckWalkLocation& Location, ACharacter& Requester);
	/** Restores a still-valid previous claim if route replacement fails. */
	void RestoreLocationClaim(const FDeckWalkLocation& Previous, ACharacter& Requester);
	void ReleaseLocationClaim(const AActor* Requester);
	bool IsSupportedSegment(const FDeckWalkLocation& Start, const FDeckWalkLocation& End) const;
	bool IsSupportedSegment(const FDeckWalkLocation& Start, const FVector& LocalEnd) const;
	UPrimitiveComponent* GetFloorComponent(const FDeckWalkLocation& Location) const;
	UPrimitiveComponent* GetMovementBase(const AActor& Actor) const;
	bool TraceDeckObstacle(const FVector& WorldStart, const FVector& WorldEnd, FHitResult& OutHit) const;
	FVector ToWorld(const FVector& LocalFloor) const;
	FVector ToLocal(const FVector& WorldPosition) const;
	FVector GetActorFeetWorld(const AActor& Actor) const;

private:
	UStaticMeshComponent* GetFrame() const;
	bool FindNearestNode(const FVector& LocalFloor, FName SurfaceId, float XYTolerance, float ZTolerance, int32& OutNode) const;
	FDeckWalkLocation MakeLocation(int32 Node) const;
	bool ResolveSources();
	bool FilterSeedRegions();

	UPROPERTY(EditDefaultsOnly, Category = "Ship|Deck Walk|Surfaces", meta = (TitleProperty = "SurfaceId"))
	TArray<FDeckWalkSurfaceSettings> Surfaces;
	/** Include all floors/ceilings and rails. Visual and hull collisions are not inferred. */
	UPROPERTY(EditDefaultsOnly, Category = "Ship|Deck Walk|Surfaces")
	TArray<FName> ObstacleComponentNames = { TEXT("DeckMesh_Complex"), TEXT("DeckMesh_Simple") };
	UPROPERTY(EditDefaultsOnly, Category = "Ship|Deck Walk|Surfaces")
	TArray<FDeckWalkSurfaceConnection> WalkingConnections;
	UPROPERTY(EditDefaultsOnly, Category = "Ship|Deck Walk|Surfaces")
	bool bPatrolAcrossSurfaces = false;
	UPROPERTY(EditDefaultsOnly, Category = "Ship|Deck Walk|Tracking", meta = (TitleProperty = "SurfaceId"))
	TArray<FDeckTrackingSupportRegion> TrackingSupportRegions;
	UPROPERTY(EditDefaultsOnly, Category = "Ship|Deck Walk|Sampling", meta = (ClampMin = "30.0", Units = "cm"))
	float CellSize = 75.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Ship|Deck Walk|Sampling", meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float MaximumFloorSlopeDegrees = 40.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Ship|Deck Walk|Clearance", meta = (ClampMin = "20.0", Units = "cm"))
	float ClearanceRadius = 45.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Ship|Deck Walk|Clearance", meta = (ClampMin = "40.0", Units = "cm"))
	float ClearanceHalfHeight = 100.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Ship|Deck Walk|Clearance", meta = (ClampMin = "1.0", Units = "cm"))
	float MaximumStepHeight = 45.0f;
	/** Height of the spawned character's actor origin above the resolved deck floor. */
	UPROPERTY(EditAnywhere, Category = "Ship|Deck Walk|Spawn", meta = (ClampMin = "0.0", Units = "cm"))
	float SpawnHeightOffset = 90.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Ship|Deck Walk|Sampling", meta = (ClampMin = "1"))
	int32 MinimumRegionCells = 6;
	UPROPERTY(EditDefaultsOnly, Category = "Ship|Deck Walk|Sampling", meta = (ClampMin = "1.0", Units = "cm"))
	float PointHeightTolerance = 100.0f;
	UPROPERTY(EditAnywhere, Category = "Ship|Deck Walk|Debug")
	bool bDrawDebugArea = false;
	UPROPERTY(EditAnywhere, Category = "Ship|Deck Walk|Debug")
	bool bDrawDebugConnections = false;
	UPROPERTY(EditAnywhere, Category = "Ship|Deck Walk|Debug")
	FName DebugSurfaceFilter;

	TUniquePtr<FDeckWalkRuntime, FDeckWalkRuntimeDeleter> Runtime;
	int32 Revision = 0;
	bool bReady = false;
	struct FLocationClaim
	{
		FDeckWalkLocation Location;
		float Radius = 0.0f;
	};
	TMap<TWeakObjectPtr<AActor>, FLocationClaim> LocationClaims;
};
