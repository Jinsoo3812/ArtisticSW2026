#pragma once
#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GameplayEffectCustomApplicationRequirement.h"
#include "StatusGameplayEffect.generated.h"

UCLASS()
class GASCORE_API UStatusApplicationRequirement : public UGameplayEffectCustomApplicationRequirement
{
	GENERATED_BODY()
public:
	virtual bool CanApplyGameplayEffect_Implementation(const UGameplayEffect* Effect,
		const FGameplayEffectSpec& Spec, UAbilitySystemComponent* ASC) const override;
};

/** Fixed lifetime and opt-in reception, including direct ASC application. */
UCLASS(Abstract, Blueprintable)
class GASCORE_API UStatusGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UStatusGameplayEffect();
protected:
	void AddStatusTag(FGameplayTag Tag);
};

UCLASS(Blueprintable)
class GASCORE_API UStunGameplayEffect : public UStatusGameplayEffect
{
	GENERATED_BODY()
public:
	UStunGameplayEffect();
};

UCLASS(Blueprintable)
class GASCORE_API UPoisonStatusGameplayEffect : public UStatusGameplayEffect
{
	GENERATED_BODY()
public:
	UPoisonStatusGameplayEffect();
};

UCLASS(Blueprintable)
class GASCORE_API UBurnStatusGameplayEffect : public UStatusGameplayEffect
{
	GENERATED_BODY()
public:
	UBurnStatusGameplayEffect();
};
