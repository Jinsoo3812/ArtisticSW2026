#include "ShipAI/EnemyShipArchetypeData.h"

#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "Misc/DataValidation.h"
#include "Ship.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipAbilitySet.h"
#include "ShipAI/EnemyShipNavigationComponent.h"
#include "ShipAI/EnemyShipPatternData.h"
#include "ShipAI/EnemyShipPatternRuntimeComponent.h"

bool UEnemyShipArchetypeData::ApplyToShip(AEnemyShip* Ship) const
{
	if (!IsValid(Ship) || !Ship->HasAuthority() || !Pattern)
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
		if (FMath::IsNearlyEqual(Row->ForwardPropulsionMultiplier, 1.0f)
			&& FMath::IsNearlyEqual(Row->TurnTorqueMultiplier, 1.0f)
			&& !FMath::IsNearlyEqual(Row->ShipSpeedMultiplier, 1.0f))
		{
			Snapshot.ForwardPropulsionMultiplier = Row->ShipSpeedMultiplier;
			Snapshot.TurnTorqueMultiplier = Row->ShipSpeedMultiplier;
		}
		Ship->ApplyStatSnapshot(Snapshot, true);
	}

	if (UEnemyShipNavigationComponent* Navigation = Ship->GetNavigationComponent())
	{
		Navigation->SetNavigationProfile(Pattern->NavigationProfile);
	}
	if (UEnemyShipPatternRuntimeComponent* Runtime = Ship->GetPatternRuntimeComponent())
	{
		Runtime->SetPattern(Pattern);
	}
	Ship->GrantEnemyShipAbilities(AbilitySet);
	return true;
}

EDataValidationResult UEnemyShipArchetypeData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!Pattern)
	{
		Context.AddError(FText::FromString(TEXT("Enemy Ship Archetype requires a Pattern.")));
		Result = EDataValidationResult::Invalid;
	}
	if (!AbilitySet)
	{
		Context.AddError(FText::FromString(TEXT("Enemy Ship Archetype requires an Ability Set.")));
		Result = EDataValidationResult::Invalid;
	}
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

	if (Pattern && AbilitySet)
	{
		FGameplayTagContainer GrantedTags;
		for (const TSubclassOf<UGameplayAbility>& AbilityClass : AbilitySet->Abilities)
		{
			if (const UGameplayAbility* AbilityCDO = AbilityClass ? AbilityClass->GetDefaultObject<UGameplayAbility>() : nullptr)
			{
				GrantedTags.AppendTags(AbilityCDO->GetAssetTags());
			}
		}
		for (const FEnemyShipSkillRule& Rule : Pattern->SkillRules)
		{
			if (Rule.AbilityTag.IsValid() && !GrantedTags.HasTagExact(Rule.AbilityTag))
			{
				Context.AddError(FText::Format(
					NSLOCTEXT("EnemyShipArchetype", "MissingAbility", "Pattern ability {0} is not granted by AbilitySet."),
					FText::FromString(Rule.AbilityTag.ToString())));
				Result = EDataValidationResult::Invalid;
			}
		}
	}
	return Result;
}
