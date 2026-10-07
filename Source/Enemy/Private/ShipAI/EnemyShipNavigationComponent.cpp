#include "ShipAI/EnemyShipNavigationComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include "Ship.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipAvoidanceSettings.h"
#include "ShipAI/ShipSwarmSubsystem.h"
#include "Network/SWFinalEncounterDiagnostics.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"
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
	else if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AWaterBody> It(World); It; ++It)
		{
			WaterBodies.Add(*It);
		}
	}
}

void UEnemyShipNavigationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearAllOverrides();
	StopOwnerShip();
	OwnerShip.Reset();
	TargetShip = nullptr;
	WaterBodies.Reset();
	Super::EndPlay(EndPlayReason);
}

void UEnemyShipNavigationComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SW_Navigation_Tick);
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
	if (!bNavigationEnabled || Ship->IsDeathHandled() || Ship->IsStoryGateDormant()
		|| Ship->IsDistanceOptimizationDormant())
	{
		ResetTerrainAvoidance();
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

	UpdateAvoidance(DeltaTime);
	UpdateTerrainAvoidance(DeltaTime);
	ApplyControl(LastNavigationOutput);
}

void UEnemyShipNavigationComponent::OnRep_CurrentState(ENavalCombatState PreviousState)
{
	if (PreviousState != CurrentState)
	{
		OnNavigationStateChanged.Broadcast(PreviousState, CurrentState);
	}
}

void UEnemyShipNavigationComponent::SetNavigationEnabled(bool bEnabled)
{
	bNavigationEnabled = bEnabled;
	if (!bNavigationEnabled)
	{
		ResetAvoidance();
		ResetTerrainAvoidance();
		StopOwnerShip();
	}
}

void UEnemyShipNavigationComponent::SetNavigationProfile(const FEnemyShipNavigationProfile& InProfile)
{
	NavigationProfile = InProfile;
	NavigationProfile.DetectionDistance = FMath::Max(0.0f, NavigationProfile.DetectionDistance);
	NavigationProfile.IdealDistance = FMath::Max(1.0f, NavigationProfile.IdealDistance);
	NavigationProfile.OrbitTolerance = FMath::Max(0.0f, NavigationProfile.OrbitTolerance);
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

	if (bTerrainAvoidanceActive)
	{
		Ship->SetAIControlInput(TerrainMoveInput, TerrainTurnInput);
		return;
	}

	if (bAvoidanceManeuverActive)
	{
		const UEnemyShipAvoidanceSettings* Settings = GetDefault<UEnemyShipAvoidanceSettings>();
		// Ship traffic keeps the navigation turn; static obstacles provide a stable
		// side-step turn. Reverse thrust supplies braking for both cases.
		Ship->SetAIControlInput(
			Settings->ReverseMoveInput,
			bAvoidanceOverridesTurn ? AvoidanceTurnInput : BaseOutput.TurnInput);
		return;
	}

	const float PropulsionMultiplier = BaseOutput.State == ENavalCombatState::Return
		? NavigationProfile.ReturnPropulsionMultiplier
		: 1.0f;
	Ship->SetAIControlInput(BaseOutput.MoveInput, BaseOutput.TurnInput, PropulsionMultiplier);
}

