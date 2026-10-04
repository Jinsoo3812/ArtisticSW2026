#include "DeckAI/DeckWalkAreaComponent.h"

#include "DeckAI/DeckWalkGraph.h"
#include "DeckAI/DeckWalkHeightResolver.h"
#include "DeckAI/DeckWalkSurfaceSampler.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "DeckAI/DeckEnemySpawnerComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ShipAI/EnemyShip.h"

struct FDeckWalkRuntime
{
	FDeckWalkGraph Graph;
	TArray<FDeckWalkSurfaceSettings> Surfaces;
	TArray<FDeckWalkHeightBand> HeightBands;
	TArray<FDeckWalkFloorSource> Sources;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> Obstacles;
	FDeckWalkSamplingSettings Settings;
};

void FDeckWalkRuntimeDeleter::operator()(FDeckWalkRuntime* Runtime) const { delete Runtime; }

UDeckWalkAreaComponent::UDeckWalkAreaComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
	PrimaryComponentTick.bAllowTickOnDedicatedServer = false;
	SetIsReplicatedByDefault(false);
}
UDeckWalkAreaComponent::~UDeckWalkAreaComponent() = default;

void UDeckWalkAreaComponent::BeginPlay()
{
	Super::BeginPlay();
	// Simulated proxies consume CharacterMovement replication, never their own walk graph.
	SetComponentTickEnabled(GetOwner() && GetOwner()->HasAuthority() && GetNetMode() != NM_DedicatedServer);
}

UStaticMeshComponent* UDeckWalkAreaComponent::GetFrame() const
{
	const AEnemyShip* Ship = Cast<AEnemyShip>(GetOwner());
	return Ship ? Ship->GetDeckMeshComplex() : nullptr;
}
FVector UDeckWalkAreaComponent::ToWorld(const FVector& LocalFloor) const
{
	return GetFrame() ? GetFrame()->GetComponentTransform().TransformPosition(LocalFloor) : FVector::ZeroVector;
}
FVector UDeckWalkAreaComponent::ToLocal(const FVector& WorldPosition) const
{
	return GetFrame() ? GetFrame()->GetComponentTransform().InverseTransformPosition(WorldPosition) : FVector::ZeroVector;
}

