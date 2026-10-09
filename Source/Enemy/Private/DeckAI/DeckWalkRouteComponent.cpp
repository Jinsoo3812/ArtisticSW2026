#include "DeckAI/DeckWalkRouteComponent.h"

#include "DeckAI/DeckWalkAreaComponent.h"
#include "DeckAI/DeckCombatTargetResolverComponent.h"
#include "DeckAI/DeckWaypointMovementInterface.h"
#include "DeckAI/DeckEnemyCombatComponent.h"
#include "Components/CapsuleComponent.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "DrawDebugHelpers.h"
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
	++RouteGeneration; bHasPendingImpact = false;
	bHasGoal = false;
	TargetActor.Reset(); bTrackTarget = false;
	LocalPath.Reset(); LocalGoal = FDeckWalkLocation(); PathCursor = 0;
	ElapsedTime = TimeSinceProgress = EstimatedMoveTime = 0.0f;
	ProgressDistance = TNumericLimits<float>::Max();
}
void UDeckWalkRouteComponent::AcceptPath(TArray<FDeckWalkLocation>&& Path)
{
	++RouteGeneration; bHasPendingImpact = false;
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
	if (bDrawCollisionDebug)
		if (const UDeckWalkAreaComponent* Area = GetArea())
			for (int32 I = 1; I < LocalPath.Num(); ++I)
				DrawDebugLine(GetWorld(), Area->ToWorld(LocalPath[I - 1].LocalFloor),
					Area->ToWorld(LocalPath[I].LocalFloor), FColor::Green, false, 1.f, 0, 3.f);
}

void UDeckWalkRouteComponent::ResetNavigationState()
{
	ClearGoal(); BlockedMoves.Reset(); AvoidanceArea.Reset(); AvoidanceRevision = INDEX_NONE;
	RepeatedAttempts = 0; NextGoalSelectionTime = 0.; bHasProgressAnchor = false;
	LastBlockReason = EDeckWalkBlockReason::None;
	PathQueryBudgetTime = -1.; RemainingPathCollisionQueries = 512;
}

void UDeckWalkRouteComponent::RefreshAvoidanceContext()
{
	UDeckWalkAreaComponent* Area = GetArea();
	if (Area != AvoidanceArea.Get() || (Area && Area->GetRevision() != AvoidanceRevision))
	{
		BlockedMoves.Reset(); RepeatedAttempts = 0; NextGoalSelectionTime = 0.; bHasProgressAnchor = false;
		AvoidanceArea = Area; AvoidanceRevision = Area ? Area->GetRevision() : INDEX_NONE;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	BlockedMoves.RemoveAll([Now, Area](const FBlockedMove& Move)
	{
		return Move.ExpiresAt <= Now || (Move.bHadBlocker && (!Move.Blocker.IsValid()
			|| !Move.Blocker->IsQueryCollisionEnabled() || (Area && FVector::DistSquared2D(
				Area->ToLocal(Move.Blocker->GetComponentLocation()), Move.LocalBlockerOrigin) > FMath::Square(50.f))));
	});
}

bool UDeckWalkRouteComponent::BeginGoalSelection()
{
	RefreshAvoidanceContext();
	if (!GetOwner()->HasAuthority()) return false;
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < NextGoalSelectionTime) return false;
	if (RepeatedAttempts > 0)
	{
		++RepeatedAttempts;
	}
	// Shared by every selection entry point, including normal failures in adjacent BT branches.
	NextGoalSelectionTime = Now + GetGoalSelectionDelay();
	return true;
}

float UDeckWalkRouteComponent::GetGoalSelectionDelay() const
{
	// Spread retries from a crowd without randomly reversing the chosen escape side.
	return (RepeatedAttempts >= 4 ? 0.8f : CollisionReplanInterval) + (GetOwner()->GetUniqueID() % 5) * 0.015f;
}

void UDeckWalkRouteComponent::RecordGoalSelectionFailure()
{
	RepeatedAttempts = FMath::Max(1, RepeatedAttempts);
	NextGoalSelectionTime = FMath::Max(NextGoalSelectionTime, double(GetWorld()->GetTimeSeconds() + GetGoalSelectionDelay()));
	if (!bHasProgressAnchor)
		if (const UDeckWalkAreaComponent* Area = GetArea())
		{ ProgressAnchor = Area->ToLocal(Area->GetActorFeetWorld(*GetOwner())); bHasProgressAnchor = true; }
}

