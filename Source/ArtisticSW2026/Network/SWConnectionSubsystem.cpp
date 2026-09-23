#include "Network/SWConnectionSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Network/SWNetworkLog.h"
#include "UObject/UObjectGlobals.h"

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

	const FURL URL(nullptr, *TrimmedAddress, TRAVEL_Absolute);
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
	bConnectionAttemptActive = true;
	bIntentionalDisconnect = false;
	TransitionTo(ESWConnectionState::Connecting);
	UE_LOG(LogSWConnection, Display, TEXT("Direct connection started. AttemptId=%d Port=%d"), ActiveAttemptId, URL.Port);
	PlayerController->ClientTravel(TrimmedAddress, TRAVEL_Absolute);
	return true;
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
	bIntentionalDisconnect = true;
	bConnectionAttemptActive = false;
	GameInstance->ReturnToMainMenu();
}

void USWConnectionSubsystem::ResetFailure()
{
	if (ConnectionState == ESWConnectionState::Failed)
	{
		LastFailure = FSWConnectionFailure();
		TransitionTo(ESWConnectionState::Idle);
	}
}

void USWConnectionSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	if (bIntentionalDisconnect) return;
	bConnectionAttemptActive = false;
	bIntentionalDisconnect = false;
	RecordFailure(ClassifyNetworkFailure(FailureType, ErrorString), ENetworkFailure::ToString(FailureType), ErrorString);
}

void USWConnectionSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	bConnectionAttemptActive = false;
	bIntentionalDisconnect = false;
	const ESWConnectionFailureReason Reason = FailureType == ETravelFailure::InvalidURL ? ESWConnectionFailureReason::InvalidAddress : ESWConnectionFailureReason::TravelFailure;
	RecordFailure(Reason, ETravelFailure::ToString(FailureType), ErrorString);
}

void USWConnectionSubsystem::HandlePreLoadMap(const FString& MapName)
{
	if (bConnectionAttemptActive || bIntentionalDisconnect) TransitionTo(ESWConnectionState::LoadingMap);
}

void USWConnectionSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	if (!LoadedWorld) return;
	if (bIntentionalDisconnect)
	{
		bIntentionalDisconnect = false;
		bConnectionAttemptActive = false;
		TransitionTo(ESWConnectionState::Idle);
		return;
	}
	if (bConnectionAttemptActive && LoadedWorld->GetNetMode() == NM_Client)
	{
		TransitionTo(ESWConnectionState::Synchronizing);
		TransitionTo(ESWConnectionState::Playing);
		bConnectionAttemptActive = false;
	}
}

void USWConnectionSubsystem::TransitionTo(ESWConnectionState NewState)
{
	if (ConnectionState == NewState) return;
	const ESWConnectionState PreviousState = ConnectionState;
	UE_LOG(LogSWConnection, Display, TEXT("Connection state changed. Previous=%s New=%s AttemptId=%d"), *UEnum::GetValueAsString(PreviousState), *UEnum::GetValueAsString(NewState), ActiveAttemptId);
	ConnectionState = NewState;
	OnConnectionStateChanged.Broadcast(PreviousState, NewState, ActiveAttemptId);
}

void USWConnectionSubsystem::RecordFailure(ESWConnectionFailureReason Reason, const FString& EngineFailureType, const FString& EngineMessage)
{
	if (ConnectionState == ESWConnectionState::Failed && LastFailure.Reason == Reason && LastFailure.EngineFailureType == EngineFailureType && LastFailure.EngineMessage == EngineMessage) return;
	LastFailure.Reason = Reason;
	LastFailure.EngineFailureType = EngineFailureType;
	LastFailure.EngineMessage = EngineMessage;
	LastFailure.AttemptId = ActiveAttemptId;
	TransitionTo(ESWConnectionState::Failed);
	UE_LOG(LogSWConnection, Warning, TEXT("Connection failed. Reason=%s Type=%s AttemptId=%d"), *UEnum::GetValueAsString(Reason), *EngineFailureType, ActiveAttemptId);
	OnConnectionFailed.Broadcast(LastFailure);
}

ESWConnectionFailureReason USWConnectionSubsystem::ClassifyNetworkFailure(ENetworkFailure::Type FailureType, const FString& ErrorString) const
{
	if (FailureType == ENetworkFailure::FailureReceived && ErrorString.Contains(TEXT("ServerIsFull"))) return ESWConnectionFailureReason::ServerFull;
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