bool UDeckWalkAreaComponent::ResolveSources()
{
	AEnemyShip* Ship = Cast<AEnemyShip>(GetOwner());
	if (!Ship || !GetFrame()) return false;
	Runtime->Surfaces = Surfaces;
	if (Runtime->Surfaces.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[DeckWalk] Ship=%s Reason=MissingSurfaces"), *GetNameSafe(Ship));
		return false;
	}

	TArray<UPrimitiveComponent*> Components;
	Ship->GetComponents<UPrimitiveComponent>(Components);
	TSet<FName> Ids;
	for (int32 Index = 0; Index < Runtime->Surfaces.Num(); ++Index)
	{
		const auto& Surface = Runtime->Surfaces[Index];
		if (Surface.SurfaceId.IsNone() || Ids.Contains(Surface.SurfaceId)
			|| Surface.FloorComponentNames.IsEmpty())
		{
			UE_LOG(LogTemp, Error, TEXT("[DeckWalk] Ship=%s Surface=%s Reason=%s"),
				*Ship->GetName(), *Surface.SurfaceId.ToString(), Surface.FloorComponentNames.IsEmpty()
					? TEXT("MissingFloorSources") : TEXT("MissingOrDuplicateSurfaceId"));
			return false;
		}
		Ids.Add(Surface.SurfaceId);
		FDeckWalkHeightBand HeightBand;
		FString HeightError;
		if (!FDeckWalkHeightResolver::Resolve(Surface, *Ship, GetFrame()->GetComponentTransform(), HeightBand, HeightError))
		{
			UE_LOG(LogTemp, Error, TEXT("[DeckWalk] %s Surface=%s %s"),
				*GetNameSafe(Ship), *Surface.SurfaceId.ToString(), *HeightError);
			return false;
		}
		Runtime->HeightBands.Add(HeightBand);
		UE_LOG(LogTemp, Log, TEXT("[DeckWalk] %s Surface=%s ReferencePoint=%d ReferenceLocalZ=%.2f SamplingZ=[%.2f, %.2f]"),
			*GetNameSafe(Ship), *Surface.SurfaceId.ToString(),
			HeightBand.bUseReferenceHeight ? Surface.HeightReferencePointId : INDEX_NONE,
			HeightBand.ReferenceLocalPosition.Z, HeightBand.MinimumZ, HeightBand.MaximumZ);
		for (const FName Name : Surface.FloorComponentNames)
		{
			UStaticMeshComponent* Mesh = nullptr;
			for (auto* Component : Components)
			{
				if (Component->GetFName() == Name) { Mesh = Cast<UStaticMeshComponent>(Component); break; }
			}
			if (!Mesh || !Mesh->GetStaticMesh() || !Mesh->IsQueryCollisionEnabled())
			{
				UE_LOG(LogTemp, Error, TEXT("[DeckWalk] Missing/query-disabled floor component %s for %s"), *Name.ToString(), *Surface.SurfaceId.ToString());
				return false;
			}
			FDeckWalkFloorSource Source;
			Source.Component = Mesh; Source.Surface = Index; Source.bTraceComplex = Surface.bTraceComplex;
			Source.MinZ = HeightBand.MinimumZ; Source.MaxZ = HeightBand.MaximumZ;
			Source.bUseReferenceHeight = HeightBand.bUseReferenceHeight;
			Source.ReferenceZ = HeightBand.ReferenceLocalPosition.Z;
			Runtime->Sources.Add(Source);
			Runtime->Obstacles.AddUnique(Mesh);
		}
	}
	for (FName Name : ObstacleComponentNames)
	{
		UPrimitiveComponent* Found = nullptr;
		for (auto* Component : Components) if (Component->GetFName() == Name) { Found = Component; break; }
		if (!Found || !Found->IsQueryCollisionEnabled())
		{
			UE_LOG(LogTemp, Error, TEXT("[DeckWalk] Ship=%s Component=%s Reason=MissingOrQueryDisabledObstacle"),
				*Ship->GetName(), *Name.ToString());
			return false;
		}
		Runtime->Obstacles.AddUnique(Found);
	}
	for (const auto& Link : WalkingConnections)
	{
		if (!Ids.Contains(Link.FromSurface) || !Ids.Contains(Link.ToSurface) || Link.FromSurface == Link.ToSurface)
		{
			UE_LOG(LogTemp, Error, TEXT("[DeckWalk] Ship=%s From=%s To=%s Reason=InvalidWalkingConnection"),
				*Ship->GetName(), *Link.FromSurface.ToString(), *Link.ToSurface.ToString());
			return false;
		}
	}
	return true;
}

void UDeckWalkAreaComponent::Rebuild()
{
	// Check before touching the snapshot or revision, including direct C++ calls on a client.
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
#if WITH_EDITOR
	// Editor actors do not run EnemyShip::BeginPlay's registration step.
	if (GetWorld() && !GetWorld()->IsGameWorld())
	{
		if (AEnemyShip* Ship = Cast<AEnemyShip>(GetOwner()); Ship && Ship->GetDeckEnemySpawnerComponent())
			Ship->GetDeckEnemySpawnerComponent()->InitializeWaypoints();
	}
#endif
	bReady = false;
	LocationClaims.Reset();
	++Revision;
	Runtime.Reset(new FDeckWalkRuntime());
	if (!ResolveSources())
	{
		UE_LOG(LogTemp, Error, TEXT("[DeckWalk] Invalid surface configuration on %s"), *GetNameSafe(GetOwner()));
		return;
	}
	if (!GetFrame()->GetComponentScale().Equals(FVector::OneVector, 0.01f))
	{
		UE_LOG(LogTemp, Error, TEXT("[DeckWalk] Navigation frame requires unit scale on %s"), *GetNameSafe(GetOwner()));
		return;
	}
	auto& Settings = Runtime->Settings;
	Settings.CellSize = FMath::Max(30.0f, CellSize);
	Settings.Radius = FMath::Max(20.0f, ClearanceRadius);
	Settings.HalfHeight = FMath::Max(Settings.Radius, ClearanceHalfHeight);
	Settings.StepHeight = FMath::Max(1.0f, MaximumStepHeight);
	Settings.SlopeDegrees = FMath::Clamp(MaximumFloorSlopeDegrees, 0.0f, 60.0f);
	const FDeckWalkSurfaceSampler Sampler(GetFrame()->GetComponentTransform(), Settings, Runtime->Sources, Runtime->Obstacles);
	if (!Sampler.Build(Runtime->Surfaces, WalkingConnections, Runtime->Graph) || !FilterSeedRegions())
	{
		UE_LOG(LogTemp, Error, TEXT("[DeckWalk] Required surface build failed on %s"), *GetNameSafe(GetOwner()));
		return;
	}
	bReady = Runtime->Graph.LabelRegions(true) > 0;
	for (const auto& Surface : Runtime->Surfaces)
	{
		UE_LOG(LogTemp, Log, TEXT("[DeckWalk] %s Surface=%s Nodes=%d Ready=%d"),
			*GetNameSafe(GetOwner()), *Surface.SurfaceId.ToString(), GetSurfaceNodeCount(Surface.SurfaceId), bReady);
	}
}