bool UDeckWalkRouteComponent::IsGoalSelectionDelayed() const
{
	const UDeckWalkAreaComponent* Area = GetArea();
	return Area == AvoidanceArea.Get() && Area && Area->GetRevision() == AvoidanceRevision
		&& GetWorld()->GetTimeSeconds() < NextGoalSelectionTime;
}

bool UDeckWalkRouteComponent::IsGoalRecentlyBlocked(const FDeckWalkLocation& Goal) const
{
	const double Now = GetWorld()->GetTimeSeconds();
	for (const FBlockedMove& Move : BlockedMoves)
		if (Move.ExpiresAt > Now && Move.Goal.SurfaceId == Goal.SurfaceId
			&& (Move.Goal.NodeIndex == Goal.NodeIndex
				|| FVector::DistSquared2D(Move.Goal.LocalFloor, Goal.LocalFloor) < FMath::Square(60.f))) return true;
	return false;
}

FVector UDeckWalkRouteComponent::GetPreferredEscapeDirection() const
{
	const double Now = GetWorld()->GetTimeSeconds();
	for (int32 I = BlockedMoves.Num() - 1; I >= 0; --I)
		if (BlockedMoves[I].ExpiresAt > Now) return BlockedMoves[I].EscapeDirection;
	return FVector::ZeroVector;
}

bool UDeckWalkRouteComponent::IsMovementSuspended() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	const UDeckEnemyCombatComponent* Combat = GetOwner()->FindComponentByClass<UDeckEnemyCombatComponent>();
	const AAIController* AI = Character ? Cast<AAIController>(Character->GetController()) : nullptr;
	const UBrainComponent* Brain = AI ? AI->GetBrainComponent() : nullptr;
	const IAbilitySystemInterface* AbilityOwner = Cast<IAbilitySystemInterface>(GetOwner());
	const UAbilitySystemComponent* ASC = AbilityOwner ? AbilityOwner->GetAbilitySystemComponent() : nullptr;
	return !Movement || Movement->HasAnimRootMotion() || Movement->HasRootMotionSources()
		|| (Brain && (Brain->IsPaused() || Brain->IsResourceLocked()))
		|| (ASC && (ASC->HasMatchingGameplayTag(State_Damaged) || ASC->HasMatchingGameplayTag(State_Attacking)
			|| ASC->HasMatchingGameplayTag(State_Control_MovementBlocked) || ASC->HasMatchingGameplayTag(State_Status_Stun)
			|| ASC->HasMatchingGameplayTag(State_Status_Knockback) || ASC->HasMatchingGameplayTag(State_Boss_Busy)))
		|| (Combat && Combat->HasCommittedAttack());
}

bool UDeckWalkRouteComponent::IsOrdinaryWalking() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const IDeckWaypointMovementInterface* Mover = Cast<IDeckWaypointMovementInterface>(GetOwner());
	return bAvoidMovementCollisions && bHasGoal && Character && Character->HasAuthority()
		&& Mover && Mover->CanMoveOnDeck() && Character->GetCharacterMovement()
		&& Character->GetCharacterMovement()->IsMovingOnGround() && !IsMovementSuspended();
}

void UDeckWalkRouteComponent::StopWalkingMovement()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || !Character->HasAuthority()) return;
	Character->ConsumeMovementInputVector();
	if (IsMovementSuspended()) return;
	if (AAIController* AI = Cast<AAIController>(Character->GetController())) AI->StopMovement();
	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement()) Movement->StopMovementImmediately();
}

