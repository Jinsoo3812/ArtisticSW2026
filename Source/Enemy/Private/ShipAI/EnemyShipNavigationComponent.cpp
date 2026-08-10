#include "ShipAI/EnemyShipNavigationComponent.h"

#include "Ship.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/ShipSwarmSubsystem.h"

UEnemyShipNavigationComponent::UEnemyShipNavigationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UEnemyShipNavigationComponent::BeginPlay()
{
	Super::BeginPlay();
	OwnerShip = Cast<AEnemyShip>(GetOwner());
	if (!OwnerShip.IsValid())
	{
		SetComponentTickEnabled(false);
	}
}

void UEnemyShipNavigationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearAllOverrides();
	StopOwnerShip();
	OwnerShip.Reset();
	TargetShip.Reset();
	HomeActor.Reset();
	Super::EndPlay(EndPlayReason);
}

void UEnemyShipNavigationComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!OwnerShip.IsValid())
	{
		OwnerShip = Cast<AEnemyShip>(GetOwner());
	}
	AEnemyShip* Ship = OwnerShip.Get();
	if (!Ship || !Ship->HasAuthority())
	{
		return;
	}

	RemoveInvalidOverrides();
	if (!bNavigationEnabled || Ship->IsDeathHandled())
	{
		StopOwnerShip();
		return;
	}

	const ENavalCombatState PreviousState = CurrentState;
	LastNavigationOutput = FEnemyShipNavigationModel::Evaluate(CurrentState, NavigationProfile, BuildContext());
	ApplySquadAvoidance(LastNavigationOutput);
	CurrentState = LastNavigationOutput.State;
	if (PreviousState != CurrentState)
	{
		OnNavigationStateChanged.Broadcast(PreviousState, CurrentState);
	}

	Ship->SetAITarget(TargetShip.Get());
	Ship->SetNavalCombatState(CurrentState);
	Ship->SetMaxActiveCannons(NavigationProfile.MaxActiveCannons);
	ApplyControl(LastNavigationOutput);
}

void UEnemyShipNavigationComponent::SetNavigationEnabled(bool bEnabled)
{
	bNavigationEnabled = bEnabled;
	if (!bNavigationEnabled)
	{
		StopOwnerShip();
	}
}

void UEnemyShipNavigationComponent::SetNavigationProfile(const FEnemyShipNavigationProfile& InProfile)
{
	NavigationProfile = InProfile;
	NavigationProfile.DetectionDistance = FMath::Max(0.0f, NavigationProfile.DetectionDistance);
	NavigationProfile.IdealDistance = FMath::Max(1.0f, NavigationProfile.IdealDistance);
	NavigationProfile.OrbitTolerance = FMath::Max(0.0f, NavigationProfile.OrbitTolerance);
	NavigationProfile.DangerCloseDistance = FMath::Clamp(
		NavigationProfile.DangerCloseDistance,
		0.0f,
		NavigationProfile.IdealDistance);
	NavigationProfile.ReturnArrivalDistance = FMath::Max(0.0f, NavigationProfile.ReturnArrivalDistance);
	NavigationProfile.ForwardInputScale = FMath::Max(0.0f, NavigationProfile.ForwardInputScale);
	NavigationProfile.TurnInputScale = FMath::Max(0.0f, NavigationProfile.TurnInputScale);
	NavigationProfile.MaxActiveCannons = FMath::Max(1, NavigationProfile.MaxActiveCannons);
	NavigationProfile.AvoidanceDecisionInterval = FMath::Max(0.02f, NavigationProfile.AvoidanceDecisionInterval);
	NavigationProfile.AvoidanceSafetyBuffer = FMath::Max(0.0f, NavigationProfile.AvoidanceSafetyBuffer);
}

void UEnemyShipNavigationComponent::SetTargetShip(AShip* InTargetShip)
{
	if (InTargetShip
		&& (InTargetShip == OwnerShip.Get() || InTargetShip->IsEnemyShipForEffects()))
	{
		return;
	}
	TargetShip = InTargetShip;
}

void UEnemyShipNavigationComponent::SetHomeActor(AActor* InHomeActor)
{
	HomeActor = InHomeActor;
}

FEnemyShipNavigationOverrideHandle UEnemyShipNavigationComponent::AcquireOverride(
	UObject* Requester,
	int32 Priority,
	const FEnemyShipNavigationOverrideRequest& Request)
{
	FEnemyShipNavigationOverrideHandle Handle;
	if (!OwnerShip.IsValid())
	{
		OwnerShip = Cast<AEnemyShip>(GetOwner());
	}
	if (!IsValid(Requester) || !OwnerShip.IsValid() || !OwnerShip->HasAuthority())
	{
		return Handle;
	}

	Handle.Id = FGuid::NewGuid();
	FRuntimeOverride& Entry = Overrides.Add(Handle.Id);
	Entry.Requester = Requester;
	Entry.Priority = Priority;
	Entry.Sequence = NextOverrideSequence++;
	Entry.Request = Request;
	return Handle;
}

