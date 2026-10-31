#include "ShipAI/EnemyShipNavigationComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include "Ship.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipAvoidanceSettings.h"
#include "ShipAI/EnemyShipLocalSafety.h"
#include "ShipAI/Abilities/GA_EnemyShipCharge.h"
#include "HAL/IConsoleManager.h"
#include "ShipAI/ShipSwarmSubsystem.h"
#include "Network/SWFinalEncounterDiagnostics.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"
#include "Net/UnrealNetwork.h"
#include "Room/SWRoomSnapshotSubsystem.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/ScopeExit.h"

namespace
{
	TAutoConsoleVariable<int32> CVarEnemyShipCombatLog(TEXT("sw.EnemyShipSafety.CombatLog"),
		0,
		TEXT("Combat ships only: 0=off, 1=events (8/s) + 1Hz snapshots, 2=verbose (32/s)."), ECVF_Default);
	TAutoConsoleVariable<int32> CVarEnemyShipSafetyDebug(TEXT("sw.EnemyShipSafety.Debug"), 0,
		TEXT("Log local ship safety transitions."), ECVF_Default);
}

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
	DOREPLIFETIME(UEnemyShipNavigationComponent, HomeAlignment);
}

void UEnemyShipNavigationComponent::BeginPlay()
{
	Super::BeginPlay();
	OwnerShip = Cast<AEnemyShip>(GetOwner());
	if (OwnerShip.IsValid())
	{
		OwnerShip->PrimaryActorTick.AddPrerequisite(this, PrimaryComponentTick);
		SafetyRandom.Initialize(GetTypeHash(OwnerShip->GetFName()));
	}
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
	ResetLocalSafety();
	if (OwnerShip.IsValid()) OwnerShip->PrimaryActorTick.RemovePrerequisite(this, PrimaryComponentTick);
	StopOwnerShip();
	OwnerShip.Reset();
	TargetShip = nullptr;
	WaterBodies.Reset();
	Super::EndPlay(EndPlayReason);
}

void UEnemyShipNavigationComponent::ResetSpawnHome(const FTransform& Transform)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	if (!OwnerShip.IsValid()) OwnerShip = Cast<AEnemyShip>(GetOwner());
	if (!OwnerShip.IsValid()) return;
	HomeAlignment.bActive = false;
	bAlignmentPhysicsSuspended = false;
	SpawnHomeLocation = Transform.GetLocation();
	SpawnHomeRotation = Transform.Rotator();
	bHasSpawnHomeLocation = true;
	SetTargetShip(nullptr);
	ClearAllOverrides();
	CurrentState = ENavalCombatState::Idle;
	LostTargetElapsed = 0.0f;
	ResetAvoidance();
	ResetTerrainAvoidance();
	ResetLocalSafety();
	const FName SafetySeedName = OwnerShip->GetBundleAuthoredName().IsNone() ? OwnerShip->GetFName() : OwnerShip->GetBundleAuthoredName();
	SafetyRandom.Initialize(GetTypeHash(SafetySeedName));
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
	if (!Ship) return;
	const double CombatTickStart = IsCombatSafetyLogEnabled() ? FPlatformTime::Seconds() : 0.0;
	ON_SCOPE_EXIT
	{
		if (CombatTickStart > 0.0) CombatLastTickMs = (FPlatformTime::Seconds() - CombatTickStart) * 1000.0;
	};
	TickCombatSafetyDiagnostics(DeltaTime);
	if (HomeAlignment.bActive)
	{
		TickHomeAlignment(DeltaTime);
		return;
	}
	if (!Ship->HasAuthority())
	{
		return;
	}

	RemoveInvalidOverrides();
	const auto* Room = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>();
	if (!bNavigationEnabled || Ship->IsDeathHandled() || Ship->IsSinking() || Ship->IsStoryGateDormant()
		|| Ship->IsDistanceOptimizationDormant() || Ship->IsBundlePoolIdle() || Ship->IsPlayerControlled()
		|| (Room && Room->IsRestoringSnapshot()))
	{
		ResetLocalSafety();
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
	if (PreviousState == ENavalCombatState::Return && CurrentState == ENavalCombatState::Idle)
	{
		FTransform Home;
		const bool bHasHome = GetSpawnHomeTransform(Home);
		Home.SetScale3D(Ship->GetActorScale3D());
		if (bHasHome && !Ship->GetActorTransform().Equals(Home, 0.1f))
		{
			CurrentState = ENavalCombatState::Return;
			LastNavigationOutput.State = ENavalCombatState::Return;
			if (!IsHomePathSafe(Ship->GetActorTransform(), Home))
			{
				bPendingHomeAlignmentSafety = true;
				if (SafetyState == EEnemyShipLocalSafetyState::Normal) EnterSafetyWait();
				ApplyControl(LastNavigationOutput, DeltaTime);
				return;
			}
			ResetLocalSafety();
			HomeAlignment.bActive = true;
			HomeAlignment.bRestorePhysics = Ship->IsShipRuntimePhysicsEnabled();
			HomeAlignment.Start = Ship->GetActorTransform();
			HomeAlignment.Home = Home;
			HomeAlignment.Duration = FMath::Max(0.0f, HomeAlignmentDuration);
			HomeAlignment.Elapsed = 0.0f;
			OnRep_HomeAlignment();
			Ship->ForceNetUpdate();
			/* Diagnostics muted after pooling/crew verification.
			UE_LOG(LogTemp, Display, TEXT("[EnemyShipPoolAlignment] Event=Begin Ship=%s Duration=%.2f DistanceCM=%.2f"),
				*Ship->GetBundleAuthoredName().ToString(), HomeAlignment.Duration,
				FVector::Distance(Home.GetLocation(), HomeAlignment.Start.GetLocation())); */ {}
			return;
		}
	}
	if (PreviousState != CurrentState)
	{
		OnNavigationStateChanged.Broadcast(PreviousState, CurrentState);
	}
	if (PreviousState == ENavalCombatState::Return && CurrentState == ENavalCombatState::Idle)
	{
		Ship->ResetAfterReturnToSpawn(false);
	}

	ApplyControl(LastNavigationOutput, DeltaTime);
}

