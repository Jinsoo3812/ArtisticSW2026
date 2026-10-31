#include "StorySubsystem.h"

#include "Kismet/GameplayStatics.h"
#include "StorySaveGame.h"
#include "StorySettings.h"
#include "StoryStateReplicator.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void UStorySubsystem::Deinitialize()
{
	ActiveReplicator.Reset();
	Super::Deinitialize();
}

void UStorySubsystem::InitializeConfiguredProgress()
{
	if (bStartupProgressInitialized || !HasStoryAuthority())
	{
		return;
	}
	FString HostedRunId;
	if (IsRunningDedicatedServer() && FParse::Value(FCommandLine::Get(), TEXT("SWRoomRunId="), HostedRunId))
	{
		bStartupProgressInitialized = true;
		return;
	}

	const UStorySettings* Settings = GetDefault<UStorySettings>();
	if (!Settings
		|| !Settings->bAutoLoadDefaultSlot
		|| !LoadProgressFromSlot(Settings->DefaultSaveSlot))
	{
		ResetProgress();
	}
	bStartupProgressInitialized = true;
}

bool UStorySubsystem::ConfigureDefinition(UStoryDefinition* NewDefinition, bool bResetProgress)
{
	// Compatibility entry point for old Blueprints. Campaign progression is owned by the facade.
	(void)NewDefinition;
	(void)bResetProgress;
	return false;
}

bool UStorySubsystem::HasStoryAuthority() const
{
	const UWorld* World = GetWorld();
	return !World || World->GetNetMode() != NM_Client;
}

bool UStorySubsystem::AddFact(FGameplayTag FactTag)
{
	FGameplayTagContainer FactTags;
	if (FactTag.IsValid())
	{
		FactTags.AddTag(FactTag);
	}
	return AddFacts(FactTags);
}

bool UStorySubsystem::AddFacts(const FGameplayTagContainer& FactTags)
{
	if (!HasStoryAuthority())
	{
		return false;
	}

	bool bChanged = false;
	for (const FGameplayTag& FactTag : FactTags)
	{
		if (FactTag.IsValid() && !Facts.HasTagExact(FactTag))
		{
			Facts.AddTag(FactTag);
			bChanged = true;
		}
	}
	if (!bChanged)
	{
		return false;
	}

	CommitProgressChange();
	return true;
}

bool UStorySubsystem::RemoveFact(FGameplayTag FactTag)
{
	if (!HasStoryAuthority() || !FactTag.IsValid() || !Facts.HasTagExact(FactTag))
	{
		return false;
	}

	Facts.RemoveTag(FactTag);
	CommitProgressChange();
	return true;
}

bool UStorySubsystem::HasFact(FGameplayTag FactTag) const
{
	return FactTag.IsValid() && Facts.HasTagExact(FactTag);
}

bool UStorySubsystem::MatchesCondition(const FStoryConditionSet& Condition) const
{
	return Condition.IsSatisfied(Facts, Counters);
}

bool UStorySubsystem::SetCounter(FGameplayTag CounterTag, int32 NewValue)
{
	if (!HasStoryAuthority() || !CounterTag.IsValid() || Counters.FindRef(CounterTag) == NewValue)
	{
		return false;
	}

	Counters.FindOrAdd(CounterTag) = NewValue;
	CommitProgressChange();
	return true;
}

int32 UStorySubsystem::AddToCounter(FGameplayTag CounterTag, int32 Delta)
{
	if (!HasStoryAuthority() || !CounterTag.IsValid() || Delta == 0)
	{
		return GetCounter(CounterTag);
	}

	const int32 CurrentValue = Counters.FindRef(CounterTag);
	const int64 NewValue64 = static_cast<int64>(CurrentValue) + static_cast<int64>(Delta);
	const int32 NewValue = static_cast<int32>(FMath::Clamp<int64>(NewValue64, MIN_int32, MAX_int32));
	SetCounter(CounterTag, NewValue);
	return GetCounter(CounterTag);
}

int32 UStorySubsystem::GetCounter(FGameplayTag CounterTag) const
{
	return Counters.FindRef(CounterTag);
}

bool UStorySubsystem::ResetProgress()
{
	if (!HasStoryAuthority())
	{
		return false;
	}

	Facts.Reset();
	Counters.Reset();
	AppliedActionKeys.Reset();
	bStartupProgressInitialized = true;

	CommitProgressChange();
	return true;
}

bool UStorySubsystem::SaveProgressToSlot(const FString& SlotName)
{
	if (!HasStoryAuthority())
	{
		return false;
	}

	UStorySaveGame* Save = BuildRoomProgress();
	if (!Save)
	{
		return false;
	}

	return UGameplayStatics::SaveGameToSlot(Save, ResolveSlotName(SlotName), 0);
}

