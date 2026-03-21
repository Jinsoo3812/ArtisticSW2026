// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseGameplayAbility.h"
#include "GA_Equip.generated.h"

class ABaseEnemy;
class ABaseItem;
class UAnimMontage;

UCLASS()
class ENEMY_API UGA_Equip : public UBaseGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Equip();

protected:
	// 장착 Montage
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EquipMontage")
	TObjectPtr<UAnimMontage> EquipMontage;

	// 장착될 무기
	UPROPERTY()
	TObjectPtr<ABaseItem> PendingWeaponToEquip;

public:
	UFUNCTION()
	void OnEquipMontageCompleted();

	UFUNCTION()
	void OnEquipMontageInterrupted();

	UFUNCTION()
	void OnEquipMontageCancelled();

	// Montage이후 EndAbility로 이어지는 Helper함수
	void FinishEquip(bool bWasCancelled);
	
protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	/** TriggerEventData의 OptionalObject 또는 Enemy의 DefaultWeapon에서 장착할 무기를 찾는다 */
	virtual ABaseItem* ResolveWeaponToEquip(ABaseEnemy* EnemyOwner, const FGameplayEventData* TriggerEventData) const;
};
