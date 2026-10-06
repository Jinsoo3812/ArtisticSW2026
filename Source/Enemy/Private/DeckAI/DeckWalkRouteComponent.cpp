#include "DeckAI/DeckWalkRouteComponent.h"

#include "DeckAI/DeckWalkAreaComponent.h"
#include "DeckAI/DeckCombatTargetResolverComponent.h"
#include "DeckAI/DeckWaypointMovementInterface.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ShipAI/EnemyShip.h"

UDeckWalkRouteComponent::UDeckWalkRouteComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}
UDeckWalkAreaComponent* UDeckWalkRouteComponent::GetArea() const
{
	const IDeckWaypointMovementInterface* Mover = Cast<IDeckWaypointMovementInterface>(GetOwner());
	AEnemyShip* Ship = Mover ? Mover->GetDeckHostShip() : nullptr;
	return Ship ? Ship->GetDeckWalkAreaComponent() : nullptr;
}
void UDeckWalkRouteComponent::ClearGoal()
{
	bHasGoal = false;
	TargetActor.Reset(); bTrackTarget = false;
	LocalPath.Reset(); LocalGoal = FDeckWalkLocation(); PathCursor = 0;
	ElapsedTime = TimeSinceProgress = EstimatedMoveTime = 0.0f;
	ProgressDistance = TNumericLimits<float>::Max();
}
void UDeckWalkRouteComponent::AcceptPath(TArray<FDeckWalkLocation>&& Path)
{
	LocalPath = MoveTemp(Path);
	LocalGoal = LocalPath.Last();
	PathCursor = 0;
	ElapsedTime = TimeSinceProgress = EstimatedMoveTime = 0.0f;
	ProgressDistance = TNumericLimits<float>::Max();
	for (int32 I = 1; I < LocalPath.Num(); ++I)
	{
		EstimatedMoveTime += FVector::Dist(LocalPath[I - 1].LocalFloor, LocalPath[I].LocalFloor);
	}
	bHasGoal = true;
}
bool UDeckWalkRouteComponent::Replan(const FDeckWalkLocation& Goal)
{
	UDeckWalkAreaComponent* Area = GetArea();
	FDeckWalkLocation Start;
	TArray<FDeckWalkLocation> Path;
	if (!Area || !GetOwner()->HasAuthority() || !Area->ResolveActorOnDeck(*GetOwner(), Start)
		|| !Area->FindPath(Start, Goal, Path) || Path.IsEmpty()) return false;
	if (!Path.Last().LocalFloor.Equals(Goal.LocalFloor, 1.0f))
	{
		if (!Area->IsSupportedSegment(Path.Last(), Goal)) return false;
		Path.Add(Goal);
	}
	AcceptPath(MoveTemp(Path));
	return true;
}
bool UDeckWalkRouteComponent::SetLocationGoal(const FDeckWalkLocation& Goal)
{
	if (!Replan(Goal)) return false;
	TargetActor.Reset(); bTrackTarget = false;
	return true;
}
bool UDeckWalkRouteComponent::SetActorGoal(AActor* MovingTarget)
{
	if (!PlanActorGoal(MovingTarget)) return false;
	TargetActor = MovingTarget; bTrackTarget = true;
	NextActorReplanTime = GetWorld()->GetTimeSeconds() + 0.35;
	return true;
}

bool UDeckWalkRouteComponent::PlanActorGoal(AActor* Target)
{
	const UDeckWalkAreaComponent* Area = GetArea();
	FDeckTargetAnchor Anchor; FDeckWalkLocation Start;
	if (!Area || !Area->ResolveActorOnDeck(*GetOwner(), Start)
		|| !UDeckCombatTargetResolverComponent::ResolveFor(GetOwner(), Target, Anchor)) return false;
	TArray<FDeckWalkLocation> Candidates; Area->GetReachableLocations(Start, Candidates);
	Candidates.Sort([&](const FDeckWalkLocation& A, const FDeckWalkLocation& B)
	{ return FVector::DistSquared2D(A.LocalFloor, Anchor.LocalCenter) < FVector::DistSquared2D(B.LocalFloor, Anchor.LocalCenter); });
	for (const auto& Candidate : Candidates)
	{
		const ACharacter* Character = Cast<ACharacter>(GetOwner());
		if (Character && Candidate.SurfaceId == Anchor.SurfaceId
			&& FVector::Dist2D(Candidate.LocalFloor, Anchor.LocalCenter) <= 350.f
			&& Area->IsLocationAvailable(Candidate, *Character) && Replan(Candidate)) return true;
	}
	return false;
}

