#include "ShipAI/EnemyShipSkillModuleData.h"

#include "Abilities/GameplayAbility.h"
#include "BaseGameplayTags.h"
#include "Misc/DataValidation.h"

FGameplayTag UEnemyShipSkillModuleData::GetAbilityTag() const
{
	const UGameplayAbility* AbilityCDO = AbilityClass
		? AbilityClass->GetDefaultObject<UGameplayAbility>()
		: nullptr;
	if (!AbilityCDO)
	{
		return FGameplayTag();
	}

	const FGameplayTagContainer& AssetTags = AbilityCDO->GetAssetTags();
	for (const FGameplayTag& Tag : AssetTags)
	{
		if (Tag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("GameplayAbility.EnemyShip"), false)))
		{
			return Tag;
		}
	}
	return FGameplayTag();
}

EDataValidationResult UEnemyShipSkillModuleData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!AbilityClass)
	{
		Context.AddError(FText::FromString(TEXT("Enemy Ship Skill Module requires an AbilityClass.")));
		Result = EDataValidationResult::Invalid;
	}
	else if (!GetAbilityTag().IsValid())
	{
		Context.AddError(FText::FromString(TEXT("AbilityClass must provide a GameplayAbility.EnemyShip asset tag.")));
		Result = EDataValidationResult::Invalid;
	}
	if (Weight < 0.0f)
	{
		Context.AddError(FText::FromString(TEXT("Skill Module Weight must not be negative.")));
		Result = EDataValidationResult::Invalid;
	}
	if (GetAbilityTag() == GameplayAbility_EnemyShip_CannonVolley)
	{
		if (CannonVolleySettings.MinimumElevationDegrees < 0.0f
			|| CannonVolleySettings.MinimumElevationDegrees > 45.0f)
		{
			Context.AddError(FText::FromString(TEXT("Cannon Volley MinimumElevationDegrees must be in [0, 45].")));
			Result = EDataValidationResult::Invalid;
		}
		if (CannonVolleySettings.ImpactEllipseSemiMajorAxisCm <= 0.0f
			|| CannonVolleySettings.ImpactEllipseSemiMinorAxisCm <= 0.0f)
		{
			Context.AddError(FText::FromString(TEXT("Cannon Volley ellipse semi-axes must be positive.")));
			Result = EDataValidationResult::Invalid;
		}
		if (CannonVolleySettings.AttackerFacingHalfWeight < 0.0f
			|| CannonVolleySettings.AttackerOppositeHalfWeight < 0.0f)
		{
			Context.AddError(FText::FromString(TEXT("Cannon Volley half weights must not be negative.")));
			Result = EDataValidationResult::Invalid;
		}
		if (CannonVolleySettings.AttackerFacingHalfWeight
			+ CannonVolleySettings.AttackerOppositeHalfWeight <= 0.0f)
		{
			Context.AddError(FText::FromString(TEXT("Cannon Volley half weights must have a positive sum.")));
			Result = EDataValidationResult::Invalid;
		}
	}
	return Result;
}
