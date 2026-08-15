#include "NPCDialogueSourceComponent.h"

#include "DialogueInventoryProvider.h"
#include "NPCDialogueData.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

UNPCDialogueSourceComponent::UNPCDialogueSourceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UNPCDialogueSourceComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UNPCDialogueSourceComponent, DialogueData);
}

bool UNPCDialogueSourceComponent::TryReserve(AActor* Interactor)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Interactor)
	{
		return false;
	}
	if (ActiveInteractor.IsValid() && ActiveInteractor.Get() != Interactor)
	{
		return false;
	}

	ActiveInteractor = Interactor;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ReservationTimerHandle,
			this,
			&UNPCDialogueSourceComponent::HandleReservationTimeout,
			ReservationTimeoutSeconds,
			false);
	}
	return true;
}

void UNPCDialogueSourceComponent::Release(AActor* Interactor)
{
	if (!ActiveInteractor.IsValid() || ActiveInteractor.Get() == Interactor)
	{
		ActiveInteractor.Reset();
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ReservationTimerHandle);
		}
	}
}

bool UNPCDialogueSourceComponent::IsReservedBy(AActor* Interactor) const
{
	return ActiveInteractor.IsValid() && ActiveInteractor.Get() == Interactor;
}

const FNPCDialogueRule* UNPCDialogueSourceComponent::ResolveBestRule(
	const UStoryFacadeSubsystem* Story,
	const IDialogueInventoryProvider* Inventory) const
{
	if (!DialogueData)
	{
		return nullptr;
	}

	const FNPCDialogueRule* BestRule = nullptr;
	for (const FNPCDialogueRule& Rule : DialogueData->Rules)
	{
		if (!IsRuleAvailable(Rule, Story, Inventory))
		{
			continue;
		}
		if (!BestRule
			|| Rule.Priority > BestRule->Priority
			|| (Rule.Priority == BestRule->Priority && Rule.RuleId.LexicalLess(BestRule->RuleId)))
		{
			BestRule = &Rule;
		}
	}
	return BestRule;
}

bool UNPCDialogueSourceComponent::IsRuleAvailable(
	const FNPCDialogueRule& Rule,
	const UStoryFacadeSubsystem* Story,
	const IDialogueInventoryProvider* Inventory) const
{
	if (!Story || Rule.RuleId.IsNone() || Rule.Lines.IsEmpty())
	{
		return false;
	}
	for (const EStoryNode Node : Rule.RequiredStoryNodes)
	{
		if (!Story->IsStoryNodeReached(Node))
		{
			return false;
		}
	}
	for (const EStoryNode Node : Rule.BlockedStoryNodes)
	{
		if (Story->IsStoryNodeReached(Node))
		{
			return false;
		}
	}
	if (Rule.bCompleteStoryNode && Rule.bHideAfterStoryCompletion
		&& Story->IsStoryNodeReached(Rule.StoryNodeToComplete))
	{
		return false;
	}
	if (!Rule.RequiredItems.IsEmpty() && !Inventory)
	{
		return false;
	}
	for (const FCraftingItemStack& Item : Rule.RequiredItems)
	{
		if (!Item.ItemTag.IsValid() || Item.Quantity <= 0
			|| Inventory->GetDialogueItemCount(Item.ItemTag) < Item.Quantity)
		{
			return false;
		}
	}
	return true;
}

FTransform UNPCDialogueSourceComponent::GetDialogueCameraTransform() const
{
	if (const USceneComponent* Anchor = GetDialogueCameraAnchor())
	{
		return Anchor->GetComponentTransform();
	}
	if (const AActor* Owner = GetOwner())
	{
		return Owner->GetActorTransform();
	}
	return FTransform::Identity;
}

USceneComponent* UNPCDialogueSourceComponent::GetDialogueCameraAnchor() const
{
	if (const AActor* Owner = GetOwner())
	{
		TArray<USceneComponent*> SceneComponents;
		Owner->GetComponents(SceneComponents);
		for (USceneComponent* Component : SceneComponents)
		{
			if (Component && Component->ComponentHasTag(TEXT("DialogueCamera")))
			{
				return Component;
			}
		}
	}
	return nullptr;
}

void UNPCDialogueSourceComponent::HandleReservationTimeout()
{
	ActiveInteractor.Reset();
}
