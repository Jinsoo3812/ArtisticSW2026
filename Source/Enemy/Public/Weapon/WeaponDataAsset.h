// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "BaseGameplayTags.h"

#include "WeaponDataAsset.generated.h"

class UBaseGameplayAbility;
class ABaseWeapon;

USTRUCT(BlueprintType)
struct FGrantedWeaponAbility
{
	GENERATED_BODY()

public:
	// 부여할 AbilityClass
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability")
	TSubclassOf<UGameplayAbility> AbilityClass;

	// GE에서 Damage 변주를 위해 활용할 AbilityLevel
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability")
	int32 AbilityLevel = 1;

	// BB에서 홤용할 수 있는 InputTag 아직 활용 정확히 정해지지 않음
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability")
	FGameplayTag InputTag;
};


UCLASS()
class ENEMY_API UWeaponDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UWeaponDataAsset();

public:
	// 무기 대표 식별 태그
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	FGameplayTag WeaponTag;

	// 무기 속성 태그들 (예: Weapon.Type.Melee, Weapon.Type.Ranged)
	// 이후에 무기에 따라 BT에서 적정 싸움 거리를 판단하게 만들 요소
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	FGameplayTagContainer WeaponTypeTags;

	// 실제 스폰할 무기 클래스
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TSubclassOf<ABaseWeapon> WeaponActorClass;

	// Enemy Mesh에 붙일 소켓 이름
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	FName BackSocketName = TEXT("BackSocket");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	FName EquipSocketName = TEXT("EquipSocket");

	// 장착 시 ASC에 부여할 Ability
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ability")
	TArray<FGrantedWeaponAbility> GrantedAbilities;

	// AI 전투 판단용
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Combat")
	float IdealRange = 150.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Combat")
	float MinAttackRange = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Combat")
	float MaxAttackRange = 200.f;

	// 무기 데이터 차원의 공격 간격
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Combat", meta = (ClampMin = "0.0"))
	float AttackCooldown = 1.0f;
};
