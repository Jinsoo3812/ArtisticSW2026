#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Room/SWRoomSnapshotTypes.h"
#include "SWRoomSnapshotSubsystem.generated.h"

class AActor;
class ULevel;
class ULevelStreaming;

/** Server authority for registered actor snapshots. */
UCLASS()
class ARTISTICSWCORE_API USWRoomSnapshotSubsystem : public UWorldSubsystem
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
	void UpdateRegisteredActorId(AActor* Actor, const FGuid& PreviousId, const FGuid& ReservedId);

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
};
