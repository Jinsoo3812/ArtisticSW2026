#include "StoryFacadeSubsystem.h"

#include "NativeGameplayTags.h"
#include "StorySubsystem.h"

namespace StoryFacadeTags
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(GameStarted, "Story.Internal.GameStarted");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(FirstSailingCompleted, "Story.Internal.FirstSailingCompleted");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(ReconQuestAccepted, "Story.Internal.Quest.Recon");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(StoryClue1Acquired, "Story.Internal.Item.StoryClue1");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(CipherBookAcquired, "Story.Internal.Item.CipherBook");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(MiddleBoss1Defeated, "Story.Internal.Boss.Middle1Defeated");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(SupplyPatrolQuestAccepted, "Story.Internal.Quest.SupplyPatrol");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(CurrentGeneratorUnlocked, "Story.Internal.Feature.CurrentGenerator");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(MiddleBoss2Defeated, "Story.Internal.Boss.Middle2Defeated");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(DecipherQuestAccepted, "Story.Internal.Quest.Decipher");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(
		SuppressJapaneseForcesQuestAccepted,
		"Story.Internal.Quest.SuppressJapaneseForces");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(WaterBombUnlocked, "Story.Internal.Feature.WaterBomb");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(MiddleBoss3Defeated, "Story.Internal.Boss.Middle3Defeated");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(UldolmokBattleQuestAccepted, "Story.Internal.Quest.UldolmokBattle");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(BombardmentUnlocked, "Story.Internal.Feature.Bombardment");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(FinalBossDefeated, "Story.Internal.Boss.FinalDefeated");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(EndingDialogueCompleted, "Story.Internal.CampaignCompleted");
}

void UStoryFacadeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UStorySubsystem>();
	if (UStorySubsystem* Story = ResolveStory())
	{
		Story->OnProgressChanged.AddUniqueDynamic(
			this,
			&UStoryFacadeSubsystem::HandleStoryProgressChanged);
	}
}

void UStoryFacadeSubsystem::Deinitialize()
{
	if (UStorySubsystem* Story = ResolveStory())
	{
		Story->OnProgressChanged.RemoveDynamic(
			this,
			&UStoryFacadeSubsystem::HandleStoryProgressChanged);
	}
	StoryOverride.Reset();
	Super::Deinitialize();
}

bool UStoryFacadeSubsystem::StartNewCampaign()
{
	UStorySubsystem* Story = ResolveStory();
	return Story
		&& Story->ResetProgress()
		&& CompleteStoryNode(EStoryNode::GameStarted);
}

bool UStoryFacadeSubsystem::CompleteStoryNode(EStoryNode Node)
{
	UStorySubsystem* Story = ResolveStory();
	if (!Story)
	{
		return false;
	}

	if (IsStoryNodeReached(Node))
	{
		return true;
	}

	const FGameplayTag NodeTag = GetInternalTag(Node);
	return NodeTag.IsValid()
		&& ArePrerequisitesReached(Node)
		&& Story->AddFact(NodeTag);
}

bool UStoryFacadeSubsystem::IsStoryNodeReached(EStoryNode Node) const
{
	const UStorySubsystem* Story = ResolveStory();
	const FGameplayTag NodeTag = GetInternalTag(Node);
	return Story && NodeTag.IsValid() && Story->HasFact(NodeTag);
}

bool UStoryFacadeSubsystem::CanCompleteStoryNode(EStoryNode Node) const
{
	const UStorySubsystem* Story = ResolveStory();
	const FGameplayTag NodeTag = GetInternalTag(Node);
	return Story && NodeTag.IsValid()
		&& (IsStoryNodeReached(Node) || ArePrerequisitesReached(Node));
}

bool UStoryFacadeSubsystem::SaveCampaign(const FString& SlotName)
{
	UStorySubsystem* Story = ResolveStory();
	return Story && Story->SaveProgressToSlot(SlotName);
}

bool UStoryFacadeSubsystem::LoadCampaign(const FString& SlotName)
{
	UStorySubsystem* Story = ResolveStory();
	return Story && Story->LoadProgressFromSlot(SlotName);
}

void UStoryFacadeSubsystem::ConfigureForUseCase(UStorySubsystem* StorySubsystem)
{
	StoryOverride = StorySubsystem;
}

void UStoryFacadeSubsystem::HandleStoryProgressChanged(int32 Revision)
{
	OnStoryChanged.Broadcast();
}

