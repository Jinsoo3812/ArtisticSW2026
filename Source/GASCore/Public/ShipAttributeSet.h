// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseAttributeSet.h"
#include "ShipAttributeSet.generated.h"

/**
 * UBaseAttributeSet을 상속받는 배 전용 AttributeSet입니다.
 * 캐릭터 공통 스탯(Health, MaxHealth, MoveSpeed 등) 외에 배에 특화된 스탯을 포함합니다.
 */
UCLASS()
class GASCORE_API UShipAttributeSet : public UBaseAttributeSet
{
	GENERATED_BODY()

public:
	UShipAttributeSet();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	/* --- Attributes --- */

	// 배의 이동속도 계수 (WS, AD 조작 시 배의 물리 출력에 곱해집니다. 기본값: 1.0f)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Ship", ReplicatedUsing = OnRep_ShipSpeedMultiplier)
	FGameplayAttributeData ShipSpeedMultiplier;
	ATTRIBUTE_ACCESSORS(UShipAttributeSet, ShipSpeedMultiplier)

	// 대포 공격력 (대포알이 발사될 때 적용되는 기본 피해량)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Cannon", ReplicatedUsing = OnRep_CannonDamage)
	FGameplayAttributeData CannonDamage;
	ATTRIBUTE_ACCESSORS(UShipAttributeSet, CannonDamage)

	// 대포 공격속도 (포탄 발사 쿨타임 초 단위 값)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Cannon", ReplicatedUsing = OnRep_CannonFireCooldown)
	FGameplayAttributeData CannonFireCooldown;
	ATTRIBUTE_ACCESSORS(UShipAttributeSet, CannonFireCooldown)

	// 대포 속도 (포탄 발사시 초기 속도)
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Cannon", ReplicatedUsing = OnRep_CannonballSpeed)
	FGameplayAttributeData CannonballSpeed;
	ATTRIBUTE_ACCESSORS(UShipAttributeSet, CannonballSpeed)

protected:
	/* --- RepNotify callbacks --- */

	UFUNCTION()
	virtual void OnRep_ShipSpeedMultiplier(const FGameplayAttributeData& OldShipSpeedMultiplier);

	UFUNCTION()
	virtual void OnRep_CannonDamage(const FGameplayAttributeData& OldCannonDamage);

	UFUNCTION()
	virtual void OnRep_CannonFireCooldown(const FGameplayAttributeData& OldCannonFireCooldown);

	UFUNCTION()
	virtual void OnRep_CannonballSpeed(const FGameplayAttributeData& OldCannonballSpeed);
};
