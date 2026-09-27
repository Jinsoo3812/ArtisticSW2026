// Fill out your copyright notice in the Description page of Project Settings.

#include "GASCombatLibrary.h"

#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "GAS/SWCombatEffectContextLibrary.h"
#include "GameplayEffect.h"
#include "GASAttributeDamageExecution.h"
#include "GASAttributeDamageGameplayEffect.h"
#include "Components/CombatHitResolverComponent.h"
#include "Components/EquipmentStatComponent.h"

float UGASCombatLibrary::CalculateStrengthDamage(float Strength, float AttackCoefficient, float ChargeMultiplier)
{
	return FMath::Max(1.0f,
		FMath::Max(0.0f, Strength)
		* FMath::Max(0.0f, AttackCoefficient)
		* FMath::Max(0.0f, ChargeMultiplier));
}

FGameplayEffectSpecHandle UGASCombatLibrary::MakeStrengthDamageEffectSpec(const FStrengthDamageRequest& Request)
{
	UAbilitySystemComponent* ASC = Request.SourceASC;
	if (!ASC || !ASC->IsOwnerActorAuthoritative() || ASC->HasMatchingGameplayTag(State_Dead)
		|| !ASC->HasAttributeSetForAttribute(UBaseAttributeSet::GetStrengthAttribute())
		|| !FMath::IsFinite(Request.AttackCoefficient) || Request.AttackCoefficient <= 0.f
		|| !FMath::IsFinite(Request.ChargeMultiplier) || Request.ChargeMultiplier <= 0.f
		|| !IsValid(Request.EffectCauser) || !Request.EffectCauser->HasAuthority()) return FGameplayEffectSpecHandle();
	if (const auto* Equipment = ASC->GetAvatarActor() ? ASC->GetAvatarActor()->FindComponentByClass<UEquipmentStatComponent>() : nullptr)
	{
		if (Equipment->IsTransitioning()) return FGameplayEffectSpecHandle();
	}
	const TSubclassOf<UGameplayEffect> EffectClass = UGASAttributeDamageGameplayEffect::StaticClass();
	const float Strength = ASC->GetNumericAttribute(UBaseAttributeSet::GetStrengthAttribute());
	if (!FMath::IsFinite(Strength)) return FGameplayEffectSpecHandle();
	FGameplayEffectContextHandle Context = USWCombatEffectContextLibrary::MakeCombatEffectContext(
		ASC, Request.InstigatorActor, Request.EffectCauser, nullptr, Request.bAddHitResult, Request.HitResult);
	Context = USWCombatEffectContextLibrary::SetDamageDeliveryType(
		Context, ESWDamageDeliveryType::DirectHit);
	FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(EffectClass, FMath::Max(1, Request.EffectLevel), Context);
	if (SpecHandle.IsValid())
	{
		SpecHandle.Data->SetSetByCallerMagnitude(Data_AttackCoefficient, Request.AttackCoefficient);
		SpecHandle.Data->SetSetByCallerMagnitude(Data_ChargeMultiplier, Request.ChargeMultiplier);
		UCombatHitResolverComponent::GetOrCreate(Request.EffectCauser);
	}

	return SpecHandle;
}

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
	FGameplayEffectContextHandle ContextHandle =
		USWCombatEffectContextLibrary::MakeCombatEffectContext(
			SourceASC,
			InstigatorActor,
			EffectCauser,
			nullptr,
			bAddHitResult,
			HitResult);
	ContextHandle = USWCombatEffectContextLibrary::SetDamageDeliveryType(
		ContextHandle, ESWDamageDeliveryType::DirectHit);

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

	FGameplayEffectContextHandle ContextHandle =
		USWCombatEffectContextLibrary::MakeCombatEffectContext(
			SourceASC, InstigatorActor, EffectCauser);

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
