// Fill out your copyright notice in the Description page of Project Settings.

#include "ShipAttributeSet.h"
#include "Net/UnrealNetwork.h"
#include "GameplayEffectExtension.h"

UShipAttributeSet::UShipAttributeSet()
{
	// 기본값 초기화
	ForwardPropulsionMultiplier = 1.0f;
	TurnTorqueMultiplier = 1.0f;
	CannonDamage = 20.0f;
	CannonFireCooldown = 2.0f;
	CannonballSpeed = 3000.0f;
}

void UShipAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 배 전용 Attribute들의 네트워크 복제 및 RepNotify를 등록합니다.
	DOREPLIFETIME_CONDITION_NOTIFY(UShipAttributeSet, ForwardPropulsionMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UShipAttributeSet, TurnTorqueMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UShipAttributeSet, CannonDamage, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UShipAttributeSet, CannonFireCooldown, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UShipAttributeSet, CannonballSpeed, COND_None, REPNOTIFY_Always);
}

void UShipAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	// 스탯의 최소 보정 로직
	if (Attribute == GetForwardPropulsionMultiplierAttribute()
		|| Attribute == GetTurnTorqueMultiplierAttribute())
	{
		NewValue = FMath::Max(0.0f, NewValue);
	}
	else if (Attribute == GetCannonDamageAttribute())
	{
		NewValue = FMath::Max(0.0f, NewValue);
	}
	else if (Attribute == GetCannonFireCooldownAttribute())
	{
		// 쿨타임이 0이 되어 발생할 수 있는 이상 현상(예: 무한 난사 등)을 막기 위해 최소 0.05초로 보정합니다.
		NewValue = FMath::Max(0.05f, NewValue);
	}
	else if (Attribute == GetCannonballSpeedAttribute())
	{
		NewValue = FMath::Max(0.0f, NewValue);
	}
}

void UShipAttributeSet::OnRep_ForwardPropulsionMultiplier(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UShipAttributeSet, ForwardPropulsionMultiplier, OldValue);
}

void UShipAttributeSet::OnRep_TurnTorqueMultiplier(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UShipAttributeSet, TurnTorqueMultiplier, OldValue);
}

void UShipAttributeSet::OnRep_CannonDamage(const FGameplayAttributeData& OldCannonDamage)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UShipAttributeSet, CannonDamage, OldCannonDamage);
}

void UShipAttributeSet::OnRep_CannonFireCooldown(const FGameplayAttributeData& OldCannonFireCooldown)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UShipAttributeSet, CannonFireCooldown, OldCannonFireCooldown);
}

void UShipAttributeSet::OnRep_CannonballSpeed(const FGameplayAttributeData& OldCannonballSpeed)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UShipAttributeSet, CannonballSpeed, OldCannonballSpeed);
}
