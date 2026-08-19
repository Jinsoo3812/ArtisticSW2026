#include "Buoyancy/SWBuoyancyDebugSubsystem.h"

#include "Buoyancy/SWBuoyancyComponent.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#include "Ship.h"

void USWBuoyancyDebugSubsystem::Tick(float DeltaTime)
{
#if !UE_SERVER
	UWorld* World = GetWorld();
	const IConsoleVariable* DebugCVar = IConsoleManager::Get().FindConsoleVariable(
		TEXT("p.ShowShipNetworkBuoyancyDebug"));
	if (!World || IsRunningDedicatedServer() || !DebugCVar || DebugCVar->GetInt() <= 0)
	{
		return;
	}

	for (TObjectIterator<USWBuoyancyComponent> It; It; ++It)
	{
		USWBuoyancyComponent* Buoyancy = *It;
		AActor* Owner = Buoyancy ? Buoyancy->GetOwner() : nullptr;
		if (!Owner || Owner->GetWorld() != World || Owner->IsA<AShip>())
		{
			continue;
		}

		const USceneComponent* Root = Owner->GetRootComponent();
		const FTransform BodyTransform = Root ? Root->GetComponentTransform() : Owner->GetActorTransform();
		const bool bActive = Buoyancy->IsActive() && Buoyancy->IsComponentTickEnabled();
		const FColor PontoonColor = bActive ? FColor::Cyan : FColor::Orange;
		for (const FSWBuoyancyPontoon& Pontoon : Buoyancy->GetPontoons())
		{
			const FVector WorldPosition = BodyTransform.TransformPosition(Pontoon.RelativeLocation);
			DrawDebugSphere(
				World,
				WorldPosition,
				Pontoon.Radius,
				12,
				PontoonColor,
				false,
				0.0f,
				0,
				1.75f);
		}

		const FSWBuoyancyRuntimeDiagnostic& Runtime = Buoyancy->GetLastRuntimeDiagnostic();
		if (Runtime.bWaterSurfaceFound)
		{
			const FVector SurfacePoint(
				Runtime.PontoonWorldPosition.X,
				Runtime.PontoonWorldPosition.Y,
				Runtime.WaterHeight);
			DrawDebugLine(
				World,
				Runtime.PontoonWorldPosition,
				SurfacePoint,
				FColor::Blue,
				false,
				0.0f,
				0,
				1.25f);
		}

		const FSWBuoyancyForceSettings& Settings = Buoyancy->GetForceSettings();
		DrawDebugString(
			World,
			Owner->GetActorLocation() + FVector(0.0f, 0.0f, 100.0f),
			FString::Printf(
				TEXT("%s | SW Buoyancy %s | Pontoons=%d | Coeff=%.2f Deep=%.2f"),
				*Owner->GetName(),
				bActive ? TEXT("ACTIVE") : TEXT("INACTIVE"),
				Buoyancy->GetPontoons().Num(),
				Settings.BuoyancyCoefficient,
				Settings.DeepWaterBuoyancyMultiplier),
			Owner,
			PontoonColor,
			0.0f,
			false,
			1.0f);
	}
#endif
}

TStatId USWBuoyancyDebugSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWBuoyancyDebugSubsystem, STATGROUP_Tickables);
}
