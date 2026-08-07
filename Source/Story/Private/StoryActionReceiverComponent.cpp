#include "StoryActionReceiverComponent.h"

#include "StorySubsystem.h"

UStoryActionReceiverComponent::UStoryActionReceiverComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UStoryActionReceiverComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UStorySubsystem* Story = GameInstance->GetSubsystem<UStorySubsystem>())
			{
				Story->RegisterActionReceiver(this);
			}
		}
	}
}

void UStoryActionReceiverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UStorySubsystem* Story = GameInstance->GetSubsystem<UStorySubsystem>())
			{
				Story->UnregisterActionReceiver(this);
			}
		}
	}
	Super::EndPlay(EndPlayReason);
}

bool UStoryActionReceiverComponent::CanHandleStoryAction(FGameplayTag ActionType) const
{
	for (const FGameplayTag& HandledType : HandledActionTypes)
	{
		if (ActionType.MatchesTag(HandledType))
		{
			return true;
		}
	}
	return false;
}

bool UStoryActionReceiverComponent::ExecuteStoryAction_Implementation(const FStoryActionSpec& Action)
{
	return false;
}
