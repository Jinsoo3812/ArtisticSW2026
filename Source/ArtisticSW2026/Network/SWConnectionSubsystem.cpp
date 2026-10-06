#include "Network/SWConnectionSubsystem.h"
#include "Room/SWVoyageResetSubsystem.h"
#include "Engine/World.h"

FName USWConnectionSubsystem::GetVoyageParticipantId_Implementation() const
{
	USWVoyageResetSubsystem* Voyage = GetWorld() ? GetWorld()->GetSubsystem<USWVoyageResetSubsystem>() : nullptr;
	return Voyage ? Voyage->ResolveParticipantId(const_cast<USWConnectionSubsystem*>(this)) : NAME_None;
}

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/Pawn.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Network/SWNetworkLog.h"
#include "Network/SWRoomLoadDiagnostics.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Network/SWInputDiag.h"
#include "SWRoomName.h"
#include "Room/SWRoomReadyState.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Styling/CoreStyle.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
	constexpr uint8 ClientWorld = 1 << 0;
	constexpr uint8 LocalController = 1 << 1;
	constexpr uint8 PlayerState = 1 << 2;
	constexpr uint8 Pawn = 1 << 3;
	constexpr uint8 Possessed = 1 << 4;
	constexpr uint8 PawnBegunPlay = 1 << 5;
	constexpr uint8 Camera = 1 << 6;
	constexpr uint8 RoomWorldReady = 1 << 7;
	constexpr uint8 AllReady = ClientWorld | LocalController | PlayerState | Pawn | Possessed | PawnBegunPlay | Camera | RoomWorldReady;
}

bool USWConnectionSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (IsRunningDedicatedServer() || IsRunningCommandlet())
	{
		return false;
	}
	return Super::ShouldCreateSubsystem(Outer);
}

void USWConnectionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &USWConnectionSubsystem::HandleNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &USWConnectionSubsystem::HandleTravelFailure);
	}
	PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &USWConnectionSubsystem::HandlePreLoadMap);
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &USWConnectionSubsystem::HandlePostLoadMap);
	ConnectionState = ESWConnectionState::Idle;
	LastFailure = FSWConnectionFailure();
}

void USWConnectionSubsystem::Deinitialize()
{
	StopReadinessCheck();
	HideLoadingPresentation();
	if (GEngine)
	{
		if (NetworkFailureHandle.IsValid()) GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		if (TravelFailureHandle.IsValid()) GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	if (PreLoadMapHandle.IsValid()) FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadMapHandle);
	if (PostLoadMapHandle.IsValid()) FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	NetworkFailureHandle.Reset();
	TravelFailureHandle.Reset();
	PreLoadMapHandle.Reset();
	PostLoadMapHandle.Reset();
	bConnectionAttemptActive = false;
	bIntentionalDisconnect = false;
	OnConnectionStateChanged.Clear();
	OnConnectionFailed.Clear();
	Super::Deinitialize();
}

void USWConnectionSubsystem::Tick(float DeltaTime)
{
	if (bInPlaceVoyageActive)
	{
		if (CanCompleteInPlaceVoyagePresentation()) CompleteReadiness();
		return;
	}
	ReadinessElapsedSeconds += FMath::Max(DeltaTime, 0.0f);
	const uint8 ReadinessMask = BuildReadinessMask();
	if (ReadinessMask != LastLoggedReadinessMask)
	{
		if (SWRoomLoadDiagnostics::IsEnabled())
			UE_LOG(LogTemp, Display, TEXT("[SWLoadDiag] Real=%.6f Phase=Client.Readiness Attempt=%d Mask=0x%02X RoomWorldReady=%d Debug=%s"),
				FPlatformTime::Seconds(), ActiveAttemptId, ReadinessMask, (ReadinessMask & RoomWorldReady) != 0, *GetReadinessDebugStatus());
		UE_LOG(LogSWConnection, Display, TEXT("Readiness changed. AttemptId=%d Mask=0x%02X Elapsed=%.2f"), ActiveAttemptId, ReadinessMask, ReadinessElapsedSeconds);
		LastLoggedReadinessMask = ReadinessMask;
	}

	ConsecutiveReadyTicks = ReadinessMask == AllReady ? ConsecutiveReadyTicks + 1 : 0;
	if (ConsecutiveReadyTicks >= RequiredConsecutiveReadyTicks)
	{
		CompleteReadiness();
		return;
	}
	if (ReadinessElapsedSeconds >= ReadinessTimeoutSeconds)
	{
		RecordFailure(ESWConnectionFailureReason::ReadinessTimeout, TEXT("ReadinessTimeout"), GetReadinessDebugStatus());
	}
}

