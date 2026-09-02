#include "ShipAI/EnemyShipArchetypeData.h"

#include "Misc/DataValidation.h"
#include "Ship.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipSkillModuleData.h"

bool UEnemyShipArchetypeData::ApplyToShip(AEnemyShip* Ship)
{
	if (!IsValid(Ship) || !Ship->HasAuthority())
	{
		return false;
	}

	if (SpecRow.DataTable && !SpecRow.RowName.IsNone())
	{
		static const FString Context(TEXT("Enemy Ship Archetype"));
		const FShipStatRow* Row = SpecRow.GetRow<FShipStatRow>(Context);
		if (!Row)
		{
			return false;
		}

		FShipStatSnapshot Snapshot;
		Snapshot.MaxHealth = Row->MaxHealth;
		Snapshot.CannonDamage = Row->CannonDamage;
		Snapshot.CannonFireCooldownSeconds = Row->CannonFireCooldown;
		Snapshot.CannonballSpeed = Row->CannonballSpeed;
		Snapshot.ForwardPropulsionMultiplier = Row->ForwardPropulsionMultiplier;
		Snapshot.TurnTorqueMultiplier = Row->TurnTorqueMultiplier;
		Ship->ApplyStatSnapshot(Snapshot, true);
	}

	return Ship->ConfigureEnemyShipArchetype(this);
}

EDataValidationResult UEnemyShipArchetypeData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if ((SpecRow.DataTable == nullptr) != SpecRow.RowName.IsNone())
	{
		Context.AddError(FText::FromString(TEXT("SpecRow must provide both DataTable and RowName, or neither.")));
		Result = EDataValidationResult::Invalid;
	}
	else if (SpecRow.DataTable && !SpecRow.GetRow<FShipStatRow>(TEXT("Enemy Ship Archetype Validation")))
	{
		Context.AddError(FText::FromString(TEXT("SpecRow does not resolve to FShipStatRow.")));
		Result = EDataValidationResult::Invalid;
	}

	if (ZeroHealthCannonCooldownMultiplier < 1.0f)
	{
		Context.AddError(FText::FromString(TEXT("ZeroHealthCannonCooldownMultiplier must be at least 1.")));
		Result = EDataValidationResult::Invalid;
	}
	if (NavigationProfile.DangerCloseDistance > NavigationProfile.IdealDistance)
	{
		Context.AddError(FText::FromString(TEXT("DangerCloseDistance must not exceed IdealDistance.")));
		Result = EDataValidationResult::Invalid;
	}
	if (NavigationProfile.ReturnTriggerDistance < NavigationProfile.ReturnArrivalDistance)
	{
		Context.AddError(FText::FromString(TEXT("ReturnTriggerDistance must be at least ReturnArrivalDistance.")));
		Result = EDataValidationResult::Invalid;
	}

	TSet<const UEnemyShipSkillModuleData*> SeenModules;
	TSet<FGameplayTag> SeenAbilityTags;
	for (const UEnemyShipSkillModuleData* Module : SkillModules)
	{
		if (!Module || SeenModules.Contains(Module))
		{
			Context.AddError(FText::FromString(TEXT("SkillModules must contain unique non-null modules.")));
			Result = EDataValidationResult::Invalid;
			continue;
		}
		SeenModules.Add(Module);
		const FGameplayTag AbilityTag = Module->GetAbilityTag();
		if (!AbilityTag.IsValid() || SeenAbilityTags.Contains(AbilityTag))
		{
			Context.AddError(FText::FromString(TEXT("SkillModules must resolve to unique valid EnemyShip ability tags.")));
			Result = EDataValidationResult::Invalid;
		}
		SeenAbilityTags.Add(AbilityTag);
		if (SelectionPolicy == EEnemyShipSkillSelectionPolicy::WeightedRandom && Module->Weight <= 0.0f)
		{
			Context.AddError(FText::FromString(TEXT("WeightedRandom modules require Weight > 0.")));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}
