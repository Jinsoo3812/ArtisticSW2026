#include "Skills/PlayerSkillComponent.h"

#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/InventoryComponent.h"
#include "Net/UnrealNetwork.h"

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
		Item_Id_Skill_CurrentGenerator,
		Item_Id_Material_SkillMaterial_RareSkill);
	AddDefinition(
		GameplayAbility_Skill_WaterBomb,
		Item_Id_Skill_WaterBomb,
		Item_Id_Material_SkillMaterial_EpicSkill);
	AddDefinition(
		GameplayAbility_Skill_Bombardment,
		Item_Id_Skill_CannonBarrage,
		Item_Id_Material_SkillMaterial_LegendarySkill);
}

void UPlayerSkillComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		InitializeSkillStates();
	}
}

void UPlayerSkillComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UPlayerSkillComponent, SkillStates);
}

bool UPlayerSkillComponent::IsSkillUnlocked(FGameplayTag SkillTag) const
{
	if (const FPlayerSkillState* State = FindSkillState(SkillTag))
	{
		return State->bUnlocked;
	}
	if (const FPlayerSkillDefinition* Definition = FindSkillDefinition(SkillTag))
	{
		return Definition->bUnlockedByDefault;
	}
	return false;
}

int32 UPlayerSkillComponent::GetSkillUseCount(FGameplayTag SkillTag) const
{
	const FPlayerSkillDefinition* Definition = FindSkillDefinition(SkillTag);
	const UInventoryComponent* Inventory = ResolveInventory();
	return Definition && Inventory && Definition->UsageMaterialTag.IsValid()
		? Inventory->GetItemCount(Definition->UsageMaterialTag)
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
	return Definition
		&& Inventory
		&& IsSkillUnlocked(SkillTag)
		&& Inventory->GetItemCount(Definition->UsageMaterialTag) > 0;
}

bool UPlayerSkillComponent::TryConsumeSkillUseWithInventory(
	FGameplayTag SkillTag,
	UInventoryComponent* Inventory)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsSkillUnlocked(SkillTag))
	{
		return false;
	}

	const FPlayerSkillDefinition* Definition = FindSkillDefinition(SkillTag);
	if (!Definition || !Inventory || !Definition->UsageMaterialTag.IsValid())
	{
		return false;
	}

	const bool bConsumed = Inventory->RemoveItem(Definition->UsageMaterialTag, 1);
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
	}
	else if (State->bUnlocked == bUnlocked)
	{
		return true;
	}
	else
	{
		State->bUnlocked = bUnlocked;
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
