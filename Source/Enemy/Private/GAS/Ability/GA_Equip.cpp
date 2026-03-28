/*
// Fill out your copyright notice in the Description page of Project Settings.


#include "GAS/Ability/GA_Equip.h"
#include "BaseEnemy.h"

// ArtisticCore
#include "Item/BaseItem.h"

// Unreal Engine
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimMontage.h"

UGA_Equip::UGA_Equip()
{
	// Enemy AI 장착은 서버 권한에서만 실행되도록 덮어쓴다.
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

void UGA_Equip::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// GameplayAbility를 실행하는 주체 저장
	ABaseEnemy* EnemyOwner = Cast<ABaseEnemy>(GetAvatarActorFromActorInfo());
	if (!EnemyOwner)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	
	// 장착할 Weapon 결정
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

	// Item의 PickUpItem 재사용
	WeaponToEquip->SetOwner(EnemyOwner);
	WeaponToEquip->PickUpItem(EnemyOwner);

	// GA에서 PlayMontageAndWait 함수를 사용하는 기본적인 방법
	if (EquipMontage)
	{
		if (UAbilityTask_PlayMontageAndWait* MontageTask =
		UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this,
			FName(TEXT("EquipMontageTask")),
			EquipMontage,
			1.0f,
			NAME_None,
			true))
		{
			MontageTask->OnCompleted.AddDynamic(this, &UGA_Equip::OnEquipMontageCompleted);
			MontageTask->OnInterrupted.AddDynamic(this, &UGA_Equip::OnEquipMontageInterrupted);
			MontageTask->OnCancelled.AddDynamic(this, &UGA_Equip::OnEquipMontageCancelled);
			
			MontageTask->ReadyForActivation();
		}
	}
	

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
	return Cast<ABaseItem>(EnemyOwner->GetCurrentWeapon());
}

void UGA_Equip::OnEquipMontageCompleted()
{
	FinishEquip(false);
}

void UGA_Equip::OnEquipMontageInterrupted()
{
	FinishEquip(true);
}

void UGA_Equip::OnEquipMontageCancelled()
{
	FinishEquip(true);
}

void UGA_Equip::FinishEquip(bool bWasCancelled)
{
	ABaseEnemy* EnemyOwner = Cast<ABaseEnemy>(GetAvatarActorFromActorInfo());

	// 방해 받으면 PickUpItem 실행안됨
	if (!bWasCancelled && PendingWeaponToEquip && EnemyOwner)
	{
		PendingWeaponToEquip->SetOwner(EnemyOwner);
		PendingWeaponToEquip->PickUpItem(EnemyOwner);
	}

	PendingWeaponToEquip = nullptr;

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bWasCancelled);
}
*/
