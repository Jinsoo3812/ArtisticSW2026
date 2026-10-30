#include "Skills/PlayerSkillComponent.h"
#include "Development/SWDevelopmentOptions.h"

#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/InventoryComponent.h"
#include "Net/UnrealNetwork.h"
#include "Engine/GameInstance.h"
#include "StoryFacadeSubsystem.h"

namespace
{
	bool GetSkillStoryNode(FGameplayTag SkillTag, EStoryNode& OutNode)
	{
		if (SkillTag == GameplayAbility_Skill_GravityVortex) OutNode = EStoryNode::CurrentGeneratorUnlocked;
		else if (SkillTag == GameplayAbility_Skill_WaterBomb) OutNode = EStoryNode::WaterBombUnlocked;
		else if (SkillTag == GameplayAbility_Skill_Bombardment) OutNode = EStoryNode::BombardmentUnlocked;
		else return false;
		return true;
	}
}

UPlayerSkillComponent::UPlayerSkillComponent()
{
	SetIsReplicatedByDefault(true);

	auto AddDefinition = [this](
		const FGameplayTag& SkillTag,
		const FGameplayTag& SkillItemTag,
		const FGameplayTag& UsageMaterialTag)
	{
		FPlayerSkillDefinition& Definition = SkillDefinitions.AddDefaulted_GetRef();
		Definition.SkillTag = SkillTag;
		Definition.SkillItemTag = SkillItemTag;
		Definition.UsageMaterialTag = UsageMaterialTag;
		Definition.bUnlockedByDefault = false;
	};

	AddDefinition(
		GameplayAbility_Skill_GravityVortex,
		Item_Id_Skill_GravityVortex,
		Item_Id_Skill_GravityVortex);
	AddDefinition(
		GameplayAbility_Skill_WaterBomb,
		Item_Id_Skill_WaterBomb,
		Item_Id_Skill_WaterBomb);
	AddDefinition(
		GameplayAbility_Skill_Bombardment,
		Item_Id_Skill_Bombardment,
		Item_Id_Skill_Bombardment);
	AddDefinition(
		GameplayAbility_Skill_AreaSlow,
		Item_Id_Skill_AreaSlow,
		Item_Id_Material_SkillMaterial_RareSkill);
}

void UPlayerSkillComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		InitializeSkillStates();
	}
	if (UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
		if (UStoryFacadeSubsystem* Story = GameInstance->GetSubsystem<UStoryFacadeSubsystem>())
			Story->OnStoryChanged.AddUniqueDynamic(this, &UPlayerSkillComponent::HandleStoryChanged);
}

void UPlayerSkillComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
		if (UStoryFacadeSubsystem* Story = GameInstance->GetSubsystem<UStoryFacadeSubsystem>())
			Story->OnStoryChanged.RemoveDynamic(this, &UPlayerSkillComponent::HandleStoryChanged);
	Super::EndPlay(EndPlayReason);
}

void UPlayerSkillComponent::HandleStoryChanged()
{
	NotifyInventoryChanged();
}

void UPlayerSkillComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UPlayerSkillComponent, SkillStates);
}

bool UPlayerSkillComponent::IsSkillUnlocked(FGameplayTag SkillTag) const
{
	return IsSkillUnlockedWithInventory(SkillTag, ResolveInventory());
}

