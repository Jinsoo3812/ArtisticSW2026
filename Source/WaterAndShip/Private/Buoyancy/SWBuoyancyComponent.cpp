#include "Buoyancy/SWBuoyancyComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include "BuoyancyComponent.h"
#include "BuoyancyTypes.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Water/SWBuoyancyMath.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"
#include "WaterBodyTypes.h"
#include "Buoyancy/ChestLaunchPhysicsDiagnostic.h"
#include "Buoyancy/SWPhysicsStepBuoyancy.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "GameFramework/GameStateBase.h"
#include "SWRippleWaterWaves.h"

USWBuoyancyComponent::USWBuoyancyComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void USWBuoyancyComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bImportLegacyWaterBuoyancy)
	{
		if (AActor* Owner = GetOwner())
		{
			bUsingLegacyFallback = ImportFromLegacyComponent(
				Owner->FindComponentByClass<UBuoyancyComponent>(), false);
		}
	}

	RefreshWaterBodies();
	bCommandLineDiagnostics = FParse::Param(FCommandLine::Get(), TEXT("BuoyancyDiagnostics"));
	SetComponentTickEnabled(
		ExecutionMode == ESWBuoyancyExecutionMode::ServerAuthority || bCommandLineDiagnostics);

}

void USWBuoyancyComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopPhysicsStepBuoyancy();
	if (ChestPhysicsDiagnostic && GetWorld() && GetWorld()->GetPhysicsScene())
	{
		GetWorld()->GetPhysicsScene()->GetSolver()->UnregisterAndFreeSimCallbackObject_External(ChestPhysicsDiagnostic);
	}
	ChestPhysicsDiagnostic = nullptr;
	Super::EndPlay(EndPlayReason);
}

void USWBuoyancyComponent::StopPhysicsStepBuoyancy()
{
	if (PhysicsStepBuoyancy && GetWorld() && GetWorld()->GetPhysicsScene())
	{
		GetWorld()->GetPhysicsScene()->GetSolver()->UnregisterAndFreeSimCallbackObject_External(PhysicsStepBuoyancy);
	}
	PhysicsStepBuoyancy = nullptr;
}

void USWBuoyancyComponent::SetComponentTickEnabled(bool bEnabled)
{
	if (!bEnabled) StopPhysicsStepBuoyancy();
	Super::SetComponentTickEnabled(bEnabled);
}

void USWBuoyancyComponent::OnUnregister()
{
	StopPhysicsStepBuoyancy();
	Super::OnUnregister();
}

void USWBuoyancyComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SW_Buoyancy_Tick);
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	LastRuntimeDiagnostic = FSWBuoyancyRuntimeDiagnostic();
	LastRuntimeDiagnostic.WaterBodyCount = WaterBodies.Num();
	LastRuntimeDiagnostic.WorldTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	LastRuntimeDiagnostic.ServerTimeSeconds = GetWorld() && GetWorld()->GetGameState()
		? GetWorld()->GetGameState()->GetServerWorldTimeSeconds() : LastRuntimeDiagnostic.WorldTimeSeconds;
	LastRuntimeDiagnostic.bForceApplicationAllowed = ShouldApplyForces();

	if (bCommandLineDiagnostics && GetWorld() && GetOwner()
		&& GetWorld()->GetTimeSeconds() >= NextDiagnosticTime)
	{
		NextDiagnosticTime = GetWorld()->GetTimeSeconds() + 2.0f;
		UE_LOG(LogTemp, Warning, TEXT("[SW-BUOYANCY-STATE] Owner=%s Role=%s Location=%s Velocity=%s"),
			*GetNameSafe(GetOwner()),
			GetOwner()->HasAuthority() ? TEXT("Authority") : TEXT("Proxy"),
			*GetOwner()->GetActorLocation().ToString(),
			*GetOwner()->GetVelocity().ToString());
	}

	if (!LastRuntimeDiagnostic.bForceApplicationAllowed)
	{
		StopPhysicsStepBuoyancy();
		return;
	}

	UPrimitiveComponent* SimulatingComponent = ResolveSimulatingComponent();
	LastRuntimeDiagnostic.bResolvedSimulatingComponent = SimulatingComponent != nullptr;
	LastRuntimeDiagnostic.SimulatingComponentName = GetNameSafe(SimulatingComponent);
	LastRuntimeDiagnostic.bPhysicsSimulationActive = SimulatingComponent
		&& SimulatingComponent->IsSimulatingPhysics();
	if (!LastRuntimeDiagnostic.bPhysicsSimulationActive)
	{
		StopPhysicsStepBuoyancy();
		bHasPreviousLaunchSample = false;
		return;
	}

	const FTransform BodyTransform = SimulatingComponent->GetComponentTransform();
	FSWPhysicsStepBuoyancyInput* PhysicsInput = nullptr;
	if (bUsePhysicsStepBuoyancy)
	{
		if (!PhysicsStepBuoyancy && GetWorld()->GetPhysicsScene())
		{
			PhysicsStepBuoyancy = GetWorld()->GetPhysicsScene()->GetSolver()
				->CreateAndRegisterSimCallbackObject_External<FSWPhysicsStepBuoyancy>();
		}
		// Never fall back to the stale GT force path if the physics callback is unavailable.
		if (!PhysicsStepBuoyancy) return;
		PhysicsInput = PhysicsStepBuoyancy->GetProducerInputData_External();
		PhysicsInput->Object = SimulatingComponent->GetPhysicsObjectByName(NAME_None);
		PhysicsInput->Sequence = ++PhysicsStepBuoyancySequence;
		PhysicsInput->Settings = ForceSettings;
		PhysicsInput->Pontoons.Reset();
	}
	else
	{
		StopPhysicsStepBuoyancy();
	}
	for (const FSWBuoyancyPontoon& Pontoon : Pontoons)
	{
		const FVector WorldPosition = BodyTransform.TransformPosition(Pontoon.RelativeLocation);
		LastRuntimeDiagnostic.PontoonWorldPosition = WorldPosition;
		float WaterHeight = 0.0f;
		FVector WaterVelocity = FVector::ZeroVector;
		FString WaterBodyName;
		const UWaterBodyComponent* SelectedWaterBody = nullptr;
		if (!QueryWaterSurface(WorldPosition, WaterHeight, WaterVelocity, WaterBodyName, SelectedWaterBody))
		{
			continue;
		}
		LastRuntimeDiagnostic.bWaterSurfaceFound = true;
		LastRuntimeDiagnostic.WaterHeight = WaterHeight;
		LastRuntimeDiagnostic.WaterBodyName = MoveTemp(WaterBodyName);
		if (PhysicsInput)
		{
			FSWPhysicsBuoyancyPontoon& Sample = PhysicsInput->Pontoons.AddDefaulted_GetRef();
			Sample.ScaledLocalOffset = BodyTransform.GetScale3D() * Pontoon.RelativeLocation;
			Sample.Radius = Pontoon.Radius;
			Sample.ForceScale = Pontoon.ForceScale;
			Sample.WaterHeight = WaterHeight;
			Sample.WaterVelocity = WaterVelocity;
		}
		if (bMonitorChestLaunch && SelectedWaterBody)
		{
			const UWaterWavesBase* Waves = SelectedWaterBody->GetWaterWaves();
			LastRuntimeDiagnostic.WaveClass = GetNameSafe(Waves ? Waves->GetClass() : nullptr);
			LastRuntimeDiagnostic.WaveReferenceTime = SelectedWaterBody->GetWaveReferenceTime();
			const USWRippleWaterWaves* RippleWaves = Cast<USWRippleWaterWaves>(Waves);
			if (!RippleWaves && Waves) RippleWaves = Cast<USWRippleWaterWaves>(Waves->GetWaterWaves());
			LastRuntimeDiagnostic.EffectiveWaveTime = RippleWaves
				? RippleWaves->ResolveQueryTime(LastRuntimeDiagnostic.WaveReferenceTime)
				: LastRuntimeDiagnostic.WaveReferenceTime;
		}

		const FVector PointVelocity = SimulatingComponent->GetPhysicsLinearVelocityAtPoint(WorldPosition);
		FSWBuoyancySolveInput Input;
		Input.WaterHeight = WaterHeight;
		Input.PontoonCenterZ = WorldPosition.Z;
		Input.PontoonRadius = Pontoon.Radius;
		Input.RelativeVelocityZ = PointVelocity.Z - WaterVelocity.Z;
		Input.ForceScale = Pontoon.ForceScale;
		LastRuntimeDiagnostic.RelativeVelocityZ = Input.RelativeVelocityZ;

		const FSWBuoyancySolveResult Result = FSWBuoyancyMath::SolvePontoon(Input, ForceSettings);
		LastRuntimeDiagnostic.bPontoonInWater = Result.bIsInWater;
		LastRuntimeDiagnostic.ImmersionDepth = Result.ImmersionDepth;
		LastRuntimeDiagnostic.BuoyantForceZ += Result.BuoyantForceZ;
		LastRuntimeDiagnostic.DampingForce += Result.DampingForce;
		// GT result remains an estimate for diagnostics; only one path writes forces.
		if (!PhysicsInput && Result.BuoyantForceZ > 0.0f)
		{
			SimulatingComponent->AddForceAtLocation(
				FVector::UpVector * Result.BuoyantForceZ,
				WorldPosition);
		}

	}
	if (bMonitorChestLaunch)
	{
		MonitorChestLaunch(SimulatingComponent, DeltaTime);
		UpdateChestPhysicsDiagnostic(SimulatingComponent);
	}
}