bool USWConnectionSubsystem::IsTickable() const
{
	return (bReadinessCheckActive || bInPlaceVoyageActive) && !IsTemplate();
}

TStatId USWConnectionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWConnectionSubsystem, STATGROUP_Tickables);
}

UWorld* USWConnectionSubsystem::GetTickableGameObjectWorld() const
{
	return GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
}

bool USWConnectionSubsystem::ConnectDirect(const FString& Address)
{
	if (ConnectionState != ESWConnectionState::Idle && ConnectionState != ESWConnectionState::Failed)
	{
		UE_LOG(LogSWConnection, Warning, TEXT("ConnectDirect rejected. State=%s AttemptId=%d"), *UEnum::GetValueAsString(ConnectionState), ActiveAttemptId);
		return false;
	}

	const FString TrimmedAddress = Address.TrimStartAndEnd();
	if (TrimmedAddress.IsEmpty())
	{
		RecordFailure(ESWConnectionFailureReason::InvalidAddress, TEXT("InvalidURL"), TEXT("Address is empty."));
		return false;
	}

	FURL URL(nullptr, *TrimmedAddress, TRAVEL_Absolute);
	if (URL.Valid == 0 || !URL.IsInternal() || URL.Host.IsEmpty())
	{
		RecordFailure(ESWConnectionFailureReason::InvalidAddress, TEXT("InvalidURL"), TEXT("Address is not a valid remote host URL."));
		return false;
	}

	UGameInstance* GameInstance = GetGameInstance();
	APlayerController* PlayerController = GameInstance ? GameInstance->GetFirstLocalPlayerController() : nullptr;
	if (!PlayerController || !PlayerController->IsLocalPlayerController())
	{
		RecordFailure(ESWConnectionFailureReason::NoLocalPlayerController, TEXT("NoLocalPlayerController"), TEXT("No local player controller is available."));
		return false;
	}

	LastFailure = FSWConnectionFailure();
	AttemptSerial = AttemptSerial >= MAX_int32 ? 1 : AttemptSerial + 1;
	ActiveAttemptId = AttemptSerial;
	DiagnosticConnectStartedAt = SWRoomLoadDiagnostics::IsEnabled() ? FPlatformTime::Seconds() : 0.0;
	SWRoomLoadDiagnostics::Mark(TEXT("Client.ConnectRequested"));
	if (SWRoomLoadDiagnostics::IsEnabled()) TRACE_BEGIN_REGION(TEXT("SW.ClientConnect"));
	FSWInputDiag::BeginAttempt(GetGameInstance(), ActiveAttemptId, PendingHostKey.IsValid());
	bConnectionAttemptActive = true;
	bIntentionalDisconnect = false;
	TransitionTo(ESWConnectionState::Connecting);
	ShowLoadingPresentation();
	UE_LOG(LogSWConnection, Display, TEXT("Direct connection started. AttemptId=%d Port=%d"), ActiveAttemptId, URL.Port);
	if (!PendingDisplayName.IsEmpty()) URL.AddOption(*FString::Printf(TEXT("SWNameHex=%s"), *FSWRoomName::ToHex(PendingDisplayName)));
	if (PendingHostKey.IsValid()) URL.AddOption(*FString::Printf(TEXT("SWHostKey=%s"), *PendingHostKey.ToString(EGuidFormats::DigitsWithHyphens)));
	PlayerController->ClientTravel(URL.ToString(), TRAVEL_Absolute);
	return true;
}

bool USWConnectionSubsystem::ConnectDirectWithName(const FString& Address, const FString& DisplayName, const FGuid& HostKey)
{
	FString Normalized;
	if (!FSWRoomName::Normalize(DisplayName, Normalized)) return false;
	PendingDisplayName = MoveTemp(Normalized);
	PendingHostKey = HostKey;
	return ConnectDirect(Address);
}

