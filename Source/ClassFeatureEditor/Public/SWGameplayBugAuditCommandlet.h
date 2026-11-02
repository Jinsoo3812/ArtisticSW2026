#pragma once
#include "Commandlets/Commandlet.h"
#include "SWGameplayBugAuditCommandlet.generated.h"

/** Read-only audit of authored data behind the 2026-10-31 playtest issues. */
UCLASS()
class USWGameplayBugAuditCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	virtual int32 Main(const FString& Params) override;
};
