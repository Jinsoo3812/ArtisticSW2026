// Fill out your copyright notice in the Description page of Project Settings.


#include "BaseAttributeSet.h"
#include "Net/UnrealNetwork.h"
#include "GameplayEffectExtension.h"
#include "Abilities/GameplayAbility.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Animation/AnimInstance.h"
#include "BaseGameplayTags.h"

#include "Engine/Engine.h"

UBaseAttributeSet::UBaseAttributeSet()
{
	// 기본 체력값입니다. 이후 초기화 GE나 캐릭터별 AttributeSet에서 덮어쓸 수 있습니다.
	MaxHealth = 100.0f;
	Health = 100.0f;
	Strength = 10.0f;
	AttackSpeedMultiplier = 1.0f;
}

void UBaseAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 모든 클라이언트에 Attribute를 복제하고, 예측/롤백 보정을 위해 항상 RepNotify를 호출합니다.
	DOREPLIFETIME_CONDITION_NOTIFY(UBaseAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBaseAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBaseAttributeSet, AttackPower, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBaseAttributeSet, Strength, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBaseAttributeSet, MoveSpeed, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBaseAttributeSet, AttackSpeedMultiplier, COND_None, REPNOTIFY_Always);

	// Damage와 Healing은 GE 실행 중에만 쓰는 메타 Attribute라 복제하지 않습니다.
}

void UBaseAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	// Health는 0과 현재 MaxHealth 사이로 제한합니다.
	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxHealth());
	}

	if (Attribute == GetMaxHealthAttribute())
	{
		NewValue = FMath::Max(0.0f, NewValue);
	}

	if (Attribute == GetStrengthAttribute())
	{
		NewValue = FMath::Max(0.0f, NewValue);
	}

	if (Attribute == GetAttackSpeedMultiplierAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.1f, 3.0f);
	}
}

bool UBaseAttributeSet::PreGameplayEffectExecute(FGameplayEffectModCallbackData& Data)
{
	if (!Super::PreGameplayEffectExecute(Data))
	{
		return false;
	}

	// Project damage effects converge on the Damage meta attribute. Rejecting the
	// modifier here prevents health changes, hit reactions, and damage cues from
	// being produced while an ability owns the invulnerability state.
	if (Data.EvaluatedData.Attribute == GetDamageAttribute())
	{
		const UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent();
		if (ASC && ASC->HasMatchingGameplayTag(State_Invulnerable))
		{
			return false;
		}
	}

	return true;
}

void UBaseAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	// GameplayEffect로 Health가 직접 바뀐 경우에도 유효 범위를 보장합니다.
	if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
		SetHealth(FMath::Clamp(GetHealth(), 0.0f, GetMaxHealth()));
	}

	// Damage는 최종 피해량을 받는 메타 Attribute입니다.
	// 실제 체력 감소만 Health에 남기고 Damage 값은 다음 GE를 위해 비웁니다.
	if (Data.EvaluatedData.Attribute == GetDamageAttribute())
	{
		const float LocalDamage = FMath::Max(0.0f, GetDamage());
		SetDamage(0.0f);

		if (LocalDamage > 0.0f)
		{
			SetHealth(FMath::Clamp(GetHealth() - LocalDamage, 0.0f, GetMaxHealth()));
		}
	}

	// Healing은 최종 회복량을 받는 메타 Attribute입니다.
	// 실제 체력 회복만 Health에 남기고 Healing 값은 다음 GE를 위해 비웁니다.
	if (Data.EvaluatedData.Attribute == GetHealingAttribute())
	{
		const float LocalHealing = FMath::Max(0.0f, GetHealing());
		SetHealing(0.0f);

		if (LocalHealing > 0.0f)
		{
			SetHealth(FMath::Clamp(GetHealth() + LocalHealing, 0.0f, GetMaxHealth()));
		}
	}
}

void UBaseAttributeSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	Super::PostAttributeChange(Attribute, OldValue, NewValue);

	if (Attribute == GetMaxHealthAttribute() && GetHealth() > NewValue)
	{
		SetHealth(NewValue);
	}

	// 실행 중인 기본 공격에도 GE 적용/해제 시점의 새 배율을 즉시 반영합니다.
	if (Attribute == GetAttackSpeedMultiplierAttribute()
		&& !FMath::IsNearlyEqual(OldValue, NewValue)
		&& OldValue > KINDA_SMALL_NUMBER)
	{
		UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent();
		if (ASC && ASC->IsOwnerActorAuthoritative())
		{
			UE_LOG(LogTemp, Log, TEXT("[WaterBomb] AttackSpeedMultiplier changed: owner=%s %.2f -> %.2f"),
				*GetNameSafe(ASC->GetAvatarActor()), OldValue, NewValue);
			const UGameplayAbility* AnimatingAbility = ASC->GetAnimatingAbility();
			const FGameplayAbilityActorInfo* ActorInfo = AnimatingAbility
				? AnimatingAbility->GetCurrentActorInfo()
				: nullptr;
			UAnimInstance* AnimInstance = ActorInfo ? ActorInfo->GetAnimInstance() : nullptr;
			if (AnimatingAbility
				&& AnimatingAbility->GetAssetTags().HasTagExact(GameplayAbility_BasicAttack)
				&& ASC->GetCurrentMontage()
				&& AnimInstance)
			{
				const float CurrentRate = AnimInstance->Montage_GetPlayRate(ASC->GetCurrentMontage());
				ASC->CurrentMontageSetPlayRate(FMath::Max(0.01f, CurrentRate * (NewValue / OldValue)));
			}
		}
	}
}

void UBaseAttributeSet::OnRep_Health(const FGameplayAttributeData& OldHealth)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UBaseAttributeSet, Health, OldHealth);
}

void UBaseAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UBaseAttributeSet, MaxHealth, OldMaxHealth);
}

void UBaseAttributeSet::OnRep_AttackPower(const FGameplayAttributeData& OldAttackPower)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UBaseAttributeSet, AttackPower, OldAttackPower);
}

void UBaseAttributeSet::OnRep_Strength(const FGameplayAttributeData& OldStrength)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UBaseAttributeSet, Strength, OldStrength);
}

void UBaseAttributeSet::OnRep_MoveSpeed(const FGameplayAttributeData& OldMoveSpeed)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UBaseAttributeSet, MoveSpeed, OldMoveSpeed);
}

void UBaseAttributeSet::OnRep_AttackSpeedMultiplier(const FGameplayAttributeData& OldAttackSpeedMultiplier)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UBaseAttributeSet, AttackSpeedMultiplier, OldAttackSpeedMultiplier);
}