void USWBuoyancyComponent::MonitorChestLaunch(UPrimitiveComponent* Body, float DeltaTime)
{
	FChestLaunchSample Current;
	Current.Solve = LastRuntimeDiagnostic;
	Current.Location = Body->GetComponentLocation();
	Current.Velocity = Body->GetPhysicsLinearVelocity();
	Current.DeltaTime = DeltaTime;
	const double Now = Current.Solve.WorldTimeSeconds;
	// A resumed component must not compare a dormant interval against one frame's dt.
	const double SampleGap = Now - PreviousLaunchSample.Solve.WorldTimeSeconds;
	const bool bContinuous = bHasPreviousLaunchSample && SampleGap > 0.0 && SampleGap < 0.25;
	const float Mass = FMath::Max(Body->GetMass(), UE_SMALL_NUMBER);
	const float Gravity = Body->IsGravityEnabled() && GetWorld() ? GetWorld()->GetGravityZ() : 0.0f;
	const float ExpectedDeltaV = bContinuous
		? (PreviousLaunchSample.Solve.BuoyantForceZ / Mass + Gravity) * SampleGap : 0.0f;
	const float DeltaV = bContinuous ? Current.Velocity.Z - PreviousLaunchSample.Velocity.Z : 0.0f;
	const float WaterDelta = bContinuous && Current.Solve.bWaterSurfaceFound
		&& PreviousLaunchSample.Solve.bWaterSurfaceFound
		? Current.Solve.WaterHeight - PreviousLaunchSample.Solve.WaterHeight : 0.0f;
	uint32 Reasons = 0;
	if (Current.Velocity.Z > 400.0f) Reasons |= 1;
	if (bContinuous && DeltaV > 150.0f) Reasons |= 2;
	if (bContinuous && DeltaV - ExpectedDeltaV > 150.0f) Reasons |= 4;
	if (FMath::Abs(WaterDelta) > 100.0f) Reasons |= 8;
	if (Current.Solve.BuoyantForceZ / Mass > 8.0f * FMath::Max(FMath::Abs(Gravity), 1.0f)) Reasons |= 16;
	// The GT estimate is not aligned with async PT integration; bit 4 alone is not an event.
	if ((Reasons & ~4u) != 0 && (Reasons & ~PreviousLaunchReasons & ~4u) != 0 && Now >= NextLaunchLogTime)
	{
		NextLaunchLogTime = Now + 10.0;
		++ChestDiagnosticRequest;
		// UE_LOG(LogTemp, Warning, TEXT("[CHEST-LAUNCH-REQUEST] Actor=%s Request=%u GTSequence=%u World=%.6f"),
			// *GetOwner()->GetName(), ChestDiagnosticRequest, ChestDiagnosticSequence + 1, Now);
		for (const UWaterBodyComponent* WaterBody : WaterBodies)
		{
			if (!IsValid(WaterBody) || WaterBody->GetPathName() != Current.Solve.WaterBodyName) continue;
			const auto PlaneQuery = WaterBody->TryQueryWaterInfoClosestToWorldLocation(
				Current.Solve.PontoonWorldPosition, EWaterBodyQueryFlags::ComputeLocation | EWaterBodyQueryFlags::ComputeDepth);
			const UWaterWavesBase* Waves = WaterBody->GetWaterWaves();
			if (PlaneQuery.HasValue() && Waves)
			{
				const auto& Plane = PlaneQuery.GetValue();
				FVector Normal = FVector::UpVector;
				const float AtReference = Waves->GetWaveHeightAtPosition(Plane.GetWaterSurfaceLocation(),
					Plane.GetWaterSurfaceDepth(), Current.Solve.WaveReferenceTime, Normal);
				const float AtServerNow = Waves->GetWaveHeightAtPosition(Plane.GetWaterSurfaceLocation(),
					Plane.GetWaterSurfaceDepth(), static_cast<float>(Current.Solve.ServerTimeSeconds), Normal);
				// UE_LOG(LogTemp, Warning, TEXT("[CHEST-LAUNCH-WAVE-COMPARE] Actor=%s Request=%u PlaneZ=%.3f Depth=%.3f ReferenceTime=%.6f EffectiveTime=%.6f ServerNow=%.6f RawAtReference=%.3f RawAtServerNow=%.3f RawHeightDifference=%.3f (same_position_depth_before_attenuation)"),
					// *GetOwner()->GetName(), ChestDiagnosticRequest, Plane.GetWaterSurfaceLocation().Z,
					// Plane.GetWaterSurfaceDepth(), Current.Solve.WaveReferenceTime, Current.Solve.EffectiveWaveTime,
					// Current.Solve.ServerTimeSeconds, AtReference, AtServerNow, AtServerNow - AtReference);
			}
		}
		TInlineComponentArray<UActorComponent*> Components(GetOwner());
		for (UActorComponent* Component : Components)
		{
			if (Component->IsA<UBuoyancyComponent>() || Component->IsA<USWBuoyancyComponent>())
			{
				// UE_LOG(LogTemp, Warning, TEXT("[CHEST-LAUNCH-WRITER] Actor=%s Component=%s Class=%s Active=%d Tick=%d"),
					// *GetOwner()->GetName(), *Component->GetName(), *Component->GetClass()->GetName(),
					// Component->IsActive(), Component->IsComponentTickEnabled());
			}
		}
		// UE_LOG(LogTemp, Warning, TEXT("[CHEST-LAUNCH] Actor=%s Class=%s SpawnOwner=%s Time=%.3f Age=%.3f Reasons=%u (1=Vz,2=DeltaVz,4=UnexplainedDeltaVz,8=WaterJump,16=Force) Scale=%s Mass=%.2f Gravity=%.1f DeltaVz=%.1f ExpectedDeltaVz=%.1f WaterDelta=%.1f Pontoons=%d Coeff=%.3f Deep=%.2f Damp=%.1f Damp2=%.2f MaxForce=%.1f LinearDamp=%.2f AngularDamp=%.2f"),
			// *GetOwner()->GetPathName(), *GetOwner()->GetClass()->GetPathName(), *GetNameSafe(GetOwner()->GetOwner()),
			// Now, GetOwner()->GetGameTimeSinceCreation(), Reasons, *Body->GetComponentScale().ToString(), Mass, Gravity,
			// DeltaV, ExpectedDeltaV, WaterDelta, Pontoons.Num(), ForceSettings.BuoyancyCoefficient,
			// ForceSettings.DeepWaterBuoyancyMultiplier, ForceSettings.BuoyancyDamp, ForceSettings.BuoyancyDamp2,
			// ForceSettings.MaxBuoyantForce, Body->GetLinearDamping(), Body->GetAngularDamping());
		const auto LogSample = [this](const TCHAR* Phase, const FChestLaunchSample& Sample)
		{
			// UE_LOG(LogTemp, Warning, TEXT("[CHEST-LAUNCH-CLOCK] Actor=%s Phase=%s World=%.6f Server=%.6f WaveReference=%.6f EffectiveWave=%.6f WaveClass=%s"),
				// *GetOwner()->GetName(), Phase, Sample.Solve.WorldTimeSeconds, Sample.Solve.ServerTimeSeconds,
				// Sample.Solve.WaveReferenceTime, Sample.Solve.EffectiveWaveTime, *Sample.Solve.WaveClass);
			// UE_LOG(LogTemp, Warning, TEXT("[CHEST-LAUNCH-SAMPLE] Actor=%s Phase=%s Time=%.3f Dt=%.4f Pos=%s Vel=%s Water=%s Found=%d Height=%.1f Pontoon=%s Immersion=%.1f RelativeVz=%.1f ForceZ=%.1f DampingForce=%.1f Bodies=%d"),
				// *GetOwner()->GetName(), Phase, Sample.Solve.WorldTimeSeconds, Sample.DeltaTime,
				// *Sample.Location.ToString(), *Sample.Velocity.ToString(), *Sample.Solve.WaterBodyName,
				// Sample.Solve.bWaterSurfaceFound, Sample.Solve.WaterHeight, *Sample.Solve.PontoonWorldPosition.ToString(),
				// Sample.Solve.ImmersionDepth, Sample.Solve.RelativeVelocityZ, Sample.Solve.BuoyantForceZ,
				// Sample.Solve.DampingForce, Sample.Solve.WaterBodyCount);
		};
		for (int32 Index = 0; Index < LaunchHistoryCount; ++Index)
		{
			LogSample(TEXT("History"), LaunchHistory[(LaunchHistoryNext - LaunchHistoryCount + Index + 8) % 8]);
		}
		if (bContinuous) LogSample(TEXT("PreviousFrame"), PreviousLaunchSample);
		LogSample(TEXT("Trigger"), Current);
		for (const FSWBuoyancyPontoon& Pontoon : Pontoons)
		{
			// UE_LOG(LogTemp, Warning, TEXT("[CHEST-LAUNCH-PONTOON] Actor=%s Name=%s Radius=%.1f Offset=%s ForceScale=%.2f"),
				// *GetOwner()->GetName(), *Pontoon.Name.ToString(), Pontoon.Radius,
				// *Pontoon.RelativeLocation.ToString(), Pontoon.ForceScale);
		}
	}
	if (!bContinuous)
	{
		LaunchHistoryCount = 0;
		LaunchHistoryNext = 0;
		NextLaunchHistoryTime = Now;
	}
	if (Now >= NextLaunchHistoryTime)
	{
		LaunchHistory[LaunchHistoryNext] = Current;
		LaunchHistoryNext = (LaunchHistoryNext + 1) % 8;
		LaunchHistoryCount = FMath::Min(LaunchHistoryCount + 1, 8);
		NextLaunchHistoryTime = Now + 0.1;
	}
	PreviousLaunchSample = MoveTemp(Current);
	bHasPreviousLaunchSample = true;
	PreviousLaunchReasons = Reasons;
}