void UDeckWalkRouteComponent::ObserveMovementImpact(const FHitResult& Hit, const FVector& MoveDelta)
{
	if (!IsOrdinaryWalking() || !Hit.bBlockingHit || (bHasPendingImpact && PendingImpact.bStartPenetrating)) return;
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if ((!Cast<APawn>(Hit.GetActor()) && Movement->IsWalkable(Hit)) || (Hit.bStartPenetrating && !Cast<APawn>(Hit.GetActor())
		&& FVector::DotProduct(Hit.Normal, Character->GetActorUpVector()) >= Movement->GetWalkableFloorZ())) return;
	FVector Direction = MoveDelta.GetSafeNormal();
	if (Direction.IsNearlyZero() && LocalPath.IsValidIndex(PathCursor))
	{
		if (const UDeckWalkAreaComponent* Area = GetArea())
			Direction = (Area->ToWorld(LocalPath[PathCursor].LocalFloor) - Area->GetActorFeetWorld(*Character)).GetSafeNormal();
	}
	if (!Hit.bStartPenetrating && !Cast<APawn>(Hit.GetActor()) && FVector::DotProduct(Direction, Hit.Normal) >= -0.35f) return;
	// Impact after a failed StepUp is observed here; normal floor/step contacts are handled by CMC.
	PendingImpact = Hit; PendingImpactGeneration = RouteGeneration; bHasPendingImpact = true;
}

bool UDeckWalkRouteComponent::TryGetSafePenetrationAdjustment(const FHitResult& Hit, FVector& OutAdjustment) const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const IDeckWaypointMovementInterface* Mover = Cast<IDeckWaypointMovementInterface>(GetOwner());
	if (!bAvoidMovementCollisions || !Hit.bStartPenetrating || !Character || !Character->HasAuthority()
		|| !Mover || !Mover->CanMoveOnDeck() || IsMovementSuspended()
		|| (!bHasGoal && GetPreferredEscapeDirection().IsNearlyZero())) return false;
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const UDeckWalkAreaComponent* Area = GetArea();
	FDeckWalkLocation Current;
	if (!Area || !Capsule || !Movement->IsMovingOnGround()
		|| (!Cast<APawn>(Hit.GetActor()) && Movement->IsWalkable(Hit)) || !Movement->CurrentFloor.IsWalkableFloor()
		|| !Area->ResolveActorOnDeck(*Character, Current)) return false;
	const FVector Up = Capsule->GetUpVector();
	FVector Away = FVector::VectorPlaneProject(Hit.Normal, Up).GetSafeNormal();
	if (Away.IsNearlyZero())
	{
		FVector LocalAway = GetPreferredEscapeDirection();
		if (LocalAway.IsNearlyZero() && LocalPath.IsValidIndex(PathCursor))
			LocalAway = (Area->ToLocal(Area->GetActorFeetWorld(*Character)) - LocalPath[PathCursor].LocalFloor).GetSafeNormal2D();
		Away = (Area->ToWorld(LocalAway) - Area->ToWorld(FVector::ZeroVector)).GetSafeNormal();
	}
	if (Away.IsNearlyZero()) return false;
	const FVector Feet = Area->ToLocal(Area->GetActorFeetWorld(*Character));
	const float Maximum = FMath::Min(60.f, Capsule->GetScaledCapsuleRadius() * 1.5f);
	for (float Length = FMath::Clamp(Hit.PenetrationDepth + 2.f, 5.f, Maximum); Length <= Maximum + 0.1f; Length += 10.f)
	{
		const FVector Adjustment = Away * Length;
		FDeckWalkLocation Exit;
		if (!Area->ResolvePreciseLocalFloor(Area->ToLocal(Area->GetActorFeetWorld(*Character) + Adjustment), Current.SurfaceId, Exit)
			|| FMath::Abs(Exit.LocalFloor.Z - Feet.Z) > 5.f || !Area->IsLocationAvailable(Exit, *Character)) continue;
		// Verify the actual, horizontally adjusted capsule, not only the floor-aligned candidate.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DeckWalkPenetrationExit), false, Character);
		FCollisionResponseParams Responses;
		Capsule->InitSweepCollisionParams(Params, Responses);
		if (!GetWorld()->OverlapBlockingTestByChannel(Capsule->GetComponentLocation() + Adjustment,
			Capsule->GetComponentQuat(), Capsule->GetCollisionObjectType(), Capsule->GetCollisionShape(), Params,
			Responses))
		{
			OutAdjustment = Adjustment; return true;
		}
	}
	return false; // Preserve engine correction when a safe horizontal exit is unavailable.
}

