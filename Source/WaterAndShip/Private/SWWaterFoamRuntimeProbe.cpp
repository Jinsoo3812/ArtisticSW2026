#include "SWWaterFoamRuntimeProbe.h"

#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "RHI.h"
#include "SWPersistentFoamField.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWWaterFoamProbe, Log, All);

ASWWaterFoamRuntimeProbe::ASWWaterFoamRuntimeProbe()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
}

void ASWWaterFoamRuntimeProbe::BeginPlay()
{
	Super::BeginPlay();

	LogRuntimeState(TEXT("BeginPlay"));

	FTimerHandle TimerHandle;
	GetWorldTimerManager().SetTimer(
		TimerHandle,
		FTimerDelegate::CreateWeakLambda(
			this,
			[this]()
			{
				LogRuntimeState(TEXT("After2s"));
			}),
		2.0f,
		false);
}

void ASWWaterFoamRuntimeProbe::LogRuntimeState(const FString& Phase) const
{
	const UWorld* World = GetWorld();
	UE_LOG(
		LogSWWaterFoamProbe,
		Display,
		TEXT("[RW-RUNTIME] phase=%s world=%s rhi=%s feature=%s time=%.3f"),
		*Phase,
		*GetNameSafe(World),
		GDynamicRHI ? GDynamicRHI->GetName() : TEXT("None"),
		GMaxRHIFeatureLevel == ERHIFeatureLevel::SM6 ? TEXT("SM6") : TEXT("NonSM6"),
		World ? World->GetTimeSeconds() : 0.0);

	int32 WaterBodyCount = 0;
	int32 FoamFieldCount = 0;
	if (!World)
	{
		return;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		const AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}

		const FString ClassName = Actor->GetClass()->GetName();
		if (ClassName.Contains(TEXT("WaterBody")))
		{
			++WaterBodyCount;
			UE_LOG(
				LogSWWaterFoamProbe,
				Display,
				TEXT("[RW-RUNTIME] water_actor=%s class=%s loc=%s"),
				*Actor->GetName(),
				*ClassName,
				*Actor->GetActorLocation().ToCompactString());

			TArray<UActorComponent*> Components;
			Actor->GetComponents(Components);
			for (const UActorComponent* Component : Components)
			{
				if (!IsValid(Component) || !Component->GetClass()->GetName().Contains(TEXT("WaterBody")))
				{
					continue;
				}

				const FObjectProperty* WaterMaterialProperty =
					FindFProperty<FObjectProperty>(Component->GetClass(), TEXT("WaterMaterial"));
				UObject* WaterMaterialObject = WaterMaterialProperty
					? WaterMaterialProperty->GetObjectPropertyValue_InContainer(Component)
					: nullptr;
				UE_LOG(
					LogSWWaterFoamProbe,
					Display,
					TEXT("[RW-RUNTIME] water_component=%s class=%s water_material=%s"),
					*Component->GetName(),
					*Component->GetClass()->GetName(),
					*GetNameSafe(WaterMaterialObject));
			}
		}

		const ASWPersistentFoamField* FoamField = Cast<ASWPersistentFoamField>(Actor);
		if (FoamField)
		{
			++FoamFieldCount;
			const UTextureRenderTarget2D* FoamState = FoamField->GetCurrentFoamState();
			UE_LOG(
				LogSWWaterFoamProbe,
				Display,
				TEXT("[RW-RUNTIME] foam_field=%s state=%s size=%dx%d"),
				*FoamField->GetName(),
				*GetNameSafe(FoamState),
				FoamState ? FoamState->SizeX : 0,
				FoamState ? FoamState->SizeY : 0);
		}
	}

	UE_LOG(
		LogSWWaterFoamProbe,
		Display,
		TEXT("[RW-RUNTIME] summary water_bodies=%d persistent_foam_fields=%d verdict=%s"),
		WaterBodyCount,
		FoamFieldCount,
		FoamFieldCount > 0 ? TEXT("CPU/GPU persistent foam path present") : TEXT("No persistent foam producer in this map"));
}

