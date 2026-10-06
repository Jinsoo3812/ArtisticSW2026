#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "DeckCombatAuthoringCommandlet.generated.h"

/** Creates editor graphs and switches only the two ordinary Deck BehaviorSets. Not a test runner. */
UCLASS()
class ENEMY_API UDeckCombatAuthoringCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UDeckCombatAuthoringCommandlet();
	virtual int32 Main(const FString& Params) override;
};
