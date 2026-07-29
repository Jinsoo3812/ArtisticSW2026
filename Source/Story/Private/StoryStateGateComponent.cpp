#include "StoryStateGateComponent.h"

UStoryStateGateComponent::UStoryStateGateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UStoryStateGateComponent::BeginPlay()
{
	Super::BeginPlay();
	if (AActor* Owner = GetOwner())
	{
		bInitialHidden = Owner->IsHidden();
		bInitialCollisionEnabled = Owner->GetActorEnableCollision();
		bInitialTickEnabled = Owner->IsActorTickEnabled();
	}

	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UStoryFacadeSubsystem* Story =
				GameInstance->GetSubsystem<UStoryFacadeSubsystem>())
			{
				Story->OnStoryChanged.AddUniqueDynamic(
					this,
					&UStoryStateGateComponent::HandleStoryChanged);
			}
		}
	}
	RefreshFromStory();
}

void UStoryStateGateComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UStoryFacadeSubsystem* Story =
				GameInstance->GetSubsystem<UStoryFacadeSubsystem>())
			{
				Story->OnStoryChanged.RemoveDynamic(
					this,
					&UStoryStateGateComponent::HandleStoryChanged);
			}
		}
	}
	Super::EndPlay(EndPlayReason);
}

void UStoryStateGateComponent::RefreshFromStory()
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UStoryFacadeSubsystem* Story = GameInstance
		? GameInstance->GetSubsystem<UStoryFacadeSubsystem>()
		: nullptr;
	if (!Owner || !Story)
	{
		return;
	}

	bGateOpen = Story->IsStoryNodeReached(RequiredStoryNode);
	if (bHideActorWhenClosed)
	{
		Owner->SetActorHiddenInGame(bGateOpen ? bInitialHidden : true);
	}
	if (bDisableCollisionWhenClosed)
	{
		Owner->SetActorEnableCollision(bGateOpen ? bInitialCollisionEnabled : false);
	}
	if (bDisableActorTickWhenClosed)
	{
		Owner->SetActorTickEnabled(bGateOpen ? bInitialTickEnabled : false);
	}
}

void UStoryStateGateComponent::HandleStoryChanged()
{
	RefreshFromStory();
}
