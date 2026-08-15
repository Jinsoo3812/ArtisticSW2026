#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "NPCDialogueUIGeneratorCommandlet.generated.h"

UCLASS()
class UNPCDialogueUIGeneratorCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UNPCDialogueUIGeneratorCommandlet();
	virtual int32 Main(const FString& Params) override;
};
