#pragma once

#include "CoreMinimal.h"
#include "NPCDialogueData.h"
#include "BaseManagerDialogueData.generated.h"

/** Fixed dialogue and free skill-unlock behavior for the base NPC. */
UCLASS(BlueprintType)
class CLASSFEATURE_API UBaseManagerDialogueData : public UNPCDialogueData
{
	GENERATED_BODY()

public:
	UBaseManagerDialogueData();

	virtual bool ResolveReply(AActor* Player, UStoryFacadeSubsystem* Story,
		const FNPCDialogueRule& Rule, const FNPCDialogueLine& Line,
		const FNPCDialogueReply& Reply, FName& OutNextLineId) const override;
	virtual FName ResolveAdvanceTarget(
		const FNPCDialogueRule& Rule, const FNPCDialogueLine& Line) const override;
};
