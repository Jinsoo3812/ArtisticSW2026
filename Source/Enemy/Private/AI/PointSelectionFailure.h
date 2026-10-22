#pragma once

#include "CoreMinimal.h"

namespace EnemyPointSelectionFailure
{
	// Call once after the candidate search fails, before cancelling its owner.
	// BT selectors fail their branch; ability tasks notify the GA's cancellation handler.
	inline void Log(const UObject* Owner, const UObject* Avatar, const TCHAR* Reason)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Logtemp] Suitable Point selection failed. Task=%s Avatar=%s Reason=%s. Cancelling owning action/ability."),
			*GetNameSafe(Owner), *GetNameSafe(Avatar), Reason);
	}
}
