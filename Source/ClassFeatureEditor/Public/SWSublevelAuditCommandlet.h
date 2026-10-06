#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "SWSublevelAuditCommandlet.generated.h"

UCLASS()
class CLASSFEATUREEDITOR_API USWSublevelAuditCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	USWSublevelAuditCommandlet();
	virtual int32 Main(const FString& Params) override;
};