bool UEnemyShipNavigationComponent::UpdateOverride(
	FEnemyShipNavigationOverrideHandle Handle,
	const FEnemyShipNavigationOverrideRequest& Request)
{
	if (FRuntimeOverride* Entry = Overrides.Find(Handle.Id))
	{
		if (!Entry->Requester.IsValid())
		{
			Overrides.Remove(Handle.Id);
			return false;
		}
		Entry->Request = Request;
		return true;
	}
	return false;
}

bool UEnemyShipNavigationComponent::ReleaseOverride(FEnemyShipNavigationOverrideHandle Handle)
{
	return Overrides.Remove(Handle.Id) > 0;
}

void UEnemyShipNavigationComponent::ReleaseOverridesFor(UObject* Requester)
{
	for (auto It = Overrides.CreateIterator(); It; ++It)
	{
		if (It.Value().Requester.Get() == Requester)
		{
			It.RemoveCurrent();
		}
	}
}

void UEnemyShipNavigationComponent::ClearAllOverrides()
{
	Overrides.Reset();
}

bool UEnemyShipNavigationComponent::HasActiveOverride() const
{
	return FindWinningOverride() != nullptr;
}

FEnemyShipNavigationContext UEnemyShipNavigationComponent::BuildContext() const
{
	FEnemyShipNavigationContext Context;
	if (const AEnemyShip* Ship = OwnerShip.Get())
	{
		Context.ShipLocation = Ship->GetActorLocation();
		Context.ShipForward = Ship->GetActorForwardVector();
		Context.ShipRight = Ship->GetActorRightVector();
	}
	if (const AShip* Target = TargetShip.Get())
	{
		Context.bHasTarget = true;
		Context.TargetLocation = Target->GetActorLocation();
	}
	if (const AActor* Home = HomeActor.Get())
	{
		Context.bHasHome = true;
		Context.HomeLocation = Home->GetActorLocation();
	}
	return Context;
}

