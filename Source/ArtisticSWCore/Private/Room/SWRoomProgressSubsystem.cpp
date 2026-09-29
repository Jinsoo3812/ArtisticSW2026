#include "Room/SWRoomProgressSubsystem.h"

#include "Room/SWRoomSaveGame.h"
#include "Room/SWRoomSaveStore.h"
#include "Network/SWNetworkLog.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void USWRoomProgressSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FString RunText, Mode, RoomText, HostText;
	if (!IsRunningDedicatedServer() || !FParse::Value(FCommandLine::Get(), TEXT("SWRoomRunId="), RunText)) return;
	bHostedRoom = true;
	UE_LOG(LogSWRoom, Display, TEXT("Flow=ServerStartup RunId=%s Phase=ReadArguments"), *RunText);
	FGuid RunId, RoomId;
	if (!FGuid::Parse(RunText, RunId) || !RunId.IsValid()
		|| !FParse::Value(FCommandLine::Get(), TEXT("SWRoomMode="), Mode)
		|| !FParse::Value(FCommandLine::Get(), TEXT("SWRoomId="), RoomText)
		|| !FGuid::Parse(RoomText, RoomId) || !RoomId.IsValid()
		|| !FParse::Value(FCommandLine::Get(), TEXT("SWRoomHostKey="), HostText)
		|| !FGuid::Parse(HostText, HostKey) || !HostKey.IsValid())
	{
		bStartupError = true;
		UE_LOG(LogSWRoom, Error, TEXT("Flow=ServerStartup RunId=%s Result=Failed Reason=InvalidArguments"), *RunText);
		return;
	}
	if (Mode == TEXT("Continue"))
	{
		ActiveRoom = FSWRoomSaveStore::LoadCurrentRoom(this);
		bStartupError = !ActiveRoom || ActiveRoom->RoomId != RoomId;
	}
	else if (Mode == TEXT("New"))
	{
		ActiveRoom = FSWRoomSaveStore::LoadStagedNewRoom(this);
		bStartupError = !ActiveRoom || ActiveRoom->RoomId != RoomId || ActiveRoom->bComplete;
		bNewRoomPending = !bStartupError;
	}
	else bStartupError = true;
	if (bStartupError)
	{
		UE_LOG(LogSWRoom, Error, TEXT("Flow=ServerStartup RunId=%s RoomId=%s Mode=%s Result=Failed"), *RunText, *RoomText, *Mode);
	}
	else
	{
		UE_LOG(LogSWRoom, Display, TEXT("Flow=ServerStartup RunId=%s RoomId=%s Mode=%s Result=Loaded Backup=%d Sequence=%llu"),
			*RunText, *RoomText, *Mode, ActiveRoom->bRecoveredFromBackup ? 1 : 0, ActiveRoom->CaptureSequence);
	}
}

bool USWRoomProgressSubsystem::WriteCheckpoint()
{
	if (!bHostedRoom || bStartupError || !ActiveRoom || bSaving || !ActiveRoom->bComplete)
	{
		UE_LOG(LogSWRoomSave, Error,
			TEXT("Flow=Checkpoint Result=Rejected RoomId=%s Sequence=%llu Hosted=%d StartupError=%d HasRoom=%d Busy=%d Complete=%d"),
			ActiveRoom ? *ActiveRoom->RoomId.ToString() : TEXT("None"), ActiveRoom ? ActiveRoom->CaptureSequence : 0,
			bHostedRoom, bStartupError, ActiveRoom != nullptr, bSaving, ActiveRoom && ActiveRoom->bComplete);
		UE_LOG(LogSWRoom, Warning, TEXT("Flow=Checkpoint Result=Rejected Hosted=%d StartupError=%d HasRoom=%d Busy=%d Complete=%d"),
			bHostedRoom, bStartupError, ActiveRoom != nullptr, bSaving, ActiveRoom && ActiveRoom->bComplete);
		return false;
	}
	TGuardValue<bool> SavingGuard(bSaving, true);
	UE_LOG(LogSWRoom, Display, TEXT("Flow=Checkpoint RoomId=%s Kind=%s Sequence=%llu Phase=WriteRequested NewPending=%d"),
		*ActiveRoom->RoomId.ToString(), *UEnum::GetValueAsString(ActiveRoom->SaveKind), ActiveRoom->CaptureSequence, bNewRoomPending);
	UE_LOG(LogSWRoomSave, Display, TEXT("Flow=Checkpoint Phase=WriteRequested RoomId=%s Kind=%s Sequence=%llu NewPending=%d"),
		*ActiveRoom->RoomId.ToString(), *UEnum::GetValueAsString(ActiveRoom->SaveKind), ActiveRoom->CaptureSequence, bNewRoomPending);
	const bool bSuccess = bNewRoomPending
		? (FSWRoomSaveStore::StageCompleteNewRoom(ActiveRoom) && FSWRoomSaveStore::CommitStagedNewRoom())
		: FSWRoomSaveStore::WriteCurrentRoom(ActiveRoom);
	if (bSuccess && bNewRoomPending)
	{
		bNewRoomPending = false;
		bNewRoomCommitted = true;
	}
	if (bSuccess)
	{
		UE_LOG(LogSWRoom, Display, TEXT("Flow=Checkpoint RoomId=%s Kind=%s Sequence=%llu Result=Committed"),
			*ActiveRoom->RoomId.ToString(), *UEnum::GetValueAsString(ActiveRoom->SaveKind), ActiveRoom->CaptureSequence);
		UE_LOG(LogSWRoomSave, Display, TEXT("Flow=Checkpoint Result=Committed RoomId=%s Kind=%s Sequence=%llu"),
			*ActiveRoom->RoomId.ToString(), *UEnum::GetValueAsString(ActiveRoom->SaveKind), ActiveRoom->CaptureSequence);
	}
	else
	{
		UE_LOG(LogSWRoom, Error, TEXT("Flow=Checkpoint RoomId=%s Kind=%s Sequence=%llu Result=Failed"),
			*ActiveRoom->RoomId.ToString(), *UEnum::GetValueAsString(ActiveRoom->SaveKind), ActiveRoom->CaptureSequence);
		UE_LOG(LogSWRoomSave, Error, TEXT("Flow=Checkpoint Result=Failed RoomId=%s Kind=%s Sequence=%llu Reason=FileTransactionFailed"),
			*ActiveRoom->RoomId.ToString(), *UEnum::GetValueAsString(ActiveRoom->SaveKind), ActiveRoom->CaptureSequence);
	}
	return bSuccess;
}
