#include "NPCDialogueData.h"

#include "Misc/DataValidation.h"

const FNPCDialogueRule* UNPCDialogueData::FindRule(FName RuleId) const
{
	return Rules.FindByPredicate([RuleId](const FNPCDialogueRule& Rule)
	{
		return Rule.RuleId == RuleId;
	});
}

EDataValidationResult UNPCDialogueData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = CombineDataValidationResults(
		Super::IsDataValid(Context), EDataValidationResult::Valid);
	TSet<FName> SeenRuleIds;

	if (DisplayName.IsEmpty())
	{
		Context.AddError(FText::FromString(TEXT("NPC DisplayName is empty.")));
		Result = EDataValidationResult::Invalid;
	}
	if (Rules.IsEmpty())
	{
		Context.AddError(FText::FromString(TEXT("NPC dialogue data has no rules.")));
		Result = EDataValidationResult::Invalid;
	}

	for (const FNPCDialogueRule& Rule : Rules)
	{
		if (Rule.RuleId.IsNone())
		{
			Context.AddError(FText::FromString(TEXT("A dialogue rule has an empty RuleId.")));
			Result = EDataValidationResult::Invalid;
		}
		else if (SeenRuleIds.Contains(Rule.RuleId))
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("NPCDialogue", "DuplicateRuleId", "Duplicate RuleId: {0}"),
				FText::FromName(Rule.RuleId)));
			Result = EDataValidationResult::Invalid;
		}
		SeenRuleIds.Add(Rule.RuleId);

		if (Rule.Lines.IsEmpty())
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("NPCDialogue", "RuleHasNoLines", "Rule {0} has no lines."),
				FText::FromName(Rule.RuleId)));
			Result = EDataValidationResult::Invalid;
		}

		TSet<FName> SeenLineIds;
		for (const FNPCDialogueLine& Line : Rule.Lines)
		{
			if (Line.LineId.IsNone() || Line.Text.IsEmpty())
			{
				Context.AddError(FText::Format(
					NSLOCTEXT("NPCDialogue", "InvalidLine", "Rule {0} has a line with an empty id or text."),
					FText::FromName(Rule.RuleId)));
				Result = EDataValidationResult::Invalid;
			}
			else if (SeenLineIds.Contains(Line.LineId))
			{
				Context.AddError(FText::Format(
					NSLOCTEXT("NPCDialogue", "DuplicateLine", "Rule {0} has duplicate LineId {1}."),
					FText::FromName(Rule.RuleId), FText::FromName(Line.LineId)));
				Result = EDataValidationResult::Invalid;
			}
			SeenLineIds.Add(Line.LineId);
		}

		for (const FNPCDialogueLine& Line : Rule.Lines)
		{
			TSet<FName> SeenReplyIds;
			for (const FNPCDialogueReply& Reply : Line.Replies)
			{
				if (Reply.ReplyId.IsNone() || Reply.Text.IsEmpty())
				{
					Context.AddError(FText::Format(
						NSLOCTEXT("NPCDialogue", "InvalidReply", "Line {0} has a reply with an empty id or text."),
						FText::FromName(Line.LineId)));
					Result = EDataValidationResult::Invalid;
				}
				else if (SeenReplyIds.Contains(Reply.ReplyId))
				{
					Context.AddError(FText::Format(
						NSLOCTEXT("NPCDialogue", "DuplicateReply", "Line {0} has duplicate ReplyId {1}."),
						FText::FromName(Line.LineId), FText::FromName(Reply.ReplyId)));
					Result = EDataValidationResult::Invalid;
				}
				if (!Reply.NextLineId.IsNone() && !SeenLineIds.Contains(Reply.NextLineId))
				{
					Context.AddError(FText::Format(
						NSLOCTEXT("NPCDialogue", "MissingReplyTarget", "Reply {0} targets missing line {1}."),
						FText::FromName(Reply.ReplyId), FText::FromName(Reply.NextLineId)));
					Result = EDataValidationResult::Invalid;
				}
				SeenReplyIds.Add(Reply.ReplyId);
			}
		}

		auto ValidateStacks = [&Context, &Result, &Rule](const TArray<FCraftingItemStack>& Stacks, const TCHAR* Label)
		{
			for (const FCraftingItemStack& Stack : Stacks)
			{
				if (!Stack.ItemTag.IsValid() || Stack.Quantity <= 0)
				{
					Context.AddError(FText::FromString(FString::Printf(
						TEXT("Rule %s has an invalid %s item stack."), *Rule.RuleId.ToString(), Label)));
					Result = EDataValidationResult::Invalid;
				}
			}
		};
		ValidateStacks(Rule.RequiredItems, TEXT("required"));
		ValidateStacks(Rule.ConsumedItems, TEXT("consumed"));
		ValidateStacks(Rule.RewardItems, TEXT("reward"));
	}

	return Result;
}
