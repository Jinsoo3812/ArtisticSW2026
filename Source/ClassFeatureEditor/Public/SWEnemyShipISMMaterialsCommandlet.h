#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "SWEnemyShipISMMaterialsCommandlet.generated.h"

/** Authors project-only ISM variants; never modifies source or installed-engine materials. */
UCLASS()
class CLASSFEATUREEDITOR_API USWEnemyShipISMMaterialsCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	USWEnemyShipISMMaterialsCommandlet();
	virtual int32 Main(const FString& Params) override;
};
