#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SWVoyageResetAnchor.generated.h"

class USWVoyageResetProfile;

UCLASS()
class ARTISTICSWCORE_API ASWVoyageResetAnchor : public AActor
{
	GENERATED_BODY()
public:
	ASWVoyageResetAnchor();
	UPROPERTY(EditInstanceOnly, Category="Voyage") TObjectPtr<USWVoyageResetProfile> Profile;
};
