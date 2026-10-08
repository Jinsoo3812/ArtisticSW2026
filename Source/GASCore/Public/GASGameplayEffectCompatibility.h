#pragma once

#include "GameplayEffect.h"
#include "Runtime/Launch/Resources/Version.h"

namespace GASGameplayEffectCompatibility
{
	inline EGameplayEffectStackingType GetStackingType(const UGameplayEffect& Effect)
	{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 7
		// UE 5.7 deprecates the field but does not export its replacement getter
		// from GameplayAbilities. Mirror that getter until the engine exports it.
		if (Effect.DurationPolicy == EGameplayEffectDurationType::Instant)
		{
			return EGameplayEffectStackingType::None;
		}
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		const EGameplayEffectStackingType StackingType = Effect.StackingType;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		return StackingType;
#else
		return Effect.GetStackingType();
#endif
	}
}
