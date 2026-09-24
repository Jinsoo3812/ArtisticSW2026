#include "Network/SWConnectionSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/Pawn.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Network/SWNetworkLog.h"
#include "Network/SWInputDiag.h"
#include "SWRoomName.h"
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
	constexpr uint8 AllReady = ClientWorld | LocalController | PlayerState | Pawn | Possessed | PawnBegunPlay | Camera;
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
	ReadinessElapsedSeconds += FMath::Max(DeltaTime, 0.0f);
	const uint8 ReadinessMask = BuildReadinessMask();
	if (ReadinessMask != LastLoggedReadinessMask)
	{
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
	return bReadinessCheckActive && !IsTemplate();
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
	StopReadinessCheck();
	bConnectionAttemptActive = false;
	bIntentionalDisconnect = false;
	if (UWorld* CurrentWorld = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr; CurrentWorld && CurrentWorld->GetMapName().Contains(TEXT("ConnectionLobby"))) HideLoadingPresentation();
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
	if (!bReadinessCheckActive || BuildReadinessMask() != AllReady) return;
	const float CompletedElapsedSeconds = ReadinessElapsedSeconds;
	StopReadinessCheck();
	bConnectionAttemptActive = false;
	FSWInputDiag::Record(GetGameInstance(), TEXT("Ready"));
	TransitionTo(ESWConnectionState::Playing);
	if (ConnectionState != ESWConnectionState::Playing) return;
	HideLoadingPresentation();
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
}

bool USWConnectionSubsystem::IsFailureRelevantToThisInstance(const UWorld* FailureWorld) const
{
	if ((ConnectionState == ESWConnectionState::Idle || ConnectionState == ESWConnectionState::Failed) && !bConnectionAttemptActive && !bIntentionalDisconnect) return false;
	if (!FailureWorld) return bConnectionAttemptActive || bIntentionalDisconnect || ConnectionState == ESWConnectionState::Playing;
	return FailureWorld->GetGameInstance() == GetGameInstance();
}

void USWConnectionSubsystem::ShowLoadingPresentation()
{
	if (IsRunningDedicatedServer() || IsRunningCommandlet()) return;
	if (bLoadingPresentationVisible)
	{
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