void UEnemyShipNavigationComponent::OnRep_CurrentState(ENavalCombatState PreviousState)
{
	if (PreviousState != CurrentState)
	{
		OnNavigationStateChanged.Broadcast(PreviousState, CurrentState);
	}
}

void UEnemyShipNavigationComponent::OnRep_HomeAlignment()
{
	AlignmentClientReceivedTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	if (AEnemyShip* Ship = Cast<AEnemyShip>(GetOwner()))
	{
		if (HomeAlignment.bActive)
		{
			Ship->SetShipRuntimePhysicsEnabled(false);
			bAlignmentPhysicsSuspended = true;
		}
		else if (bAlignmentPhysicsSuspended)
		{
			if (!Ship->IsBundlePoolIdle() && !Ship->IsDistanceOptimizationDormant() && !Ship->IsStoryGateDormant())
				Ship->SetShipRuntimePhysicsEnabled(HomeAlignment.bRestorePhysics);
			bAlignmentPhysicsSuspended = false;
		}
	}
}

bool UEnemyShipNavigationComponent::IsHomePathSafe(const FTransform& Start, const FTransform& End) const
{
	AEnemyShip* Ship = OwnerShip.Get();
	return Ship && EnemyShipLocalSafety::FQuery(*Ship, WaterBodies).CheckTransform(Start, End).bSafe;
}

void UEnemyShipNavigationComponent::TickHomeAlignment(float DeltaTime)
{
	AEnemyShip* Ship = Cast<AEnemyShip>(GetOwner());
	if (!Ship) return;
	if (const auto* Room = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>(); Room && Room->IsRestoringSnapshot()) return;
	if (Ship->IsDeathHandled() || Ship->IsSinking() || Ship->IsStoryGateDormant() || Ship->IsBundlePoolIdle())
	{
		if (Ship->HasAuthority())
		{
			HomeAlignment.bActive = false;
			OnRep_HomeAlignment();
			Ship->ForceNetUpdate();
		}
		return;
	}
	if (!bAlignmentPhysicsSuspended) OnRep_HomeAlignment();
	// Replicated movement/physics notifications can arrive during the alignment.
	// Reassert the kinematic body while this finite movement owns its transform.
	Ship->SetShipRuntimePhysicsEnabled(false);
	StopOwnerShip();
	const float Elapsed = Ship->HasAuthority() ? (HomeAlignment.Elapsed + DeltaTime)
		: HomeAlignment.Elapsed + GetWorld()->GetTimeSeconds() - AlignmentClientReceivedTime;
	const float Alpha = FMath::Clamp(Elapsed / FMath::Max(UE_SMALL_NUMBER, HomeAlignment.Duration), 0.0f, 1.0f);
	const float SmoothAlpha = Alpha * Alpha * (3.0f - 2.0f * Alpha);
	const FTransform Transform(FQuat::Slerp(HomeAlignment.Start.GetRotation(), HomeAlignment.Home.GetRotation(), SmoothAlpha),
		FMath::Lerp(HomeAlignment.Start.GetLocation(), HomeAlignment.Home.GetLocation(), SmoothAlpha),
		FMath::Lerp(HomeAlignment.Start.GetScale3D(), HomeAlignment.Home.GetScale3D(), SmoothAlpha));
	if (Ship->HasAuthority() && !IsHomePathSafe(Ship->GetActorTransform(), Transform))
	{
		HomeAlignment.bActive = false;
		OnRep_HomeAlignment();
		CurrentState = ENavalCombatState::Return;
		bPendingHomeAlignmentSafety = true;
		EnterSafetyWait();
		Ship->ForceNetUpdate();
		return;
	}
	if (Ship->HasAuthority()) HomeAlignment.Elapsed = Elapsed;
	Ship->SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	if (Ship->HasAuthority() && Alpha >= 1.0f)
	{
		HomeAlignment.bActive = false;
		CurrentState = ENavalCombatState::Idle;
		LastNavigationOutput.State = ENavalCombatState::Idle;
		ResetLocalSafety();
		Ship->ResetAfterReturnToSpawn(false);
		OnRep_HomeAlignment();
		OnNavigationStateChanged.Broadcast(ENavalCombatState::Return, CurrentState);
		Ship->ForceNetUpdate();
		/* Diagnostics muted after pooling/crew verification.
		UE_LOG(LogTemp, Display, TEXT("[EnemyShipPoolAlignment] Event=Complete Ship=%s"), *Ship->GetBundleAuthoredName().ToString()); */ {}
	}
}