void USWBuoyancyComponent::UpdateChestPhysicsDiagnostic(UPrimitiveComponent* Body)
{
	if (!ChestPhysicsDiagnostic && GetWorld() && GetWorld()->GetPhysicsScene())
	{
		ChestPhysicsDiagnostic = GetWorld()->GetPhysicsScene()->GetSolver()
			->CreateAndRegisterSimCallbackObject_External<FChestLaunchPhysicsDiagnostic>();
	}
	if (!ChestPhysicsDiagnostic) return;
	FChestLaunchPhysicsInput* Input = ChestPhysicsDiagnostic->GetProducerInputData_External();
	Input->Object = Body->GetPhysicsObjectByName(NAME_None);
	Input->Sequence = ++ChestDiagnosticSequence;
	Input->Request = ChestDiagnosticRequest;
	Input->WorldTime = LastRuntimeDiagnostic.WorldTimeSeconds;
	Input->ServerTime = LastRuntimeDiagnostic.ServerTimeSeconds;
	Input->ForceZ = LastRuntimeDiagnostic.BuoyantForceZ;
	Input->GravityZ = Body->IsGravityEnabled() ? GetWorld()->GetGravityZ() : 0.0f;
	Input->WaterHeight = LastRuntimeDiagnostic.WaterHeight;
	Input->WaveReferenceTime = LastRuntimeDiagnostic.WaveReferenceTime;
	Input->EffectiveWaveTime = LastRuntimeDiagnostic.EffectiveWaveTime;
	while (auto Output = ChestPhysicsDiagnostic->PopOutputData_External())
	{
		// UE_LOG(LogTemp, Warning, TEXT("[CHEST-LAUNCH-PT] Actor=%s Request=%u PhysicsTrigger=%d TriggerSimTime=%.6f Samples=%d"),
			// *GetOwner()->GetPathName(), Output->Request, Output->bPhysicsTrigger, Output->TriggerTime, Output->Count);
		for (int32 Index = 0; Index < Output->Count; ++Index)
		{
			const FChestLaunchPhysicsSample& Sample = Output->Samples[Index];
			// UE_LOG(LogTemp, Warning, TEXT("[CHEST-LAUNCH-PT-SAMPLE] Actor=%s Frame=%d SimTime=%.6f Dt=%.6f GTSequence=%u GTWorld=%.6f GTServer=%.6f GTForceZ=%.1f GTWaterHeight=%.3f WaveReference=%.6f EffectiveWave=%.6f Mass=%.2f PosZ=%.3f BeforeVz=%.3f IntegratedVz=%.3f AfterVz=%.3f AppliedAccelerationZ=%.3f GravityZ=%.1f ForceStageDeltaVz=%.3f SolveStageDeltaVz=%.3f"),
				// *GetOwner()->GetName(), Sample.Frame, Sample.Time, Sample.Dt, Sample.Input.Sequence,
				// Sample.Input.WorldTime, Sample.Input.ServerTime, Sample.Input.ForceZ,
				// Sample.Input.WaterHeight, Sample.Input.WaveReferenceTime, Sample.Input.EffectiveWaveTime, Sample.Mass,
				// Sample.Position.Z, Sample.BeforeVelocity.Z, Sample.IntegratedVelocity.Z, Sample.AfterVelocity.Z,
				// Sample.Acceleration.Z, Sample.Input.GravityZ,
				// Sample.IntegratedVelocity.Z - Sample.BeforeVelocity.Z,
				// Sample.AfterVelocity.Z - Sample.IntegratedVelocity.Z);
		}
	}
}

