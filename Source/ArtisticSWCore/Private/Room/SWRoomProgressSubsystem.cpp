#include "Room/SWRoomProgressSubsystem.h"
#include "Room/SWRoomRuntimePaths.h"
#include "Engine/World.h"

namespace SWDevTestInput
{
FString Package(UWorld* World)
{
 return World ? UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) : FString();
}
bool Authority(UWorld* World) { return World && World->IsGameWorld() && World->GetNetMode() != NM_Client; }
}
bool USWRoomProgressSubsystem::IsDevelopmentTestSessionEnabled(UWorld* World) const
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
 return SWDevTestInput::Authority(World) && DevelopmentSessionWorld.Get() == World && bDevelopmentSessionEnabled;
#else
 return false;
#endif
}
void USWRoomProgressSubsystem::SetDevelopmentTestSessionEnabled(UWorld* World, bool bEnabled)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
 if (SWDevTestInput::Authority(World)) { DevelopmentSessionWorld = World; bDevelopmentSessionEnabled = bEnabled; }
#endif
}
void USWRoomProgressSubsystem::SetDevelopmentFinalDeparturePending(UWorld* World, bool bPending, bool bValidatedTransition)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
 if (!bPending) { bDevelopmentFinalDeparturePending = false; DevelopmentFinalTargetPackage.Reset(); return; }
 if (SWDevTestInput::Authority(World) && (IsDevelopmentTestSessionEnabled(World) || bValidatedTransition)) { bDevelopmentFinalDeparturePending = true; DevelopmentFinalTargetPackage = SWDevTestInput::Package(World); }
#endif
}
bool USWRoomProgressSubsystem::ConsumeDevelopmentFinalDeparturePending(UWorld* World)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
 const bool bPermit = bDevelopmentFinalDeparturePending && IsFinalDepartureTravelPending()
  && SWDevTestInput::Authority(World) && World != DevelopmentSessionWorld.Get()
  && DevelopmentFinalTargetPackage == SWDevTestInput::Package(World);
 SetDevelopmentFinalDeparturePending(World, false);
 DevelopmentEncounterWorld = bPermit ? World : nullptr;
 SetDevelopmentTestSessionEnabled(World, false);
 return bPermit;
#else
 return false;
#endif
}
bool USWRoomProgressSubsystem::IsDevelopmentFinalEncounterWorld(UWorld* World) const
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
 return SWDevTestInput::Authority(World) && DevelopmentEncounterWorld.Get() == World;
#else
 return false;
#endif
}
void USWRoomProgressSubsystem::ClearDevelopmentFinalEncounterWorld(UWorld* World)
{
 SetDevelopmentFinalDeparturePending(World, false); DevelopmentEncounterWorld.Reset();
}
void USWRoomProgressSubsystem::Deinitialize()
{
 DevelopmentSessionWorld.Reset(); DevelopmentEncounterWorld.Reset(); bDevelopmentSessionEnabled = false;
 bDevelopmentFinalDeparturePending = false; DevelopmentFinalTargetPackage.Reset(); Super::Deinitialize();
}

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
	FString Root, RootError;
	if (!FSWRoomRuntimePaths::TryResolveRoot(Root, RootError))
	{
		bStartupError = true;
		UE_LOG(LogSWRoom, Error, TEXT("%s"), *RootError);
		return;
	}
	UE_LOG(LogSWRoom, Display, TEXT("Flow=ServerStartup RunId=%s Phase=ReadArguments Root=%s"), *RunText, *Root);
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
	SW_ROOM_DETAIL_LOG(LogSWRoomSave, Display, TEXT("Flow=Checkpoint Phase=WriteRequested RoomId=%s Kind=%s Sequence=%llu NewPending=%d"),
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
		SW_ROOM_DETAIL_LOG(LogSWRoomSave, Display, TEXT("Flow=Checkpoint Result=Committed RoomId=%s Kind=%s Sequence=%llu"),
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