void UEnemyShipNavigationComponent::SetNavigationEnabled(bool bEnabled)
{
	bNavigationEnabled = bEnabled;
	if (!bNavigationEnabled)
	{
		ResetLocalSafety();
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

bool UEnemyShipNavigationComponent::IsCombatSafetyLogEnabled() const
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	const AEnemyShip* Ship = OwnerShip.Get();
	return CVarEnemyShipCombatLog.GetValueOnGameThread() > 0 && Ship && IsValid(TargetShip.Get())
		&& (CurrentState == ENavalCombatState::Approach || CurrentState == ENavalCombatState::Orbit)
		&& bNavigationEnabled && !Ship->IsDeathHandled() && !Ship->IsSinking() && !Ship->IsCrewDefeated()
		&& !Ship->IsStoryGateDormant() && !Ship->IsDistanceOptimizationDormant() && !Ship->IsBundlePoolIdle()
		&& !Ship->IsPlayerControlled();
#else
	return false;
#endif
}

void UEnemyShipNavigationComponent::LogCombatSafety(const TCHAR* Event, const TCHAR* Reason, const FString& Details, bool bSnapshot)
{
	if (!IsCombatSafetyLogEnabled()) return;
	AEnemyShip* Ship = OwnerShip.Get();
	const double Now = FPlatformTime::Seconds();
	if (Now - CombatLogWindowStart >= 1.0) { CombatLogWindowStart = Now; CombatLogLines = 0; }
	const int32 Limit = CVarEnemyShipCombatLog.GetValueOnGameThread() >= 2 ? 32 : 8;
	if (!bSnapshot && CombatLogLines >= Limit) { ++CombatSuppressedLines; return; }
	if (!bSnapshot) ++CombatLogLines;
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	const double ServerTime = GameState ? GameState->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.0);
	const FVector V = Ship->BuoyancyRoot ? Ship->BuoyancyRoot->GetPhysicsLinearVelocity() : Ship->GetVelocity();
	const float W = Ship->BuoyancyRoot ? Ship->BuoyancyRoot->GetPhysicsAngularVelocityInDegrees().Z : 0.0f;
	UE_LOG(LogTemp, Log, TEXT("[EnemyShipCombat] Side=%s UTC=%s Mono=%.6f ServerTime=%.3f HasGameState=%d Frame=%llu Ship=%s Bundle=%s Squad=%s Event=%s Reason=%s Nav=%d Safety=%d Action=%d Pos=%s V=%s Speed=%.1f Yaw=%.2f YawRate=%.2f DtMs=%.2f WallGapMs=%.2f PrevNavTickMs=%.3f BuildMs=%.3f ChecksMs=%.3f Checks=%d GTMove=%.3f GTTurn=%.3f GTMode=%d GTControlAuthority=%d Retry=%.3f Resume=%.3f ForwardResume=%.3f Suppressed=%llu %s"),
		Ship->HasAuthority() ? TEXT("Server") : TEXT("Client"), *FDateTime::UtcNow().ToIso8601(), Now, ServerTime, GameState != nullptr,
		GFrameCounter, *Ship->GetName(), *Ship->GetBundleAuthoredName().ToString(), *Ship->SquadID.ToString(), Event, Reason,
		static_cast<int32>(CurrentState), static_cast<int32>(SafetyState), static_cast<int32>(EscapeAction),
		*Ship->GetActorLocation().ToCompactString(), *V.ToCompactString(), V.Size2D(), Ship->GetActorRotation().Yaw, W,
		CombatDeltaTimeMs, CombatWallGapMs, CombatLastTickMs, CombatQueryBuildMs, CombatChecksMs, CombatCheckCount,
		Ship->GetCurrentMoveInput(), Ship->GetCurrentTurnInput(), static_cast<int32>(Ship->GetAISafetyControl().Mode),
		Ship->HasAuthority(),
		RetryRemaining, SafeResumeElapsed, ForwardResumeElapsed, CombatSuppressedLines, *Details);
}