void USWBuoyancyComponent::RefreshWaterBodies()
{
	WaterBodies.Reset();
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AWaterBody> It(World); It; ++It)
		{
			if (UWaterBodyComponent* Component = It->GetWaterBodyComponent())
			{
				WaterBodies.Add(Component);
			}
		}
	}
}

bool USWBuoyancyComponent::ImportFromLegacyComponent(UBuoyancyComponent* LegacyComponent, bool bOverwriteExisting)
{
	if (!LegacyComponent || (!bOverwriteExisting && Pontoons.Num() > 0))
	{
		return false;
	}

	Pontoons.Reset();
	for (const FSphericalPontoon& LegacyPontoon : LegacyComponent->BuoyancyData.Pontoons)
	{
		if (!LegacyPontoon.bEnabled)
		{
			continue;
		}

		FSWBuoyancyPontoon Pontoon;
		Pontoon.Name = LegacyPontoon.CenterSocket;
		Pontoon.RelativeLocation = LegacyPontoon.RelativeLocation;
		Pontoon.Radius = LegacyPontoon.Radius;
		Pontoons.Add(Pontoon);
	}

	ForceSettings.BuoyancyCoefficient = LegacyComponent->BuoyancyData.BuoyancyCoefficient;
	ForceSettings.BuoyancyDamp = LegacyComponent->BuoyancyData.BuoyancyDamp;
	ForceSettings.BuoyancyDamp2 = LegacyComponent->BuoyancyData.BuoyancyDamp2;
	ForceSettings.MaxBuoyantForce = LegacyComponent->BuoyancyData.MaxBuoyantForce;
	LegacyComponent->Deactivate();
	LegacyComponent->SetComponentTickEnabled(false);
	return true;
}