void USWConnectionSubsystem::DisconnectToDefaultMap()
{
	if (ConnectionState == ESWConnectionState::Idle) return;
	bInPlaceVoyageActive = bInPlaceVoyageFinishReceived = bInPlaceVoyageFailed = false;
	InPlaceVoyageAttemptId = 0; InPlaceVoyageGeneration = 0; InPlaceVoyageMessage.Reset();
	RoomLoadingReason = ERoomLoadingReason::None;
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance || !GameInstance->GetWorld())
	{
		RecordFailure(ESWConnectionFailureReason::NetworkFailure, TEXT("MissingWorld"), TEXT("Game instance or world is unavailable."));
		return;
	}
	StopReadinessCheck();
	ShowLoadingPresentation();
	bIntentionalDisconnect = true;
	bConnectionAttemptActive = false;
	UGameplayStatics::OpenLevel(GameInstance->GetWorld(), FName(TEXT("/Game/Level/ConnectionLobby")), true);
}

void USWConnectionSubsystem::ResetFailure()
{
	if (ConnectionState == ESWConnectionState::Failed)
	{
		StopReadinessCheck();
		HideLoadingPresentation();
		LastFailure = FSWConnectionFailure();
		TransitionTo(ESWConnectionState::Idle);
	}
}

void USWConnectionSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	if (!IsFailureRelevantToThisInstance(World))
	{
		UE_LOG(LogSWConnection, VeryVerbose, TEXT("Ignoring unrelated network failure."));
		return;
	}
	if (bIntentionalDisconnect) return;
	RecordFailure(ClassifyNetworkFailure(FailureType, ErrorString), ENetworkFailure::ToString(FailureType), ErrorString);
}

void USWConnectionSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	if (!IsFailureRelevantToThisInstance(World)) return;
	const ESWConnectionFailureReason Reason = FailureType == ETravelFailure::InvalidURL ? ESWConnectionFailureReason::InvalidAddress : ESWConnectionFailureReason::TravelFailure;
	RecordFailure(Reason, ETravelFailure::ToString(FailureType), ErrorString);
}

void USWConnectionSubsystem::HandlePreLoadMap(const FString& MapName)
{
	if (bConnectionAttemptActive || bIntentionalDisconnect)
	{
		ShowLoadingPresentation();
		TransitionTo(ESWConnectionState::LoadingMap);
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UWorld* CurrentWorld = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (ConnectionState == ESWConnectionState::Playing && CurrentWorld && CurrentWorld->GetNetMode() == NM_Client)
	{
		bConnectionAttemptActive = true;
		ShowLoadingPresentation();
		TransitionTo(ESWConnectionState::LoadingMap);
	}
}

void USWConnectionSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	if (!LoadedWorld || LoadedWorld->GetGameInstance() != GetGameInstance()) return;
	FSWInputDiag::Record(GetGameInstance(), TEXT("MapLoaded"));
	if (bIntentionalDisconnect)
	{
		StopReadinessCheck();
		bIntentionalDisconnect = false;
		bConnectionAttemptActive = false;
		TransitionTo(ESWConnectionState::Idle);
		HideLoadingPresentation();
		FSWInputDiag::Record(GetGameInstance(), TEXT("LobbyReturn"));
		return;
	}
	if (bConnectionAttemptActive)
	{
		StartReadinessCheck(LoadedWorld);
	}
}

void USWConnectionSubsystem::TransitionTo(ESWConnectionState NewState)
{
	if (ConnectionState == NewState) return;
	const ESWConnectionState PreviousState = ConnectionState;
	UE_LOG(LogSWConnection, Display, TEXT("Connection state changed. Previous=%s New=%s AttemptId=%d"), *UEnum::GetValueAsString(PreviousState), *UEnum::GetValueAsString(NewState), ActiveAttemptId);
	ConnectionState = NewState;
	if (bLoadingPresentationVisible) UpdateLoadingPresentationText();
	OnConnectionStateChanged.Broadcast(PreviousState, NewState, ActiveAttemptId);
}

