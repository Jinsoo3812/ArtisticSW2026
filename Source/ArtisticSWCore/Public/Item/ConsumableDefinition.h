#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ConsumableDefinition.generated.h"

UENUM(BlueprintType)
enum class EConsumableEffect : uint8
{
    NativeDefault,
    Disabled,
    InstantHeal,
    Regeneration,
    Cleanse,
    HealAndCleanse,
    MovementBuff,
    StrengthBuff
};

/** Server-authored values. NativeDefault resolves only the eight known item identities. */
USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FConsumableDefinition
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    EConsumableEffect Effect = EConsumableEffect::NativeDefault;
    /** Instant amount, or healing per one-second tick. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0"))
    float Healing = 25.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0.1"))
    float Duration = 5.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0.1"))
    float Cooldown = 5.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0"))
    float StrengthBonus = 5.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="1", ClampMax="3"))
    float MoveMultiplier = 1.2f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="1", ClampMax="3"))
    float AttackMultiplier = 1.f;
    /** Higher rank replaces lower rank; lower rank cannot overwrite a stronger active buff. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="1", ClampMax="4"))
    int32 Rank = 1;

    bool IsValid() const;
    bool IsRecovery() const;
    static bool Resolve(FGameplayTag ItemTag, const FConsumableDefinition& Authored, FConsumableDefinition& Out);
};