bool UDeckWalkAreaComponent::FilterSeedRegions()
{
	auto& Graph = Runtime->Graph;
	Graph.LabelRegions(false);
	TMap<int32, int32> Counts;
	for (const auto& Node : Graph.Nodes) ++Counts.FindOrAdd(Node.Region);
	for (auto& Node : Graph.Nodes) Node.bEnabled = Counts.FindRef(Node.Region) >= MinimumRegionCells;
	const AEnemyShip* Ship = Cast<AEnemyShip>(GetOwner());
	bool bValid = true;
	for (int32 SurfaceIndex = 0; SurfaceIndex < Runtime->Surfaces.Num(); ++SurfaceIndex)
	{
		const auto& Surface = Runtime->Surfaces[SurfaceIndex];
		TSet<int32> SeedRegions;
		for (int32 PointId : Surface.SeedPointIds)
		{
			const UDeckWaypointComponent* Point = Ship->GetDeckWaypoint(PointId);
			int32 Node = INDEX_NONE;
			if (!Point || (!Point->GetWalkSurfaceId().IsNone() && Point->GetWalkSurfaceId() != Surface.SurfaceId)
				|| !FindNearestNode(ToLocal(Point->GetComponentLocation()), Surface.SurfaceId, 250.0f, PointHeightTolerance, Node))
			{
				UE_LOG(LogTemp, Error, TEXT("[DeckWalk] Surface=%s Seed=%d has no matching floor"), *Surface.SurfaceId.ToString(), PointId);
				if (Surface.bRequired) bValid = false;
				continue;
			}
			SeedRegions.Add(Graph.Nodes[Node].Region);
		}
		int32 Kept = 0;
		for (auto& Node : Graph.Nodes)
		{
			if (Node.Surface != SurfaceIndex || !Node.bEnabled) continue;
			if (!Surface.SeedPointIds.IsEmpty() && !SeedRegions.Contains(Node.Region)) Node.bEnabled = false;
			else ++Kept;
		}
		if (Surface.bRequired && Kept == 0)
		{
			UE_LOG(LogTemp, Error, TEXT("[DeckWalk] Required surface %s has no usable nodes"), *Surface.SurfaceId.ToString());
			bValid = false;
		}
	}
	return bValid;
}

bool UDeckWalkAreaComponent::FindNearestNode(const FVector& LocalFloor, FName SurfaceId,
	float XYTolerance, float ZTolerance, int32& OutNode) const
{
	OutNode = INDEX_NONE;
	if (!Runtime) return false;
	float Best = TNumericLimits<float>::Max();
	for (int32 I = 0; I < Runtime->Graph.Nodes.Num(); ++I)
	{
		const auto& Node = Runtime->Graph.Nodes[I];
		if (!Node.bEnabled || (!SurfaceId.IsNone() && Runtime->Surfaces[Node.Surface].SurfaceId != SurfaceId)) continue;
		const float XY = FVector::Dist2D(Node.Floor, LocalFloor);
		const float Z = FMath::Abs(Node.Floor.Z - LocalFloor.Z);
		if (XY > XYTolerance || Z > ZTolerance) continue;
		const float Score = FMath::Square(XY) + FMath::Square(Z * 2.0f);
		if (Score < Best) { Best = Score; OutNode = I; }
	}
	return OutNode != INDEX_NONE;
}

