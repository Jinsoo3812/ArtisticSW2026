#include "BossAI/BossGunMovementComponent.h"
#include "BossAI/GunnerBossEnemy.h"
#include "BossAI/GunnerBossCombatData.h"
#include "BossAI/BossGunCombatComponent.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "AIController.h"
#include "Components/StaticMeshComponent.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "DeckAI/DeckWalkRouteComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ShipAI/EnemyShip.h"

UBossGunMovementComponent::UBossGunMovementComponent() { PrimaryComponentTick.bCanEverTick = false; }
AGunnerBossEnemy* UBossGunMovementComponent::Boss() const { return Cast<AGunnerBossEnemy>(GetOwner()); }

void UBossGunMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	const UGunnerBossCombatData* Data = Boss() ? Boss()->CombatData.Get() : nullptr;
	bSettingsValid = Data && FMath::IsFinite(RetreatEnterDistance) && RetreatEnterDistance >= 0.f
		&& FMath::IsFinite(PreferredDistance) && FMath::IsFinite(ApproachEnterDistance)
		&& RetreatEnterDistance < PreferredDistance && PreferredDistance < ApproachEnterDistance
		&& ApproachEnterDistance < Data->AttackRange && FMath::IsFinite(ReturnBandTolerance) && ReturnBandTolerance > 0.f
		&& PreferredDistance - ReturnBandTolerance > RetreatEnterDistance && PreferredDistance + ReturnBandTolerance < ApproachEnterDistance
		&& FMath::IsFinite(MoveSpeed) && MoveSpeed > 0.f && FMath::IsFinite(ReplanInterval) && ReplanInterval > 0.f
		&& FMath::IsFinite(MinimumTravelDistance) && MinimumTravelDistance > 0.f
		&& FMath::IsFinite(TargetMoveThreshold) && TargetMoveThreshold > 0.f
		&& FMath::IsFinite(TravelScoreWeight) && TravelScoreWeight >= 0.f
		&& AcceptanceRadius > 0.f && MaximumMoveTime > 0.f && ProgressTimeout > 0.f
		&& MinimumProgressDistance > 0.f && MaximumCandidateChecks > 0;
	if (GetOwner()->HasAuthority() && !bSettingsValid)
		UE_LOG(LogTemp, Error, TEXT("GunnerBoss %s: invalid GunMovement settings or missing CombatData. Check distance ordering and return band."), *GetOwner()->GetName());
}

void UBossGunMovementComponent::BeginFocus(AActor* Target)
{
	AGunnerBossEnemy* OwnerBoss = Boss();
	if (!bHaveFocus)
	{
		bHaveFocus = true;
		bOldOrient = OwnerBoss->GetCharacterMovement()->bOrientRotationToMovement;
		bOldDesiredRotation = OwnerBoss->GetCharacterMovement()->bUseControllerDesiredRotation;
		bOldControllerYaw = OwnerBoss->bUseControllerRotationYaw;
		OwnerBoss->GetCharacterMovement()->bOrientRotationToMovement = false;
		OwnerBoss->GetCharacterMovement()->bUseControllerDesiredRotation = true;
		OwnerBoss->bUseControllerRotationYaw = true;
	}
	if (AAIController* AI = Cast<AAIController>(OwnerBoss->GetController())) AI->SetFocus(Target);
}

void UBossGunMovementComponent::Stop()
{
	AGunnerBossEnemy* OwnerBoss = Boss();
	if (!OwnerBoss || !OwnerBoss->HasAuthority()) return;
	if (bHaveFocus)
	{
		if (AAIController* AI = Cast<AAIController>(OwnerBoss->GetController())) AI->ClearFocus(EAIFocusPriority::Gameplay);
		OwnerBoss->GetCharacterMovement()->bOrientRotationToMovement = bOldOrient;
		OwnerBoss->GetCharacterMovement()->bUseControllerDesiredRotation = bOldDesiredRotation;
		OwnerBoss->bUseControllerRotationYaw = bOldControllerYaw;
		bHaveFocus = false;
	}
	if (OwnerBoss->GetDeckWalkRouteComponent()->HasGoal())
	{
		OwnerBoss->ConsumeMovementInputVector();
		OwnerBoss->GetCharacterMovement()->StopMovementImmediately();
		OwnerBoss->SetBaseMovementSpeed(0.f);
		OwnerBoss->ClearDestination();
	}
	PlannedTarget.Reset();
}

