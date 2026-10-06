#include "Buoyancy/SWBuoyancyDebugSubsystem.h"

#include "Buoyancy/SWBuoyancyComponent.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#include "Ship.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "WaterSubsystem.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"
#include "SWRippleWaterWaves.h"
#include "Water/SWRippleStateSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Materials/MaterialParameterCollection.h"
#include "UObject/UnrealType.h"
#include "Room/SWVoyageResetSubsystem.h"

ESWVoyagePolicy USWBuoyancyDebugSubsystem::GetVoyagePolicy_Implementation() const { return ESWVoyagePolicy::Preserve; }
FName USWBuoyancyDebugSubsystem::GetVoyageParticipantId_Implementation() const
{
	USWVoyageResetSubsystem* Voyage = GetWorld()->GetSubsystem<USWVoyageResetSubsystem>();
	return Voyage ? Voyage->ResolveParticipantId(const_cast<USWBuoyancyDebugSubsystem*>(this)) : NAME_None;
}

namespace
{
	FAutoConsoleCommandWithWorld WaveClockAuditCommand(
		TEXT("sw.WaveClockAudit"), TEXT("One-shot wave clock/source audit for the current PIE/game world; no periodic logging."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (!World) return;
			int32 ControllerCount = 0;
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				if (const APlayerController* Controller = It->Get())
				{
					++ControllerCount;
					UE_LOG(LogTemp, Warning, TEXT("[WAVE-CLOCK-AUDIT-CONTROLLER] Actor=%s Class=%s Tick=%d"),
					*Controller->GetPathName(), *Controller->GetClass()->GetPathName(), Controller->IsActorTickEnabled());
				}
			}
			const double Server = World->GetGameState() ? World->GetGameState()->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
			const USWRippleStateSubsystem* Ripple = World->GetSubsystem<USWRippleStateSubsystem>();
			const UWaterSubsystem* Water = UWaterSubsystem::GetWaterSubsystem(World);
			float MPCTime = -1.0f;
			bool bHasMPC = false;
			if (Water && Water->GetMaterialParameterCollection())
			{
				bHasMPC = World->GetParameterCollectionInstance(Water->GetMaterialParameterCollection())
					->GetScalarParameterValue(TEXT("Time"), MPCTime);
			}
			UE_LOG(LogTemp, Warning, TEXT("[WAVE-CLOCK-AUDIT] World=%s NetMode=%d Controllers=%d Local=%.6f GameStateServer=%.6f RippleRender=%.6f WaterTime=%.6f WaterSmoothed=%.6f WaterOverride=%d MPC=%s HasMPCTime=%d MPCTime=%.6f MPCMinusServer=%.6f"),
				*World->GetPathName(), static_cast<int32>(World->GetNetMode()), ControllerCount, World->GetTimeSeconds(), Server,
				Ripple ? Ripple->GetServerTime() : -1.0, Water ? Water->GetWaterTimeSeconds() : -1.0f,
				Water ? Water->GetSmoothedWorldTimeSeconds() : -1.0f, Water && Water->GetShouldOverrideSmoothedWorldTimeSeconds(),
				*GetPathNameSafe(Water ? Water->GetMaterialParameterCollection() : nullptr), bHasMPC, MPCTime, MPCTime - Server);
			for (TActorIterator<AWaterBody> It(World); It; ++It)
			{
				UWaterBodyComponent* Body = It->GetWaterBodyComponent();
				if (!Body) continue;
				const UWaterWavesBase* Waves = Body->GetWaterWaves();
				const USWRippleWaterWaves* Wrapper = Cast<USWRippleWaterWaves>(Waves);
				if (!Wrapper && Waves) Wrapper = Cast<USWRippleWaterWaves>(Waves->GetWaterWaves());
				const float Reference = Body->GetWaveReferenceTime();
				UMaterialInstanceDynamic* MID = Body->GetWaterMaterialInstance();
				float RippleMID = -1.0f;
				float WakeMID = -1.0f;
				const bool bRippleMID = MID && MID->GetScalarParameterValue(FMaterialParameterInfo(TEXT("ServerTime")), RippleMID);
				const bool bWakeMID = MID && MID->GetScalarParameterValue(FMaterialParameterInfo(TEXT("ShipWakeServerTime")), WakeMID);
				UE_LOG(LogTemp, Warning, TEXT("[WAVE-CLOCK-AUDIT-BODY] Body=%s Waves=%s Wrapper=%s BaseWavesAsset=%s WaveReference=%.6f Effective=%.6f MID=%s HasRippleTime=%d RippleMID=%.6f HasWakeTime=%d WakeMID=%.6f (parameter_values_do_not_prove_material_graph_usage)"),
					*It->GetPathName(), *GetPathNameSafe(Waves), *GetPathNameSafe(Wrapper),
					*GetPathNameSafe(Wrapper ? Wrapper->BaseWavesAsset.Get() : nullptr), Reference,
					Wrapper ? Wrapper->ResolveQueryTime(Reference) : Reference, *GetPathNameSafe(MID),
					bRippleMID, RippleMID, bWakeMID, WakeMID);
			}
			for (TActorIterator<AShip> It(World); It; ++It)
			{
				const FDoubleProperty* OriginProperty = FindFProperty<FDoubleProperty>(It->GetClass(), TEXT("ServerPhysicsTimeOrigin"));
				const FFloatProperty* StepProperty = FindFProperty<FFloatProperty>(It->GetClass(), TEXT("ServerPhysicsStepSeconds"));
				UE_LOG(LogTemp, Warning, TEXT("[WAVE-CLOCK-AUDIT-SHIP] Ship=%s Role=%d Origin=%.9f Step=%.9f"),
					*It->GetPathName(), static_cast<int32>(It->GetLocalRole()),
					OriginProperty ? OriginProperty->GetPropertyValue_InContainer(*It) : -1.0,
					StepProperty ? StepProperty->GetPropertyValue_InContainer(*It) : -1.0f);
			}
		}));
}

void USWBuoyancyDebugSubsystem::Tick(float DeltaTime)
{
#if !UE_SERVER
	UWorld* World = GetWorld();
	if (USWVoyageResetSubsystem* Voyage = World ? World->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
		Voyage && Voyage->IsGameplayBlocked()) return;
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