bool UDeckWalkRouteComponent::CanTraverse(const FDeckWalkLocation& From, const FDeckWalkLocation& To) const
{
	const FVector Direction = FVector(To.LocalFloor.X - From.LocalFloor.X, To.LocalFloor.Y - From.LocalFloor.Y, 0.f).GetSafeNormal();
	const double Now = GetWorld()->GetTimeSeconds();
	for (const FBlockedMove& Move : BlockedMoves)
	{
		if (Move.ExpiresAt <= Now || Move.ContactSurface != From.SurfaceId) continue;
		const FVector Closest = FMath::ClosestPointOnSegment(Move.LocalContact, From.LocalFloor, To.LocalFloor);
		if (FVector::DistSquared2D(Closest, Move.LocalContact) < FMath::Square(75.f)
			&& FVector::DotProduct(Direction, Move.EscapeDirection) < -0.25f) return false;
	}
	return true;
}

bool UDeckWalkRouteComponent::InstallPath(TArray<FDeckWalkLocation>&& Path,
	const FDeckWalkLocation& Start, const FDeckWalkLocation& Goal)
{
	const UDeckWalkAreaComponent* Area = GetArea();
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Area || !Character || Path.IsEmpty()) return false;
	// The sampled start center can be behind the blocker; locomotion starts at actual feet.
	Path[0] = Start; Path[0].LocalFloor = Area->ToLocal(Area->GetActorFeetWorld(*Character));
	if (!Path.Last().LocalFloor.Equals(Goal.LocalFloor, 1.f))
	{
		if (!Area->IsSupportedSegment(Path.Last(), Goal) || !CanTraverse(Path.Last(), Goal)) return false;
		FHitResult Hit;
		const FVector Offset = Character->GetActorUpVector() * (Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f);
		if (bAvoidMovementCollisions && Area->TraceMovementSegment(*Character,
			Area->ToWorld(Path.Last().LocalFloor) + Offset, Area->ToWorld(Goal.LocalFloor) + Offset, Hit)) return false;
		Path.Add(Goal);
	}
	AcceptPath(MoveTemp(Path)); return true;
}

