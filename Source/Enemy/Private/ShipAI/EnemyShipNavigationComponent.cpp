#include "ShipAI/EnemyShipNavigationComponent.h"

#include "Ship.h"
#include "ShipAI/EnemyShip.h"
#include "Net/UnrealNetwork.h"

UEnemyShipNavigationComponent::UEnemyShipNavigationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(true);
}

void UEnemyShipNavigationComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UEnemyShipNavigationComponent, NavigationProfile);
	DOREPLIFETIME(UEnemyShipNavigationComponent, TargetShip);
	DOREPLIFETIME(UEnemyShipNavigationComponent, SpawnHomeLocation);
	DOREPLIFETIME(UEnemyShipNavigationComponent, SpawnHomeRotation);
	DOREPLIFETIME(UEnemyShipNavigationComponent, bHasSpawnHomeLocation);
	DOREPLIFETIME(UEnemyShipNavigationComponent, CurrentState);
	DOREPLIFETIME(UEnemyShipNavigationComponent, bNavigationEnabled);
}

void UEnemyShipNavigationComponent::BeginPlay()
{
	Super::BeginPlay();
	OwnerShip = Cast<AEnemyShip>(GetOwner());
	if (OwnerShip.IsValid() && OwnerShip->HasAuthority())
	{
		SpawnHomeLocation = OwnerShip->GetActorLocation();
		SpawnHomeRotation = OwnerShip->GetActorRotation();
		bHasSpawnHomeLocation = true;
	}
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
	TargetShip = nullptr;
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
	FEnemyShipNavigationContext Context = BuildContext();
	if (Context.bHasTarget || CurrentState == ENavalCombatState::Return)
	{
		LostTargetElapsed = 0.0f;
	}
	else
	{
		LostTargetElapsed = FMath::Min(
			LostTargetElapsed + DeltaTime,
			NavigationProfile.LostTargetReturnDelay);
	}
	Context.bReturnRequested = !Context.bHasTarget
		&& LostTargetElapsed >= NavigationProfile.LostTargetReturnDelay;
	LastNavigationOutput = FEnemyShipNavigationModel::Evaluate(CurrentState, NavigationProfile, Context);
	CurrentState = LastNavigationOutput.State;
	if (PreviousState != CurrentState)
	{
		OnNavigationStateChanged.Broadcast(PreviousState, CurrentState);
	}
	if (PreviousState == ENavalCombatState::Return && CurrentState == ENavalCombatState::Idle)
	{
		Ship->ResetAfterReturnToSpawn();
	}

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
	NavigationProfile.ReturnTriggerDistance = FMath::Max(
		NavigationProfile.ReturnArrivalDistance,
		NavigationProfile.ReturnTriggerDistance);
	NavigationProfile.ReturnPropulsionMultiplier = FMath::Max(
		0.0f,
		NavigationProfile.ReturnPropulsionMultiplier);
	NavigationProfile.LostTargetReturnDelay = FMath::Max(0.0f, NavigationProfile.LostTargetReturnDelay);
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

bool UEnemyShipNavigationComponent::GetResolvedHomeLocation(FVector& OutHomeLocation) const
{
	if (bHasSpawnHomeLocation)
	{
		OutHomeLocation = SpawnHomeLocation;
		return true;
	}
	return false;
}

bool UEnemyShipNavigationComponent::GetSpawnHomeTransform(FTransform& OutTransform) const
{
	if (!bHasSpawnHomeLocation)
	{
		return false;
	}
	OutTransform = FTransform(SpawnHomeRotation, SpawnHomeLocation);
	return true;
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
	if (const AShip* Target = TargetShip)
	{
		Context.bHasTarget = true;
		Context.TargetLocation = Target->GetActorLocation();
	}
	FVector ResolvedHomeLocation;
	if (GetResolvedHomeLocation(ResolvedHomeLocation))
	{
		Context.bHasHome = true;
		Context.HomeLocation = ResolvedHomeLocation;
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

void UEnemyShipNavigationComponent::ApplyControl(const FEnemyShipNavigationOutput& BaseOutput)
{
	AEnemyShip* Ship = OwnerShip.Get();
	if (!Ship)
	{
		return;
	}

	if (Ship->IsAnchorDropped())
	{
		Ship->SetAIControlInput(0.0f, 0.0f);
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

	const float PropulsionMultiplier = BaseOutput.State == ENavalCombatState::Return
		? NavigationProfile.ReturnPropulsionMultiplier
		: 1.0f;
	Ship->SetAIControlInput(BaseOutput.MoveInput, BaseOutput.TurnInput, PropulsionMultiplier);
}

void UEnemyShipNavigationComponent::StopOwnerShip()
{
	if (AEnemyShip* Ship = OwnerShip.Get())
	{
		Ship->SetAIControlInput(0.0f, 0.0f);
	}
}
