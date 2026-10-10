#include "Network/SWConnectionSubsystem.h"
#include "Network/SWLoadingScreenWidget.h"

#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/PendingNetGame.h"
#include "UObject/UnrealType.h"
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
	PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(this, &USWConnectionSubsystem::HandlePreLoadMap);
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &USWConnectionSubsystem::HandlePostLoadMap);
	ConnectionState = ESWConnectionState::Idle;
	LastFailure = FSWConnectionFailure();
	// Load the presentation (and its photo references) before map travel blocks the game thread.
	LoadingWidgetClass = LoadClass<USWLoadingScreenWidget>(nullptr,
		TEXT("/Game/Blueprints/02_UI/UI_Loading/WBP_LoadingScreen.WBP_LoadingScreen_C"));
}

void USWConnectionSubsystem::Deinitialize()
{
	RestoreReturnReconnectTimeout();
	StopReadinessCheck();
	HideLoadingPresentation();
	if (GEngine)
	{
		if (NetworkFailureHandle.IsValid()) GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		if (TravelFailureHandle.IsValid()) GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	if (PreLoadMapHandle.IsValid()) FCoreUObjectDelegates::PreLoadMapWithContext.Remove(PreLoadMapHandle);
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
	if (RoomLoadingReason == ERoomLoadingReason::Return && GEngine)
	{
		const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
		const FWorldContext* Context = World ? GEngine->GetWorldContextFromWorld(World) : nullptr;
		if (Context && Context->PendingNetGame)
			ApplyReturnReconnectTimeout(Context->PendingNetGame->NetDriver);
	}
	if (!bReadinessCheckActive) return;
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
	if (ReadinessElapsedSeconds >= GetActiveReadinessTimeout())
	{
		RecordFailure(ESWConnectionFailureReason::ReadinessTimeout, TEXT("ReadinessTimeout"), GetReadinessDebugStatus());
	}
}

bool USWConnectionSubsystem::IsTickable() const
{
	return (bReadinessCheckActive || RoomLoadingReason == ERoomLoadingReason::Return) && !IsTemplate();
}

void USWConnectionSubsystem::ApplyReturnReconnectTimeout(UNetDriver* NetDriver)
{
	if (RoomLoadingReason != ERoomLoadingReason::Return || !NetDriver) return;
	const bool bNewDriver = ReturnTimeoutDriver.Get() != NetDriver;
	if (bNewDriver)
	{
		RestoreReturnReconnectTimeout();
		ReturnTimeoutDriver = NetDriver;
		PreviousInitialConnectTimeout = NetDriver->InitialConnectTimeout;
		PreviousConnectionTimeout = NetDriver->ConnectionTimeout;
		if (const FFloatProperty* Resolution = FindFProperty<FFloatProperty>(NetDriver->GetClass(), TEXT("ResolutionConnectionTimeout")))
			PreviousResolutionConnectionTimeout = Resolution->GetPropertyValue_InContainer(NetDriver);
	}
	// IP address-resolution connection attempts have their own timeout, independent of UNetDriver.
	NetDriver->InitialConnectTimeout = ReturnReconnectTimeoutSeconds;
	NetDriver->ConnectionTimeout = ReturnReconnectTimeoutSeconds;
	// UE exposes this private config property through reflection but provides no public setter.
	const FFloatProperty* Resolution = FindFProperty<FFloatProperty>(NetDriver->GetClass(), TEXT("ResolutionConnectionTimeout"));
	if (Resolution) Resolution->SetPropertyValue_InContainer(NetDriver, ReturnReconnectTimeoutSeconds);
	if (bNewDriver)
		UE_LOG(LogSWConnection, Display, TEXT("Flow=Return Phase=ReconnectTimeout Driver=%s Initial=%.1f Connection=%.1f Resolution=%.1f"),
			*NetDriver->GetName(), NetDriver->InitialConnectTimeout, NetDriver->ConnectionTimeout,
			Resolution ? Resolution->GetPropertyValue_InContainer(NetDriver) : 0.0f);
}

void USWConnectionSubsystem::RestoreReturnReconnectTimeout()
{
	if (UNetDriver* Driver = ReturnTimeoutDriver.Get())
	{
		Driver->InitialConnectTimeout = PreviousInitialConnectTimeout;
		Driver->ConnectionTimeout = PreviousConnectionTimeout;
		if (const FFloatProperty* Resolution = FindFProperty<FFloatProperty>(Driver->GetClass(), TEXT("ResolutionConnectionTimeout")))
			Resolution->SetPropertyValue_InContainer(Driver, PreviousResolutionConnectionTimeout);
	}
	ReturnTimeoutDriver.Reset();
}

float USWConnectionSubsystem::GetActiveReadinessTimeout() const
{
	return RoomLoadingReason == ERoomLoadingReason::Return ? ReturnReconnectTimeoutSeconds : ReadinessTimeoutSeconds;
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
	RestoreReturnReconnectTimeout();
	if (ConnectionState == ESWConnectionState::Idle) return;
	RoomLoadingReason = ERoomLoadingReason::None;
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance || !GameInstance->GetWorld())
	{
		RecordFailure(ESWConnectionFailureReason::NetworkFailure, TEXT("MissingWorld"), TEXT("Game instance or world is unavailable."));
		return;
	}
	StopReadinessCheck();
	LoadingDestination = TEXT("/Game/Level/ConnectionLobby");
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

void USWConnectionSubsystem::HandlePreLoadMap(const FWorldContext& WorldContext, const FString& MapName)
{
	if (WorldContext.OwningGameInstance != GetGameInstance()) return;
	if (bConnectionAttemptActive || bIntentionalDisconnect)
	{
		LoadingDestination = MapName;
		ShowLoadingPresentation();
		TransitionTo(ESWConnectionState::LoadingMap);
		// Submit the updated destination photo before synchronous map loading starts.
		if (FSlateApplication::IsInitialized()) FSlateApplication::Get().Tick(ESlateTickType::TimeAndWidgets);
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UWorld* CurrentWorld = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (ConnectionState == ESWConnectionState::Playing && CurrentWorld && CurrentWorld->GetNetMode() == NM_Client)
	{
		bConnectionAttemptActive = true;
		LoadingDestination = MapName;
		ShowLoadingPresentation();
		TransitionTo(ESWConnectionState::LoadingMap);
		if (FSlateApplication::IsInitialized()) FSlateApplication::Get().Tick(ESlateTickType::TimeAndWidgets);
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
	RestoreReturnReconnectTimeout();
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
	UE_LOG(LogSWConnection, Display, TEXT("Readiness check started. AttemptId=%d Timeout=%.1f"), ActiveAttemptId, GetActiveReadinessTimeout());
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
	if (!bReadinessCheckActive || BuildReadinessMask() != AllReady) return;
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
	RestoreReturnReconnectTimeout();
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
	if (ConnectionState != ESWConnectionState::Playing || bIntentionalDisconnect) return false;
	RoomLoadingReason = ERoomLoadingReason::Return;
	// Hosted return currently restarts this same level.
	LoadingDestination = GetGameInstance()->GetWorld()->GetOutermost()->GetName();
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

bool USWConnectionSubsystem::BeginRoomFinalDeparturePresentation(int32 AttemptId)
{
	if (ConnectionState != ESWConnectionState::Playing || bIntentionalDisconnect) return false;
	FinalDepartureAttemptId = AttemptId;
	RoomLoadingReason = ERoomLoadingReason::FinalDeparture;
	// Final departure also uses ?Restart; PreLoadMap supplies the authoritative destination.
	LoadingDestination = GetGameInstance()->GetWorld()->GetOutermost()->GetName();
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
	if (RoomLoadingReason == ERoomLoadingReason::None) return;
	RestoreReturnReconnectTimeout();
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
		if (UGameViewportClient* Viewport = LoadingViewport.Get()) Viewport->SetIgnoreInput(true);
		if (LoadingWidget) LoadingWidget->SetDestination(LoadingDestination);
		UpdateLoadingPresentationText();
		return;
	}
	UGameViewportClient* Viewport = GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr;
	if (!Viewport)
	{
		UE_LOG(LogSWConnection, Warning, TEXT("Loading presentation unavailable because the game viewport is missing."));
		return;
	}

	bViewportIgnoredInputBeforeLoading = Viewport->IgnoreInput();
	if (LoadingWidgetClass)
	{
		LoadingWidget = CreateWidget<USWLoadingScreenWidget>(GetGameInstance(), LoadingWidgetClass);
	}
	if (LoadingWidget)
	{
		LoadingOverlayWidget = LoadingWidget->TakeWidget();
		LoadingWidget->SetDestination(LoadingDestination);
	}
	else
	{
	UE_LOG(LogSWConnection, Warning, TEXT("WBP_LoadingScreen unavailable; using fallback loading presentation."));
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
	}
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
	LoadingWidget = nullptr;
	LoadingDestination.Reset();
	LoadingStatusText.Reset();
	LoadingViewport.Reset();
	bLoadingPresentationVisible = false;
	bViewportIgnoredInputBeforeLoading = false;
}

void USWConnectionSubsystem::UpdateLoadingPresentationText()
{
	FText StatusText;
	if (RoomLoadingReason == ERoomLoadingReason::FinalDeparture)
	{
		StatusText = NSLOCTEXT("SWConnection", "FinalDeparture", "울돌목으로 출항 중...");
	}
	else if (RoomLoadingReason == ERoomLoadingReason::Return)
	{
		StatusText = NSLOCTEXT("SWConnection", "Returning", "귀환 준비 중...");
	}
	else
	{
	switch (ConnectionState)
	{
	case ESWConnectionState::Connecting: StatusText = NSLOCTEXT("SWConnection", "Connecting", "Connecting..."); break;
	case ESWConnectionState::Synchronizing: StatusText = NSLOCTEXT("SWConnection", "Synchronizing", "Preparing player..."); break;
	case ESWConnectionState::LoadingMap:
	default: StatusText = NSLOCTEXT("SWConnection", "Loading", "Loading..."); break;
	}
	}
	if (LoadingWidget) LoadingWidget->SetStatus(StatusText);
	if (LoadingStatusText.IsValid()) LoadingStatusText->SetText(StatusText);
}
