#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
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

/** Single source of truth for a weapon; item identity and inventory stay in ItemData. */
UCLASS(BlueprintType)
class ARTISTICSWCORE_API UEquippableWeaponDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
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