bool UBossGunMovementComponent::Plan(AActor* Target, bool bRetreat, bool bApproach)
{
	AGunnerBossEnemy* OwnerBoss = Boss();
	UDeckWalkAreaComponent* Area = OwnerBoss->GetHostShip()->GetDeckWalkAreaComponent();
	UDeckWalkRouteComponent* Route = OwnerBoss->GetDeckWalkRouteComponent();
	FDeckWalkLocation Start;
	FVector Socket;
	if (!Area || !Area->ResolveActorOnDeck(*OwnerBoss, Start) || !OwnerBoss->GunCombat->GetOrigin(Socket)
		|| !Route->BeginGoalSelection()) return false;
	const FVector TargetLocal = Area->ToLocal(Target->GetActorLocation());
	const FVector CurrentLocal = Area->ToLocal(OwnerBoss->GetActorLocation());
	const float CurrentDistance = FVector::Dist2D(CurrentLocal, TargetLocal);
	TArray<FDeckWalkLocation> Candidates;
	Area->GetReachableLocations(Start, Candidates, false);
	Candidates.Sort([&](const FDeckWalkLocation& A, const FDeckWalkLocation& B)
	{
		const auto Score = [&](const FDeckWalkLocation& Point)
		{
			return FMath::Abs(FVector::Dist2D(Point.LocalFloor, TargetLocal) - PreferredDistance)
				+ FVector::Dist2D(Point.LocalFloor, Start.LocalFloor) * TravelScoreWeight;
		};
		const float SA = Score(A), SB = Score(B);
		return SA == SB ? A.NodeIndex < B.NodeIndex : SA < SB;
	});
	const FVector SocketOffset = OwnerBoss->GetActorTransform().InverseTransformPosition(Socket);
	const FVector Up = OwnerBoss->GetHostShip()->GetDeckMeshComplex()->GetUpVector();
	int32 Checks = 0;
	for (const FDeckWalkLocation& Candidate : Candidates)
	{
		const float Distance = FVector::Dist2D(Candidate.LocalFloor, TargetLocal);
		if (Candidate.NodeIndex == Start.NodeIndex || FVector::Dist2D(Candidate.LocalFloor, Start.LocalFloor) < MinimumTravelDistance
			|| (bRetreat && Distance <= CurrentDistance) || (bApproach && Distance >= CurrentDistance)
			|| Distance >= OwnerBoss->CombatData->AttackRange || Route->IsGoalRecentlyBlocked(Candidate)) continue;
		if (++Checks > MaximumCandidateChecks) break;
		FTransform Transform;
		if (!Area->IsLocationAvailable(Candidate, *OwnerBoss) || !Area->ResolveLocationTransform(Candidate, *OwnerBoss, Transform)) continue;
		const FVector Facing = FVector::VectorPlaneProject(Target->GetActorLocation() - Transform.GetLocation(), Up).GetSafeNormal();
		if (Facing.IsNearlyZero()) continue;
		Transform.SetRotation(FRotationMatrix::MakeFromXZ(Facing, Up).ToQuat());
		if (!OwnerBoss->GunCombat->HasClearLineFrom(Target, Transform.TransformPosition(SocketOffset))) continue;
		// This call claims the position and uses the existing collision-aware route search.
		if (!OwnerBoss->TrySetDestinationLocation(Candidate, true)) continue;
		PlannedTargetLocal = TargetLocal;
		PlannedTarget = Target;
		OwnerBoss->SetBase(Area->GetMovementBase(*OwnerBoss));
		OwnerBoss->SetBaseMovementSpeed(MoveSpeed);
		BeginFocus(Target);
		return true;
	}
	Route->RecordGoalSelectionFailure();
	return false;
}

EBossGunMovementResult UBossGunMovementComponent::UpdatePosition(AActor* Target, float DeltaSeconds)
{
	AGunnerBossEnemy* OwnerBoss = Boss();
	if (!bSettingsValid || !OwnerBoss || !OwnerBoss->HasAuthority() || !OwnerBoss->GunCombat->IsRuntimeValid(Target)
		|| OwnerBoss->GunCombat->IsAttackLocked() || OwnerBoss->GetAbilitySystemComponent()->HasMatchingGameplayTag(State_Boss_Busy)
		|| !OwnerBoss->CanMoveOnDeck()) return EBossGunMovementResult::Waiting;
	UDeckWalkAreaComponent* Area = OwnerBoss->GetHostShip()->GetDeckWalkAreaComponent();
	UDeckWalkRouteComponent* Route = OwnerBoss->GetDeckWalkRouteComponent();
	if (!Area || !Area->IsReady() || !OwnerBoss->GetCharacterMovement()->IsMovingOnGround()) return EBossGunMovementResult::Waiting;
	const float Distance = FVector::Dist2D(Area->ToLocal(OwnerBoss->GetActorLocation()), Area->ToLocal(Target->GetActorLocation()));
	const bool bClear = OwnerBoss->GunCombat->HasClearLine(Target);
	const bool bRetreat = Distance < RetreatEnterDistance;
	const bool bApproach = Distance > ApproachEnterDistance;
	const double Now = GetWorld()->GetTimeSeconds();
	if (Route->HasGoal())
	{
		if (bClear && FMath::Abs(Distance - PreferredDistance) <= ReturnBandTolerance)
		{
			Stop();
			return EBossGunMovementResult::Ready;
		}
		if (PlannedTarget.Get() != Target || (Now >= NextPlanTime
			&& FVector::Dist2D(PlannedTargetLocal, Area->ToLocal(Target->GetActorLocation())) >= TargetMoveThreshold))
		{
			Stop();
		}
		else
		{
			BeginFocus(Target);
			const EDeckWalkRouteTick Result = Route->TickRoute(DeltaSeconds, AcceptanceRadius, ProgressTimeout,
				MaximumMoveTime, MoveSpeed, MinimumProgressDistance);
			if (Result == EDeckWalkRouteTick::Moving) return EBossGunMovementResult::Moving;
			Stop();
			NextPlanTime = Now + ReplanInterval;
			return bClear ? EBossGunMovementResult::Ready : EBossGunMovementResult::Waiting;
		}
	}
	if (!bRetreat && !bApproach && bClear) return EBossGunMovementResult::Ready;
	if (Now < NextPlanTime) return bClear ? EBossGunMovementResult::Ready : EBossGunMovementResult::Waiting;
	NextPlanTime = Now + ReplanInterval;
	if (Plan(Target, bRetreat, bApproach)) return EBossGunMovementResult::Moving;
	// A trapped boss with a clear shot still fights instead of retrying retreat forever.
	return bClear ? EBossGunMovementResult::Ready : EBossGunMovementResult::Waiting;
}

void UBossGunMovementComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Stop();
	Super::EndPlay(EndPlayReason);
}