void USWConnectionSubsystem::RecordFailure(ESWConnectionFailureReason Reason, const FString& EngineFailureType, const FString& EngineMessage)
{
	if (ConnectionState == ESWConnectionState::Failed && LastFailure.Reason == Reason && LastFailure.EngineFailureType == EngineFailureType && LastFailure.EngineMessage == EngineMessage) return;
	const bool bWasReturning = RoomLoadingReason != ERoomLoadingReason::None;
	bInPlaceVoyageActive = bInPlaceVoyageFinishReceived = bInPlaceVoyageFailed = false;
	InPlaceVoyageAttemptId = 0; InPlaceVoyageGeneration = 0; InPlaceVoyageMessage.Reset();
	RoomLoadingReason = ERoomLoadingReason::None;
	StopReadinessCheck();
	bConnectionAttemptActive = false;
	bIntentionalDisconnect = false;
	if (UWorld* CurrentWorld = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr; bWasReturning || (CurrentWorld && CurrentWorld->GetMapName().Contains(TEXT("ConnectionLobby")))) HideLoadingPresentation();
	LastFailure.Reason = Reason;
	LastFailure.EngineFailureType = EngineFailureType;
	LastFailure.EngineMessage = EngineMessage;
	LastFailure.AttemptId = ActiveAttemptId;
	FSWInputDiag::Record(GetGameInstance(), TEXT("Failure"));
	TransitionTo(ESWConnectionState::Failed);
	UE_LOG(LogSWConnection, Warning, TEXT("Connection failed. Reason=%s Type=%s AttemptId=%d"), *UEnum::GetValueAsString(Reason), *EngineFailureType, ActiveAttemptId);
	OnConnectionFailed.Broadcast(LastFailure);
}

ESWConnectionFailureReason USWConnectionSubsystem::ClassifyNetworkFailure(ENetworkFailure::Type FailureType, const FString& ErrorString) const
{
	if (ErrorString.Contains(TEXT("ServerIsFull"))) return ESWConnectionFailureReason::ServerFull;
	if (ErrorString.Contains(TEXT("VersionMismatch")) || ErrorString.Contains(TEXT("OutdatedClient")) || ErrorString.Contains(TEXT("OutdatedServer"))) return ESWConnectionFailureReason::VersionMismatch;
	if (ErrorString.Contains(TEXT("Timeout"), ESearchCase::IgnoreCase)
		|| ErrorString.Contains(TEXT("No response"), ESearchCase::IgnoreCase)) return ESWConnectionFailureReason::ConnectionTimeout;
	if (FailureType == ENetworkFailure::PendingConnectionFailure
		&& !ErrorString.Contains(TEXT("Rejected"), ESearchCase::IgnoreCase)) return ESWConnectionFailureReason::ConnectionTimeout;
	switch (FailureType)
	{
	case ENetworkFailure::ConnectionTimeout: return ESWConnectionFailureReason::ConnectionTimeout;
	case ENetworkFailure::ConnectionLost: return ESWConnectionFailureReason::ConnectionLost;
	case ENetworkFailure::OutdatedClient:
	case ENetworkFailure::OutdatedServer:
	case ENetworkFailure::NetGuidMismatch:
	case ENetworkFailure::NetChecksumMismatch: return ESWConnectionFailureReason::VersionMismatch;
	default: return ESWConnectionFailureReason::NetworkFailure;
	}
}

uint8 USWConnectionSubsystem::BuildReadinessMask() const
{
	uint8 Mask = 0;
	UWorld* World = ReadinessWorld.Get();
	UGameInstance* GameInstance = GetGameInstance();
	if (!World || !GameInstance || GameInstance->GetWorld() != World || World->GetNetMode() != NM_Client) return Mask;
	Mask |= ClientWorld;

	APlayerController* Controller = World->GetFirstPlayerController();
	if (!Controller || !Controller->IsLocalPlayerController()) return Mask;
	Mask |= LocalController;
	if (Controller->PlayerState) Mask |= PlayerState;

	APawn* ControlledPawn = Controller->GetPawn();
	if (!ControlledPawn) return Mask;
	Mask |= Pawn;
	if (ControlledPawn->GetController() == Controller) Mask |= Possessed;
	if (ControlledPawn->HasActorBegunPlay()) Mask |= PawnBegunPlay;
	if (Controller->PlayerCameraManager && Controller->GetViewTarget()) Mask |= Camera;
	if (PendingDisplayName.IsEmpty()) Mask |= RoomWorldReady;
	else
	{
		for (TActorIterator<ASWRoomReadyState> It(World); It; ++It)
			if (It->RestoreGeneration > 0 && It->bWorldReady && It->RoomRunId.IsValid()) { Mask |= RoomWorldReady; break; }
	}
	return Mask;
}

