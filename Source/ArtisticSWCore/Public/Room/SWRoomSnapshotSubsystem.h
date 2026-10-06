#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Room/SWRoomSnapshotTypes.h"
#include "Room/SWVoyageResetParticipant.h"
#include "SWRoomSnapshotSubsystem.generated.h"

class AActor;
class ULevel;
class ULevelStreaming;

/** Server authority for registered actor snapshots. */
UCLASS()
class ARTISTICSWCORE_API USWRoomSnapshotSubsystem : public UWorldSubsystem, public ISWVoyageResetParticipant
{
	GENERATED_BODY()
public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	bool Capture(FSWRoomWorldSnapshot& OutSnapshot, ESWRoomSaveKind Kind, uint64 Sequence, FString& OutError,
		ESWRoomCaptureFailureKind* OutFailureKind = nullptr);
	bool Restore(const FSWRoomWorldSnapshot& Snapshot, FString& OutError, bool bReturn = false);
	static bool CompareDeclared(const FSWRoomWorldSnapshot& Expected, const FSWRoomWorldSnapshot& Actual, TArray<FString>& OutDifferences);
	bool CompareRestored(const FSWRoomWorldSnapshot& Expected, const FSWRoomWorldSnapshot& Actual, TArray<FString>& OutDifferences) const;
	bool ValidateRegistration(FString& OutError) { return Audit(OutError); }
	int32 GetUnsupportedCandidateCount() const { return UnsupportedCandidates.Num(); }
	const TArray<FString>& GetUnsupportedCandidates() const { return UnsupportedCandidates; }
	bool IsRestoringSnapshot() const { return bRestoring; }
	bool CompleteRestore(FString& OutError);
	bool BeginVoyageDiscard(ULevel* GameplayLevel, int32 Generation, FString& OutError);
	bool EndVoyageDiscard(int32 Generation, FString& OutError);
	bool IsDiscardingVoyage() const { return bDiscardingVoyage; }
	void UpdateRegisteredActorId(AActor* Actor, const FGuid& PreviousId, const FGuid& ReservedId);
	virtual ESWVoyagePolicy GetVoyagePolicy_Implementation() const override;
	virtual ESWVoyageRestoreStage GetVoyageRestoreStage_Implementation() const override;
	virtual FName GetVoyageParticipantId_Implementation() const override;
	virtual TArray<FName> GetVoyageAfterParticipants_Implementation() const override;
	virtual ESWVoyageStepResult PrepareVoyageReset_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult ResetVoyageTransientState_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult RestoreVoyageState_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual ESWVoyageStepResult IsVoyageReady_Implementation(const FSWVoyageResetContext& Context, FString& OutError) override;
	virtual void ResumeVoyage_Implementation(const FSWVoyageResetContext& Context) override;
	virtual void CancelVoyagePreparation_Implementation(const FSWVoyageResetContext& Context) override;

private:
	void HandleActorSpawned(AActor* Actor);
	void HandleActorDestroyed(AActor* Actor);
	void HandlePreLevelRemoved(ULevel* Level, UWorld* World);
	void HandleLevelAdded(ULevel* Level, UWorld* World);
	void HandleLevelBeginMakingVisible(UWorld* World, const ULevelStreaming* Streaming, ULevel* Level);
	bool Audit(FString& OutError);
	FDelegateHandle SpawnHandle;
	FDelegateHandle DestroyHandle;
	FDelegateHandle PreLevelRemovedHandle;
	FDelegateHandle LevelAddedHandle;
	FDelegateHandle LevelBeginVisibleHandle;
	TMap<FGuid, TWeakObjectPtr<AActor>> RegisteredActors;
	TMap<FGuid, FSWRoomActorRecord> UnloadedRecords;
	TSet<FGuid> PromotedUnloadedIds;
	TSet<FString> CachedPartitions;
	TMap<FString, FString> FailedPartitions;
	TSet<FString> StructuralFailedPartitions;
	TMap<FString, FString> FailedInstancePartitions;
	bool bStructuralPartitionFailure = false;
	TMap<FGuid, FString> DestroyedActorPartitions;
	FString PartitionRestorePackage;
	TMap<FGuid, ESWRoomPersistenceClass> DestroyedLevelActorIds;
	TArray<FString> UnsupportedCandidates;
	TArray<FString> AuditRows;
	TArray<FSWRoomCaptureIssue> RegistrationIssues;
	TArray<FSWRoomCaptureIssue> RestoreIssues;
	bool bRestoring = false;
	uint64 NextCreatorSequence = 0;
	bool bDiscardingVoyage = false;
	int32 DiscardGeneration = 0;
	FString DiscardPartition;
	TSet<FGuid> DiscardActorIds;
	TArray<TWeakObjectPtr<AActor>> DiscardActors;
	TWeakObjectPtr<ULevel> DiscardLevel;
	TSet<FString> DiscardActorPaths;
	int32 PreparedVoyageGeneration = -1;
	int32 RestoredVoyageGeneration = -1;
};
