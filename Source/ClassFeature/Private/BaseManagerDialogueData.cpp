#include "BaseManagerDialogueData.h"

#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "Skills/PlayerSkillComponent.h"
#include "StoryFacadeSubsystem.h"

#define LOCTEXT_NAMESPACE "BaseManagerDialogue"

namespace
{
	FNPCDialogueReply MakeReply(const TCHAR* Id, const FText& Text, const TCHAR* NextLine)
	{
		FNPCDialogueReply Reply;
		Reply.ReplyId = FName(Id);
		Reply.Text = Text;
		Reply.NextLineId = NextLine && *NextLine ? FName(NextLine) : NAME_None;
		return Reply;
	}

	FNPCDialogueLine MakeLine(const TCHAR* Id, const FText& Text,
		std::initializer_list<FNPCDialogueReply> Replies = {})
	{
		FNPCDialogueLine Line;
		Line.LineId = FName(Id);
		Line.Text = Text;
		Line.Replies = Replies;
		return Line;
	}

	struct FSkillUnlockRoute
	{
		FGameplayTag SkillTag;
		EStoryNode RequiredNode;
		EStoryNode CompletedNode;
		FName SuccessLine;
		FName UnavailableLine;
		FName AlreadyLine;
	};

	bool FindRoute(FName ReplyId, FSkillUnlockRoute& OutRoute)
	{
		if (ReplyId == TEXT("UnlockCurrentGenerator"))
		{
			OutRoute = { GameplayAbility_Skill_GravityVortex, EStoryNode::SupplyPatrolQuestAccepted,
				EStoryNode::CurrentGeneratorUnlocked, TEXT("CurrentSuccess1"), TEXT("CurrentUnavailable"), TEXT("CurrentAlready") };
			return true;
		}
		if (ReplyId == TEXT("UnlockWaterBomb"))
		{
			OutRoute = { GameplayAbility_Skill_WaterBomb, EStoryNode::SuppressJapaneseForcesQuestAccepted,
				EStoryNode::WaterBombUnlocked, TEXT("WaterBombSuccess1"), TEXT("WaterBombUnavailable"), TEXT("WaterBombAlready") };
			return true;
		}
		if (ReplyId == TEXT("UnlockBombardment"))
		{
			OutRoute = { GameplayAbility_Skill_Bombardment, EStoryNode::UldolmokBattleQuestAccepted,
				EStoryNode::BombardmentUnlocked, TEXT("BombardmentSuccess1"), TEXT("BombardmentUnavailable"), TEXT("BombardmentAlready") };
			return true;
		}
		return false;
	}
}

