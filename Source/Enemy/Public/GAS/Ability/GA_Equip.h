// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseGameplayAbility.h"
#include "GA_Equip.generated.h"

class ABaseEnemy;
class ABaseItem;

UCLASS()
class ENEMY_API UGA_Equip : public UBaseGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Equip();

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	/** TriggerEventData의 OptionalObject 또는 Enemy의 DefaultWeapon에서 장착할 무기를 찾는다 */
	virtual ABaseItem* ResolveWeaponToEquip(ABaseEnemy* EnemyOwner, const FGameplayEventData* TriggerEventData) const;
};