void UEnemyShipNavigationComponent::UpdateAvoidance(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SW_Navigation_Avoidance);
	AEnemyShip* Ship = OwnerShip.Get();
	const bool bCombatNavigation = CurrentState == ENavalCombatState::Approach
		|| CurrentState == ENavalCombatState::Orbit;
	if (!Ship || !bCombatNavigation || HasActiveOverride() || !TargetShip)
	{
		ResetAvoidance();
		return;
	}

	const UEnemyShipAvoidanceSettings* Settings = GetDefault<UEnemyShipAvoidanceSettings>();
	AvoidanceMinimumTimeRemaining = FMath::Max(0.0f, AvoidanceMinimumTimeRemaining - DeltaTime);
	AvoidanceEvaluationAccumulator += DeltaTime;
	const float Interval = FMath::Max(0.05f, Settings->EvaluationInterval);
	if (AvoidanceEvaluationAccumulator < Interval)
	{
		return;
	}
	const float EvaluationElapsed = AvoidanceEvaluationAccumulator;
	AvoidanceEvaluationAccumulator = 0.0f;

	FEnemyShipAvoidanceDecision Decision;
	if (UWorld* World = GetWorld())
	{
		if (UShipSwarmSubsystem* Swarm = World->GetSubsystem<UShipSwarmSubsystem>())
		{
			Decision = Swarm->EvaluateAvoidance(Ship);
		}
	}

	if (Decision.bShouldYield)
	{
		AvoidanceThreatActor = Decision.ThreatActor;
		bAvoidanceOverridesTurn = Decision.bOverrideTurnInput;
		AvoidanceTurnInput = Decision.TurnInput;
		AvoidanceSafeElapsed = 0.0f;
		if (!bAvoidanceManeuverActive)
		{
			bAvoidanceManeuverActive = true;
			AvoidanceMinimumTimeRemaining = FMath::Max(0.0f, Settings->MinimumManeuverTime);
		}
		return;
	}

	if (bAvoidanceManeuverActive)
	{
		AvoidanceSafeElapsed += EvaluationElapsed;
		if (AvoidanceMinimumTimeRemaining <= 0.0f
			&& AvoidanceSafeElapsed >= FMath::Max(0.0f, Settings->ClearConfirmationTime))
		{
			ResetAvoidance();
		}
	}
}

void UEnemyShipNavigationComponent::ResetAvoidance()
{
	bAvoidanceManeuverActive = false;
	bAvoidanceOverridesTurn = false;
	AvoidanceTurnInput = 0.0f;
	AvoidanceMinimumTimeRemaining = 0.0f;
	AvoidanceSafeElapsed = 0.0f;
	AvoidanceThreatActor.Reset();
}