bool UPlayerSkillComponent::IsSkillUnlockConditionMet(FGameplayTag SkillTag) const
{
	EStoryNode RequiredNode;
	if (GetSkillStoryNode(SkillTag, RequiredNode))
		if (const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
			if (const UStoryFacadeSubsystem* Story = GameInstance->GetSubsystem<UStoryFacadeSubsystem>())
			{
				// Earlier room saves may predate the automatic skill-eligibility fact.
				const EStoryNode QuestNode = SkillTag == GameplayAbility_Skill_GravityVortex ? EStoryNode::SupplyPatrolQuestAccepted
					: SkillTag == GameplayAbility_Skill_WaterBomb ? EStoryNode::SuppressJapaneseForcesQuestAccepted : EStoryNode::UldolmokBattleQuestAccepted;
				return Story->IsStoryNodeReached(RequiredNode) || Story->IsStoryNodeReached(QuestNode);
			}
	const FPlayerSkillDefinition* Definition = FindSkillDefinition(SkillTag);
	if (!Definition)
	{
		return false;
	}

	const FPlayerSkillState* State = FindSkillState(SkillTag);
	return State
		? State->bUnlockConditionMet || State->bUnlocked
		: Definition->bUnlockedByDefault;
}

bool UPlayerSkillComponent::IsSkillUnlockedWithInventory(
	FGameplayTag SkillTag,
	const UInventoryComponent* Inventory) const
{
	(void)Inventory;
	const FPlayerSkillDefinition* Definition = FindSkillDefinition(SkillTag);
	if (!Definition)
	{
		return false;
	}

	const FPlayerSkillState* State = FindSkillState(SkillTag);
	const bool bProgressionUnlocked = State
		? State->bUnlocked
		: Definition->bUnlockedByDefault;
	return bProgressionUnlocked && IsSkillUnlockConditionMet(SkillTag);
}

int32 UPlayerSkillComponent::GetSkillUseCount(FGameplayTag SkillTag) const
{
	const FPlayerSkillDefinition* Definition = FindSkillDefinition(SkillTag);
	const UInventoryComponent* Inventory = ResolveInventory();
	const FGameplayTag UsageTag = GetUsageMaterialTag(SkillTag);
	return Definition && Inventory && UsageTag.IsValid()
		? Inventory->GetItemCount(UsageTag)
		: 0;
}

bool UPlayerSkillComponent::CanUseSkill(FGameplayTag SkillTag) const
{
	return CanUseSkillWithInventory(SkillTag, ResolveInventory());
}

bool UPlayerSkillComponent::TryConsumeSkillUse(FGameplayTag SkillTag)
{
	return TryConsumeSkillUseWithInventory(SkillTag, ResolveInventory());
}

bool UPlayerSkillComponent::CanUseSkillWithInventory(
	FGameplayTag SkillTag,
	const UInventoryComponent* Inventory) const
{
	const FPlayerSkillDefinition* Definition = FindSkillDefinition(SkillTag);
 if (SWDevelopmentOptions::IsEnabled(this, SWDevelopmentOptions::SkillFree)) return Definition != nullptr;
	return Definition
		&& Inventory
		&& IsSkillUnlockedWithInventory(SkillTag, Inventory)
		&& Inventory->GetItemCount(GetUsageMaterialTag(SkillTag)) > 0;
}

bool UPlayerSkillComponent::TryConsumeSkillUseWithInventory(
	FGameplayTag SkillTag,
	UInventoryComponent* Inventory)
{
 if (SWDevelopmentOptions::IsEnabled(this, SWDevelopmentOptions::SkillFree))
  return GetOwner() && GetOwner()->HasAuthority() && FindSkillDefinition(SkillTag);
	if (!GetOwner()
		|| !GetOwner()->HasAuthority()
		|| !IsSkillUnlockedWithInventory(SkillTag, Inventory))
	{
		return false;
	}

	const FPlayerSkillDefinition* Definition = FindSkillDefinition(SkillTag);
	const FGameplayTag UsageTag = GetUsageMaterialTag(SkillTag);
	if (!Definition || !Inventory || !UsageTag.IsValid())
	{
		return false;
	}

	const bool bConsumed = Inventory->RemoveItem(UsageTag, 1);
	if (bConsumed)
	{
		OnSkillChanged.Broadcast(SkillTag);
	}
	return bConsumed;
}

void UPlayerSkillComponent::RegisterInventorySource(UInventoryComponent* Inventory)
{
	InventorySource = Inventory;
}

bool UPlayerSkillComponent::UnlockSkill(FGameplayTag SkillTag)
{
	return SetSkillUnlocked(SkillTag, true);
}

bool UPlayerSkillComponent::SetSkillUnlocked(FGameplayTag SkillTag, bool bUnlocked)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !FindSkillDefinition(SkillTag))
	{
		return false;
	}

	FPlayerSkillState* State = FindMutableSkillState(SkillTag);
	if (!State)
	{
		FPlayerSkillState& NewState = SkillStates.AddDefaulted_GetRef();
		NewState.SkillTag = SkillTag;
		NewState.bUnlocked = bUnlocked;
		NewState.bUnlockConditionMet = bUnlocked;
	}
	else if (State->bUnlocked == bUnlocked)
	{
		return true;
	}
	else
	{
		State->bUnlocked = bUnlocked;
		State->bUnlockConditionMet |= bUnlocked;
	}

	OnSkillChanged.Broadcast(SkillTag);
	GetOwner()->ForceNetUpdate();
	return true;
}

