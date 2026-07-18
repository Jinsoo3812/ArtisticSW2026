#pragma once

#include "CoreMinimal.h"
#include "Water/SWBuoyancyTypes.h"

struct ARTISTICSWCORE_API FSWBuoyancyMath
{
	static FSWBuoyancySolveResult SolvePontoon(
		const FSWBuoyancySolveInput& Input,
		const FSWBuoyancyForceSettings& Settings);
};

