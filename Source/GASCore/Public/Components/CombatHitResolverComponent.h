#pragma once
#include "CoreMinimal.h"
#include "GAS/CombatHitResolver.h"
#include "CombatHitResolverComponent.generated.h"

/** One collision producer owns one active window; each new spec is a server-issued window token. */
UCLASS()
class GASCORE_API UCombatHitResolverComponent : public UCombatHitResolver
{
	GENERATED_BODY()
public:
	static UCombatHitResolverComponent* GetOrCreate(AActor* Causer);
	virtual bool OpenWindow(const FGameplayEffectSpecHandle& Spec) override;
	virtual void CloseWindow() override;
	virtual bool ResolveHit(UAbilitySystemComponent* TargetASC, const FHitResult& Hit,
		bool bIgnoreSameTeam = true, bool bRequireAnimatedHurtbox = false) override;
private:
	FGameplayEffectSpecHandle ActiveSpec;
	TWeakPtr<FGameplayEffectSpec> LastSpec;
	TSet<TWeakObjectPtr<UAbilitySystemComponent>> HitTargets;
	uint64 Sequence = 0;
};
