#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayEffectTypes.h"
#include "CombatHitResolver.generated.h"

class UAbilitySystemComponent;

/** Collision adapters depend on this contract; GASCore owns damage rules. Server only. */
UCLASS(Abstract)
class ARTISTICSWCORE_API UCombatHitResolver : public UActorComponent
{
	GENERATED_BODY()
public:
	virtual bool OpenWindow(const FGameplayEffectSpecHandle& Spec) PURE_VIRTUAL(UCombatHitResolver::OpenWindow, return false;);
	virtual void CloseWindow() PURE_VIRTUAL(UCombatHitResolver::CloseWindow, );
	virtual bool ResolveHit(UAbilitySystemComponent* TargetASC, const FHitResult& Hit,
		bool bIgnoreSameTeam = true, bool bRequireAnimatedHurtbox = false)
		PURE_VIRTUAL(UCombatHitResolver::ResolveHit, return false;);
};
