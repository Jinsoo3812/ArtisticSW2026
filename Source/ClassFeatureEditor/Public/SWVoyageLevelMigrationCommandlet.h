#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "SWVoyageLevelMigrationCommandlet.generated.h"

UCLASS()
class CLASSFEATUREEDITOR_API USWVoyageLevelMigrationCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	USWVoyageLevelMigrationCommandlet();
	virtual void CreateCustomEngine(const FString& Params) override;
	virtual int32 Main(const FString& Params) override;
};
