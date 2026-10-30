#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SkillCursorInputInterface.generated.h"

UINTERFACE(MinimalAPI)
class USkillCursorInputInterface : public UInterface
{
	GENERATED_BODY()
};

class ARTISTICSWCORE_API ISkillCursorInputInterface
{
	GENERATED_BODY()

public:
	virtual bool IsSkillCursorInputBlocked() const = 0;
	virtual void RestoreInputAfterSkillCursor() = 0;
};