UStorySaveGame* UStorySubsystem::BuildRoomProgress() const
{
	if (!HasStoryAuthority()) return nullptr;
	UStorySaveGame* Save = NewObject<UStorySaveGame>(GetTransientPackage());
	// StoryDefinitionId is retained only to deserialize legacy saves.
	Save->StoryDefinitionId = FPrimaryAssetId();
	Save->Facts = Facts;
	for (const TPair<FGameplayTag, int32>& Counter : Counters)
	{
		FStoryCounterValue& Value = Save->Counters.AddDefaulted_GetRef();
		Value.CounterTag = Counter.Key;
		Value.Value = Counter.Value;
	}
	Save->Counters.Sort([](const FStoryCounterValue& A, const FStoryCounterValue& B)
	{
		return A.CounterTag.ToString() < B.CounterTag.ToString();
	});
	Save->AppliedActionKeys = AppliedActionKeys.Array();
	Save->AppliedActionKeys.Sort(FNameLexicalLess());
	return Save;
}

bool UStorySubsystem::ApplyRoomProgress(const UStorySaveGame* Progress)
{
	if (!HasStoryAuthority() || !Progress || Progress->SaveVersion != 1) return false;
	TSet<FGameplayTag> SeenCounters;
	for (const FStoryCounterValue& Counter : Progress->Counters)
	{
		if (!Counter.CounterTag.IsValid() || SeenCounters.Contains(Counter.CounterTag)) return false;
		SeenCounters.Add(Counter.CounterTag);
	}
	Facts = Progress->Facts;
	Counters.Reset();
	for (const FStoryCounterValue& Counter : Progress->Counters)
	{
		Counters.Add(Counter.CounterTag, Counter.Value);
	}
	AppliedActionKeys = TSet<FName>(Progress->AppliedActionKeys);
	bStartupProgressInitialized = true;
	CommitProgressChange();
	return true;
}

bool UStorySubsystem::LoadProgressFromSlot(const FString& SlotName)
{
	if (!HasStoryAuthority())
	{
		return false;
	}

	const FString ResolvedSlot = ResolveSlotName(SlotName);
	if (!UGameplayStatics::DoesSaveGameExist(ResolvedSlot, 0))
	{
		return false;
	}

	const UStorySaveGame* Save = Cast<UStorySaveGame>(
		UGameplayStatics::LoadGameFromSlot(ResolvedSlot, 0));
	if (!Save)
	{
		return false;
	}

	return ApplyRoomProgress(Save);
}

FStoryProgressSnapshot UStorySubsystem::GetProgressSnapshot() const
{
	FStoryProgressSnapshot Snapshot;
	Snapshot.Facts = Facts;
	Snapshot.Revision = Revision;
	Snapshot.Counters.Reserve(Counters.Num());
	for (const TPair<FGameplayTag, int32>& Counter : Counters)
	{
		FStoryCounterValue& Value = Snapshot.Counters.AddDefaulted_GetRef();
		Value.CounterTag = Counter.Key;
		Value.Value = Counter.Value;
	}
	Snapshot.Counters.Sort([](const FStoryCounterValue& A, const FStoryCounterValue& B)
	{
		return A.CounterTag.ToString() < B.CounterTag.ToString();
	});
	return Snapshot;
}

void UStorySubsystem::RegisterReplicator(AStoryStateReplicator* Replicator)
{
	if (!Replicator)
	{
		return;
	}

	ActiveReplicator = Replicator;
	if (Replicator->HasAuthority())
	{
		SynchronizeReplicator();
	}
	else
	{
		ApplyReplicatedSnapshot(Replicator->GetSnapshot());
	}
}

void UStorySubsystem::UnregisterReplicator(AStoryStateReplicator* Replicator)
{
	if (ActiveReplicator.Get() == Replicator)
	{
		ActiveReplicator.Reset();
	}
}

void UStorySubsystem::ApplyReplicatedSnapshot(const FStoryProgressSnapshot& Snapshot)
{
	if (HasStoryAuthority() || Snapshot.Revision < Revision)
	{
		return;
	}

	Facts = Snapshot.Facts;
	Counters.Reset();
	for (const FStoryCounterValue& Counter : Snapshot.Counters)
	{
		if (Counter.CounterTag.IsValid())
		{
			Counters.Add(Counter.CounterTag, Counter.Value);
		}
	}
	Revision = Snapshot.Revision;
	OnProgressChanged.Broadcast(Revision);
}

// Legacy action-receiver components remain loadable, but definition actions are retired.
void UStorySubsystem::RegisterActionReceiver(UStoryActionReceiverComponent* Receiver)
{
	(void)Receiver;
}

void UStorySubsystem::UnregisterActionReceiver(UStoryActionReceiverComponent* Receiver)
{
	(void)Receiver;
}

void UStorySubsystem::CommitProgressChange()
{
	++Revision;
	OnProgressChanged.Broadcast(Revision);
	SynchronizeReplicator();
}

void UStorySubsystem::SynchronizeReplicator()
{
	if (AStoryStateReplicator* Replicator = ActiveReplicator.Get())
	{
		if (Replicator->HasAuthority())
		{
			Replicator->SetSnapshot(GetProgressSnapshot());
		}
	}
}

FString UStorySubsystem::ResolveSlotName(const FString& RequestedSlotName) const
{
	if (!RequestedSlotName.IsEmpty())
	{
		return RequestedSlotName;
	}
	const UStorySettings* Settings = GetDefault<UStorySettings>();
	return Settings ? Settings->DefaultSaveSlot : TEXT("StoryCampaign");
}
