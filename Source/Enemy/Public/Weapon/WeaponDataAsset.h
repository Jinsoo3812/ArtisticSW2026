// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "BaseGameplayTags.h"

#include "WeaponDataAsset.generated.h"

class UGameplayAbility;
class ABaseWeapon;

USTRUCT(BlueprintType)
struct FGrantedWeaponAbility
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ability")
	TSubclassOf<UGameplayAbility> AbilityClass = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ability", meta = (ClampMin = "1"))
	int32 AbilityLevel = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ability")
	FGameplayTag InputTag;
};

USTRUCT(BlueprintType)
struct FWeaponSocketData
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FName BackSocketName = TEXT("BackSocket");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FName EquipSocketName = TEXT("EquipSocket");
};

// 무기마다 적절한 전투 변수들을 만들어놓고 이후 BT에서 읽어서 사용하기
USTRUCT(BlueprintType)
struct FWeaponCombatData
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	float IdealRange = 150.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	float MinAttackRange = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	float MaxAttackRange = 200.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0.0"))
	float AttackCooldown = 1.f;
};

USTRUCT(BlueprintType)
struct FWeaponAbilityData
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TArray<FGrantedWeaponAbility> GrantedAbilities;
};

USTRUCT(BlueprintType)
struct FWeaponDefinition
{
	GENERATED_BODY()
	// 무기를 식별하기 위한 WeaponTag
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FGameplayTag WeaponTag;

	// 무기 Type을 보관할 WeaponTypeTags
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FGameplayTagContainer WeaponTypeTags;

	// 무기의 실제 액터 클래스
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSubclassOf<ABaseWeapon> WeaponActorClass;

	// 무기의 Socket
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FWeaponSocketData SocketData;

	// 무기의 Ability
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FWeaponAbilityData AbilityData;

	// 무기의 전투 스타일을 결정
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FWeaponCombatData CombatData;
};


UCLASS(BlueprintType)
class ENEMY_API UWeaponDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Weapon")
	TArray<FWeaponDefinition> WeaponDefinitions;

	const FWeaponDefinition* FindWeaponDefinitionByTag(FGameplayTag InTag) const
	{
		for (const FWeaponDefinition& Definition : WeaponDefinitions){
			if (Definition.WeaponTag == InTag)
				return &Definition;
		}
		return nullptr;
	}
};