UBaseManagerDialogueData::UBaseManagerDialogueData()
{
	DisplayName = LOCTEXT("DisplayName", "기지NPC");
	InteractionActionText = LOCTEXT("Talk", "대화하기");

	FNPCDialogueRule Rule;
	Rule.RuleId = TEXT("BaseManager_Default");
	Rule.Lines = {
		MakeLine(TEXT("Root"), LOCTEXT("Root", "기지의 상황은 안정적이야. 필요한 게 있으면 말해."), {
			MakeReply(TEXT("OpenSkillUnlock"), LOCTEXT("OpenSkills", "스킬 개방"), TEXT("SkillMenu")),
			MakeReply(TEXT("ExitDialogue"), LOCTEXT("Exit", "그만 이야기한다"), TEXT("")) }),
		MakeLine(TEXT("SkillMenu"), LOCTEXT("SkillMenu", "어떤 스킬을 개방할래?"), {
			MakeReply(TEXT("UnlockCurrentGenerator"), LOCTEXT("CurrentChoice", "해류 발생기"), TEXT("CurrentSuccess1")),
			MakeReply(TEXT("UnlockWaterBomb"), LOCTEXT("WaterBombChoice", "물폭탄"), TEXT("WaterBombSuccess1")),
			MakeReply(TEXT("UnlockBombardment"), LOCTEXT("BombardmentChoice", "포탄 세례"), TEXT("BombardmentSuccess1")),
			MakeReply(TEXT("BackToRoot"), LOCTEXT("Back", "돌아간다"), TEXT("Root")) }),
		MakeLine(TEXT("CurrentSuccess1"), LOCTEXT("CurrentSuccess1", "해류 발생기를 개방했어.")),
		MakeLine(TEXT("CurrentSuccess2"), LOCTEXT("CurrentSuccess2", "강한 해류를 만들어 적과 주변 물체의 움직임을 흔드는 기술이야.")),
		MakeLine(TEXT("CurrentUnavailable"), LOCTEXT("CurrentUnavailable", "해류 발생기는 아직 개방할 수 없어.")),
		MakeLine(TEXT("CurrentAlready"), LOCTEXT("CurrentAlready", "해류 발생기는 이미 개방한 기술이야.")),
		MakeLine(TEXT("WaterBombSuccess1"), LOCTEXT("WaterBombSuccess1", "물폭탄을 개방했어.")),
		MakeLine(TEXT("WaterBombSuccess2"), LOCTEXT("WaterBombSuccess2", "대포에 특수 포탄을 장전해 넓은 범위에 물 폭발을 일으키는 기술이야.")),
		MakeLine(TEXT("WaterBombUnavailable"), LOCTEXT("WaterBombUnavailable", "물폭탄은 아직 개방할 수 없어.")),
		MakeLine(TEXT("WaterBombAlready"), LOCTEXT("WaterBombAlready", "물폭탄은 이미 개방한 기술이야.")),
		MakeLine(TEXT("BombardmentSuccess1"), LOCTEXT("BombardmentSuccess1", "포탄 세례를 개방했어.")),
		MakeLine(TEXT("BombardmentSuccess2"), LOCTEXT("BombardmentSuccess2", "지정한 해역에 여러 발의 포탄을 연속으로 쏟아붓는 기술이야.")),
		MakeLine(TEXT("BombardmentUnavailable"), LOCTEXT("BombardmentUnavailable", "포탄 세례는 아직 개방할 수 없어.")),
		MakeLine(TEXT("BombardmentAlready"), LOCTEXT("BombardmentAlready", "포탄 세례는 이미 개방한 기술이야."))
	};
	Rules = { MoveTemp(Rule) };
}

bool UBaseManagerDialogueData::ResolveReply(AActor* Player, UStoryFacadeSubsystem* Story,
	const FNPCDialogueRule& Rule, const FNPCDialogueLine& Line,
	const FNPCDialogueReply& Reply, FName& OutNextLineId) const
{
	FSkillUnlockRoute Route;
	if (!FindRoute(Reply.ReplyId, Route))
	{
		return Super::ResolveReply(Player, Story, Rule, Line, Reply, OutNextLineId);
	}

	ABasePlayer* BasePlayer = Cast<ABasePlayer>(Player);
	UPlayerSkillComponent* Skills = BasePlayer ? BasePlayer->GetPlayerSkillComponent() : nullptr;
	if (!BasePlayer || !BasePlayer->HasAuthority() || !Skills || !Story)
	{
		return false;
	}
	if (Skills->IsSkillUnlocked(Route.SkillTag))
	{
		OutNextLineId = Route.AlreadyLine;
		return true;
	}
	if (!Story->IsStoryNodeReached(Route.RequiredNode))
	{
		OutNextLineId = Route.UnavailableLine;
		return true;
	}
	if (!Story->CanCompleteStoryNode(Route.CompletedNode)
		|| !Story->CompleteStoryNode(Route.CompletedNode)
		|| !Skills->UnlockSkill(Route.SkillTag))
	{
		return false;
	}
	OutNextLineId = Route.SuccessLine;
	return true;
}

FName UBaseManagerDialogueData::ResolveAdvanceTarget(
	const FNPCDialogueRule& Rule, const FNPCDialogueLine& Line) const
{
	if (Line.LineId == TEXT("CurrentSuccess1")) return TEXT("CurrentSuccess2");
	if (Line.LineId == TEXT("WaterBombSuccess1")) return TEXT("WaterBombSuccess2");
	if (Line.LineId == TEXT("BombardmentSuccess1")) return TEXT("BombardmentSuccess2");
	if (Line.LineId != TEXT("Root") && Line.LineId != TEXT("SkillMenu")) return TEXT("SkillMenu");
	return NAME_None;
}

#undef LOCTEXT_NAMESPACE