bool UPlayerSkillComponent::SetSkillUnlockConditionMet(FGameplayTag SkillTag, bool bConditionMet)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !FindSkillDefinition(SkillTag))
	{
		return false;
	}

	FPlayerSkillState* State = FindMutableSkillState(SkillTag);
	if (!State)
	{
		FPlayerSkillState& NewState = SkillStates.AddDefaulted_GetRef();
		NewState.SkillTag = SkillTag;
		NewState.bUnlockConditionMet = bConditionMet;
	}
	else if (State->bUnlockConditionMet == bConditionMet)
	{
		return true;
	}
	else
	{
		State->bUnlockConditionMet = bConditionMet;
	}

	OnSkillChanged.Broadcast(SkillTag);
	GetOwner()->ForceNetUpdate();
	return true;
}

FGameplayTag UPlayerSkillComponent::GetSkillItemTag(FGameplayTag SkillTag) const
{
	const FPlayerSkillDefinition* Definition = FindSkillDefinition(SkillTag);
	return Definition ? Definition->SkillItemTag : FGameplayTag();
}

FGameplayTag UPlayerSkillComponent::GetUsageMaterialTag(FGameplayTag SkillTag) const
{
	const FPlayerSkillDefinition* Definition = FindSkillDefinition(SkillTag);
	EStoryNode Node;
	if (Definition && GetSkillStoryNode(SkillTag, Node)) return Definition->SkillItemTag;
	return Definition ? Definition->UsageMaterialTag : FGameplayTag();
}

TArray<FGameplayTag> UPlayerSkillComponent::GetRegisteredSkillTags() const
{
	TArray<FGameplayTag> Result;
	Result.Reserve(SkillDefinitions.Num());
	for (const FPlayerSkillDefinition& Definition : SkillDefinitions)
	{
		if (Definition.SkillTag.IsValid())
		{
			Result.Add(Definition.SkillTag);
		}
	}
	return Result;
}

const FPlayerSkillDefinition* UPlayerSkillComponent::FindSkillDefinition(FGameplayTag SkillTag) const
{
	return SkillDefinitions.FindByPredicate([SkillTag](const FPlayerSkillDefinition& Definition)
	{
		return Definition.SkillTag.MatchesTagExact(SkillTag);
	});
}

void UPlayerSkillComponent::NotifyInventoryChanged()
{
	for (const FPlayerSkillDefinition& Definition : SkillDefinitions)
	{
		OnSkillChanged.Broadcast(Definition.SkillTag);
	}
}

void UPlayerSkillComponent::OnRep_SkillStates()
{
	for (const FPlayerSkillDefinition& Definition : SkillDefinitions)
	{
		OnSkillChanged.Broadcast(Definition.SkillTag);
	}
}

void UPlayerSkillComponent::InitializeSkillStates()
{
	for (const FPlayerSkillDefinition& Definition : SkillDefinitions)
	{
		if (!Definition.SkillTag.IsValid() || FindSkillState(Definition.SkillTag))
		{
			continue;
		}

		FPlayerSkillState& State = SkillStates.AddDefaulted_GetRef();
		State.SkillTag = Definition.SkillTag;
		State.bUnlocked = Definition.bUnlockedByDefault;
		State.bUnlockConditionMet = Definition.bUnlockedByDefault;
	}
}

UInventoryComponent* UPlayerSkillComponent::ResolveInventory() const
{
	if (InventorySource.IsValid())
	{
		return InventorySource.Get();
	}

	const APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	const ABasePlayer* Player = PlayerState ? Cast<ABasePlayer>(PlayerState->GetPawn()) : nullptr;
	return Player ? Player->GetInventoryComponent() : nullptr;
}

FPlayerSkillState* UPlayerSkillComponent::FindMutableSkillState(FGameplayTag SkillTag)
{
	return SkillStates.FindByPredicate([SkillTag](const FPlayerSkillState& State)
	{
		return State.SkillTag.MatchesTagExact(SkillTag);
	});
}

const FPlayerSkillState* UPlayerSkillComponent::FindSkillState(FGameplayTag SkillTag) const
{
	return SkillStates.FindByPredicate([SkillTag](const FPlayerSkillState& State)
	{
		return State.SkillTag.MatchesTagExact(SkillTag);
	});
}
