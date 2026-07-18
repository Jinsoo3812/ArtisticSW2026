#include "Buoyancy/SWBuoyancyComponent.h"

#include "BuoyancyComponent.h"
#include "BuoyancyTypes.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
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

	UE_LOG(LogTemp, Log, TEXT("[SW-BUOYANCY] %s Mode=%s Pontoons=%d Authority=%s SettingsSource=%s"),
		*GetNameSafe(GetOwner()),
		ExecutionMode == ESWBuoyancyExecutionMode::ServerAuthority ? TEXT("ServerAuthority") : TEXT("ExternalNetworkPhysics"),
		Pontoons.Num(),
		GetOwner() && GetOwner()->HasAuthority() ? TEXT("true") : TEXT("false"),
		bUsingLegacyFallback ? TEXT("LegacyFallback") : TEXT("SWComponent"));
}

void USWBuoyancyComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

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

	if (!ShouldApplyForces())
	{
		return;
	}

	UPrimitiveComponent* SimulatingComponent = ResolveSimulatingComponent();
	if (!SimulatingComponent || !SimulatingComponent->IsSimulatingPhysics())
	{
		return;
	}

	const FTransform BodyTransform = SimulatingComponent->GetComponentTransform();
	for (const FSWBuoyancyPontoon& Pontoon : Pontoons)
	{
		const FVector WorldPosition = BodyTransform.TransformPosition(Pontoon.RelativeLocation);
		float WaterHeight = 0.0f;
		FVector WaterVelocity = FVector::ZeroVector;
		if (!QueryWaterSurface(WorldPosition, WaterHeight, WaterVelocity))
		{
			continue;
		}

		const FVector PointVelocity = SimulatingComponent->GetPhysicsLinearVelocityAtPoint(WorldPosition);
		FSWBuoyancySolveInput Input;
		Input.WaterHeight = WaterHeight;
		Input.PontoonCenterZ = WorldPosition.Z;
		Input.PontoonRadius = Pontoon.Radius;
		Input.RelativeVelocityZ = PointVelocity.Z - WaterVelocity.Z;
		Input.ForceScale = Pontoon.ForceScale;

		const FSWBuoyancySolveResult Result = FSWBuoyancyMath::SolvePontoon(Input, ForceSettings);
		if (Result.BuoyantForceZ > 0.0f)
		{
			SimulatingComponent->AddForceAtLocation(
				FVector::UpVector * Result.BuoyantForceZ,
				WorldPosition);
		}

#if ENABLE_DRAW_DEBUG
		if (bDrawDebugPontoons)
		{
			DrawDebugSphere(
				GetWorld(), WorldPosition, Pontoon.Radius, 12,
				Result.bIsInWater ? FColor::Cyan : FColor::Green,
				false, 0.0f, 0, 1.5f);
		}
#endif
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
