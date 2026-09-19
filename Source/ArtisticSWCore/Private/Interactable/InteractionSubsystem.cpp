#include "Interactable/InteractionSubsystem.h"

#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Interactable/InteractionSettings.h"

void UInteractionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UWorld* World = GetWorld();
	if (World && !World->IsGameWorld())
	{
		return;
	}

	const UInteractionSettings* Settings = GetDefault<UInteractionSettings>();
	UDataTable* DataTable = Settings ? Settings->InteractionFeatureDataTable.LoadSynchronous() : nullptr;
	if (!DataTable)
	{
		UE_LOG(LogTemp, Warning, TEXT("[InteractionSubsystem] Interaction Feature Data Table is not configured."));
		return;
	}

	static const FString ContextString(TEXT("Interaction Feature Initialization"));
	for (const FName RowName : DataTable->GetRowNames())
	{
		const FGameplayTag InteractableIdTag = FGameplayTag::RequestGameplayTag(RowName, false);
		const FInteractionFeatureData* Row = DataTable->FindRow<FInteractionFeatureData>(
			RowName,
			ContextString,
			false);
		if (!InteractableIdTag.IsValid())
		{
			UE_LOG(LogTemp, Error,
				TEXT("[InteractionSubsystem] Row '%s' is not a registered Gameplay Tag."),
				*RowName.ToString());
			continue;
		}

		if (Row)
		{
			CachedInteractionFeatures.Add(InteractableIdTag, *Row);
		}
	}

	UE_LOG(LogTemp, Log,
		TEXT("[InteractionSubsystem] Cached %d interaction features."),
		CachedInteractionFeatures.Num());
}

void UInteractionSubsystem::Deinitialize()
{
	CachedInteractionFeatures.Reset();
	Super::Deinitialize();
}

const FInteractionFeatureData* UInteractionSubsystem::GetInteractionFeature(
	const FGameplayTag InteractableIdTag) const
{
	return InteractableIdTag.IsValid()
		? CachedInteractionFeatures.Find(InteractableIdTag)
		: nullptr;
}