EDeckWalkRouteTick UDeckWalkRouteComponent::BlockRoute(const FHitResult& Hit,
	const FDeckWalkLocation& Current, const FVector& LocalDirection, EDeckWalkBlockReason Reason)
{
	const UDeckWalkAreaComponent* Area = GetArea();
	FBlockedMove Move; Move.Goal = LocalGoal; Move.ContactSurface = Current.SurfaceId;
	Move.Blocker = Hit.GetComponent(); Move.bHadBlocker = Move.Blocker.IsValid();
	if (Move.bHadBlocker) Move.LocalBlockerOrigin = Area->ToLocal(Move.Blocker->GetComponentLocation());
	Move.LocalContact = Hit.bBlockingHit ? Area->ToLocal(Hit.Location) : Current.LocalFloor;
	if (Hit.bBlockingHit)
	{
		const FVector Origin = Area->ToWorld(FVector::ZeroVector);
		Move.EscapeDirection = Area->ToLocal(Origin + Hit.Normal) - Area->ToLocal(Origin);
		Move.EscapeDirection.Z = 0.f; Move.EscapeDirection.Normalize();
	}
	if (Move.EscapeDirection.IsNearlyZero()) Move.EscapeDirection = -LocalDirection;
	Move.ExpiresAt = GetWorld()->GetTimeSeconds() + BlockMemorySeconds;
	if (BlockedMoves.Num() >= 6) BlockedMoves.RemoveAt(0);
	BlockedMoves.Add(Move); LastBlockReason = Reason;
	RepeatedAttempts = FMath::Max(1, RepeatedAttempts);
	NextGoalSelectionTime = GetWorld()->GetTimeSeconds() + GetGoalSelectionDelay();
	if (!bHasProgressAnchor) { ProgressAnchor = Area->ToLocal(Area->GetActorFeetWorld(*GetOwner())); bHasProgressAnchor = true; }
	bHasPendingImpact = false;
	UE_LOG(LogTemp, Verbose, TEXT("[DeckWalkBlocked] Enemy=%s Generation=%u Reason=%d Goal=%d Blocker=%s Surface=%s Escape=%s Attempts=%d"),
		*GetNameSafe(GetOwner()), RouteGeneration, int32(Reason), LocalGoal.NodeIndex, *GetNameSafe(Hit.GetComponent()),
		*Current.SurfaceId.ToString(), *Move.EscapeDirection.ToCompactString(), RepeatedAttempts);
	if (bDrawCollisionDebug)
	{
		const FVector Origin = Area->GetActorFeetWorld(*GetOwner());
		const FVector End = Area->ToWorld(Area->ToLocal(Origin) + Move.EscapeDirection * 200.f);
		DrawDebugDirectionalArrow(GetWorld(), Origin, End, 20.f, FColor::Cyan, false, BlockMemorySeconds, 0, 4.f);
		DrawDebugSphere(GetWorld(), Area->ToWorld(LocalGoal.LocalFloor), 30.f, 12, FColor::Red, false, BlockMemorySeconds);
	}
	return EDeckWalkRouteTick::Blocked;
}
bool UDeckWalkRouteComponent::Replan(const FDeckWalkLocation& Goal, bool bCrossSurfaces)
{
	UDeckWalkAreaComponent* Area = GetArea();
	FDeckWalkLocation Start;
	TArray<FDeckWalkLocation> Path;
	RefreshAvoidanceContext();
	if (HasPendingMovementBlock()) return false;
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	FDeckWalkPathConstraints Constraints; Constraints.Requester = Character;
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now != PathQueryBudgetTime) { PathQueryBudgetTime = Now; RemainingPathCollisionQueries = 512; }
	Constraints.RemainingCollisionQueries = &RemainingPathCollisionQueries;
	Constraints.CanTraverse = [this](const FDeckWalkLocation& A, const FDeckWalkLocation& B) { return CanTraverse(A, B); };
	if (!Area || !GetOwner()->HasAuthority() || !Area->ResolveActorOnDeck(*GetOwner(), Start)
		|| IsGoalRecentlyBlocked(Goal)
		|| !Area->FindPath(Start, Goal, Path, bCrossSurfaces, bAvoidMovementCollisions ? &Constraints : nullptr)) return false;
	return InstallPath(MoveTemp(Path), Start, Goal);
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
	if (!BeginGoalSelection()) return false;
	const UDeckWalkAreaComponent* Area = GetArea();
	FDeckTargetAnchor Anchor; FDeckWalkLocation Start;
	if (!Area || !Area->ResolveActorOnDeck(*GetOwner(), Start)
		|| !UDeckCombatTargetResolverComponent::ResolveFor(GetOwner(), Target, Anchor)) return false;
	TArray<FDeckWalkLocation> Candidates; Area->GetReachableLocations(Start, Candidates);
	const FVector Away = GetPreferredEscapeDirection();
	Candidates.Sort([&](const FDeckWalkLocation& A, const FDeckWalkLocation& B)
	{
		const float BiasA = FVector::DotProduct((A.LocalFloor - Start.LocalFloor).GetSafeNormal2D(), Away);
		const float BiasB = FVector::DotProduct((B.LocalFloor - Start.LocalFloor).GetSafeNormal2D(), Away);
		if (!Away.IsNearlyZero() && (BiasA >= 0.35f) != (BiasB >= 0.35f)) return BiasA >= 0.35f;
		return FVector::DistSquared2D(A.LocalFloor, Anchor.LocalCenter) < FVector::DistSquared2D(B.LocalFloor, Anchor.LocalCenter);
	});
	int32 Attempts = 0, EscapeAttempts = 0;
	for (const auto& Candidate : Candidates)
	{
		const ACharacter* Character = Cast<ACharacter>(GetOwner());
		if (Character && Candidate.SurfaceId == Anchor.SurfaceId
			&& FVector::Dist2D(Candidate.LocalFloor, Anchor.LocalCenter) <= 350.f
			&& !IsGoalRecentlyBlocked(Candidate) && Area->IsLocationAvailable(Candidate, *Character))
		{
			if (!Away.IsNearlyZero() && (FVector::Dist2D(Candidate.LocalFloor, Start.LocalFloor) < 75.f
				|| (FVector::DotProduct((Candidate.LocalFloor - Start.LocalFloor).GetSafeNormal2D(), Away) >= 0.35f
					&& ++EscapeAttempts > 6))) continue;
			if (++Attempts > 12) break;
			if (Replan(Candidate)) return true;
		}
	}
	RecordGoalSelectionFailure();
	return false;
}

