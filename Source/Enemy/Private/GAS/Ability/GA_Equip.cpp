// Fill out your copyright notice in the Description page of Project Settings.


#include "GAS/Ability/GA_Equip.h"
#include "BaseEnemy.h"

// ArtisticCore
#include "Item/BaseItem.h"

UGA_Equip::UGA_Equip()
{
	// Enemy AI 장착은 서버 권한에서만 실행되도록 덮어쓴다.
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

void UGA_Equip::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ABaseEnemy* EnemyOwner = Cast<ABaseEnemy>(GetAvatarActorFromActorInfo());
	if (!EnemyOwner)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ABaseItem* WeaponToEquip = ResolveWeaponToEquip(EnemyOwner, TriggerEventData);
	if (!WeaponToEquip)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	WeaponToEquip->SetOwner(EnemyOwner);
	WeaponToEquip->PickUpItem(EnemyOwner);

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

ABaseItem* UGA_Equip::ResolveWeaponToEquip(ABaseEnemy* EnemyOwner, const FGameplayEventData* TriggerEventData) const
{
	
	if (!EnemyOwner)
	{
		return nullptr;
	}
	
	// 1. 이벤트로 전달된 무기가 있으면 그걸 우선 사용
	if (TriggerEventData)
	{
		if (UObject* OptionalObject = const_cast<UObject*>(TriggerEventData->OptionalObject.Get()))
		{
			if (ABaseItem* EventWeapon = Cast<ABaseItem>(OptionalObject))
			{
				return EventWeapon;
			}
		}
	}

	
	// 2. 없으면 Enemy의 기본 무기 사용
	return Cast<ABaseItem>(EnemyOwner->GetDefaultWeapon());
}