FString USWConnectionSubsystem::GetReadinessDebugStatus() const
{
	const uint8 Mask = BuildReadinessMask();
	return FString::Printf(TEXT("Mask=0x%02X Elapsed=%.2f Consecutive=%d CW=%d LC=%d PS=%d P=%d Poss=%d BP=%d Cam=%d"),
		Mask, ReadinessElapsedSeconds, ConsecutiveReadyTicks,
		(Mask & ClientWorld) != 0, (Mask & LocalController) != 0, (Mask & PlayerState) != 0,
		(Mask & Pawn) != 0, (Mask & Possessed) != 0, (Mask & PawnBegunPlay) != 0, (Mask & Camera) != 0);
}

void USWConnectionSubsystem::StartReadinessCheck(UWorld* LoadedWorld)
{
	if (!LoadedWorld || LoadedWorld->GetGameInstance() != GetGameInstance() || LoadedWorld->GetNetMode() != NM_Client)
	{
		RecordFailure(ESWConnectionFailureReason::TravelFailure, TEXT("UnexpectedClientWorld"), TEXT("Loaded world is not the active client world."));
		return;
	}
	ReadinessWorld = LoadedWorld;
	ReadinessElapsedSeconds = 0.0f;
	ConsecutiveReadyTicks = 0;
	LastLoggedReadinessMask = 0;
	bReadinessCheckActive = true;
	TransitionTo(ESWConnectionState::Synchronizing);
	UE_LOG(LogSWConnection, Display, TEXT("Readiness check started. AttemptId=%d Timeout=%.1f"), ActiveAttemptId, ReadinessTimeoutSeconds);
}

void USWConnectionSubsystem::StopReadinessCheck()
{
	ReadinessWorld.Reset();
	bReadinessCheckActive = false;
	ReadinessElapsedSeconds = 0.0f;
	ConsecutiveReadyTicks = 0;
	LastLoggedReadinessMask = 0;
}

void USWConnectionSubsystem::CompleteReadiness()
{
	if (bInPlaceVoyageActive && !CanCompleteInPlaceVoyagePresentation()) return;
	if (!bReadinessCheckActive || BuildReadinessMask() != AllReady) return;
	const bool bCompletedVoyage = bInPlaceVoyageActive;
	const FString CompletedVoyageMessage = InPlaceVoyageMessage;
	bInPlaceVoyageActive = false;
	bInPlaceVoyageFinishReceived = false;
	bInPlaceVoyageFailed = false;
	const float CompletedElapsedSeconds = ReadinessElapsedSeconds;
	if (SWRoomLoadDiagnostics::IsEnabled())
	{
		TRACE_END_REGION(RoomLoadingReason == ERoomLoadingReason::Return ? TEXT("SW.ClientReturn") : TEXT("SW.ClientConnect"));
		SWRoomLoadDiagnostics::MarkMemory(TEXT("Client.Playing"));
	}
	if (SWRoomLoadDiagnostics::IsEnabled())
		UE_LOG(LogTemp, Display, TEXT("[SWLoadDiag] Real=%.6f Phase=Client.Playing Attempt=%d TotalMs=%.3f"),
			FPlatformTime::Seconds(), ActiveAttemptId, DiagnosticConnectStartedAt > 0.0 ? (FPlatformTime::Seconds() - DiagnosticConnectStartedAt) * 1000.0 : -1.0);
	FGuid ReadyRoomRunId;
	if (UWorld* World = ReadinessWorld.Get())
		for (TActorIterator<ASWRoomReadyState> It(World); It; ++It)
			if (It->bWorldReady) { ReadyRoomRunId = It->RoomRunId; break; }
	StopReadinessCheck();
	bConnectionAttemptActive = false;
	FSWInputDiag::Record(GetGameInstance(), TEXT("Ready"));
	const bool bFinalDeparture = RoomLoadingReason == ERoomLoadingReason::FinalDeparture;
	RoomLoadingReason = ERoomLoadingReason::None;
	TransitionTo(ESWConnectionState::Playing);
	if (ConnectionState != ESWConnectionState::Playing) return;
	HideLoadingPresentation();
	if (bFinalDeparture)
		UE_LOG(LogSWConnection, Display, TEXT("Flow=FinalDeparture Phase=PresentationCleared AttemptId=%d"), FinalDepartureAttemptId);
	FSWInputDiag::Record(GetGameInstance(), TEXT("LoadingRemoved"));
	if (UGameInstance* Instance = GetGameInstance())
	{
		if (APlayerController* Controller = Instance->GetFirstLocalPlayerController())
		{
			if (bCompletedVoyage && !CompletedVoyageMessage.IsEmpty()) Controller->ClientMessage(CompletedVoyageMessage);
			FInputModeGameOnly InputMode;
			Controller->SetInputMode(InputMode);
			Controller->bShowMouseCursor = false;
			if (UGameViewportClient* Viewport = GEngine ? GEngine->GameViewport : nullptr)
			{
				Viewport->SetIgnoreInput(false);
				Viewport->SetMouseCaptureMode(EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown);
			}
			if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetAllUserFocusToGameViewport();
		}
	}
	FSWInputDiag::Record(GetGameInstance(), TEXT("GameInputMode"));
	UE_LOG(LogSWConnection, Display, TEXT("Readiness completed. AttemptId=%d Elapsed=%.2f"), ActiveAttemptId, CompletedElapsedSeconds);
	UE_LOG(LogSWConnection, Display, TEXT("Flow=ClientReady AttemptId=%d RoomRunId=%s Elapsed=%.2f"),
		ActiveAttemptId, *ReadyRoomRunId.ToString(), CompletedElapsedSeconds);
}

