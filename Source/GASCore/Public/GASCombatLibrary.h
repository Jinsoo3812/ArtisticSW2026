// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GASCombatLibrary.generated.h"

class UAbilitySystemComponent;
class UGameplayEffect;

UCLASS()
class GASCORE_API UGASCombatLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/*
	 * 피해 GameplayEffect Spec을 생성하고 SetByCaller.Data.Damage 값을 주입합니다.
	 * 반환된 SpecHandle은 무기, 투사체, 트랩 등이 타겟 ASC에 적용할 때 사용합니다.
	 */
	UFUNCTION(BlueprintCallable, Category = "GAS|Combat", meta = (AdvancedDisplay = "EffectLevel,bAddHitResult,HitResult", AutoCreateRefTerm = "HitResult"))
	static FGameplayEffectSpecHandle MakeDamageEffectSpec(
		UAbilitySystemComponent* SourceASC,
		TSubclassOf<UGameplayEffect> DamageEffectClass,
		float Damage,
		AActor* InstigatorActor, // 공격자 Actor
		AActor* EffectCauser, // 공격 원인(무기, 투사체..)
		int32 EffectLevel = 1,
		bool bAddHitResult = false,
		const FHitResult& HitResult = FHitResult()
		// 충돌에 관한 정보로
		// 피격 방향, 충돌 지점, 공격자 판정, 킬 로그, GameplayCue 등에 쓸 수 있는 정보
		);

	/*
	 * 회복 GameplayEffect Spec을 생성하고 SetByCaller.Data.Heal 값을 주입합니다.
	 * 회복 아이템, 스킬, 지속 회복 효과에서 같은 방식으로 사용할 수 있습니다.
	 */
	UFUNCTION(BlueprintCallable, Category = "GAS|Combat", meta = (AdvancedDisplay = "EffectLevel"))
	static FGameplayEffectSpecHandle MakeHealingEffectSpec(
		UAbilitySystemComponent* SourceASC,
		TSubclassOf<UGameplayEffect> HealingEffectClass,
		float Healing,
		AActor* InstigatorActor,
		AActor* EffectCauser,
		int32 EffectLevel = 1);
};
