#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "WeaponAnimationDataAsset.generated.h"

class UAnimMontage;
class UAnimInstance;

USTRUCT(BlueprintType)
struct FWeaponAnimationEntry
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	FGameplayTag WeaponTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> EquipMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> UnequipMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Combat")
	TObjectPtr<UAnimMontage> CombatIntroMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Combat")
	TObjectPtr<UAnimMontage> CombatOutroMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Combat")
	TObjectPtr<UAnimMontage> ReloadMontage;

	// A single montage which owns draw, hold, and release without returning to
	// the normal weapon overlay between sections.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Weapon Action")
	TObjectPtr<UAnimMontage> AimCycleMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Weapon Action")
	FName AimCycleDrawSectionName = TEXT("Bow_Draw");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Weapon Action")
	FName AimCycleHoldSectionName = TEXT("Bow_Hold");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Weapon Action")
	FName AimCycleReleaseSectionName = TEXT("Bow_Release");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Weapon Action", meta = (ClampMin = "0.01"))
	float AimCyclePlayRate = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Weapon Action", meta = (ClampMin = "0.0"))
	float AimCycleBlendOutTime = 0.1f;

	// Optional authoring contract. When enabled by the bow ability, this curve
	// synchronizes the animated bow mesh with the character montage.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Weapon Action")
	FName DrawAlphaCurveName = TEXT("Weapon.DrawAlpha");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Weapon Layer")
	TSubclassOf<UAnimInstance> WeaponAnimLayerClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment")
	FName EquipSocketName = TEXT("GripPoint");

	// Socket on the item mesh that should coincide with EquipSocketName when equipped.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment")
	FName ItemGripSocketName = NAME_None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment")
	FName StoredSocketName = TEXT("BackWeaponSocket");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "0.0"))
	float EquipPlayRate = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "0.0"))
	float UnequipPlayRate = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Combat", meta = (ClampMin = "0.0"))
	float CombatIntroPlayRate = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Combat", meta = (ClampMin = "0.0"))
	float CombatOutroPlayRate = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Combat", meta = (ClampMin = "0.0"))
	float ReloadPlayRate = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upper Body Overlay")
	bool bUseUpperBodyOverlay = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upper Body Overlay")
	FGameplayTag UpperBodyOverlayTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upper Body Overlay", meta = (ClampMin = "0"))
	int32 UpperBodyOverlayIndex = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GAS")
	TMap<FGameplayTag, TSubclassOf<UGameplayAbility>> GrantedAbilitiesByInputTag;
};

UCLASS(BlueprintType)
class CLASSFEATURE_API UWeaponAnimationDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Weapon Animation")
	const FWeaponAnimationEntry& GetDefaultEntry() const { return DefaultEntry; }

	const FWeaponAnimationEntry* FindEntryForTag(const FGameplayTag& ItemTag) const;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Animation")
	FWeaponAnimationEntry DefaultEntry;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Animation")
	TArray<FWeaponAnimationEntry> Entries;
};
