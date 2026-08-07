#include "StorySubsystem.h"

#include "Kismet/GameplayStatics.h"
#include "StoryActionReceiverComponent.h"
#include "StoryDefinition.h"
#include "StorySaveGame.h"
#include "StorySettings.h"
#include "StoryStateReplicator.h"

DEFINE_LOG_CATEGORY_STATIC(LogStory, Log, All);

void UStorySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UStorySettings* Settings = GetDefault<UStorySettings>();
	UStoryDefinition* DefaultDefinition = Settings
		? Settings->DefaultStoryDefinition.LoadSynchronous()
		: nullptr;
	if (!DefaultDefinition)
	{
		return;
	}

	TArray<FText> ValidationErrors;
	if (DefaultDefinition->ValidateDefinition(ValidationErrors))
	{
		Definition = DefaultDefinition;
	}
}

void UStorySubsystem::Deinitialize()
{
	ActionReceivers.Reset();
	PendingActions.Reset();
	ActiveReplicator.Reset();
	Super::Deinitialize();
}

void UStorySubsystem::InitializeConfiguredProgress()
{
	if (bStartupProgressInitialized || !HasStoryAuthority())
	{
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
	if (!HasStoryAuthority() || !NewDefinition)
	{
		return false;
	}

	TArray<FText> ValidationErrors;
	if (!NewDefinition->ValidateDefinition(ValidationErrors))
	{
		for (const FText& Error : ValidationErrors)
		{
			UE_LOG(LogStory, Error, TEXT("Story definition validation failed: %s"), *Error.ToString());
		}
		return false;
	}

	Definition = NewDefinition;
	if (bResetProgress)
	{
		return ResetProgress();
	}

	RebuildPendingActions();
	TryExecutePendingActions();
	return true;
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

	EvaluateStateRules();
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
	EvaluateStateRules();
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
	EvaluateStateRules();
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
	if (Definition)
	{
		Facts = Definition->InitialFacts;
		for (const FStoryCounterValue& Counter : Definition->InitialCounters)
		{
			if (Counter.CounterTag.IsValid())
			{
				Counters.Add(Counter.CounterTag, Counter.Value);
			}
		}
	}
	AppliedActionKeys.Reset();
	PendingActions.Reset();
	bStartupProgressInitialized = true;

	EvaluateStateRules();
	RebuildPendingActions();
	CommitProgressChange();
	return true;
}

bool UStorySubsystem::SaveProgressToSlot(const FString& SlotName)
{
	if (!HasStoryAuthority())
	{
		return false;
	}

	UStorySaveGame* Save = Cast<UStorySaveGame>(
		UGameplayStatics::CreateSaveGameObject(UStorySaveGame::StaticClass()));
	if (!Save)
	{
		return false;
	}

	Save->StoryDefinitionId = Definition
		? Definition->GetPrimaryAssetId()
		: FPrimaryAssetId();
	Save->Facts = Facts;
	for (const TPair<FGameplayTag, int32>& Counter : Counters)
	{
		FStoryCounterValue& Value = Save->Counters.AddDefaulted_GetRef();
		Value.CounterTag = Counter.Key;
		Value.Value = Counter.Value;
	}
	Save->AppliedActionKeys = AppliedActionKeys.Array();
	return UGameplayStatics::SaveGameToSlot(Save, ResolveSlotName(SlotName), 0);
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

	const FPrimaryAssetId CurrentDefinitionId = Definition
		? Definition->GetPrimaryAssetId()
		: FPrimaryAssetId();
	if (Save->StoryDefinitionId.IsValid()
		&& CurrentDefinitionId.IsValid()
		&& Save->StoryDefinitionId != CurrentDefinitionId)
	{
		UE_LOG(
			LogStory,
			Error,
			TEXT("Story save definition mismatch. Save=%s Current=%s"),
			*Save->StoryDefinitionId.ToString(),
			*CurrentDefinitionId.ToString());
		return false;
	}

	Facts = Save->Facts;
	Counters.Reset();
	for (const FStoryCounterValue& Counter : Save->Counters)
	{
		if (Counter.CounterTag.IsValid())
		{
			Counters.Add(Counter.CounterTag, Counter.Value);
		}
	}
	AppliedActionKeys = TSet<FName>(Save->AppliedActionKeys);
	bStartupProgressInitialized = true;

	EvaluateStateRules();
	RebuildPendingActions();
	CommitProgressChange();
	return true;
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

void UStorySubsystem::RegisterActionReceiver(UStoryActionReceiverComponent* Receiver)
{
	if (!Receiver || ActionReceivers.Contains(Receiver))
	{
		return;
	}
	ActionReceivers.Add(Receiver);
	TryExecutePendingActions();
}

void UStorySubsystem::UnregisterActionReceiver(UStoryActionReceiverComponent* Receiver)
{
	ActionReceivers.Remove(Receiver);
}

void UStorySubsystem::EvaluateStateRules()
{
	if (bEvaluatingRules || !Definition)
	{
		return;
	}

	TGuardValue<bool> Guard(bEvaluatingRules, true);
	bool bActivatedInPass = false;
	do
	{
		bActivatedInPass = false;
		for (const FStoryStateRule& Rule : Definition->StateRules)
		{
			if (!Rule.StateTag.IsValid()
				|| Facts.HasTagExact(Rule.StateTag)
				|| !Rule.ActivationCondition.IsSatisfied(Facts, Counters))
			{
				continue;
			}

			Facts.AddTag(Rule.StateTag);
			Facts.AppendTags(Rule.GrantedFacts);
			for (const FGameplayTag& RemovedFact : Rule.RemovedFacts)
			{
				Facts.RemoveTag(RemovedFact);
			}
			QueueActionsForState(Rule);
			OnStateActivated.Broadcast(Rule.StateTag);
			bActivatedInPass = true;
		}
	}
	while (bActivatedInPass);
}

void UStorySubsystem::QueueActionsForState(const FStoryStateRule& Rule)
{
	for (const FStoryActionSpec& Action : Rule.ActivationActions)
	{
		if (Action.ActionId.IsNone()
			|| !Action.ActionType.IsValid()
			|| AppliedActionKeys.Contains(MakeActionKey(Rule.StateTag, Action.ActionId)))
		{
			continue;
		}

		const bool bAlreadyPending = PendingActions.ContainsByPredicate(
			[&Rule, &Action](const FPendingAction& Pending)
			{
				return Pending.OwningStateTag.MatchesTagExact(Rule.StateTag)
					&& Pending.Action.ActionId == Action.ActionId;
			});
		if (!bAlreadyPending)
		{
			PendingActions.Add({ Rule.StateTag, Action });
		}
	}
}

void UStorySubsystem::RebuildPendingActions()
{
	PendingActions.Reset();
	if (!Definition)
	{
		return;
	}

	for (const FStoryStateRule& Rule : Definition->StateRules)
	{
		if (Facts.HasTagExact(Rule.StateTag))
		{
			QueueActionsForState(Rule);
		}
	}
}

void UStorySubsystem::TryExecutePendingActions()
{
	if (!HasStoryAuthority() || bDispatchingActions)
	{
		return;
	}

	TGuardValue<bool> Guard(bDispatchingActions, true);
	ActionReceivers.RemoveAll([](const TWeakObjectPtr<UStoryActionReceiverComponent>& Receiver)
	{
		return !Receiver.IsValid();
	});

	for (int32 PendingIndex = PendingActions.Num() - 1; PendingIndex >= 0; --PendingIndex)
	{
		const FPendingAction Pending = PendingActions[PendingIndex];
		bool bHandled = false;
		for (const TWeakObjectPtr<UStoryActionReceiverComponent>& ReceiverPtr : ActionReceivers)
		{
			UStoryActionReceiverComponent* Receiver = ReceiverPtr.Get();
			if (Receiver
				&& Receiver->CanHandleStoryAction(Pending.Action.ActionType)
				&& Receiver->ExecuteStoryAction(Pending.Action))
			{
				bHandled = true;
				break;
			}
		}

		if (bHandled)
		{
			AppliedActionKeys.Add(MakeActionKey(Pending.OwningStateTag, Pending.Action.ActionId));
			PendingActions.RemoveAtSwap(PendingIndex);
		}
		else
		{
			OnActionPending.Broadcast(Pending.OwningStateTag, Pending.Action);
		}
	}
}

void UStorySubsystem::CommitProgressChange()
{
	++Revision;
	OnProgressChanged.Broadcast(Revision);
	SynchronizeReplicator();
	TryExecutePendingActions();
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

FName UStorySubsystem::MakeActionKey(FGameplayTag StateTag, FName ActionId) const
{
	return FName(*FString::Printf(TEXT("%s:%s"), *StateTag.ToString(), *ActionId.ToString()));
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