bool USWConnectionSubsystem::IsFailureRelevantToThisInstance(const UWorld* FailureWorld) const
{
	if ((ConnectionState == ESWConnectionState::Idle || ConnectionState == ESWConnectionState::Failed) && !bConnectionAttemptActive && !bIntentionalDisconnect) return false;
	if (!FailureWorld) return bConnectionAttemptActive || bIntentionalDisconnect || ConnectionState == ESWConnectionState::Playing;
	return FailureWorld->GetGameInstance() == GetGameInstance();
}

bool USWConnectionSubsystem::BeginRoomReturnPresentation()
{
	if (bInPlaceVoyageActive || ConnectionState != ESWConnectionState::Playing || bIntentionalDisconnect) return false;
	RoomLoadingReason = ERoomLoadingReason::Return;
	DiagnosticConnectStartedAt = SWRoomLoadDiagnostics::IsEnabled() ? FPlatformTime::Seconds() : 0.0;
	SWRoomLoadDiagnostics::Mark(TEXT("Client.ReturnPresentation"));
	if (SWRoomLoadDiagnostics::IsEnabled()) TRACE_BEGIN_REGION(TEXT("SW.ClientReturn"));
	bConnectionAttemptActive = true;
	ShowLoadingPresentation();
	if (!bLoadingPresentationVisible)
	{
		RoomLoadingReason = ERoomLoadingReason::None;
		bConnectionAttemptActive = false;
		return false;
	}
	UE_LOG(LogSWConnection, Display, TEXT("Flow=Return Phase=PresentationShown"));
	return true;
}

bool USWConnectionSubsystem::BeginInPlaceVoyagePresentation(const FSWVoyageReplicatedState& State)
{
	if (bIntentionalDisconnect || State.AttemptId <= 0 || State.Generation <= 0
		|| State.AttemptId < InPlaceVoyageAttemptId || State.Generation < InPlaceVoyageGeneration) return false;
	if (State.Phase == ESWVoyagePhase::RecoveryTravel)
	{
		// The existing map-load readiness flow owns the one recovery travel.
		InPlaceVoyageAttemptId = State.AttemptId; InPlaceVoyageGeneration = State.Generation;
		bInPlaceVoyageActive = bInPlaceVoyageFinishReceived = bInPlaceVoyageFailed = false;
		bConnectionAttemptActive = true; StopReadinessCheck(); ShowLoadingPresentation();
		return bLoadingPresentationVisible;
	}
	if (State.AttemptId == InPlaceVoyageAttemptId && State.Generation == InPlaceVoyageGeneration)
		return bInPlaceVoyageActive && bLoadingPresentationVisible;
	if (State.AttemptId == InPlaceVoyageAttemptId && !State.bBootstrap) return false;
	InPlaceVoyageAttemptId = State.AttemptId; InPlaceVoyageGeneration = State.Generation;
	bInPlaceVoyageActive = true; bInPlaceVoyageFinishReceived = false; bInPlaceVoyageFailed = false;
	bInPlaceVoyageSaveSucceeded = true; InPlaceVoyageMessage.Reset();
	RoomLoadingReason = State.Reason == ESWVoyageReason::FinalDeparture ? ERoomLoadingReason::FinalDeparture : ERoomLoadingReason::Return;
	bConnectionAttemptActive = true;
	ReadinessWorld = GetWorld(); bReadinessCheckActive = true;
	ReadinessElapsedSeconds = 0.f; ConsecutiveReadyTicks = 0;
	ShowLoadingPresentation();
	return bLoadingPresentationVisible;
}

