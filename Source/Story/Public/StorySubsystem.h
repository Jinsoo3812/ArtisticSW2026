#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "StoryTypes.h"
#include "StorySubsystem.generated.h"

class AStoryStateReplicator;
class UStoryActionReceiverComponent;
class UStoryDefinition;
class UStorySaveGame;

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
	virtual void Deinitialize() override;

	/** Called once by the server world subsystem after network mode is known. */
	void InitializeConfiguredProgress();

	/** Retired compatibility API. Always rejects; use UStoryFacadeSubsystem. */
	UFUNCTION(BlueprintCallable, Category = "Story|Legacy", meta = (DeprecatedFunction, DeprecationMessage = "StoryDefinition is retired. Use StoryFacadeSubsystem."))
	bool ConfigureDefinition(UStoryDefinition* NewDefinition, bool bResetProgress = true);

	UFUNCTION(BlueprintPure, Category = "Story|Legacy", meta = (DeprecatedFunction, DeprecationMessage = "StoryDefinition is retired."))
	UStoryDefinition* GetDefinition() const { return nullptr; }

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
	UStorySaveGame* BuildRoomProgress() const;
	bool ApplyRoomProgress(const UStorySaveGame* Progress);

	UFUNCTION(BlueprintPure, Category = "Story")
	FStoryProgressSnapshot GetProgressSnapshot() const;

	/** Server-side registration used by the world replicator. */
	void RegisterReplicator(AStoryStateReplicator* Replicator);
	void UnregisterReplicator(AStoryStateReplicator* Replicator);

	/** Client-side replicated state application. */
	void ApplyReplicatedSnapshot(const FStoryProgressSnapshot& Snapshot);

	/** Legacy compatibility hooks; definition-driven actions are no longer dispatched. */
	void RegisterActionReceiver(UStoryActionReceiverComponent* Receiver);
	void UnregisterActionReceiver(UStoryActionReceiverComponent* Receiver);

	UPROPERTY(BlueprintAssignable, Category = "Story")
	FOnStoryProgressChanged OnProgressChanged;

	UPROPERTY(BlueprintAssignable, Category = "Story|Legacy", meta = (DeprecatedProperty, DeprecationMessage = "Definition state rules are retired."))
	FOnStoryStateActivated OnStateActivated;

	/** Legacy serialized delegate; no definition actions are emitted. */
	UPROPERTY(BlueprintAssignable, Category = "Story|Legacy", meta = (DeprecatedProperty, DeprecationMessage = "Definition actions are retired."))
	FOnStoryActionPending OnActionPending;

private:
	void CommitProgressChange();
	void SynchronizeReplicator();
	FString ResolveSlotName(const FString& RequestedSlotName) const;

	UPROPERTY(Transient)
	FGameplayTagContainer Facts;

	TMap<FGameplayTag, int32> Counters;
	TSet<FName> AppliedActionKeys;

	UPROPERTY(Transient)
	TWeakObjectPtr<AStoryStateReplicator> ActiveReplicator;

	int32 Revision = 0;
	bool bStartupProgressInitialized = false;
};