UStorySubsystem* UStoryFacadeSubsystem::ResolveStory() const
{
	if (StoryOverride.IsValid())
	{
		return StoryOverride.Get();
	}
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UStorySubsystem>() : nullptr;
}

bool UStoryFacadeSubsystem::ArePrerequisitesReached(EStoryNode Node) const
{
	switch (Node)
	{
	case EStoryNode::GameStarted:
		return true;
	case EStoryNode::FirstSailingCompleted:
		return IsStoryNodeReached(EStoryNode::GameStarted);
	case EStoryNode::ReconQuestAccepted:
		return IsStoryNodeReached(EStoryNode::FirstSailingCompleted);
	case EStoryNode::StoryClue1Acquired:
	case EStoryNode::CipherBookAcquired:
	case EStoryNode::MiddleBoss1Defeated:
		return IsStoryNodeReached(EStoryNode::ReconQuestAccepted);
	case EStoryNode::SupplyPatrolQuestAccepted:
		return IsStoryNodeReached(EStoryNode::MiddleBoss1Defeated);
	case EStoryNode::CurrentGeneratorUnlocked:
	case EStoryNode::MiddleBoss2Defeated:
		return IsStoryNodeReached(EStoryNode::SupplyPatrolQuestAccepted);
	case EStoryNode::DecipherQuestAccepted:
		return IsStoryNodeReached(EStoryNode::MiddleBoss2Defeated);
	case EStoryNode::SuppressJapaneseForcesQuestAccepted:
		return IsStoryNodeReached(EStoryNode::DecipherQuestAccepted)
			&& IsStoryNodeReached(EStoryNode::CipherBookAcquired);
	case EStoryNode::WaterBombUnlocked:
	case EStoryNode::MiddleBoss3Defeated:
		return IsStoryNodeReached(EStoryNode::SuppressJapaneseForcesQuestAccepted);
	case EStoryNode::UldolmokBattleQuestAccepted:
		return IsStoryNodeReached(EStoryNode::MiddleBoss3Defeated);
	case EStoryNode::BombardmentUnlocked:
	case EStoryNode::FinalBossDefeated:
		return IsStoryNodeReached(EStoryNode::UldolmokBattleQuestAccepted);
	case EStoryNode::EndingDialogueCompleted:
		return IsStoryNodeReached(EStoryNode::FinalBossDefeated);
	default:
		return false;
	}
}

FGameplayTag UStoryFacadeSubsystem::GetInternalTag(EStoryNode Node) const
{
	switch (Node)
	{
	case EStoryNode::GameStarted:
		return StoryFacadeTags::GameStarted;
	case EStoryNode::FirstSailingCompleted:
		return StoryFacadeTags::FirstSailingCompleted;
	case EStoryNode::ReconQuestAccepted:
		return StoryFacadeTags::ReconQuestAccepted;
	case EStoryNode::StoryClue1Acquired:
		return StoryFacadeTags::StoryClue1Acquired;
	case EStoryNode::CipherBookAcquired:
		return StoryFacadeTags::CipherBookAcquired;
	case EStoryNode::MiddleBoss1Defeated:
		return StoryFacadeTags::MiddleBoss1Defeated;
	case EStoryNode::SupplyPatrolQuestAccepted:
		return StoryFacadeTags::SupplyPatrolQuestAccepted;
	case EStoryNode::CurrentGeneratorUnlocked:
		return StoryFacadeTags::CurrentGeneratorUnlocked;
	case EStoryNode::MiddleBoss2Defeated:
		return StoryFacadeTags::MiddleBoss2Defeated;
	case EStoryNode::DecipherQuestAccepted:
		return StoryFacadeTags::DecipherQuestAccepted;
	case EStoryNode::SuppressJapaneseForcesQuestAccepted:
		return StoryFacadeTags::SuppressJapaneseForcesQuestAccepted;
	case EStoryNode::WaterBombUnlocked:
		return StoryFacadeTags::WaterBombUnlocked;
	case EStoryNode::MiddleBoss3Defeated:
		return StoryFacadeTags::MiddleBoss3Defeated;
	case EStoryNode::UldolmokBattleQuestAccepted:
		return StoryFacadeTags::UldolmokBattleQuestAccepted;
	case EStoryNode::BombardmentUnlocked:
		return StoryFacadeTags::BombardmentUnlocked;
	case EStoryNode::FinalBossDefeated:
		return StoryFacadeTags::FinalBossDefeated;
	case EStoryNode::EndingDialogueCompleted:
		return StoryFacadeTags::EndingDialogueCompleted;
	default:
		return FGameplayTag();
	}
}