void UEnemyShipNavigationComponent::TickCombatSafetyDiagnostics(float DeltaTime)
{
	if (!IsCombatSafetyLogEnabled())
	{
		CombatLastTickWall = CombatLastSnapshotWall = 0;
		bCombatAppliedControlValid = false;
		return;
	}
	const double Now = FPlatformTime::Seconds();
	CombatDeltaTimeMs = DeltaTime * 1000.0f;
	CombatWallGapMs = CombatLastTickWall > 0 ? (Now - CombatLastTickWall) * 1000.0 : 0;
	CombatLastTickWall = Now;
	if (CombatWallGapMs > 100.0f || CombatDeltaTimeMs > 100.0f)
		LogCombatSafety(TEXT("TickGap"), TEXT("CombatTickOver100ms"));
	if (Now - CombatLastSnapshotWall >= 1.0)
	{
		CombatLastSnapshotWall = Now;
		const FRuntimeOverride* Override = FindWinningOverride();
		LogCombatSafety(TEXT("Snapshot"), TEXT("Periodic"), FString::Printf(TEXT("Target=%s Physics=%d PendingHome=%d EvalAccum=%.3f EscapeElapsed=%.3f ProgressElapsed=%.3f ProgressDistance=%.1f Turned=%.2f NetRole=%d LastCheck=%s LastResume=%s LastBlocker=%s Override=%s OverridePriority=%d"),
			*GetNameSafe(TargetShip.Get()), OwnerShip->BuoyancyRoot && OwnerShip->BuoyancyRoot->IsSimulatingPhysics(),
			bPendingHomeAlignmentSafety, SafetyEvaluationAccumulator, EscapeElapsed, ProgressElapsed,
			FVector::Dist2D(ProgressStart, OwnerShip->GetActorLocation()), EscapeTurned,
			static_cast<int32>(OwnerShip->GetLocalRole()), CombatLastCheckReason, CombatLastResumeReason,
			*GetNameSafe(CombatLastBlocker.Get()), Override ? *GetNameSafe(Override->Requester.Get()) : TEXT("None"),
			Override ? Override->Priority : 0), true);
	}
}

UEnemyShipNavigationComponent::FRequestedControl UEnemyShipNavigationComponent::ResolveRequestedControl(
	const FEnemyShipNavigationOutput& BaseOutput) const
{
	FRequestedControl Result;
	if (const FRuntimeOverride* Winner = FindWinningOverride())
	{
		Result = Winner->Request;
		if (Result.Mode == EEnemyShipNavigationOverrideMode::StopMovement) Result.MoveInput = Result.TurnInput = 0;
	}
	else
	{
		Result.MoveInput = BaseOutput.MoveInput;
		Result.TurnInput = BaseOutput.TurnInput;
		Result.PropulsionMultiplier = BaseOutput.State == ENavalCombatState::Return ? NavigationProfile.ReturnPropulsionMultiplier : 1.0f;
	}
	return Result;
}

void UEnemyShipNavigationComponent::ResetLocalSafety()
{
	const EEnemyShipLocalSafetyState PreviousSafety = SafetyState;
	SafetyState = EEnemyShipLocalSafetyState::Normal;
	RetryRemaining = SafeResumeElapsed = ForwardResumeElapsed = 0;
	SafetyEvaluationAccumulator = EscapeReconsiderAccumulator = ProgressElapsed = EscapeElapsed = EscapeTurned = 0;
	bPendingHomeAlignmentSafety = false;
	bPreferLeftEscape = false;
	if (AEnemyShip* Ship = OwnerShip.Get()) Ship->SetAISafetyControl({});
	if (PreviousSafety != EEnemyShipLocalSafetyState::Normal)
		LogCombatSafety(TEXT("Reset"), TEXT("ResetLocalSafety"), FString::Printf(TEXT("FromSafety=%d"), static_cast<int32>(PreviousSafety)));
}