FDeckWalkLocation UDeckWalkAreaComponent::MakeLocation(int32 Node) const
{
	FDeckWalkLocation Result;
	if (!Runtime || !Runtime->Graph.Nodes.IsValidIndex(Node)) return Result;
	Result.NodeIndex = Node; Result.Revision = Revision;
	Result.LocalFloor = Runtime->Graph.Nodes[Node].Floor;
	Result.SurfaceId = Runtime->Surfaces[Runtime->Graph.Nodes[Node].Surface].SurfaceId;
	return Result;
}
bool UDeckWalkAreaComponent::IsLocationValid(const FDeckWalkLocation& Location) const
{
	return bReady && Runtime && Location.Revision == Revision
		&& Runtime->Graph.Nodes.IsValidIndex(Location.NodeIndex) && Runtime->Graph.Nodes[Location.NodeIndex].bEnabled
		&& Runtime->Surfaces[Runtime->Graph.Nodes[Location.NodeIndex].Surface].SurfaceId == Location.SurfaceId
		&& (Location.bPreciseFloor
			? !Location.LocalFloor.ContainsNaN()
				&& FVector::Dist2D(Runtime->Graph.Nodes[Location.NodeIndex].Floor, Location.LocalFloor) <= CellSize
				&& FMath::Abs(Runtime->Graph.Nodes[Location.NodeIndex].Floor.Z - Location.LocalFloor.Z) <= MaximumStepHeight
			: Runtime->Graph.Nodes[Location.NodeIndex].Floor.Equals(Location.LocalFloor, 0.1f));
}

void UDeckWalkAreaComponent::GetReachableLocations(const FDeckWalkLocation& Start,
	TArray<FDeckWalkLocation>& Out, bool bCrossSurfaces) const
{
	Out.Reset();
	if (!IsLocationValid(Start)) return;
	TSet<int32> Visited;
	TArray<int32> Queue = { Start.NodeIndex };
	Visited.Add(Start.NodeIndex);
	for (int32 Cursor = 0; Cursor < Queue.Num(); ++Cursor)
	{
		const int32 Index = Queue[Cursor];
		Out.Add(MakeLocation(Index));
		for (int32 Neighbor : Runtime->Graph.Nodes[Index].Neighbors)
		{
			const auto& Node = Runtime->Graph.Nodes[Neighbor];
			if (!Node.bEnabled || Visited.Contains(Neighbor)
				|| (!bCrossSurfaces && Node.Surface != Runtime->Graph.Nodes[Start.NodeIndex].Surface)) continue;
			Visited.Add(Neighbor);
			Queue.Add(Neighbor);
		}
	}
}

bool UDeckWalkAreaComponent::ResolveLocationTransform(const FDeckWalkLocation& Location,
	const ACharacter& Character, FTransform& Out) const
{
	const UCapsuleComponent* Capsule = Character.GetCapsuleComponent();
	if (!IsLocationValid(Location) || !GetFrame() || !Capsule
		|| Capsule->GetScaledCapsuleRadius() > ClearanceRadius
		|| Capsule->GetScaledCapsuleHalfHeight() > ClearanceHalfHeight) return false;
	const FVector Up = GetFrame()->GetUpVector();
	FVector Forward = FVector::VectorPlaneProject(Character.GetActorForwardVector(), Up).GetSafeNormal();
	if (Forward.IsNearlyZero()) Forward = GetFrame()->GetForwardVector();
	Out = FTransform(FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat(),
		ToWorld(Location.LocalFloor) + Up * (Capsule->GetScaledCapsuleHalfHeight() + 2.0f));
	return !Out.ContainsNaN();
}

bool UDeckWalkAreaComponent::IsLocationAvailable(const FDeckWalkLocation& Location,
	const ACharacter& Requester) const
{
	FTransform Transform;
	if (!GetWorld() || !ResolveLocationTransform(Location, Requester, Transform)) return false;
	const UCapsuleComponent* Capsule = Requester.GetCapsuleComponent();
	for (const auto& Pair : LocationClaims)
	{
		if (!Pair.Key.IsValid() || Pair.Key.Get() == &Requester || !IsLocationValid(Pair.Value.Location)) continue;
		if (FMath::Abs(Pair.Value.Location.LocalFloor.Z - Location.LocalFloor.Z) <= MaximumStepHeight
			&& FVector::Dist2D(Pair.Value.Location.LocalFloor, Location.LocalFloor)
				< Pair.Value.Radius + Capsule->GetScaledCapsuleRadius()) return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DeckWalkDestination), false, &Requester);
	return !GetWorld()->OverlapBlockingTestByChannel(Transform.GetLocation(), Transform.GetRotation(),
		ECC_Pawn, FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),
			Capsule->GetScaledCapsuleHalfHeight()), Params);
}

