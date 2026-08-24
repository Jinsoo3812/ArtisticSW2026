#pragma once

#include "CoreMinimal.h"
#include "SkillUpgradeTypes.generated.h"

UENUM(BlueprintType)
enum class ESkillUpgradeSelection : uint8
{
	GravityVortex UMETA(DisplayName = "해류 발생기"),
	WaterBomb UMETA(DisplayName = "물폭탄"),
	Bombardment UMETA(DisplayName = "폭탄세례")
};

UENUM(BlueprintType)
enum class ESkillCraftingUIState : uint8
{
	NoSelection,
	Locked,
	AwaitingIngredients,
	Ready,
	RequestPending,
	Animating,
	Complete,
	Error
};
