#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "StoryTypes.h"
#include "StorySubsystem.generated.h"

class AStoryStateReplicator;
class UStoryActionReceiverComponent;
class UStoryDefinition;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStoryProgressChanged, int32, Revision);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStoryStateActivated, FGameplayTag, StateTag);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnStoryActionPending,
	FGameplayTag,
	OwningStateTag,
	FStoryActionSpec,
	Action);

/**
 * Shared campaign authority and query facade.
 *
 * There is deliberately no per-player progress. On a server, this subsystem is
 * authoritative and mirrors one snapshot to every client through a replicator.
 */
UCLASS()
class STORY_API UStorySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Called once by the server world subsystem after network mode is known. */
	void InitializeConfiguredProgress();

	UFUNCTION(BlueprintCallable, Category = "Story")
	bool ConfigureDefinition(UStoryDefinition* NewDefinition, bool bResetProgress = true);

	UFUNCTION(BlueprintPure, Category = "Story")
	UStoryDefinition* GetDefinition() const { return Definition; }

	UFUNCTION(BlueprintPure, Category = "Story")
	bool HasStoryAuthority() const;

	UFUNCTION(BlueprintCallable, Category = "Story", meta = (ReturnDisplayName = "Changed"))
	bool AddFact(FGameplayTag FactTag);

	/** Internal/facade bulk mutation. All facts commit as one replicated revision. */
	bool AddFacts(const FGameplayTagContainer& FactTags);

	UFUNCTION(BlueprintCallable, Category = "Story", meta = (ReturnDisplayName = "Changed"))
	bool RemoveFact(FGameplayTag FactTag);

	UFUNCTION(BlueprintPure, Category = "Story")
	bool HasFact(FGameplayTag FactTag) const;

	UFUNCTION(BlueprintPure, Category = "Story")
	bool MatchesCondition(const FStoryConditionSet& Condition) const;

	UFUNCTION(BlueprintCallable, Category = "Story", meta = (ReturnDisplayName = "Changed"))
	bool SetCounter(FGameplayTag CounterTag, int32 NewValue);

	UFUNCTION(BlueprintCallable, Category = "Story", meta = (ReturnDisplayName = "New Value"))
	int32 AddToCounter(FGameplayTag CounterTag, int32 Delta);

	UFUNCTION(BlueprintPure, Category = "Story")
	int32 GetCounter(FGameplayTag CounterTag) const;

	UFUNCTION(BlueprintCallable, Category = "Story")
	bool ResetProgress();

	UFUNCTION(BlueprintCallable, Category = "Story|Persistence")
	bool SaveProgressToSlot(const FString& SlotName);

	UFUNCTION(BlueprintCallable, Category = "Story|Persistence")
	bool LoadProgressFromSlot(const FString& SlotName);

	UFUNCTION(BlueprintPure, Category = "Story")
	FStoryProgressSnapshot GetProgressSnapshot() const;

	/** Server-side registration used by the world replicator. */
	void RegisterReplicator(AStoryStateReplicator* Replicator);
	void UnregisterReplicator(AStoryStateReplicator* Replicator);

	/** Client-side replicated state application. */
	void ApplyReplicatedSnapshot(const FStoryProgressSnapshot& Snapshot);

	void RegisterActionReceiver(UStoryActionReceiverComponent* Receiver);
	void UnregisterActionReceiver(UStoryActionReceiverComponent* Receiver);

	UPROPERTY(BlueprintAssignable, Category = "Story")
	FOnStoryProgressChanged OnProgressChanged;

	UPROPERTY(BlueprintAssignable, Category = "Story")
	FOnStoryStateActivated OnStateActivated;

	/** Diagnostic signal: this action remains pending until a receiver handles it. */
	UPROPERTY(BlueprintAssignable, Category = "Story")
	FOnStoryActionPending OnActionPending;

private:
	struct FPendingAction
	{
		FGameplayTag OwningStateTag;
		FStoryActionSpec Action;
	};

	void EvaluateStateRules();
	void QueueActionsForState(const FStoryStateRule& Rule);
	void RebuildPendingActions();
	void TryExecutePendingActions();
	void CommitProgressChange();
	void SynchronizeReplicator();
	FName MakeActionKey(FGameplayTag StateTag, FName ActionId) const;
	FString ResolveSlotName(const FString& RequestedSlotName) const;

	UPROPERTY(Transient)
	TObjectPtr<UStoryDefinition> Definition = nullptr;

	UPROPERTY(Transient)
	FGameplayTagContainer Facts;

	TMap<FGameplayTag, int32> Counters;
	TSet<FName> AppliedActionKeys;
	TArray<FPendingAction> PendingActions;
	TArray<TWeakObjectPtr<UStoryActionReceiverComponent>> ActionReceivers;

	UPROPERTY(Transient)
	TWeakObjectPtr<AStoryStateReplicator> ActiveReplicator;

	int32 Revision = 0;
	bool bEvaluatingRules = false;
	bool bDispatchingActions = false;
	bool bStartupProgressInitialized = false;
};