bool UDeckWalkRouteComponent::SetLocationGoalInDistanceBand(const FDeckWalkLocation& Goal,
	const FVector& Center, float Distance, float Tolerance)
{
	const UDeckWalkAreaComponent* Area = GetArea();
	FDeckWalkLocation Start;
	TArray<FDeckWalkLocation> Path;
	if (!GetOwner()->HasAuthority() || !Area || !Area->ResolveActorOnDeck(*GetOwner(), Start)
		|| !Area->FindPathInDistanceBand(Start, Goal, Center, Distance, Tolerance, Path) || Path.IsEmpty()) return false;
	if (!Path.Last().LocalFloor.Equals(Goal.LocalFloor, 1.0f))
	{
		if (!Area->IsSupportedSegment(Path.Last(), Goal)) return false;
		Path.Add(Goal);
	}
	AcceptPath(MoveTemp(Path));
	TargetActor.Reset(); bTrackTarget = false;
	return true;
}
bool UDeckWalkRouteComponent::SetPatrolGoal(FRandomStream& Random)
{
	ClearGoal();
	UDeckWalkAreaComponent* Area = GetArea();
	TArray<FDeckWalkLocation> Path;
	if (!Area || !GetOwner()->HasAuthority() || !Area->PickPatrolPath(*GetOwner(), Random, Path) || Path.IsEmpty()) return false;
	AcceptPath(MoveTemp(Path));
	return true;
}
EDeckWalkRouteTick UDeckWalkRouteComponent::TickRoute(float DeltaSeconds,
	float AcceptanceRadius, float ProgressTimeout, float MaximumMoveTime, float MoveSpeed, float MinimumProgressDistance)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	UDeckWalkAreaComponent* Area = GetArea();
	FDeckWalkLocation Current;
	if (!bHasGoal || !Character || !Character->HasAuthority() || !Area
		|| !Area->IsLocationValid(LocalGoal) || !Area->ResolveActorOnDeck(*Character, Current)
		|| !Character->GetCharacterMovement() || !Character->GetCharacterMovement()->IsMovingOnGround()) return EDeckWalkRouteTick::Failed;
	if (bTrackTarget)
	{
		if (!TargetActor.IsValid()) return EDeckWalkRouteTick::Failed;
		if (GetWorld()->GetTimeSeconds() >= NextActorReplanTime)
		{
			NextActorReplanTime = GetWorld()->GetTimeSeconds() + 0.35;
			FDeckTargetAnchor Target;
			if (UDeckCombatTargetResolverComponent::ResolveFor(GetOwner(), TargetActor.Get(), Target)
				&& (Target.SurfaceId != LocalGoal.SurfaceId
					|| FVector::Dist2D(Target.LocalCenter, LocalGoal.LocalFloor) > 150.f))
				PlanActorGoal(TargetActor.Get()); // A failed replacement leaves the safe route intact.
		}
	}
	if (LocalPath.IsEmpty()) return EDeckWalkRouteTick::Failed;
	const FVector Feet = Area->ToLocal(Area->GetActorFeetWorld(*Character));
	while (PathCursor < LocalPath.Num()
		&& (PathCursor + 1 < LocalPath.Num() || Current.SurfaceId == LocalPath[PathCursor].SurfaceId)
		&& FMath::Abs(Feet.Z - LocalPath[PathCursor].LocalFloor.Z) <= 45.0f
		&& FVector::Dist2D(Feet, LocalPath[PathCursor].LocalFloor)
			<= (PathCursor + 1 == LocalPath.Num() ? FMath::Max(30.0f, AcceptanceRadius) : 25.0f))
	{
		++PathCursor;
		ProgressDistance = TNumericLimits<float>::Max(); TimeSinceProgress = 0.0f;
	}
	if (PathCursor >= LocalPath.Num()) return EDeckWalkRouteTick::Reached;
	const FVector Delta = LocalPath[PathCursor].LocalFloor - Feet;
	if (FMath::Abs(Delta.Z) > 65.0f) return EDeckWalkRouteTick::Failed;
	const float Distance = Delta.Size();
	ElapsedTime += DeltaSeconds; TimeSinceProgress += DeltaSeconds;
	if (Distance < ProgressDistance - FMath::Max(1.0f, MinimumProgressDistance)) { ProgressDistance = Distance; TimeSinceProgress = 0.0f; }
	const float Limit = FMath::Max(MaximumMoveTime, EstimatedMoveTime / FMath::Max(10.0f, MoveSpeed) + 4.0f);
	if (ElapsedTime > Limit || TimeSinceProgress > ProgressTimeout) return EDeckWalkRouteTick::Failed;
	const AEnemyShip* Ship = Cast<AEnemyShip>(Area->GetOwner());
	const UStaticMeshComponent* Frame = Ship ? Ship->GetDeckMeshComplex() : nullptr;
	if (!Frame) return EDeckWalkRouteTick::Failed;
	// Follow the actual floor component; lower-deck agents must not use the upper deck as their base.
	if (UPrimitiveComponent* Floor = Area->GetMovementBase(*Character); Floor && Character->GetMovementBase() != Floor)
	{
		Character->SetBase(Floor);
	}
	Character->AddMovementInput(Frame->GetComponentTransform().TransformVectorNoScale(
		FVector(Delta.X, Delta.Y, 0.0f).GetSafeNormal()), 1.0f);
	return EDeckWalkRouteTick::Moving;
}