void UEnemyShipNavigationComponent::EnterSafetyWait(const TCHAR* Reason)
{
	const auto* S = GetDefault<UEnemyShipAvoidanceSettings>();
	const float Minimum = EnemyShipLocalSafety::Value(S->RetryMinimum, 0.3f, 0.05f, 10);
	const float Maximum = EnemyShipLocalSafety::Value(S->RetryMaximum, 1, 0.05f, 10);
	if (SafetyState == EEnemyShipLocalSafetyState::Escaping)
		bPreferLeftEscape = EscapeAction == EEnemyShipEscapeAction::ForwardRight;
	SafetyState = EEnemyShipLocalSafetyState::Waiting;
	RetryRemaining = SafetyRandom.FRandRange(FMath::Min(Minimum, Maximum), FMath::Max(Minimum, Maximum));
	SafeResumeElapsed = ForwardResumeElapsed = 0;
	if (AEnemyShip* Ship = OwnerShip.Get())
	{
		FShipAISafetyControl Hold;
		Hold.Mode = EShipAISafetyMode::Hold;
		Ship->SetAISafetyControl(Hold);
		Ship->SetAIControlInput(0, 0);
		LogCombatSafety(TEXT("Wait"), Reason);
		#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
		if (IsCombatSafetyLogEnabled() && CVarEnemyShipSafetyDebug.GetValueOnGameThread()) UE_LOG(LogTemp, Log, TEXT("[EnemyShipSafety] Waiting Ship=%s Retry=%.2f"), *Ship->GetName(), RetryRemaining);
		#endif
	}
}

void UEnemyShipNavigationComponent::NotifyChargeTerrainStall()
{
	ResetLocalSafety();
	EnterSafetyWait(TEXT("ChargeTerrainStall"));
}

UEnemyShipNavigationComponent::FRequestedControl UEnemyShipNavigationComponent::EscapeControl(EEnemyShipEscapeAction Action) const
{
	const auto* S = GetDefault<UEnemyShipAvoidanceSettings>();
	FRequestedControl Control;
	Control.MoveInput = Action == EEnemyShipEscapeAction::Reverse
		? EnemyShipLocalSafety::Value(S->EscapeReverseInput, -0.35f, -1, -0.01f)
		: EnemyShipLocalSafety::Value(S->EscapeMoveInput, 0.35f, 0.01f, 1);
	const float Turn = EnemyShipLocalSafety::Value(S->EscapeTurnInput, 0.5f, 0.01f, 1);
	Control.TurnInput = Action == EEnemyShipEscapeAction::ForwardRight ? Turn : Action == EEnemyShipEscapeAction::ForwardLeft ? -Turn : 0;
	return Control;
}

