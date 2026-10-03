#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SWLevelEntryPoint.generated.h"

class UArrowComponent;

UENUM(BlueprintType)
enum class ESWLevelEntryRole : uint8
{
	Host,
	Guest,
	Ship
};

/** Authored entry location for a hosted room. Exactly one of each role is required. */
UCLASS()
class ARTISTICSWCORE_API ASWLevelEntryPoint : public AActor
{
	GENERATED_BODY()
public:
	ASWLevelEntryPoint();
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Room Entry") ESWLevelEntryRole EntryRole = ESWLevelEntryRole::Host;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Room Entry") TObjectPtr<UArrowComponent> Direction;
};
