#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "WeaponDefinition.generated.h"

class ABaseItem;
class UGameplayAbility;
class UGameplayEffect;
class UWeaponAnimationDataAsset;

USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FWeaponAbilityEntry
{
	GENERATED_BODY()
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(Categories="Key"))
	FGameplayTag InputTag;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSubclassOf<UGameplayAbility> AbilityClass;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="1"))
	int32 Level = 1;
};

/** Declarative grants. Only the equipment component owns their lifetime. */
UCLASS(BlueprintType)
class ARTISTICSWCORE_API UWeaponAbilitySet : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TArray<FWeaponAbilityEntry> Abilities;
};

UCLASS(BlueprintType)
class ARTISTICSWCORE_API UWeaponCombatDataAsset : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0"))
	float StrengthBonus = 0.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0"))
	float AttackCoefficient = 1.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSoftClassPtr<AActor> ProjectileClass;
};

/** Shared assets for a weapon family. Per-item numeric balance belongs in FWeaponBalanceRow. */
UCLASS(BlueprintType)
class ARTISTICSWCORE_API UWeaponFamilyProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TSoftClassPtr<ABaseItem> ActorClass;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UWeaponAbilitySet> AbilitySet;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UWeaponAnimationDataAsset> AnimationData;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TSoftClassPtr<AActor> ProjectileClass;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	FGameplayTagContainer AllowedRoleTags;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TArray<TSubclassOf<UGameplayEffect>> EquipEffects;
};

/** One authored row per item. Profile supplies assets; all per-item numeric values live here. */
USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FWeaponBalanceRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (Categories = "Item.Id.Weapon"))
	FGameplayTag ItemTag;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UWeaponFamilyProfile> Profile;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "0"))
	float StrengthBonus = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "0.01"))
	float AttackCoefficient = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "0.01"))
	float BasicAttackPlayRate = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "0.01"))
	float AimCyclePlayRate = 1.f;
};

/** Transient runtime view built from a catalog DT row. No per-item DA is authored. */
UCLASS(BlueprintType)
class ARTISTICSWCORE_API UEquippableWeaponDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	virtual void PostLoad() override;

	/** Empty retains legacy standalone behavior. Authored catalog weapons use this row as authority. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Balance", meta = (RowType = "/Script/ArtisticSWCore.WeaponBalanceRow"))
	FDataTableRowHandle BalanceRow;

	/** Rebuild runtime-only data after editing the table; errors clear the resolved definition. */
	bool RefreshBalanceData(FString& OutError);

	// Resolved compatibility fields. For table-backed weapons edit BalanceRow/Profile, not these values.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSoftClassPtr<ABaseItem> ActorClass;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<UWeaponAbilitySet> AbilitySet;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<UWeaponAnimationDataAsset> AnimationData;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<UWeaponCombatDataAsset> CombatData;
	/** Empty means any class. Any one of these role tags is sufficient. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FGameplayTagContainer AllowedRoleTags;
	/** Additional persistent effects, not the strength GE owned by EquipmentStatComponent. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TArray<TSubclassOf<UGameplayEffect>> EquipEffects;
};