bool UDeckWalkAreaComponent::TryClaimLocation(const FDeckWalkLocation& Location, ACharacter& Requester)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Requester.HasAuthority()
		|| !IsLocationAvailable(Location, Requester)) return false;
	for (auto It = LocationClaims.CreateIterator(); It; ++It)
		if (!It.Key().IsValid() || !IsLocationValid(It.Value().Location)) It.RemoveCurrent();
	FLocationClaim Claim;
	Claim.Location = Location;
	Claim.Radius = Requester.GetCapsuleComponent()->GetScaledCapsuleRadius();
	LocationClaims.Add(&Requester, Claim);
	return true;
}

void UDeckWalkAreaComponent::ReleaseLocationClaim(const AActor* Requester)
{
	if (GetOwner() && GetOwner()->HasAuthority() && Requester)
		LocationClaims.Remove(const_cast<AActor*>(Requester));
}

bool UDeckWalkAreaComponent::IsSupportedSegment(const FDeckWalkLocation& Start,
	const FDeckWalkLocation& End) const
{
	if (!IsLocationValid(Start) || !IsLocationValid(End) || Start.SurfaceId != End.SurfaceId) return false;
	return IsSupportedSegment(Start, End.LocalFloor);
}
bool UDeckWalkAreaComponent::IsSupportedSegment(const FDeckWalkLocation& Start, const FVector& LocalEnd) const
{
	if (!IsLocationValid(Start) || LocalEnd.ContainsNaN()) return false;
	const FDeckWalkSurfaceSampler Sampler(GetFrame()->GetComponentTransform(), Runtime->Settings,
		Runtime->Sources, Runtime->Obstacles);
	const int32 Surface = Runtime->Graph.Nodes[Start.NodeIndex].Surface;
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector::Dist2D(Start.LocalFloor, LocalEnd) / (CellSize * 0.5f)));
	FVector Previous = Start.LocalFloor;
	for (int32 I = 0; I <= Steps; ++I)
	{
		const FVector Query = FMath::Lerp(Start.LocalFloor, LocalEnd, float(I) / Steps);
		FVector Floor;
		if (!Sampler.TraceNear(Query, Surface, MaximumStepHeight, MaximumStepHeight, Floor)
			|| FMath::Abs(Floor.Z - Previous.Z) > MaximumStepHeight || !Sampler.HasClearance(Floor)) return false;
		Previous = Floor;
	}
	return true;
}
bool UDeckWalkAreaComponent::ResolveWaypoint(const UDeckWaypointComponent& Point, FDeckWalkLocation& Out) const
{
	Out = FDeckWalkLocation();
	if (!bReady || !Runtime || (Runtime->Surfaces.Num() > 1 && Point.GetWalkSurfaceId().IsNone())) return false;
	FVector Query = ToLocal(Point.GetComponentLocation());
	float HeightTolerance = PointHeightTolerance;
	for (int32 I = 0; I < Runtime->HeightBands.Num(); ++I)
	{
		if (Runtime->Surfaces[I].SurfaceId != Point.GetWalkSurfaceId()) continue;
		const auto& Band = Runtime->HeightBands[I];
		if (Band.bUseReferenceHeight)
		{
			// A referenced surface owns destination height too. Existing point Z values
			// cannot project a spawn/combat anchor back onto the old deck.
			Query.Z = Band.ReferenceLocalPosition.Z;
			HeightTolerance = FMath::Max(Query.Z - Band.MinimumZ, Band.MaximumZ - Query.Z);
		}
		break;
	}
	int32 Node = INDEX_NONE;
	if (!FindNearestNode(Query, Point.GetWalkSurfaceId(), 250.0f, HeightTolerance, Node)) return false;
	Out = MakeLocation(Node);
	return true;
}

