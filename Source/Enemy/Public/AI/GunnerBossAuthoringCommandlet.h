#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "GunnerBossAuthoringCommandlet.generated.h"

/** Authors only the T1 gunner's dedicated assets, preserving shared content. */
UCLASS()
class ENEMY_API UGunnerBossAuthoringCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UGunnerBossAuthoringCommandlet();
	virtual int32 Main(const FString& Params) override;
};
