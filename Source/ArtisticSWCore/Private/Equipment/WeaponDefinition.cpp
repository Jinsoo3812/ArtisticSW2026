#include "Equipment/WeaponDefinition.h"

#include "Equipment/WeaponAnimationDataAsset.h"

DEFINE_LOG_CATEGORY_STATIC(LogWeaponBalance, Log, All);

void UEquippableWeaponDefinition::PostLoad()
{
	Super::PostLoad();
	FString Error;
	if (!RefreshBalanceData(Error))
		UE_LOG(LogWeaponBalance, Error, TEXT("%s: %s"), *GetPathName(), *Error);
}

bool UEquippableWeaponDefinition::RefreshBalanceData(FString& OutError)
{
	OutError.Reset();
	if (BalanceRow.IsNull()) return true;
	auto Fail = [this, &OutError](const TCHAR* Error)
	{
		// A bad row must not silently fall back to obsolete per-item values.
		ActorClass.Reset();
		AbilitySet = nullptr;
		AnimationData = nullptr;
		CombatData = nullptr;
		AllowedRoleTags.Reset();
		EquipEffects.Reset();
		OutError = Error;
		return false;
	};
	if (!BalanceRow.DataTable || BalanceRow.RowName.IsNone()
		|| BalanceRow.DataTable->GetRowStruct() != FWeaponBalanceRow::StaticStruct())
		return Fail(TEXT("Missing or incompatible weapon balance table/row."));
	const FWeaponBalanceRow* Row = BalanceRow.DataTable->FindRow<FWeaponBalanceRow>(BalanceRow.RowName, TEXT("Weapon balance"), false);
	if (!Row || !Row->ItemTag.IsValid() || !Row->Profile)
		return Fail(TEXT("Weapon balance row, item identity or family profile is missing."));
	const UWeaponFamilyProfile* Profile = Row->Profile;
	if (Profile->ActorClass.IsNull() || !Profile->AbilitySet || Profile->AbilitySet->Abilities.IsEmpty() || !Profile->AnimationData)
		return Fail(TEXT("Weapon family actor, ability set or animation data is missing."));
	if (!FMath::IsFinite(Row->StrengthBonus) || Row->StrengthBonus < 0.f
		|| !FMath::IsFinite(Row->AttackCoefficient) || Row->AttackCoefficient <= 0.f
		|| !FMath::IsFinite(Row->BasicAttackPlayRate) || Row->BasicAttackPlayRate <= 0.f
		|| !FMath::IsFinite(Row->AimCyclePlayRate) || Row->AimCyclePlayRate <= 0.f)
		return Fail(TEXT("Weapon balance contains invalid strength, damage coefficient or animation speed."));

	UWeaponCombatDataAsset* Combat = NewObject<UWeaponCombatDataAsset>(this, NAME_None, RF_Transient);
	Combat->StrengthBonus = Row->StrengthBonus;
	Combat->AttackCoefficient = Row->AttackCoefficient;
	Combat->ProjectileClass = Profile->ProjectileClass;
	UWeaponAnimationDataAsset* Animation = Profile->AnimationData->CreateSpeedVariant(this,
		Row->BasicAttackPlayRate, Row->AimCyclePlayRate);
	ActorClass = Profile->ActorClass;
	AbilitySet = Profile->AbilitySet;
	AnimationData = Animation;
	CombatData = Combat;
	AllowedRoleTags = Profile->AllowedRoleTags;
	EquipEffects = Profile->EquipEffects;
	return true;
}