FVector UDeckWalkAreaComponent::GetActorFeetWorld(const AActor& Actor) const
{
	const ACharacter* Character = Cast<ACharacter>(&Actor);
	const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	return Capsule ? Capsule->GetComponentLocation() - Capsule->GetUpVector() * Capsule->GetScaledCapsuleHalfHeight() : Actor.GetActorLocation();
}
bool UDeckWalkAreaComponent::ResolveLocalFloor(const FVector& LocalFloor, FName SurfaceId, FDeckWalkLocation& Out) const
{
	Out = FDeckWalkLocation();
	int32 Node = INDEX_NONE;
	if (!bReady || !FindNearestNode(LocalFloor, SurfaceId, CellSize, MaximumStepHeight, Node)) return false;
	Out = MakeLocation(Node);
	return true;
}
bool UDeckWalkAreaComponent::ResolveActorOnDeck(const AActor& Actor, FDeckWalkLocation& Out) const
{
	Out = FDeckWalkLocation();
	if (!bReady || !Runtime || !GetFrame()) return false;
	const ACharacter* Character = Cast<ACharacter>(&Actor);
	if (!Character) return false;
	const FVector Feet = ToLocal(GetActorFeetWorld(Actor));
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (Movement && Movement->IsMovingOnGround() && Movement->CurrentFloor.IsWalkableFloor())
	{
		const FHitResult& Hit = Movement->CurrentFloor.HitResult;
		if (Runtime->Obstacles.Contains(Hit.GetComponent()))
		{
			const FVector Floor = ToLocal(Hit.ImpactPoint);
			int32 Node = INDEX_NONE;
			if (FMath::Abs(Feet.Z - Floor.Z) <= 65.0f
				&& FVector::Dist2D(Feet, Floor) <= 100.0f
				&& FindNearestNode(Floor, NAME_None, CellSize * 1.5f, MaximumStepHeight, Node))
			{
				Out = MakeLocation(Node);
				return true;
			}
		}
	}
	// Spawn/landing fallback probes feet, never the capsule center or an upper deck.
	const FDeckWalkSurfaceSampler Sampler(GetFrame()->GetComponentTransform(), Runtime->Settings, Runtime->Sources, Runtime->Obstacles);
	float Best = TNumericLimits<float>::Max();
	for (int32 Surface = 0; Surface < Runtime->Surfaces.Num(); ++Surface)
	{
		FVector Floor;
		int32 Node = INDEX_NONE;
		if (!Sampler.TraceNear(Feet, Surface, 10.0f, 65.0f, Floor)
			|| !FindNearestNode(Floor, Runtime->Surfaces[Surface].SurfaceId, CellSize * 1.5f, MaximumStepHeight, Node)) continue;
		const float Error = FMath::Abs(Feet.Z - Floor.Z);
		if (Error < Best) { Out = MakeLocation(Node); Best = Error; }
	}
	return Out.NodeIndex != INDEX_NONE;
}

bool UDeckWalkAreaComponent::ResolvePreciseLocalFloor(const FVector& LocalFloor, FName SurfaceId, FDeckWalkLocation& Out) const
{
	if (LocalFloor.ContainsNaN() || !ResolveLocalFloor(LocalFloor, SurfaceId, Out)) return false;
	const FDeckWalkSurfaceSampler Sampler(GetFrame()->GetComponentTransform(), Runtime->Settings, Runtime->Sources, Runtime->Obstacles);
	FVector Floor;
	if (!Sampler.TraceNear(LocalFloor, Runtime->Graph.Nodes[Out.NodeIndex].Surface, MaximumStepHeight, MaximumStepHeight, Floor)
		|| !Sampler.HasClearance(Floor) || !IsSupportedSegment(Out, Floor)) { Out = FDeckWalkLocation(); return false; }
	Out.LocalFloor = Floor;
	Out.bPreciseFloor = true;
	return true;
}

bool UDeckWalkAreaComponent::ResolveSpawnTransform(const UDeckWaypointComponent& Point,
	float CapsuleHalfHeight, FTransform& OutTransform) const
{
	FDeckWalkLocation Location;
	if (!ResolveWaypoint(Point, Location) || CapsuleHalfHeight > ClearanceHalfHeight) return false;
	const FVector Up = GetFrame()->GetUpVector();
	FVector Forward = FVector::VectorPlaneProject(Point.GetForwardVector(), Up).GetSafeNormal();
	if (Forward.IsNearlyZero()) Forward = GetFrame()->GetForwardVector();
	OutTransform = FTransform(FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat(),
		ToWorld(Location.LocalFloor) + Up * SpawnHeightOffset);
	return !OutTransform.ContainsNaN();
}

