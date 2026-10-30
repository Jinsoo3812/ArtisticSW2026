#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "NativeGameplayTags.h"
#include "ConsumableGameplayEffects.generated.h"

namespace ConsumableTags
{
    GASCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(CooldownRecovery);
    GASCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(CooldownUtility);
    GASCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Regeneration);
    GASCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Movement);
    GASCORE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Strength);
}

UCLASS()
class GASCORE_API UConsumableHealingEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UConsumableHealingEffect();
};

UCLASS()
class GASCORE_API UConsumableRegenerationEffect : public UConsumableHealingEffect
{
    GENERATED_BODY()
public:
    UConsumableRegenerationEffect();
};

UCLASS()
class GASCORE_API UConsumableMovementEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UConsumableMovementEffect();
};

UCLASS()
class GASCORE_API UConsumableStrengthEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UConsumableStrengthEffect();
};

UCLASS()
class GASCORE_API UConsumableRecoveryCooldownEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UConsumableRecoveryCooldownEffect();
};

UCLASS()
class GASCORE_API UConsumableUtilityCooldownEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UConsumableUtilityCooldownEffect();
};