void UEnemyShipNavigationComponent::UpdateTerrainAvoidance(float DeltaTime)
{
	AEnemyShip* Ship = OwnerShip.Get();
	const bool bMovingState = CurrentState == ENavalCombatState::Approach
		|| CurrentState == ENavalCombatState::Orbit || CurrentState == ENavalCombatState::Return;
	if (!Ship || !bMovingState || LastNavigationOutput.MoveInput <= 0.0f
		|| HasActiveOverride() || Ship->IsAnchorDropped() || Ship->IsSinking())
	{
		ResetTerrainAvoidance();
		return;
	}

	const UEnemyShipAvoidanceSettings* Settings = GetDefault<UEnemyShipAvoidanceSettings>();
	TerrainTurnMinimumRemaining = FMath::Max(0.0f, TerrainTurnMinimumRemaining - DeltaTime);
	TerrainOverlapReverseRemaining = FMath::Max(0.0f, TerrainOverlapReverseRemaining - DeltaTime);
	TerrainEvaluationAccumulator += DeltaTime;
	const float Interval = FMath::Max(0.05f, Settings->EvaluationInterval);
	if (TerrainEvaluationAccumulator < Interval)
	{
		return;
	}
	const float EvaluationElapsed = TerrainEvaluationAccumulator;
	TerrainEvaluationAccumulator = 0.0f;

	UWorld* World = GetWorld();
	UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Ship->GetRootComponent());
	if (!World || !Root || !Root->IsRegistered())
	{
		bTerrainAvoidanceActive = true;
		TerrainMoveInput = 0.0f;
		TerrainTurnInput = 0.0f;
		FSWFinalEncounterDiagnostics::Write(TEXT("TerrainAvoidance"), TEXT("QueryUnavailable"),
			FString::Printf(TEXT("Ship=%s Position=%s State=%d"), *Ship->GetPathName(),
				*Ship->GetActorLocation().ToCompactString(), static_cast<int32>(CurrentState)));
		return;
	}

	const FVector Origin = Root->GetComponentLocation();
	const FVector Forward = Ship->GetActorForwardVector().GetSafeNormal2D();
	if (Forward.IsNearlyZero())
	{
		bTerrainAvoidanceActive = true;
		TerrainMoveInput = 0.0f;
		TerrainTurnInput = 0.0f;
		FSWFinalEncounterDiagnostics::Write(TEXT("TerrainAvoidance"), TEXT("DirectionUnavailable"),
			FString::Printf(TEXT("Ship=%s Position=%s"), *Ship->GetPathName(), *Origin.ToCompactString()));
		return;
	}
	const FVector Extent = Root->Bounds.BoxExtent;
	const float ProbeDistance = FMath::Max(Settings->TerrainMinimumProbeDistance,
		Extent.X + Settings->TerrainSideMargin
		+ FMath::Max(0.0f, FVector::DotProduct(Root->GetComponentVelocity(), Forward))
			* Settings->TerrainVelocityHorizon);
	const FVector HalfExtent(FMath::Max(Extent.X, 100.0f),
		FMath::Max(Extent.Y, 100.0f) + Settings->TerrainSideMargin,
		Settings->TerrainHalfHeight);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemyShipTerrainAvoidance), false, Ship);
	for (const TWeakObjectPtr<AActor>& WaterBody : WaterBodies)
	{
		if (WaterBody.IsValid()) Params.AddIgnoredActor(WaterBody.Get());
	}
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	float FreeDistance[3] = { ProbeDistance, ProbeDistance, ProbeDistance };
	AActor* Threat[3] = { nullptr, nullptr, nullptr };
	bool bInitialOverlap = false;
	for (int32 Candidate = 0; Candidate < 3; ++Candidate)
	{
		const float Yaw = Candidate == 0 ? 0.0f : (Candidate == 1 ? 1.0f : -1.0f) * Settings->TerrainProbeYaw;
		const FVector Direction = Forward.RotateAngleAxis(Yaw, FVector::UpVector);
		const FQuat Rotation = FRotationMatrix::MakeFromX(Direction).ToQuat();
		TArray<FHitResult> Hits;
		World->SweepMultiByObjectType(Hits, Origin, Origin + Direction * ProbeDistance,
			Rotation, Objects, FCollisionShape::MakeBox(HalfExtent), Params);
		for (const FHitResult& Hit : Hits)
		{
			AActor* Actor = Hit.GetActor();
			UPrimitiveComponent* Component = Hit.GetComponent();
			if (!Hit.bBlockingHit || !Actor || !Component || Actor == Ship
				|| Actor->IsAttachedTo(Ship) || Actor->IsA(AWaterBody::StaticClass())
				|| Component->GetCollisionProfileName() == TEXT("Water")
				|| Component->IsA<UWaterBodyComponent>())
			{
				continue;
			}
			bool bLandscape = false;
			for (const UClass* Class = Actor->GetClass(); Class; Class = Class->GetSuperClass())
			{
				bLandscape |= Class->GetName().StartsWith(TEXT("Landscape"));
			}
			if (!bLandscape && !Component->IsA<UStaticMeshComponent>()) continue;
			const float Distance = Hit.bStartPenetrating ? 0.0f : Hit.Distance;
			if (Distance < FreeDistance[Candidate])
			{
				FreeDistance[Candidate] = Distance;
				Threat[Candidate] = Actor;
			}
			if (Candidate == 0 && Hit.bStartPenetrating) bInitialOverlap = true;
		}
	}

	const bool bWasActive = bTerrainAvoidanceActive;
	const bool bWasBothBlocked = bTerrainBothSidesBlocked;
	const int32 PreviousSide = TerrainSelectedSide;
	AActor* PreviousThreat = TerrainThreatActor.Get();
	if (FreeDistance[0] >= ProbeDistance)
	{
		TerrainClearElapsed += EvaluationElapsed;
		if (!bTerrainAvoidanceActive || (TerrainTurnMinimumRemaining <= 0.0f
			&& TerrainClearElapsed >= Settings->TerrainClearConfirmationTime))
		{
			ResetTerrainAvoidance();
		}
	}
	else
	{
		TerrainClearElapsed = 0.0f;
		bTerrainAvoidanceActive = true;
		const bool bBothBlocked = FreeDistance[1] < ProbeDistance && FreeDistance[2] < ProbeDistance;
		bTerrainBothSidesBlocked = bBothBlocked;
		int32 PreferredSide = FreeDistance[1] > FreeDistance[2] ? 1 : -1;
		if (FMath::Abs(FreeDistance[1] - FreeDistance[2]) <= Settings->TerrainSideTieDistance)
		{
			PreferredSide = TerrainSelectedSide != 0 ? TerrainSelectedSide
				: (LastNavigationOutput.TurnInput < 0.0f ? -1 : 1);
		}
		if (TerrainSelectedSide == 0 || TerrainTurnMinimumRemaining <= 0.0f)
		{
			if (TerrainSelectedSide != PreferredSide)
			{
				TerrainSelectedSide = PreferredSide;
				TerrainTurnMinimumRemaining = Settings->TerrainMinimumTurnTime;
			}
		}
		TerrainTurnInput = static_cast<float>(TerrainSelectedSide);
		const float SelectedFreeDistance = TerrainSelectedSide > 0 ? FreeDistance[1] : FreeDistance[2];
		TerrainMoveInput = (bBothBlocked || SelectedFreeDistance < ProbeDistance) ? 0.0f
			: FMath::Min(LastNavigationOutput.MoveInput, Settings->TerrainLimitedMoveInput);
		TerrainThreatActor = Threat[0];
		if (bInitialOverlap && !bTerrainOverlapping && !bTerrainOverlapReverseExhausted)
		{
			TerrainOverlapReverseRemaining = Settings->TerrainMinimumTurnTime;
		}
		bTerrainOverlapping = bInitialOverlap;
		if (bInitialOverlap && TerrainOverlapReverseRemaining > 0.0f)
		{
			TerrainMoveInput = Settings->TerrainOverlapReverseInput;
		}
		else if (bInitialOverlap)
		{
			bTerrainOverlapReverseExhausted = true;
		}
		else
		{
			bTerrainOverlapReverseExhausted = false;
		}
	}
	if (bWasActive != bTerrainAvoidanceActive || PreviousSide != TerrainSelectedSide
		|| bWasBothBlocked != bTerrainBothSidesBlocked || PreviousThreat != TerrainThreatActor.Get())
	{
		FSWFinalEncounterDiagnostics::Write(TEXT("TerrainAvoidance"),
			bTerrainAvoidanceActive ? (bTerrainBothSidesBlocked ? TEXT("BothBlocked")
				: (bWasActive ? TEXT("Changed") : TEXT("ThreatStarted"))) : TEXT("Cleared"),
			FString::Printf(TEXT("Ship=%s Position=%s State=%d Forward=%.0f Right=%.0f Left=%.0f Side=%d Threat=%s"),
				*Ship->GetPathName(), *Origin.ToCompactString(), static_cast<int32>(CurrentState),
				FreeDistance[0], FreeDistance[1], FreeDistance[2], TerrainSelectedSide,
				*GetNameSafe(TerrainThreatActor.Get())));
	}
}

void UEnemyShipNavigationComponent::ResetTerrainAvoidance()
{
	bTerrainAvoidanceActive = false;
	bTerrainBothSidesBlocked = false;
	bTerrainOverlapping = false;
	bTerrainOverlapReverseExhausted = false;
	TerrainMoveInput = 0.0f;
	TerrainTurnInput = 0.0f;
	TerrainTurnMinimumRemaining = 0.0f;
	TerrainClearElapsed = 0.0f;
	TerrainOverlapReverseRemaining = 0.0f;
	TerrainSelectedSide = 0;
	TerrainThreatActor.Reset();
}

void UEnemyShipNavigationComponent::StopOwnerShip()
{
	if (AEnemyShip* Ship = OwnerShip.Get())
	{
		Ship->SetAIControlInput(0.0f, 0.0f);
	}
}
