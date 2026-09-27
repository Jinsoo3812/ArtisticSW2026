#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PathCombatPresentationDataAsset.generated.h"

class UGameplayEffect;

/**
 * Reusable presentation policy for any combat action represented by a line
 * segment. Effect classes own cue tags and visual lifetime; abilities only
 * provide authoritative path geometry.
 */
UCLASS(BlueprintType)
class ARTISTICSWCORE_API UPathCombatPresentationDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	TSubclassOf<UGameplayEffect> GetTelegraphEffectClass() const { return TelegraphEffectClass; }
	TSubclassOf<UGameplayEffect> GetExecutionEffectClass() const { return ExecutionEffectClass; }

protected:
	/** Infinite effect removed when the attack commits to execution. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Path Presentation")
	TSubclassOf<UGameplayEffect> TelegraphEffectClass;

	/** Duration effect whose GameplayCue leaves the executed path visible. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Path Presentation")
	TSubclassOf<UGameplayEffect> ExecutionEffectClass;
};