void UEnemyShipNavigationComponent::ApplyControl(const FEnemyShipNavigationOutput& BaseOutput, float DeltaTime)
{
	AEnemyShip* Ship = OwnerShip.Get();
	if (!Ship) return;
	using namespace EnemyShipLocalSafety;
	const auto* S = GetDefault<UEnemyShipAvoidanceSettings>();
	FRequestedControl Requested = ResolveRequestedControl(BaseOutput);
	const FRuntimeOverride* Winner = FindWinningOverride();
	const auto* Charge = Winner ? Cast<UGA_EnemyShipCharge>(Winner->Requester.Get()) : nullptr;
	if (Charge && Charge->IsChargeStarted())
	{
		if (IsCombatSafetyLogEnabled() && CVarEnemyShipCombatLog.GetValueOnGameThread() >= 2
			&& FPlatformTime::Seconds() - CombatLastSnapshotWall < 0.05)
			LogCombatSafety(TEXT("ControlBypass"), TEXT("ActiveCharge"));
		ResetLocalSafety();
		Ship->SetAIControlInput(Requested.MoveInput, Requested.TurnInput, Requested.PropulsionMultiplier, Requested.TurnMultiplier);
		return;
	}
	if (Ship->IsAnchorDropped() || Ship->IsPropulsionSuppressed())
	{
		if (IsCombatSafetyLogEnabled() && FPlatformTime::Seconds() - CombatLastSnapshotWall < 0.05)
			LogCombatSafety(TEXT("ControlBlocked"), Ship->IsAnchorDropped() ? TEXT("Anchor") : TEXT("PropulsionSuppressed"));
		Ship->SetAIControlInput(0, 0);
		return;
	}
	const float Interval = Value(S->SafetyEvaluationInterval, 0.1f, 0.02f, 0.5f);
	SafetyEvaluationAccumulator += DeltaTime;
	RetryRemaining = FMath::Max(0.0f, RetryRemaining - DeltaTime);
	const bool bIdle = CurrentState == ENavalCombatState::Idle && !Winner && !bPendingHomeAlignmentSafety;
	if (bIdle && SafetyState != EEnemyShipLocalSafetyState::Normal) ResetLocalSafety();
	if (SafetyEvaluationAccumulator >= Interval
		&& !(SafetyState == EEnemyShipLocalSafetyState::Waiting && RetryRemaining > 0))
	{
		const float Elapsed = SafetyEvaluationAccumulator;
		SafetyEvaluationAccumulator = 0;
		const bool bDiagnostics = IsCombatSafetyLogEnabled();
		const double QueryStart = bDiagnostics ? FPlatformTime::Seconds() : 0.0;
		FQuery Query(*Ship, WaterBodies);
		if (bDiagnostics)
		{
			CombatQueryBuildMs = (FPlatformTime::Seconds() - QueryStart) * 1000.0;
			CombatChecksMs = 0;
			CombatCheckCount = 0;
		}
		auto Check = [&](const FRequestedControl& Control, bool bEscape, float Margin, const TCHAR* Label, bool bCandidate = false)
		{
			const double CheckStart = bDiagnostics ? FPlatformTime::Seconds() : 0.0;
			const FResult Result = Query.Check(Control, bEscape, Margin);
			if (bDiagnostics)
			{
				const double CheckMs = (FPlatformTime::Seconds() - CheckStart) * 1000.0;
				CombatChecksMs += CheckMs;
				++CombatCheckCount;
				CombatLastCheckReason = Result.Reason;
				if (!Result.bSafe) CombatLastBlocker = Result.Actor;
				if (bCandidate || (!Result.bSafe && !FCString::Strcmp(Label, TEXT("AppliedPath")))
					|| CVarEnemyShipCombatLog.GetValueOnGameThread() >= 2)
				{
					LogCombatSafety(TEXT("PathCheck"), Result.Reason, FString::Printf(TEXT("Label=%s Safe=%d Escape=%d Move=%.3f Turn=%.3f PropScale=%.3f TurnScale=%.3f Ms=%.3f Threat=%d Blocker=%s BlockerPos=%s BlockerV=%s Component=%s Segment=%d/%d MaxSegments=%d AtTime=%.3f Peers=%d Tested=%d Sweeps=%d InitialGap=%.2f PrevGap=%.2f Gap=%.2f Envelope=%.2f Margin=%.2f HitDistance=%.2f StartPenetrating=%d Depth=%.2f ForecastAccel=%.2f ForecastYawRate=%.2f ForecastYawAccel=%.2f LinearDamping=%.3f AngularDamping=%.3f HullExtent=%s"),
						Label, Result.bSafe, bEscape, Control.MoveInput, Control.TurnInput, Control.PropulsionMultiplier, Control.TurnMultiplier,
						CheckMs, static_cast<int32>(Result.Threat), *GetNameSafe(Result.Actor.Get()),
						Result.Actor.IsValid() ? *Result.Actor->GetActorLocation().ToCompactString() : TEXT("None"),
						Result.Actor.IsValid() ? *Result.Actor->GetVelocity().ToCompactString() : TEXT("None"), *Result.ComponentName.ToString(),
						Result.Segment, Result.Segments, S->SafetyMaximumSegments, Result.PredictionTime, Result.Peers, Result.TestedPeers, Result.TerrainSweeps,
						Result.InitialGap, Result.PreviousGap, Result.Gap, Result.Envelope, Result.Margin, Result.HitDistance, Result.bStartPenetrating, Result.PenetrationDepth,
						Result.ForecastAcceleration, Result.ForecastYawRate, Result.ForecastYawAcceleration, Result.LinearDamping, Result.AngularDamping, *Result.HullExtent.ToCompactString()));
				}
			}
			return Result;
		};
		auto StartEscape = [&](EEnemyShipEscapeAction Action)
		{
			EscapeAction = Action;
			SafetyState = EEnemyShipLocalSafetyState::Escaping;
			ProgressStart = Ship->GetActorLocation();
			EscapeLastYaw = Ship->GetActorRotation().Yaw;
			ProgressElapsed = EscapeElapsed = EscapeTurned = SafeResumeElapsed = ForwardResumeElapsed = 0;
			LogCombatSafety(TEXT("Escape"), TEXT("CandidateSelected"));
			#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
			if (IsCombatSafetyLogEnabled() && CVarEnemyShipSafetyDebug.GetValueOnGameThread()) UE_LOG(LogTemp, Log, TEXT("[EnemyShipSafety] Escape Ship=%s Action=%d"), *Ship->GetName(), static_cast<int32>(Action));
			#endif
		};
		if (Elapsed > Interval + Value(S->SafetyLatencyAllowance, 0.2f, 0.02f, 2)
			&& SafetyState != EEnemyShipLocalSafetyState::Waiting && !bIdle)
		{
			if (bDiagnostics) LogCombatSafety(TEXT("EvaluationLate"), TEXT("SafetyDeadlineExceeded"), FString::Printf(TEXT("Elapsed=%.4f Interval=%.4f LatencyAllowance=%.4f"), Elapsed, Interval, Value(S->SafetyLatencyAllowance, 0.2f, 0.02f, 2)));
			EnterSafetyWait(TEXT("EvaluationLate"));
		}
		else if (SafetyState == EEnemyShipLocalSafetyState::Normal)
		{
			const bool bSafe = !bPendingHomeAlignmentSafety && Check(Requested, false, 0, TEXT("AppliedPath")).bSafe;
			if (!bSafe && !bIdle) EnterSafetyWait(bPendingHomeAlignmentSafety ? TEXT("HomeAlignmentPending") : TEXT("RequestedPathBlocked"));
			else if (bIdle)
			{
				FShipAISafetyControl IdleControl;
				if (!bSafe) IdleControl.Mode = EShipAISafetyMode::Hold;
				Ship->SetAISafetyControl(IdleControl);
			}
		}
		else if (SafetyState == EEnemyShipLocalSafetyState::Waiting && RetryRemaining <= 0)
		{
			const bool bStopped = Ship->BuoyancyRoot && Ship->BuoyancyRoot->GetPhysicsLinearVelocity().Size2D() <= Value(S->StoppedSpeed, 10, 0.1f)
				&& FMath::Abs(Ship->BuoyancyRoot->GetPhysicsAngularVelocityInDegrees().Z) <= Value(S->StoppedYawRate, 1, 0.1f);
			bool bFound = false;
			if (bStopped && Query.IsValid())
			{
				const EEnemyShipEscapeAction Actions[] = { EEnemyShipEscapeAction::Forward,
					bPreferLeftEscape ? EEnemyShipEscapeAction::ForwardLeft : EEnemyShipEscapeAction::ForwardRight,
					bPreferLeftEscape ? EEnemyShipEscapeAction::ForwardRight : EEnemyShipEscapeAction::ForwardLeft,
					EEnemyShipEscapeAction::Reverse };
				for (auto Action : Actions)
				{
					const TCHAR* Label = Action == EEnemyShipEscapeAction::Forward ? TEXT("CandidateForward")
						: Action == EEnemyShipEscapeAction::ForwardRight ? TEXT("CandidateRight")
						: Action == EEnemyShipEscapeAction::ForwardLeft ? TEXT("CandidateLeft") : TEXT("CandidateReverse");
					if (Check(EscapeControl(Action), true, 0, Label, true).bSafe) { StartEscape(Action); bFound = true; break; }
				}
			}
			if (!bFound) EnterSafetyWait(!bStopped ? TEXT("WaitingForPhysicalStop") : !Query.IsValid() ? TEXT("QueryInvalid") : TEXT("AllCandidatesBlocked"));
		}
		else if (SafetyState == EEnemyShipLocalSafetyState::Escaping)
		{
			ProgressElapsed += Elapsed;
			EscapeElapsed += Elapsed;
			EscapeReconsiderAccumulator += Elapsed;
			const float Yaw = Ship->GetActorRotation().Yaw;
			EscapeTurned += FMath::Abs(FMath::FindDeltaAngleDegrees(EscapeLastYaw, Yaw));
			EscapeLastYaw = Yaw;
			bool bFailed = !Check(EscapeControl(EscapeAction), true, 0, TEXT("AppliedPath")).bSafe;
			bool bNoProgress = false;
			if (EscapeElapsed > Value(S->ProgressGraceTime, 1, 0, 10)
				&& ProgressElapsed >= Value(S->ProgressWindow, 3, 0.5f, 30))
			{
				bNoProgress = FVector::DistSquared2D(ProgressStart, Ship->GetActorLocation()) < FMath::Square(Value(S->ProgressMinimumDistance, 100, 1));
				bFailed |= bNoProgress;
				if (bDiagnostics) LogCombatSafety(TEXT("ProgressWindow"), bNoProgress ? TEXT("NoProgress") : TEXT("Progressing"), FString::Printf(TEXT("Moved=%.2f Minimum=%.2f Window=%.3f"), FVector::Dist2D(ProgressStart, Ship->GetActorLocation()), Value(S->ProgressMinimumDistance, 100, 1), ProgressElapsed));
				ProgressStart = Ship->GetActorLocation();
				ProgressElapsed = 0;
			}
			if (bFailed) EnterSafetyWait(bNoProgress ? TEXT("NoProgress") : TEXT("EscapePathBlocked"));
			else if (EscapeReconsiderAccumulator >= Value(S->EscapeReconsiderInterval, 0.25f, Interval, 2))
			{
				const float CheckElapsed = EscapeReconsiderAccumulator;
				EscapeReconsiderAccumulator = 0;
				const FResult ResumeResult = bPendingHomeAlignmentSafety ? FResult{ false, EThreat::Invalid, nullptr, TEXT("HomeAlignmentPending") }
					: Check(Requested, false, Value(S->ResumeAdditionalMargin, 100), TEXT("ResumeProbe"));
				const bool bNormalSafe = ResumeResult.bSafe;
				if (bDiagnostics) CombatLastResumeReason = ResumeResult.Reason;
				SafeResumeElapsed = bNormalSafe ? SafeResumeElapsed + CheckElapsed : 0;
				const float Confirmation = Value(S->ResumeConfirmationTime, 0.8f, 0.1f, 10);
				if (SafeResumeElapsed >= Confirmation)
				{
					LogCombatSafety(TEXT("Resume"), TEXT("NormalPathConfirmed"));
					ResetLocalSafety();
				}
				else if (EscapeTurned >= Value(S->MaximumEscapeTurn, 90, 15, 180)) EnterSafetyWait(TEXT("EscapeTurnLimit"));
				else if (EscapeAction == EEnemyShipEscapeAction::Reverse)
				{
					bool bForwardSafe = false;
					for (auto Action : { EEnemyShipEscapeAction::Forward, EEnemyShipEscapeAction::ForwardRight, EEnemyShipEscapeAction::ForwardLeft })
						if (Check(EscapeControl(Action), true, Value(S->ResumeAdditionalMargin, 100), TEXT("ReverseExitProbe")).bSafe) { bForwardSafe = true; break; }
					ForwardResumeElapsed = bForwardSafe ? ForwardResumeElapsed + CheckElapsed : 0;
					if (ForwardResumeElapsed >= Confirmation) EnterSafetyWait(TEXT("ReverseToForwardConfirmed"));
				}
			}
		}
	}
	FShipAISafetyControl Safety;
	if (SafetyState == EEnemyShipLocalSafetyState::Waiting)
	{
		Safety.Mode = EShipAISafetyMode::Hold;
		Requested = {};
	}
	else if (SafetyState == EEnemyShipLocalSafetyState::Escaping)
	{
		Safety.Mode = EShipAISafetyMode::Escape;
		Safety.MaximumSpeed = Value(S->EscapeMaximumSpeed, 300, 1);
		Safety.MaximumYawRate = Value(S->EscapeMaximumYawRate, 8, 0.1f, 180);
		Requested = EscapeControl(EscapeAction);
	}
	else if (bIdle) Safety = Ship->GetAISafetyControl();
	Ship->SetAISafetyControl(Safety);
	Ship->SetAIControlInput(Requested.MoveInput, Requested.TurnInput, Requested.PropulsionMultiplier, Requested.TurnMultiplier);
	if (IsCombatSafetyLogEnabled())
	{
		const float ActualMove = Ship->GetCurrentMoveInput();
		const float ActualTurn = Ship->GetCurrentTurnInput();
		const uint8 ActualMode = static_cast<uint8>(Ship->GetAISafetyControl().Mode);
		const bool bModeChanged = ActualMode != CombatAppliedMode;
		const bool bThrottleChanged = (FMath::Abs(ActualMove) > 0.01f) != (FMath::Abs(CombatAppliedMove) > 0.01f)
			|| ActualMove * CombatAppliedMove < 0;
		const bool bTurnChanged = FMath::Sign(ActualTurn) != FMath::Sign(CombatAppliedTurn);
		if (!bCombatAppliedControlValid || bModeChanged || bThrottleChanged || bTurnChanged)
		{
			LogCombatSafety(TEXT("ControlChange"), !bCombatAppliedControlValid ? TEXT("FirstControl")
				: bModeChanged ? TEXT("SafetyModeChanged") : bThrottleChanged ? TEXT("ThrottleTransition") : TEXT("SteeringTransition"),
				FString::Printf(TEXT("PrevMove=%.3f PrevTurn=%.3f PrevMode=%d BaseMove=%.3f BaseTurn=%.3f PropScale=%.3f TurnScale=%.3f Source=%s"),
					CombatAppliedMove, CombatAppliedTurn, CombatAppliedMode, BaseOutput.MoveInput, BaseOutput.TurnInput,
					Requested.PropulsionMultiplier, Requested.TurnMultiplier, Winner ? TEXT("Override") : TEXT("Navigation")));
		}
		bCombatAppliedControlValid = true;
		CombatAppliedMove = ActualMove;
		CombatAppliedTurn = ActualTurn;
		CombatAppliedMode = ActualMode;
	}
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