bool UDeckWalkRouteComponent::SetLocationGoalInDistanceBand(const FDeckWalkLocation& Goal,
	const FVector& Center, float Distance, float Tolerance)
{
	const UDeckWalkAreaComponent* Area = GetArea();
	FDeckWalkLocation Start;
	TArray<FDeckWalkLocation> Path;
	RefreshAvoidanceContext();
	if (HasPendingMovementBlock()) return false;
	FDeckWalkPathConstraints Constraints; Constraints.Requester = Cast<ACharacter>(GetOwner());
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now != PathQueryBudgetTime) { PathQueryBudgetTime = Now; RemainingPathCollisionQueries = 512; }
	Constraints.RemainingCollisionQueries = &RemainingPathCollisionQueries;
	Constraints.CanTraverse = [this](const FDeckWalkLocation& A, const FDeckWalkLocation& B) { return CanTraverse(A, B); };
	if (!GetOwner()->HasAuthority() || !Area || !Area->ResolveActorOnDeck(*GetOwner(), Start)
		|| IsGoalRecentlyBlocked(Goal)
		|| !Area->FindPathInDistanceBand(Start, Goal, Center, Distance, Tolerance, Path,
			bAvoidMovementCollisions ? &Constraints : nullptr) || !InstallPath(MoveTemp(Path), Start, Goal)) return false;
	TargetActor.Reset(); bTrackTarget = false;
	return true;
}
bool UDeckWalkRouteComponent::SetPatrolGoal(FRandomStream& Random)
{
	if (!BeginGoalSelection()) return false;
	ClearGoal();
	UDeckWalkAreaComponent* Area = GetArea();
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	FDeckWalkLocation Start;
	if (!Area || !Character || !Character->HasAuthority() || !Area->ResolveActorOnDeck(*Character, Start)) return false;
	TArray<FDeckWalkLocation> Candidates; Area->GetReachableLocations(Start, Candidates, Area->CanPatrolAcrossSurfaces());
	const FVector Away = GetPreferredEscapeDirection();
	TMap<int32, float> Scores;
	for (const FDeckWalkLocation& Candidate : Candidates)
	{
		const FVector Direction = (Candidate.LocalFloor - Start.LocalFloor).GetSafeNormal2D();
		Scores.Add(Candidate.NodeIndex, Random.FRand() * 0.25f - FVector::DotProduct(Direction, Away));
	}
	Candidates.Sort([&](const FDeckWalkLocation& A, const FDeckWalkLocation& B) { return Scores[A.NodeIndex] < Scores[B.NodeIndex]; });
	int32 Attempts = 0, EscapeAttempts = 0;
	for (const FDeckWalkLocation& Candidate : Candidates)
	{
		if (FVector::Dist2D(Candidate.LocalFloor, Start.LocalFloor) < 250.f || IsGoalRecentlyBlocked(Candidate)
			|| !Area->IsLocationAvailable(Candidate, *Character)) continue;
		if (!Away.IsNearlyZero() && FVector::DotProduct((Candidate.LocalFloor - Start.LocalFloor).GetSafeNormal2D(), Away) >= 0.35f
			&& ++EscapeAttempts > 6) continue;
		if (++Attempts > 12) break;
		if (!Area->TryClaimLocation(Candidate, *Character)) continue;
		if (Replan(Candidate, Area->CanPatrolAcrossSurfaces())) return true;
		Area->ReleaseLocationClaim(Character);
	}
	RecordGoalSelectionFailure();
	return false;
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
	RefreshAvoidanceContext();
	if (bAvoidMovementCollisions && !IsOrdinaryWalking())
	{
		// Paused logic, committed attacks and hit root motion do not accumulate walking stalls.
		bHasPendingImpact = false;
		TimeSinceProgress = 0.f; ProgressDistance = TNumericLimits<float>::Max();
		return EDeckWalkRouteTick::Moving;
	}
	if (Character->GetCharacterMovement()->GetMaxSpeed() <= UE_KINDA_SMALL_NUMBER)
	{
		TimeSinceProgress = 0.f; ProgressDistance = TNumericLimits<float>::Max();
		return EDeckWalkRouteTick::Moving;
	}
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
	if (PathCursor >= LocalPath.Num())
	{
		RepeatedAttempts = 0; bHasProgressAnchor = false;
		return EDeckWalkRouteTick::Reached;
	}
	const FVector Delta = LocalPath[PathCursor].LocalFloor - Feet;
	if (FMath::Abs(Delta.Z) > 65.0f) return EDeckWalkRouteTick::Failed;
	const FVector LocalDirection = FVector(Delta.X, Delta.Y, 0.f).GetSafeNormal();
	if (bHasProgressAnchor && FVector::Dist2D(Feet, ProgressAnchor) >= 100.f)
	{
		RepeatedAttempts = 0; NextGoalSelectionTime = 0.; bHasProgressAnchor = false;
	}
	if (bAvoidMovementCollisions)
	{
		FHitResult Hit;
		if (bHasPendingImpact && PendingImpactGeneration == RouteGeneration)
		{
			Hit = PendingImpact;
		}
		else
		{
			const FVector Direction = (Area->ToWorld(Feet + LocalDirection) - Area->ToWorld(Feet)).GetSafeNormal();
			const float LookAhead = FMath::Min(Delta.Size2D(), FMath::Max(CollisionLookAhead, MoveSpeed * 0.15f));
			Area->TraceMovementSegment(*Character, Character->GetCapsuleComponent()->GetComponentLocation(),
				Character->GetCapsuleComponent()->GetComponentLocation() + Direction * LookAhead, Hit);
		}
		bHasPendingImpact = false;
		if (Hit.bBlockingHit && (Hit.bStartPenetrating || Cast<APawn>(Hit.GetActor())
			|| FVector::DotProduct((Area->ToWorld(Feet + LocalDirection) - Area->ToWorld(Feet)).GetSafeNormal(), Hit.Normal) < -0.35f))
		{
			const APawn* Pawn = Cast<APawn>(Hit.GetActor());
			const EDeckWalkBlockReason Reason = Hit.bStartPenetrating ? EDeckWalkBlockReason::Penetration
				: (Pawn ? (Pawn->IsPlayerControlled() ? EDeckWalkBlockReason::Player : EDeckWalkBlockReason::Pawn)
					: EDeckWalkBlockReason::Geometry);
			return BlockRoute(Hit, Current, LocalDirection, Reason);
		}
	}
	const float Distance = Delta.Size();
	ElapsedTime += DeltaSeconds; TimeSinceProgress += DeltaSeconds;
	const float StallTimeout = bAvoidMovementCollisions ? FMath::Min(ProgressTimeout, 0.3f) : ProgressTimeout;
	const float AllowedSpeed = FMath::Min(MoveSpeed, Character->GetCharacterMovement()->GetMaxSpeed());
	const float ExpectedProgress = FMath::Min(AllowedSpeed * StallTimeout,
		0.5f * Character->GetCharacterMovement()->GetMaxAcceleration() * FMath::Square(StallTimeout)) * 0.4f;
	const float RequiredProgress = bAvoidMovementCollisions
		? FMath::Max(1.f, FMath::Min(MinimumProgressDistance, ExpectedProgress)) : FMath::Max(1.f, MinimumProgressDistance);
	// Slow effects and low acceleration must not look like repeated collisions on an empty deck.
	if (Distance < ProgressDistance - RequiredProgress) { ProgressDistance = Distance; TimeSinceProgress = 0.0f; }
	const float Limit = FMath::Max(MaximumMoveTime, EstimatedMoveTime / FMath::Max(1.0f, AllowedSpeed) + 4.0f);
	if (TimeSinceProgress > StallTimeout)
	{
		if (bAvoidMovementCollisions) return BlockRoute(FHitResult(), Current, LocalDirection, EDeckWalkBlockReason::NoProgress);
		return EDeckWalkRouteTick::Failed;
	}
	if (ElapsedTime > Limit) return EDeckWalkRouteTick::Failed;
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