void UEnemyShipNavigationComponent::RemoveInvalidOverrides()
{
	for (auto It = Overrides.CreateIterator(); It; ++It)
	{
		if (!It.Value().Requester.IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

const UEnemyShipNavigationComponent::FRuntimeOverride* UEnemyShipNavigationComponent::FindWinningOverride() const
{
	const FRuntimeOverride* Winner = nullptr;
	for (const TPair<FGuid, FRuntimeOverride>& Pair : Overrides)
	{
		const FRuntimeOverride& Candidate = Pair.Value;
		if (!Candidate.Requester.IsValid())
		{
			continue;
		}
		if (!Winner
			|| Candidate.Priority > Winner->Priority
			|| (Candidate.Priority == Winner->Priority && Candidate.Sequence > Winner->Sequence))
		{
			Winner = &Candidate;
		}
	}
	return Winner;
}

void UEnemyShipNavigationComponent::ApplySquadAvoidance(FEnemyShipNavigationOutput& InOutOutput)
{
	AEnemyShip* Ship = OwnerShip.Get();
	UWorld* World = Ship ? Ship->GetWorld() : nullptr;
	if (!World || InOutOutput.State == ENavalCombatState::Idle)
	{
		CachedAvoidanceHeading = InOutOutput.DesiredHeading;
		return;
	}

	const double CurrentTime = World->GetTimeSeconds();
	if (CurrentTime - LastAvoidanceDecisionTime >= NavigationProfile.AvoidanceDecisionInterval
		|| CachedAvoidanceHeading.IsNearlyZero())
	{
		LastAvoidanceDecisionTime = CurrentTime;
		constexpr int32 NumRays = 12;
		static const FVector Rays[NumRays] = {
			FVector(1.000000f, 0.000000f, 0.0f), FVector(0.866025f, 0.500000f, 0.0f),
			FVector(0.500000f, 0.866025f, 0.0f), FVector(0.000000f, 1.000000f, 0.0f),
			FVector(-0.500000f, 0.866025f, 0.0f), FVector(-0.866025f, 0.500000f, 0.0f),
			FVector(-1.000000f, 0.000000f, 0.0f), FVector(-0.866025f, -0.500000f, 0.0f),
			FVector(-0.500000f, -0.866025f, 0.0f), FVector(0.000000f, -1.000000f, 0.0f),
			FVector(0.500000f, -0.866025f, 0.0f), FVector(0.866025f, -0.500000f, 0.0f)
		};

		float Interest[NumRays];
		float Danger[NumRays];
		FMemory::Memzero(Danger, sizeof(Danger));
		for (int32 Index = 0; Index < NumRays; ++Index)
		{
			Interest[Index] = FMath::Max(0.0f, FVector::DotProduct(Rays[Index], InOutOutput.DesiredHeading));
		}

		if (UShipSwarmSubsystem* Swarm = World->GetSubsystem<UShipSwarmSubsystem>())
		{
			const float ShipSize = Ship->BuoyancyRoot
				? Ship->BuoyancyRoot->Bounds.BoxExtent.GetMax()
				: 500.0f;
			for (AEnemyShip* Member : Swarm->GetSquadMembers(Ship->SquadID))
			{
				if (!IsValid(Member) || Member == Ship)
				{
					continue;
				}
				FVector ToMember = Member->GetActorLocation() - Ship->GetActorLocation();
				ToMember.Z = 0.0f;
				const float Distance = ToMember.Size();
				const float MemberSize = Member->BuoyancyRoot
					? Member->BuoyancyRoot->Bounds.BoxExtent.GetMax()
					: 500.0f;
				const float AvoidanceRadius = ShipSize + MemberSize + NavigationProfile.AvoidanceSafetyBuffer;
				if (Distance <= 1.0f || Distance >= AvoidanceRadius)
				{
					continue;
				}

				const FVector Direction = ToMember / Distance;
				const float DangerWeight = FMath::Square(1.0f - Distance / AvoidanceRadius);
				for (int32 Index = 0; Index < NumRays; ++Index)
				{
					Danger[Index] = FMath::Max(
						Danger[Index],
						FMath::Max(0.0f, FVector::DotProduct(Rays[Index], Direction)) * DangerWeight);
				}

				// Preserve the previous head-on tie breaker: when two squad ships
				// approach bow-to-bow, make the port-side rays less attractive so
				// both ships consistently turn to starboard instead of oscillating.
				FVector ShipForward = Ship->GetActorForwardVector();
				ShipForward.Z = 0.0f;
				ShipForward.Normalize();
				if (FVector::DotProduct(ShipForward, Direction) > 0.866f)
				{
					FVector MemberForward = Member->GetActorForwardVector();
					MemberForward.Z = 0.0f;
					MemberForward.Normalize();
					if (FVector::DotProduct(MemberForward, -Direction) > 0.707f)
					{
						FVector ShipRight = Ship->GetActorRightVector();
						ShipRight.Z = 0.0f;
						ShipRight.Normalize();
						for (int32 Index = 0; Index < NumRays; ++Index)
						{
							if (FVector::DotProduct(Rays[Index], ShipRight) < -0.2f)
							{
								Danger[Index] = FMath::Max(Danger[Index], 0.35f * DangerWeight);
							}
						}
					}
				}
			}
		}

		int32 BestIndex = 0;
		float BestScore = Interest[0] - Danger[0];
		for (int32 Index = 1; Index < NumRays; ++Index)
		{
			const float Score = Interest[Index] - Danger[Index];
			if (Score > BestScore)
			{
				BestScore = Score;
				BestIndex = Index;
			}
		}
		CachedAvoidanceHeading = Rays[BestIndex];
	}

	InOutOutput.DesiredHeading = CachedAvoidanceHeading;
	FVector ShipForward = Ship->GetActorForwardVector();
	FVector ShipRight = Ship->GetActorRightVector();
	ShipForward.Z = 0.0f;
	ShipRight.Z = 0.0f;
	ShipForward.Normalize();
	ShipRight.Normalize();
	const float HeadingDot = FVector::DotProduct(ShipForward, InOutOutput.DesiredHeading);
	const float RightDot = FVector::DotProduct(ShipRight, InOutOutput.DesiredHeading);
	InOutOutput.TurnInput = HeadingDot < 0.99f ? (RightDot > 0.0f ? 1.0f : -1.0f) : 0.0f;
	InOutOutput.MoveInput = HeadingDot > 0.0f ? HeadingDot : 0.0f;
	InOutOutput.MoveInput = FMath::Clamp(
		InOutOutput.MoveInput * NavigationProfile.ForwardInputScale, -1.0f, 1.0f);
	InOutOutput.TurnInput = FMath::Clamp(
		InOutOutput.TurnInput * NavigationProfile.TurnInputScale, -1.0f, 1.0f);
}

void UEnemyShipNavigationComponent::ApplyControl(const FEnemyShipNavigationOutput& BaseOutput)
{
	AEnemyShip* Ship = OwnerShip.Get();
	if (!Ship)
	{
		return;
	}

	if (const FRuntimeOverride* Winner = FindWinningOverride())
	{
		const FEnemyShipNavigationOverrideRequest& Request = Winner->Request;
		if (Request.Mode == EEnemyShipNavigationOverrideMode::StopMovement)
		{
			Ship->SetAIControlInput(0.0f, 0.0f);
			return;
		}
		Ship->SetAIControlInput(
			Request.MoveInput,
			Request.TurnInput,
			Request.PropulsionMultiplier,
			Request.TurnMultiplier);
		return;
	}

	Ship->SetAIControlInput(BaseOutput.MoveInput, BaseOutput.TurnInput);
}

void UEnemyShipNavigationComponent::StopOwnerShip()
{
	if (AEnemyShip* Ship = OwnerShip.Get())
	{
		Ship->SetAIControlInput(0.0f, 0.0f);
	}
}
