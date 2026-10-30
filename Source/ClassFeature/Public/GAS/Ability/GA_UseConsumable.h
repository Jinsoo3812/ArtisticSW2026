#pragma once

#include "CoreMinimal.h"
#include "BaseGameplayAbility.h"
#include "Item/ConsumableDefinition.h"
#include "GA_UseConsumable.generated.h"

/** A synchronous server request, retained by the caller until the ability finishes. */
UCLASS(Transient)
class CLASSFEATURE_API UConsumableUseRequest : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY()
    FGameplayTag ItemTag;
    bool bSucceeded = false;
};

UCLASS()
class CLASSFEATURE_API UGA_UseConsumable : public UBaseGameplayAbility
{
    GENERATED_BODY()
public:
    UGA_UseConsumable();
    virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
        FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
private:
    bool ResolveUse(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        FConsumableDefinition& Out) const;
    bool ApplyConsumable(UAbilitySystemComponent* ASC, const FConsumableDefinition& Definition);
};
