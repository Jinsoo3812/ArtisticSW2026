#pragma once

#include "CoreMinimal.h"

/** Per-world development policy. Never grants permanent progression. */
namespace SWDevelopmentOptions
{
 enum EOption : int32 { ShipUpgradeFree = 1, CraftingFree = 2, SkillFree = 4, SkillNoCooldown = 8, Session = 16, All = 15 };
 ARTISTICSWCORE_API int32 GetMask(const UObject* Context);
 ARTISTICSWCORE_API bool IsEnabled(const UObject* Context, EOption Option);
}
