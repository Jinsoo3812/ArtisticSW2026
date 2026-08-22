// Fill out your copyright notice in the Description page of Project Settings.


#include "GAS/EnemyAttributeSet.h"

#include "Net/UnrealNetwork.h"

UEnemyAttributeSet::UEnemyAttributeSet()
{
	MoveSpeedBonus = 0.0f;
}

void UEnemyAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(
		UEnemyAttributeSet,
		MoveSpeedBonus,
		COND_None,
		REPNOTIFY_Always);
}

void UEnemyAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetMoveSpeedBonusAttribute())
	{
		NewValue = FMath::Max(0.0f, NewValue);
	}
}

void UEnemyAttributeSet::OnRep_MoveSpeedBonus(const FGameplayAttributeData& OldMoveSpeedBonus)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UEnemyAttributeSet, MoveSpeedBonus, OldMoveSpeedBonus);
}
