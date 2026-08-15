#pragma once

#include "CoreMinimal.h"
#include "Crafting/CraftingRecipeTypes.h"
#include "StoryFacadeSubsystem.h"
#include "NPCDialogueTypes.generated.h"

UENUM(BlueprintType)
enum class ENPCDialogueFailureReason : uint8
{
	None,
	InvalidTarget,
	MissingDialogueWidget,
	Busy,
	AlreadyInDialogue,
	NoMatchingDialogue,
	OutOfRange,
	RequirementsChanged,
	InventoryTransactionFailed,
	StoryCommitFailed
};

USTRUCT(BlueprintType)
struct NPCDIALOGUE_API FNPCDialogueLine
{
	GENERATED_BODY()

	/** Stable within the rule. Used by UI, logs, and future voice/animation lookup. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FName LineId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue", meta = (MultiLine = true))
	FText Text;
};

USTRUCT(BlueprintType)
struct NPCDIALOGUE_API FNPCDialogueRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FName RuleId = NAME_None;

	/** Highest matching priority wins. Suggested: turn-in 400, progress 300, offer 200, ambient 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue|Conditions")
	TArray<EStoryNode> RequiredStoryNodes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue|Conditions")
	TArray<EStoryNode> BlockedStoryNodes;

	/** Checked but not removed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue|Conditions")
	TArray<FCraftingItemStack> RequiredItems;

	/** Removed atomically when the last line is acknowledged. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue|Outcome")
	TArray<FCraftingItemStack> ConsumedItems;

	/** Added atomically with ConsumedItems when the last line is acknowledged. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue|Outcome")
	TArray<FCraftingItemStack> RewardItems;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue|Outcome")
	bool bCompleteStoryNode = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue|Outcome",
		meta = (EditCondition = "bCompleteStoryNode"))
	EStoryNode StoryNodeToComplete = EStoryNode::GameStarted;

	/** Makes a story-progress dialogue disappear for every player after its shared node is complete. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue|Outcome",
		meta = (EditCondition = "bCompleteStoryNode"))
	bool bHideAfterStoryCompletion = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue", meta = (TitleProperty = "LineId"))
	TArray<FNPCDialogueLine> Lines;
};

USTRUCT(BlueprintType)
struct NPCDIALOGUE_API FNPCDialogueView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FText NPCDisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FNPCDialogueLine CurrentLine;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	int32 LineIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	int32 TotalLines = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	bool bIsLastLine = false;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	TObjectPtr<AActor> NPC = nullptr;
};
