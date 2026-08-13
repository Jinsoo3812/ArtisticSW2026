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
