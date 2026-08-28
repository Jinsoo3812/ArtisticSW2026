// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.generated.h"

// Attribute 접근자 함수를 한 번에 생성하는 GAS 표준 매크로입니다.
// Getter, Setter, Init 함수를 Blueprint/C++ 양쪽에서 일관되게 사용할 수 있게 합니다.
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

	// 네트워크로 복제할 Attribute와 RepNotify 방식을 등록합니다.
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Attribute 값이 바뀌기 직전에 호출됩니다. 주로 최대/최소값 보정에 사용합니다.
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	/** Rejects every GAS damage path while the target owns State.Invulnerable. */
	virtual bool PreGameplayEffectExecute(struct FGameplayEffectModCallbackData& Data) override;

	// GameplayEffect 실행이 끝난 뒤 호출됩니다. 피해/회복 같은 최종 보정을 처리합니다.
	virtual void PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data) override;
	
	// Attribute 값이 실제로 변경된 뒤 호출됩니다. UI 이벤트나 파생값 갱신에 사용할 수 있습니다.
	virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;
	
	/* --- Attributes --- */

	// 현재 체력입니다. 0이 되면 사망 처리의 기준이 됩니다.
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_Health)
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS(UBaseAttributeSet, Health)

	// 최대 체력입니다. Health는 이 값을 넘지 않도록 보정됩니다.
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_MaxHealth)
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS(UBaseAttributeSet, MaxHealth)

	// 기본 공격력입니다. Damage GameplayEffect를 만들 때 기본 피해량으로 사용할 수 있습니다.
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_AttackPower)
	FGameplayAttributeData AttackPower;
	ATTRIBUTE_ACCESSORS(UBaseAttributeSet, AttackPower)

	// Strength-based attacks snapshot this value when their damage spec is created.
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_Strength)
	FGameplayAttributeData Strength;
	ATTRIBUTE_ACCESSORS(UBaseAttributeSet, Strength)

	// 이동 속도입니다. 캐릭터 MovementComponent의 속도와 동기화할 때 사용합니다.
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_MoveSpeed)
	FGameplayAttributeData MoveSpeed;
	ATTRIBUTE_ACCESSORS(UBaseAttributeSet, MoveSpeed)

	// 공격 애니메이션과 다음 공격까지의 회복 속도에 함께 곱해지는 배율입니다.
	// 1.0은 정상 속도, 0.5는 공격 모션과 공격 주기가 모두 절반 속도입니다.
	UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_AttackSpeedMultiplier)
	FGameplayAttributeData AttackSpeedMultiplier;
	ATTRIBUTE_ACCESSORS(UBaseAttributeSet, AttackSpeedMultiplier)


	// -------------------------------------------------------------------
	// Meta Attributes

	// 피해량을 임시로 전달받는 메타 Attribute입니다. GE 처리 후 Health에 반영하고 즉시 0으로 되돌립니다.
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Meta")
	FGameplayAttributeData Damage;
	ATTRIBUTE_ACCESSORS(UBaseAttributeSet, Damage)

	// 회복량을 임시로 전달받는 메타 Attribute입니다. GE 처리 후 Health에 반영하고 즉시 0으로 되돌립니다.
	UPROPERTY(BlueprintReadOnly, Category = "Attributes|Meta")
	FGameplayAttributeData Healing;
	ATTRIBUTE_ACCESSORS(UBaseAttributeSet, Healing)

protected:
	/* --- RepNotify callbacks --- */

	// 서버에서 복제된 Health 변경을 클라이언트 ASC에 알립니다.
	UFUNCTION()
	virtual void OnRep_Health(const FGameplayAttributeData& OldHealth);

	// 서버에서 복제된 MaxHealth 변경을 클라이언트 ASC에 알립니다.
	UFUNCTION()
	virtual void OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth);

	// 서버에서 복제된 AttackPower 변경을 클라이언트 ASC에 알립니다.
	UFUNCTION()
	virtual void OnRep_AttackPower(const FGameplayAttributeData& OldAttackPower);

	UFUNCTION()
	virtual void OnRep_Strength(const FGameplayAttributeData& OldStrength);

	// 서버에서 복제된 MoveSpeed 변경을 클라이언트 ASC에 알립니다.
	UFUNCTION()
	virtual void OnRep_MoveSpeed(const FGameplayAttributeData& OldMoveSpeed);

	UFUNCTION()
	virtual void OnRep_AttackSpeedMultiplier(const FGameplayAttributeData& OldAttackSpeedMultiplier);
};
