// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GASCore/Public/BaseAttributeSet.h"
#include "EnemyAttributeSet.generated.h"


UCLASS()
class ENEMY_API UEnemyAttributeSet : public UBaseAttributeSet
{
	GENERATED_BODY()

public:
	UEnemyAttributeSet();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	/**
	 * Additive movement-speed bonus used by Enemy buffs.
	 * The active locomotion mode remains the source of the base speed; this
	 * attribute is aggregated by GAS and added only while the base speed is > 0.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Movement", ReplicatedUsing = OnRep_MoveSpeedBonus)
	FGameplayAttributeData MoveSpeedBonus;
	ATTRIBUTE_ACCESSORS(UEnemyAttributeSet, MoveSpeedBonus)

protected:
	UFUNCTION()
	void OnRep_MoveSpeedBonus(const FGameplayAttributeData& OldMoveSpeedBonus);
};