bool UDeckWalkAreaComponent::FindPath(const FDeckWalkLocation& Start, const FDeckWalkLocation& Goal,
	TArray<FDeckWalkLocation>& OutPath, bool bCrossSurfaces) const
{
	OutPath.Reset();
	TArray<int32> Nodes;
	if (!IsLocationValid(Start) || !IsLocationValid(Goal)
		|| !Runtime->Graph.FindPath(Start.NodeIndex, Goal.NodeIndex, bCrossSurfaces, Nodes)) return false;
	for (int32 Node : Nodes) OutPath.Add(MakeLocation(Node));
	return true;
}
bool UDeckWalkAreaComponent::PickPatrolPath(const AActor& Actor, FRandomStream& Random,
	TArray<FDeckWalkLocation>& OutPath) const
{
	OutPath.Reset();
	FDeckWalkLocation Start;
	if (!ResolveActorOnDeck(Actor, Start)) return false;
	const auto& StartNode = Runtime->Graph.Nodes[Start.NodeIndex];
	TArray<int32> Candidates;
	for (int32 I = 0; I < Runtime->Graph.Nodes.Num(); ++I)
	{
		const auto& Node = Runtime->Graph.Nodes[I];
		if (Node.bEnabled && Node.Region == StartNode.Region
			&& (bPatrolAcrossSurfaces || Node.Surface == StartNode.Surface)
			&& FVector::Dist2D(Node.Floor, StartNode.Floor) >= 250.0f) Candidates.Add(I);
	}
	while (!Candidates.IsEmpty())
	{
		const int32 Choice = Random.RandRange(0, Candidates.Num() - 1);
		if (FindPath(Start, MakeLocation(Candidates[Choice]), OutPath, bPatrolAcrossSurfaces)) return true;
		Candidates.RemoveAtSwap(Choice, 1, EAllowShrinking::No);
	}
	return false;
}

bool UDeckWalkAreaComponent::FindPathInDistanceBand(const FDeckWalkLocation& Start, const FDeckWalkLocation& Goal,
	const FVector& Center, float Distance, float Tolerance, TArray<FDeckWalkLocation>& OutPath) const
{
	OutPath.Reset();
	if (!IsLocationValid(Start) || !IsLocationValid(Goal) || Start.SurfaceId != Goal.SurfaceId
		|| Center.ContainsNaN() || !FMath::IsFinite(Distance) || !FMath::IsFinite(Tolerance)) return false;
	TArray<uint8> Allowed;
	Allowed.SetNumZeroed(Runtime->Graph.Nodes.Num());
	for (int32 I = 0; I < Allowed.Num(); ++I)
	{
		const FDeckWalkNode& Node = Runtime->Graph.Nodes[I];
		Allowed[I] = Node.Surface == Runtime->Graph.Nodes[Start.NodeIndex].Surface
			&& FMath::Abs(FVector::Dist2D(Node.Floor, Center) - Distance) <= FMath::Max(30.0f, Tolerance);
	}
	TArray<int32> Nodes;
	if (!Allowed[Goal.NodeIndex] || !Runtime->Graph.FindPath(Start.NodeIndex, Goal.NodeIndex, false, Nodes, &Allowed)) return false;
	for (int32 Node : Nodes) OutPath.Add(MakeLocation(Node));
	return true;
}