bool USWConnectionSubsystem::CanCompleteInPlaceVoyagePresentation() const
{
	if (!bInPlaceVoyageActive || !bInPlaceVoyageFinishReceived || bInPlaceVoyageFailed || !ReadinessWorld.IsValid()
		|| ReadinessWorld.Get() != GetWorld() || BuildReadinessMask() != AllReady) return false;
	for (TActorIterator<ASWRoomReadyState> It(ReadinessWorld.Get()); It; ++It)
		if (It->bWorldReady && It->RestoreGeneration == InPlaceVoyageGeneration
			&& It->VoyageState.AttemptId == InPlaceVoyageAttemptId && It->VoyageState.Generation == InPlaceVoyageGeneration
			&& (It->VoyageState.Phase == ESWVoyagePhase::Release || It->VoyageState.Phase == ESWVoyagePhase::Idle)) return true;
	return false;
}

void USWConnectionSubsystem::CompleteInPlaceVoyagePresentation(int64 AttemptId, int32 Generation, bool bSaveSucceeded, const FString& Message)
{
	if (!bInPlaceVoyageActive || AttemptId != InPlaceVoyageAttemptId || Generation != InPlaceVoyageGeneration) return;
	bInPlaceVoyageFinishReceived = true; bInPlaceVoyageSaveSucceeded = bSaveSucceeded;
	InPlaceVoyageMessage = Message.Left(512);
	if (!bSaveSucceeded && InPlaceVoyageMessage.IsEmpty())
		InPlaceVoyageMessage = TEXT("새 항해는 준비되었지만 저장에 실패했습니다. 수동 저장을 다시 시도하세요.");
	if (CanCompleteInPlaceVoyagePresentation()) CompleteReadiness();
}

void USWConnectionSubsystem::CancelInPlaceVoyagePresentation(int64 AttemptId, int32 Generation)
{
	if (!bInPlaceVoyageActive || AttemptId != InPlaceVoyageAttemptId || Generation != InPlaceVoyageGeneration) return;
	bInPlaceVoyageActive = false; bInPlaceVoyageFinishReceived = false; bInPlaceVoyageFailed = false;
	StopReadinessCheck();
	CancelRoomReturnPresentation();
}

void USWConnectionSubsystem::SetVoyageFailure(int64 AttemptId, int32 Generation, const FString& Error)
{
	if (!bInPlaceVoyageActive || AttemptId != InPlaceVoyageAttemptId || Generation != InPlaceVoyageGeneration) return;
	bInPlaceVoyageFailed = true; bInPlaceVoyageFinishReceived = false;
	InPlaceVoyageMessage = Error.Left(512);
	ShowLoadingPresentation();
	if (LoadingStatusText.IsValid()) LoadingStatusText->SetText(FText::FromString(InPlaceVoyageMessage));
	// Keep viewport input ownership while the controller's native failure UI is visible.
	if (LoadingOverlayWidget.IsValid()) LoadingOverlayWidget->SetVisibility(EVisibility::Collapsed);
}

bool USWConnectionSubsystem::BeginRoomFinalDeparturePresentation(int32 AttemptId)
{
	if (ConnectionState != ESWConnectionState::Playing || bIntentionalDisconnect) return false;
	FinalDepartureAttemptId = AttemptId;
	RoomLoadingReason = ERoomLoadingReason::FinalDeparture;
	bConnectionAttemptActive = true;
	ShowLoadingPresentation();
	if (!bLoadingPresentationVisible)
	{
		RoomLoadingReason = ERoomLoadingReason::None;
		bConnectionAttemptActive = false;
		UE_LOG(LogSWConnection, Warning, TEXT("Flow=FinalDeparture Phase=PresentationFailed AttemptId=%d"), AttemptId);
		return false;
	}
	UE_LOG(LogSWConnection, Display, TEXT("Flow=FinalDeparture Phase=PresentationShown AttemptId=%d"), AttemptId);
	return true;
}

