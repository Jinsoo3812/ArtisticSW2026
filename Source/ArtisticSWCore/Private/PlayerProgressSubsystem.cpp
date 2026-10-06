#include "PlayerProgressSubsystem.h"
#include "Room/SWVoyageResetSubsystem.h"

ESWVoyagePolicy UPlayerProgressSubsystem::GetVoyagePolicy_Implementation() const { return ESWVoyagePolicy::ResetParticipant; }
ESWVoyageRestoreStage UPlayerProgressSubsystem::GetVoyageRestoreStage_Implementation() const { return ESWVoyageRestoreStage::Readiness; }
FName UPlayerProgressSubsystem::GetVoyageParticipantId_Implementation() const
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	USWVoyageResetSubsystem* Voyage = World ? World->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	return Voyage ? Voyage->ResolveParticipantId(const_cast<UPlayerProgressSubsystem*>(this)) : NAME_None;
}
TArray<FName> UPlayerProgressSubsystem::GetVoyageAfterParticipants_Implementation() const { return {}; }
ESWVoyageStepResult UPlayerProgressSubsystem::PrepareVoyageReset_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	OutError.Reset();
	if (Context.Generation <= 0 || Context.Generation < HostedVoyageResetGeneration)
	{ OutError = TEXT("VoyagePlayerProgressGenerationInvalid"); return ESWVoyageStepResult::Failed; }
	return ESWVoyageStepResult::Succeeded;
}
ESWVoyageStepResult UPlayerProgressSubsystem::ResetVoyageTransientState_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	OutError.Reset();
	if (HostedVoyageResetGeneration == Context.Generation) return ESWVoyageStepResult::Succeeded;
	if (Context.Generation <= 0 || Context.Generation < HostedVoyageResetGeneration)
	{ OutError = TEXT("VoyagePlayerProgressResetGenerationInvalid"); return ESWVoyageStepResult::Failed; }
	ClearSnapshotsForHostedReturn();
	HostedVoyageResetGeneration = Context.Generation;
	return ESWVoyageStepResult::Succeeded;
}
ESWVoyageStepResult UPlayerProgressSubsystem::RestoreVoyageState_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	OutError.Reset();
	if (HostedVoyageResetGeneration != Context.Generation)
	{ OutError = TEXT("VoyagePlayerProgressRestoreGenerationInvalid"); return ESWVoyageStepResult::Failed; }
	return ESWVoyageStepResult::Succeeded;
}
ESWVoyageStepResult UPlayerProgressSubsystem::IsVoyageReady_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	OutError.Reset();
	if (HostedVoyageResetGeneration != Context.Generation)
	{ OutError = TEXT("VoyagePlayerProgressReadyGenerationInvalid"); return ESWVoyageStepResult::Failed; }
	if (!PendingSnapshots.IsEmpty() || !ReconnectSnapshots.IsEmpty())
	{ OutError = TEXT("VoyagePlayerProgressStaleSnapshotRetained"); return ESWVoyageStepResult::Failed; }
	return ESWVoyageStepResult::Succeeded;
}
void UPlayerProgressSubsystem::ResumeVoyage_Implementation(const FSWVoyageResetContext& Context)
{
	if (HostedVoyageResetGeneration == Context.Generation) HostedVoyageResetGeneration = -1;
}
void UPlayerProgressSubsystem::CancelVoyagePreparation_Implementation(const FSWVoyageResetContext&) {}

void UPlayerProgressSubsystem::StoreSnapshot(int32 PlayerIndex, const FSWPlayerProgressSnapshot& Snapshot)
{
	PendingSnapshots.Add(PlayerIndex, Snapshot);
}

bool UPlayerProgressSubsystem::ConsumeSnapshot(int32 PlayerIndex, FSWPlayerProgressSnapshot& OutSnapshot)
{
	if (FSWPlayerProgressSnapshot* Found = PendingSnapshots.Find(PlayerIndex))
	{
		OutSnapshot = MoveTemp(*Found);
		PendingSnapshots.Remove(PlayerIndex);
		return true;
	}
	return false;
}

bool UPlayerProgressSubsystem::HasSnapshot(int32 PlayerIndex) const
{
	return PendingSnapshots.Contains(PlayerIndex);
}

void UPlayerProgressSubsystem::StoreReconnectSnapshot(const FString& PlayerKey, const FSWPlayerProgressSnapshot& Snapshot)
{
	if (!PlayerKey.IsEmpty()) ReconnectSnapshots.Add(PlayerKey, Snapshot);
}

bool UPlayerProgressSubsystem::ConsumeReconnectSnapshot(const FString& PlayerKey, FSWPlayerProgressSnapshot& OutSnapshot)
{
	if (FSWPlayerProgressSnapshot* Found = ReconnectSnapshots.Find(PlayerKey))
	{
		OutSnapshot = *Found;
		return true;
	}
	return false;
}

bool UPlayerProgressSubsystem::PeekReconnectSnapshot(const FString& PlayerKey, FSWPlayerProgressSnapshot& OutSnapshot) const
{
	if (const FSWPlayerProgressSnapshot* Found = ReconnectSnapshots.Find(PlayerKey))
	{
		OutSnapshot = *Found;
		return true;
	}
	return false;
}

void UPlayerProgressSubsystem::ClearReconnectSnapshots()
{
	ReconnectSnapshots.Reset();
}

void UPlayerProgressSubsystem::ClearSnapshotsForHostedReturn()
{
	PendingSnapshots.Reset();
	ClearReconnectSnapshots();
}
