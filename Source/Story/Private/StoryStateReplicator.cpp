#include "StoryStateReplicator.h"

#include "Net/UnrealNetwork.h"
#include "StorySubsystem.h"

AStoryStateReplicator::AStoryStateReplicator()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(10.0f);
}

void AStoryStateReplicator::BeginPlay()
{
	Super::BeginPlay();
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UStorySubsystem* Story = GameInstance->GetSubsystem<UStorySubsystem>())
		{
			Story->RegisterReplicator(this);
		}
	}
}

void AStoryStateReplicator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UStorySubsystem* Story = GameInstance->GetSubsystem<UStorySubsystem>())
		{
			Story->UnregisterReplicator(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AStoryStateReplicator::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AStoryStateReplicator, Snapshot);
}

void AStoryStateReplicator::SetSnapshot(const FStoryProgressSnapshot& NewSnapshot)
{
	if (!HasAuthority())
	{
		return;
	}
	Snapshot = NewSnapshot;
	ForceNetUpdate();
}

void AStoryStateReplicator::OnRep_Snapshot()
{
	ApplySnapshotToSubsystem();
}

void AStoryStateReplicator::ApplySnapshotToSubsystem()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UStorySubsystem* Story = GameInstance->GetSubsystem<UStorySubsystem>())
		{
			Story->ApplyReplicatedSnapshot(Snapshot);
		}
	}
}