void USWConnectionSubsystem::CancelRoomReturnPresentation()
{
	if (bInPlaceVoyageActive) return;
	if (RoomLoadingReason == ERoomLoadingReason::None) return;
	const bool bFinalDeparture = RoomLoadingReason == ERoomLoadingReason::FinalDeparture;
	RoomLoadingReason = ERoomLoadingReason::None;
	bConnectionAttemptActive = false;
	HideLoadingPresentation();
	UE_LOG(LogSWConnection, Warning, TEXT("Flow=%s Phase=PresentationCancelled AttemptId=%d"),
		bFinalDeparture ? TEXT("FinalDeparture") : TEXT("Return"), FinalDepartureAttemptId);
}

void USWConnectionSubsystem::ShowLoadingPresentation()
{
	if (IsRunningDedicatedServer() || IsRunningCommandlet()) return;
	if (bLoadingPresentationVisible)
	{
		if (LoadingOverlayWidget.IsValid()) LoadingOverlayWidget->SetVisibility(EVisibility::Visible);
		if (UGameViewportClient* Viewport = LoadingViewport.Get()) Viewport->SetIgnoreInput(true);
		UpdateLoadingPresentationText();
		return;
	}
	if (!GEngine || !GEngine->GameViewport)
	{
		UE_LOG(LogSWConnection, Warning, TEXT("Loading presentation unavailable because the game viewport is missing."));
		return;
	}

	UGameViewportClient* Viewport = GEngine->GameViewport;
	bViewportIgnoredInputBeforeLoading = Viewport->IgnoreInput();
	TSharedPtr<STextBlock> StatusText;
	LoadingOverlayWidget = SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor::Black)
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SAssignNew(StatusText, STextBlock)
			.ColorAndOpacity(FLinearColor::White)
		];
	LoadingStatusText = StatusText;
	LoadingViewport = Viewport;
	Viewport->AddViewportWidgetContent(LoadingOverlayWidget.ToSharedRef(), 10000);
	Viewport->SetIgnoreInput(true);
	bLoadingPresentationVisible = true;
	UpdateLoadingPresentationText();
}

void USWConnectionSubsystem::HideLoadingPresentation()
{
	UGameViewportClient* Viewport = LoadingViewport.Get();
	if (Viewport && LoadingOverlayWidget.IsValid()) Viewport->RemoveViewportWidgetContent(LoadingOverlayWidget.ToSharedRef());
	if (Viewport) Viewport->SetIgnoreInput(bViewportIgnoredInputBeforeLoading);
	LoadingOverlayWidget.Reset();
	LoadingStatusText.Reset();
	LoadingViewport.Reset();
	bLoadingPresentationVisible = false;
	bViewportIgnoredInputBeforeLoading = false;
}

void USWConnectionSubsystem::UpdateLoadingPresentationText()
{
	if (!LoadingStatusText.IsValid()) return;
	if (RoomLoadingReason == ERoomLoadingReason::FinalDeparture)
	{
		LoadingStatusText->SetText(NSLOCTEXT("SWConnection", "FinalDeparture", "울돌목으로 출항 중..."));
		return;
	}
	if (RoomLoadingReason == ERoomLoadingReason::Return)
	{
		LoadingStatusText->SetText(NSLOCTEXT("SWConnection", "Returning", "귀환 준비 중..."));
		return;
	}
	FText StatusText;
	switch (ConnectionState)
	{
	case ESWConnectionState::Connecting: StatusText = NSLOCTEXT("SWConnection", "Connecting", "Connecting..."); break;
	case ESWConnectionState::Synchronizing: StatusText = NSLOCTEXT("SWConnection", "Synchronizing", "Preparing player..."); break;
	case ESWConnectionState::LoadingMap:
	default: StatusText = NSLOCTEXT("SWConnection", "Loading", "Loading..."); break;
	}
	LoadingStatusText->SetText(StatusText);
}
