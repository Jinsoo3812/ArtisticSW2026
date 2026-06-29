// Fill out your copyright notice in the Description page of Project Settings.

#include "GASCombatLibrary.h"

#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "GameplayEffect.h"

FGameplayEffectSpecHandle UGASCombatLibrary::MakeDamageEffectSpec(
	UAbilitySystemComponent* SourceASC,
	TSubclassOf<UGameplayEffect> DamageEffectClass,
	float Damage,
	AActor* InstigatorActor,
	AActor* EffectCauser,
	int32 EffectLevel,
	bool bAddHitResult,
	const FHitResult& HitResult)
{
	if (!SourceASC || !DamageEffectClass)
	{
		return FGameplayEffectSpecHandle();
	}

	// 누가 무엇으로 어디에 맞췄는지에 대한 정보 == ContextHandle
	FGameplayEffectContextHandle ContextHandle = SourceASC->MakeEffectContext();
	ContextHandle.AddInstigator(InstigatorActor, EffectCauser);

	if (EffectCauser)
	{
		ContextHandle.AddSourceObject(EffectCauser);
	}

	if (bAddHitResult)
	{
		ContextHandle.AddHitResult(HitResult, true);
	}

	// 적용할 GameplayEffect 정보
	FGameplayEffectSpecHandle SpecHandle = SourceASC->MakeOutgoingSpec(
		DamageEffectClass,
		FMath::Max(1, EffectLevel),
		ContextHandle);

	if (SpecHandle.IsValid())
	{
		SpecHandle.Data->SetSetByCallerMagnitude(Data_Damage, FMath::Max(0.0f, Damage));
	}

	return SpecHandle;
}

FGameplayEffectSpecHandle UGASCombatLibrary::MakeHealingEffectSpec(
	UAbilitySystemComponent* SourceASC,
	TSubclassOf<UGameplayEffect> HealingEffectClass,
	float Healing,
	AActor* InstigatorActor,
	AActor* EffectCauser,
	int32 EffectLevel)
{
	if (!SourceASC || !HealingEffectClass)
	{
		return FGameplayEffectSpecHandle();
	}

	FGameplayEffectContextHandle ContextHandle = SourceASC->MakeEffectContext();
	ContextHandle.AddInstigator(InstigatorActor, EffectCauser);

	if (EffectCauser)
	{
		ContextHandle.AddSourceObject(EffectCauser);
	}

	FGameplayEffectSpecHandle SpecHandle = SourceASC->MakeOutgoingSpec(
		HealingEffectClass,
		FMath::Max(1, EffectLevel),
		ContextHandle);

	if (SpecHandle.IsValid())
	{
		SpecHandle.Data->SetSetByCallerMagnitude(Data_Heal, FMath::Max(0.0f, Healing));
	}

	return SpecHandle;
}