void USWBuoyancyComponent::ConfigureSinglePontoon(float Radius)
{
	Pontoons.SetNum(1);
	Pontoons[0].Name = TEXT("Center");
	Pontoons[0].RelativeLocation = FVector::ZeroVector;
	Pontoons[0].Radius = FMath::Max(Radius, 1.0f);
}

bool USWBuoyancyComponent::ShouldApplyForces() const
{
	const AActor* Owner = GetOwner();
	return ExecutionMode == ESWBuoyancyExecutionMode::ServerAuthority
		&& Owner
		&& Owner->HasAuthority();
}

bool USWBuoyancyComponent::QueryWaterSurface(
	const FVector& Position,
	float& OutWaterHeight,
	FVector& OutWaterVelocity,
	FString& OutWaterBodyName,
	const UWaterBodyComponent*& OutWaterBody) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SW_Buoyancy_WaterQuery);
	bool bFound = false;
	OutWaterHeight = -BIG_NUMBER;
	OutWaterVelocity = FVector::ZeroVector;

	const EWaterBodyQueryFlags QueryFlags =
		EWaterBodyQueryFlags::ComputeLocation
		| EWaterBodyQueryFlags::ComputeDepth
		| EWaterBodyQueryFlags::ComputeVelocity
		| EWaterBodyQueryFlags::IncludeWaves;

	for (const UWaterBodyComponent* WaterBody : WaterBodies)
	{
		if (!IsValid(WaterBody) || WaterBody->IsWorldLocationInExclusionVolume(Position))
		{
			continue;
		}

		const TValueOrError<FWaterBodyQueryResult, EWaterBodyQueryError> Query =
			WaterBody->TryQueryWaterInfoClosestToWorldLocation(Position, QueryFlags);
		if (!Query.HasValue())
		{
			continue;
		}

		const FWaterBodyQueryResult& Value = Query.GetValue();
		const float CandidateHeight = Value.GetWaterSurfaceLocation().Z;
		if (!bFound || CandidateHeight > OutWaterHeight)
		{
			bFound = true;
			OutWaterHeight = CandidateHeight;
			OutWaterVelocity = Value.GetVelocity();
			OutWaterBodyName = WaterBody->GetPathName();
			OutWaterBody = WaterBody;
		}
	}

	return bFound;
}

UPrimitiveComponent* USWBuoyancyComponent::ResolveSimulatingComponent() const
{
	return GetOwner() ? Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()) : nullptr;
}
