// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.generated.h"

// ?댄듃由щ럭???묎렐???명븯寃??댁＜???몃━???쒓났 留ㅽ겕濡?
#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

UCLASS()
class GASCORE_API UBaseAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UBaseAttributeSet();

	/*
	* ?ㅽ듃?뚰겕 ?쒖뒪?쒖씠 ?대옒?ㅼ쓽 Replication ?덉씠?꾩썐??援ъ꽦?????몄텧?섎뒗 ?⑥닔
	* ?숆린?붿뿉 ?깅줉??蹂???깆쓣 吏??
	*/
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Attribute 媛믪씠 蹂寃쎈릺湲?吏곸쟾???몄텧
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	/*
	* GameplayEffect媛 ?곸슜?????몄텧
	* @param Data - GameplayEffect???곸슜??????뺣낫媛 ?닿릿 援ъ“泥?(?대뼡 Attribute媛 蹂寃쎈릺?덈뒗吏 ??
	*/
	virtual void PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data) override;
	
	//Attribute 媛믪씠 蹂寃쎈맂 ?꾩뿉 ?몄텧
	virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;
	
	/* --- Attributes --- */

	// 泥대젰
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_Health)
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS(UBaseAttributeSet, Health)

	// 理쒕? 泥대젰
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_MaxHealth)
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS(UBaseAttributeSet, MaxHealth)

	// 怨듦꺽??
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_AttackPower)
	FGameplayAttributeData AttackPower;
	ATTRIBUTE_ACCESSORS(UBaseAttributeSet, AttackPower)

	// ?대룞 ?띾룄
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_MoveSpeed)
	FGameplayAttributeData MoveSpeed;
	ATTRIBUTE_ACCESSORS(UBaseAttributeSet, MoveSpeed)

protected:
	/* --- RepNotify ?⑥닔??(?쒕쾭?먯꽌 媛믪씠 蹂寃쎈릺?덉쓣 ???대씪?댁뼵?몄뿉???숆린?뷀븯湲??꾪빐 ?몄텧?? --- */

	UFUNCTION()
	virtual void OnRep_Health(const FGameplayAttributeData& OldHealth);

	UFUNCTION()
	virtual void OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth);

	UFUNCTION()
	virtual void OnRep_AttackPower(const FGameplayAttributeData& OldAttackPower);

	UFUNCTION()
	virtual void OnRep_MoveSpeed(const FGameplayAttributeData& OldMoveSpeed);
};