UPrimitiveComponent* UDeckWalkAreaComponent::GetFloorComponent(const FDeckWalkLocation& Location) const
{
	return IsLocationValid(Location) ? Runtime->Sources[Runtime->Graph.Nodes[Location.NodeIndex].Source].Component.Get() : nullptr;
}
UPrimitiveComponent* UDeckWalkAreaComponent::GetMovementBase(const AActor& Actor) const
{
	FDeckWalkLocation Location;
	if (!ResolveActorOnDeck(Actor, Location)) return nullptr;
	const ACharacter* Character = Cast<ACharacter>(&Actor);
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (Movement && Movement->IsMovingOnGround() && Movement->CurrentFloor.IsWalkableFloor())
	{
		const FHitResult& Hit = Movement->CurrentFloor.HitResult;
		const FVector Delta = ToLocal(GetActorFeetWorld(Actor)) - ToLocal(Hit.ImpactPoint);
		if (Runtime->Obstacles.Contains(Hit.GetComponent()) && Delta.Size2D() <= 100.0f && FMath::Abs(Delta.Z) <= 65.0f)
		{
			return Hit.GetComponent();
		}
	}
	return GetFloorComponent(Location);
}
int32 UDeckWalkAreaComponent::GetSurfaceNodeCount(FName SurfaceId) const
{
	int32 Count = 0;
	if (Runtime) for (const auto& Node : Runtime->Graph.Nodes)
	{
		if (Node.bEnabled && (SurfaceId.IsNone() || Runtime->Surfaces[Node.Surface].SurfaceId == SurfaceId)) ++Count;
	}
	return Count;
}
bool UDeckWalkAreaComponent::TraceDeckObstacle(const FVector& WorldStart, const FVector& WorldEnd, FHitResult& OutHit) const
{
	if (!bReady || !Runtime || !GetFrame()) return false;
	const FDeckWalkSurfaceSampler Sampler(GetFrame()->GetComponentTransform(), Runtime->Settings, Runtime->Sources, Runtime->Obstacles);
	return Sampler.TraceObstacle(WorldStart, WorldEnd, OutHit);
}
bool UDeckWalkAreaComponent::GetSurfaceHeightRange(FName SurfaceId, float& OutMinimumZ, float& OutMaximumZ) const
{
	OutMinimumZ = OutMaximumZ = 0.0f;
	if (!Runtime) return false;
	for (int32 I = 0; I < Runtime->HeightBands.Num(); ++I)
	{
		if (Runtime->Surfaces[I].SurfaceId != SurfaceId) continue;
		OutMinimumZ = Runtime->HeightBands[I].MinimumZ;
		OutMaximumZ = Runtime->HeightBands[I].MaximumZ;
		return true;
	}
	return false;
}
bool UDeckWalkAreaComponent::GetActorSurface(AActor* Actor, FName& OutSurfaceId) const
{
	OutSurfaceId = NAME_None;
	FDeckWalkLocation Location;
	if (!Actor || !ResolveActorOnDeck(*Actor, Location)) return false;
	OutSurfaceId = Location.SurfaceId;
	return true;
}
void UDeckWalkAreaComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bDrawDebugArea && GetOwner()->HasAuthority()) DrawWalkArea(0.15f, DebugSurfaceFilter, bDrawDebugConnections);
}
void UDeckWalkAreaComponent::DrawWalkArea(float Duration, FName SurfaceFilter, bool bDrawConnections) const
{
	if (!Runtime || !GetWorld() || GetNetMode() == NM_DedicatedServer) return;
	for (int32 I = 0; I < Runtime->HeightBands.Num(); ++I)
	{
		const auto& Band = Runtime->HeightBands[I];
		const auto& Surface = Runtime->Surfaces[I];
		if (!Band.bUseReferenceHeight || (!SurfaceFilter.IsNone() && SurfaceFilter != Surface.SurfaceId)) continue;
		const FVector& Reference = Band.ReferenceLocalPosition;
		DrawDebugLine(GetWorld(), ToWorld(Reference - FVector(100, 0, 0)), ToWorld(Reference + FVector(100, 0, 0)),
			Surface.DebugColor, false, Duration, 0, 2.0f);
		DrawDebugLine(GetWorld(), ToWorld(Reference - FVector(0, 100, 0)), ToWorld(Reference + FVector(0, 100, 0)),
			Surface.DebugColor, false, Duration, 0, 2.0f);
		DrawDebugString(GetWorld(), ToWorld(Reference + FVector(0, 0, 35)),
			FString::Printf(TEXT("%s Reference=%d Z=%.1f Range=[%.1f, %.1f]"), *Surface.SurfaceId.ToString(),
				Surface.HeightReferencePointId, Reference.Z, Band.MinimumZ, Band.MaximumZ), nullptr, Surface.DebugColor, Duration);
	}
	TSet<int32> Labeled;
	for (int32 I = 0; I < Runtime->Graph.Nodes.Num(); ++I)
	{
		const auto& Node = Runtime->Graph.Nodes[I];
		const auto& Surface = Runtime->Surfaces[Node.Surface];
		if (!Node.bEnabled || (!SurfaceFilter.IsNone() && SurfaceFilter != Surface.SurfaceId)) continue;
		const FVector P = ToWorld(Node.Floor + FVector(0,0,8));
		DrawDebugPoint(GetWorld(), P, 8.0f, bReady ? Surface.DebugColor : FColor::Red, false, Duration);
		if (!Labeled.Contains(Node.Surface))
		{
			DrawDebugString(GetWorld(), P + FVector(0,0,35), Surface.SurfaceId.ToString(), nullptr, Surface.DebugColor, Duration);
			Labeled.Add(Node.Surface);
		}
		if (bDrawConnections) for (int32 Other : Node.Neighbors)
		{
			const auto& Next = Runtime->Graph.Nodes[Other];
			if (Other > I && Next.bEnabled) DrawDebugLine(GetWorld(), P, ToWorld(Next.Floor + FVector(0,0,8)),
				Node.Surface == Next.Surface ? Surface.DebugColor : FColor::Yellow, false, Duration, 0, 1.0f);
		}
	}
}
