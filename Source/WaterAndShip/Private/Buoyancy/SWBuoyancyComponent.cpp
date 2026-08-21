#include "Buoyancy/SWBuoyancyComponent.h"

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

void USWBuoyancyComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	LastRuntimeDiagnostic = FSWBuoyancyRuntimeDiagnostic();
	LastRuntimeDiagnostic.WaterBodyCount = WaterBodies.Num();
	LastRuntimeDiagnostic.WorldTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
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
		return;
	}

	UPrimitiveComponent* SimulatingComponent = ResolveSimulatingComponent();
	LastRuntimeDiagnostic.bResolvedSimulatingComponent = SimulatingComponent != nullptr;
	LastRuntimeDiagnostic.SimulatingComponentName = GetNameSafe(SimulatingComponent);
	LastRuntimeDiagnostic.bPhysicsSimulationActive = SimulatingComponent
		&& SimulatingComponent->IsSimulatingPhysics();
	if (!LastRuntimeDiagnostic.bPhysicsSimulationActive)
	{
		return;
	}

	const FTransform BodyTransform = SimulatingComponent->GetComponentTransform();
	for (const FSWBuoyancyPontoon& Pontoon : Pontoons)
	{
		const FVector WorldPosition = BodyTransform.TransformPosition(Pontoon.RelativeLocation);
		LastRuntimeDiagnostic.PontoonWorldPosition = WorldPosition;
		float WaterHeight = 0.0f;
		FVector WaterVelocity = FVector::ZeroVector;
		if (!QueryWaterSurface(WorldPosition, WaterHeight, WaterVelocity))
		{
			continue;
		}
		LastRuntimeDiagnostic.bWaterSurfaceFound = true;
		LastRuntimeDiagnostic.WaterHeight = WaterHeight;

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
		if (Result.BuoyantForceZ > 0.0f)
		{
			SimulatingComponent->AddForceAtLocation(
				FVector::UpVector * Result.BuoyantForceZ,
				WorldPosition);
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
	FVector& OutWaterVelocity) const
{
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
		}
	}

	return bFound;
}

UPrimitiveComponent* USWBuoyancyComponent::ResolveSimulatingComponent() const
{
	return GetOwner() ? Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()) : nullptr;
}
