#include "Network/Lobby/SWRoomSubsystem.h"

#include "Network/Lobby/SWRoomCode.h"
#include "Network/SWConnectionSubsystem.h"
#include "Network/SWNetworkLog.h"
#include "SWRoomName.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Kismet/GameplayStatics.h"

namespace
{
constexpr uint16 RoomPort = 7777;
const TCHAR* LobbyMap = TEXT("/Game/Level/ConnectionLobby");
}

bool USWRoomSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && !IsRunningCommandlet() && Super::ShouldCreateSubsystem(Outer);
}

void USWRoomSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<USWConnectionSubsystem>();
	if (USWConnectionSubsystem* Connection = GetGameInstance()->GetSubsystem<USWConnectionSubsystem>())
	{
		Connection->OnConnectionStateChanged.AddDynamic(this, &USWRoomSubsystem::HandleConnectionChanged);
		Connection->OnConnectionFailed.AddDynamic(this, &USWRoomSubsystem::HandleConnectionFailed);
	}
}

void USWRoomSubsystem::Deinitialize()
{
	++OperationId;
	if (PendingRequest.IsValid()) PendingRequest->CancelRequest();
	PendingRequest.Reset();
	if (USWConnectionSubsystem* Connection = GetGameInstance()->GetSubsystem<USWConnectionSubsystem>())
	{
		Connection->OnConnectionStateChanged.RemoveDynamic(this, &USWRoomSubsystem::HandleConnectionChanged);
		Connection->OnConnectionFailed.RemoveDynamic(this, &USWRoomSubsystem::HandleConnectionFailed);
	}
	StopServer();
	OnRoomChanged.Clear();
	Super::Deinitialize();
}

TStatId USWRoomSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWRoomSubsystem, STATGROUP_Tickables);
}

UWorld* USWRoomSubsystem::GetTickableGameObjectWorld() const
{
	return GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
}

bool USWRoomSubsystem::CanHost() const
{
#if PLATFORM_WINDOWS && WITH_EDITOR
	return true;
#else
	return false;
#endif
}

void USWRoomSubsystem::SetState(ESWRoomState NewState, const FText& NewMessage)
{
	State = NewState;
	Message = NewMessage;
	OnRoomChanged.Broadcast(State, Message);
	UE_LOG(LogSWConnection, Display, TEXT("Side=%s OperationId=%llu Phase=%s Port=%d"), bOwnsServer ? TEXT("HostClient") : TEXT("JoinClient"), OperationId, *UEnum::GetValueAsString(State), RoomPort);
}

void USWRoomSubsystem::Fail(const FText& FailureMessage)
{
	++OperationId;
	if (PendingRequest.IsValid()) PendingRequest->CancelRequest();
	PendingRequest.Reset();
	StopServer();
	SetState(ESWRoomState::Failed, FailureMessage);
	ReturnToLobby();
}

bool USWRoomSubsystem::CreateRoom(const FString& Name, const FString& OptionalPublicIPv4)
{
	if (State != ESWRoomState::Idle && State != ESWRoomState::Failed) return false;
	bReturningToLobby = false;
	if (!CanHost()) { Fail(FText::FromString(TEXT("이 구성에서는 방을 만들 수 없습니다."))); return false; }
	if (!FSWRoomName::Normalize(Name, DisplayName)) { Fail(FText::FromString(TEXT("이름은 1~16자, UTF-8 48바이트 이하여야 합니다."))); return false; }
	++OperationId;
	DisplayCode.Empty();
	PublicAddress.Empty();
	bAwaitingHostJoin = false;
	bOwnsServer = false;
	uint8 Octets[4];
	if (!OptionalPublicIPv4.IsEmpty())
	{
		const FString Address = OptionalPublicIPv4.TrimStartAndEnd();
		if (!FSWRoomCode::ParsePublicIPv4(Address, Octets)) { Fail(FText::FromString(TEXT("올바른 공인 IPv4를 입력하세요."))); return false; }
		bNeedsManualPublicIP = false;
		StartServer(Address);
		return true;
	}
	SetState(ESWRoomState::ResolvingPublicIP, FText::FromString(TEXT("공인 IP 확인 중...")));
	const uint64 RequestId = OperationId;
	PendingRequest = FHttpModule::Get().CreateRequest();
	PendingRequest->SetURL(TEXT("https://api4.ipify.org"));
	PendingRequest->SetVerb(TEXT("GET"));
	PendingRequest->SetTimeout(15.0f);
	TWeakObjectPtr<USWRoomSubsystem> WeakThis(this);
	PendingRequest->OnProcessRequestComplete().BindLambda([WeakThis, RequestId](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
	{
		USWRoomSubsystem* Self = WeakThis.Get();
		if (!Self || !Self->GetGameInstance() || Self->OperationId != RequestId || Self->State != ESWRoomState::ResolvingPublicIP) return;
		Self->PendingRequest.Reset();
		uint8 Parsed[4];
		const FString Address = Response.IsValid() ? Response->GetContentAsString().TrimStartAndEnd() : FString();
		if (!bSuccess || !Response.IsValid() || Response->GetResponseCode() != 200 || !FSWRoomCode::ParsePublicIPv4(Address, Parsed))
		{
			Self->bNeedsManualPublicIP = true;
			UE_LOG(LogSWConnection, Warning, TEXT("Side=HostClient OperationId=%llu Phase=PublicIP Result=Failed"), RequestId);
			Self->Fail(FText::FromString(TEXT("공인 IP 확인 실패. 공인 IPv4를 직접 입력해 다시 시도하세요.")));
			return;
		}
		UE_LOG(LogSWConnection, Display, TEXT("Side=HostClient OperationId=%llu Phase=PublicIP Result=Success"), RequestId);
		Self->bNeedsManualPublicIP = false;
		Self->StartServer(Address);
	});
	if (!PendingRequest->ProcessRequest()) { PendingRequest.Reset(); bNeedsManualPublicIP = true; Fail(FText::FromString(TEXT("공인 IP 확인 실패. 공인 IPv4를 직접 입력해 다시 시도하세요."))); return false; }
	return true;
}

FString USWRoomSubsystem::MarkerPath() const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RoomHost"), RoomRunId.ToString(EGuidFormats::DigitsWithHyphens) + TEXT(".ready"));
}

void USWRoomSubsystem::StartServer(const FString& Address)
{
#if PLATFORM_WINDOWS && WITH_EDITOR
	PublicAddress = Address;
	RoomRunId = FGuid::NewGuid();
	const FString Run = RoomRunId.ToString(EGuidFormats::DigitsWithHyphens);
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	const FString LogDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs"), TEXT("SWRoom"), Run);
	const FString MarkerDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RoomHost"));
	if (!Files.CreateDirectoryTree(*LogDir) || !Files.CreateDirectoryTree(*MarkerDir)) { Fail(FText::FromString(TEXT("서버 로그 폴더를 만들 수 없습니다."))); return; }
	Files.DeleteFile(*MarkerPath());
	const FString Executable = FPlatformProcess::ExecutablePath();
	const FString Project = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	if (!Executable.EndsWith(TEXT("UnrealEditor.exe"), ESearchCase::IgnoreCase) || !Files.FileExists(*Project)) { Fail(FText::FromString(TEXT("Editor 서버 실행 환경을 찾을 수 없습니다."))); return; }
	const FString Params = FString::Printf(TEXT("\"%s\" /Game/Level/Play_Test -server -unattended -NoSound -NullRHI -port=7777 -log -abslog=\"%s\" -SWRoomRunId=%s -SWRoomOwnerPid=%u"), *Project, *FPaths::Combine(LogDir, TEXT("Server.log")), *Run, FPlatformProcess::GetCurrentProcessId());
	ServerHandle = FPlatformProcess::CreateProc(*Executable, *Params, true, true, true, &ServerPid, 0, nullptr, nullptr);
	if (!ServerHandle.IsValid()) { Fail(FText::FromString(TEXT("서버를 시작할 수 없습니다."))); return; }
	bOwnsServer = true;
	ServerStartElapsed = 0;
	SetState(ESWRoomState::StartingServer, FText::FromString(TEXT("서버 준비 중...")));
	UE_LOG(LogSWConnection, Display, TEXT("Side=HostClient RoomRunId=%s OperationId=%llu Phase=ServerStart Result=Started Pid=%u Port=%d"), *Run, OperationId, ServerPid, RoomPort);
#else
	Fail(FText::FromString(TEXT("이 구성에서는 방을 만들 수 없습니다.")));
#endif
}

void USWRoomSubsystem::Tick(float DeltaTime)
{
	if (State != ESWRoomState::StartingServer) return;
	if (bAwaitingHostJoin)
	{
		if (!ServerHandle.IsValid() || !FPlatformProcess::IsProcRunning(ServerHandle)) Fail(FText::FromString(TEXT("서버가 종료되었습니다.")));
		return;
	}
	ServerStartElapsed += FMath::Max(DeltaTime, 0.0f);
	if (!ServerHandle.IsValid() || !FPlatformProcess::IsProcRunning(ServerHandle)) { Fail(FText::FromString(TEXT("서버가 준비되기 전에 종료되었습니다."))); return; }
	FString Marker;
	if (FFileHelper::LoadFileToString(Marker, *MarkerPath()))
	{
		TArray<FString> Parts;
		Marker.ParseIntoArrayLines(Parts, true);
		if (Parts.Num() == 3 && Parts[0] == RoomRunId.ToString(EGuidFormats::DigitsWithHyphens)
			&& FCString::Strtoui64(*Parts[1], nullptr, 10) == ServerPid && Parts[2] == TEXT("7777"))
		{
			if (!FSWRoomCode::Encode(PublicAddress, RoomPort, DisplayCode)) { Fail(FText::FromString(TEXT("참가 코드를 만들 수 없습니다."))); return; }
			UE_LOG(LogSWConnection, Display, TEXT("Side=HostClient RoomRunId=%s OperationId=%llu Phase=ServerReady Result=Success ElapsedMs=%d Port=%d"), *RoomRunId.ToString(), OperationId, FMath::RoundToInt(ServerStartElapsed * 1000), RoomPort);
			bAwaitingHostJoin = true;
			SetState(ESWRoomState::StartingServer, FText::FromString(TEXT("방 준비 완료. 참가 코드를 복사한 뒤 서버 접속을 누르세요.")));
			return;
		}
	}
	if (ServerStartElapsed >= 30.0f) Fail(FText::FromString(TEXT("서버 준비 시간이 초과되었습니다.")));
}

bool USWRoomSubsystem::ConnectHostedRoom()
{
	if (!bOwnsServer || !bAwaitingHostJoin || State != ESWRoomState::StartingServer) return false;
	bAwaitingHostJoin = false;
	SetState(ESWRoomState::Connecting, FText::FromString(TEXT("서버 연결 중... UDP 7777 포트 전달과 NAT loopback을 확인하세요.")));
	USWConnectionSubsystem* Connection = GetGameInstance()->GetSubsystem<USWConnectionSubsystem>();
	if (!Connection || !Connection->ConnectDirectWithName(FString::Printf(TEXT("%s:%d"), *PublicAddress, RoomPort), DisplayName))
	{
		Fail(FText::FromString(TEXT("방 생성 실패: 서버 연결을 시작할 수 없습니다.")));
		return false;
	}
	return true;
}

bool USWRoomSubsystem::JoinRoom(const FString& Name, const FString& Code)
{
	if (State != ESWRoomState::Idle && State != ESWRoomState::Failed) return false;
	bReturningToLobby = false;
	if (!FSWRoomName::Normalize(Name, DisplayName)) { Fail(FText::FromString(TEXT("이름은 1~16자, UTF-8 48바이트 이하여야 합니다."))); return false; }
	FString Address;
	uint16 Port = 0;
	if (!FSWRoomCode::Decode(Code, Address, Port))
	{
		UE_LOG(LogSWConnection, Warning, TEXT("Side=JoinClient OperationId=%llu Phase=CodeValidation Result=InvalidRoomCode"), OperationId + 1);
		Fail(FText::FromString(TEXT("잘못된 코드")));
		return false;
	}
	++OperationId;
	bOwnsServer = false;
	bAwaitingHostJoin = false;
	DisplayCode.Empty();
	UE_LOG(LogSWConnection, Display, TEXT("Side=JoinClient OperationId=%llu Phase=CodeValidation Result=Success Port=%u"), OperationId, Port);
	SetState(ESWRoomState::Connecting, FText::FromString(TEXT("서버 연결 중...")));
	USWConnectionSubsystem* Connection = GetGameInstance()->GetSubsystem<USWConnectionSubsystem>();
	if (!Connection || !Connection->ConnectDirectWithName(FString::Printf(TEXT("%s:%u"), *Address, Port), DisplayName))
	{
		Fail(FText::FromString(TEXT("기타 오류")));
		return false;
	}
	return true;
}

void USWRoomSubsystem::StopServer()
{
	if (!bOwnsServer) return;
	UE_LOG(LogSWConnection, Display, TEXT("Side=HostClient RoomRunId=%s OperationId=%llu Phase=ServerStop Result=Requested Pid=%u"), *RoomRunId.ToString(), OperationId, ServerPid);
	FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*MarkerPath());
	if (ServerHandle.IsValid())
	{
		const double Deadline = FPlatformTime::Seconds() + 6.0;
		while (FPlatformProcess::IsProcRunning(ServerHandle) && FPlatformTime::Seconds() < Deadline)
			FPlatformProcess::Sleep(0.1f);
		if (FPlatformProcess::IsProcRunning(ServerHandle)) FPlatformProcess::TerminateProc(ServerHandle, false);
		FPlatformProcess::CloseProc(ServerHandle);
	}
	UE_LOG(LogSWConnection, Display, TEXT("Side=HostClient RoomRunId=%s OperationId=%llu Phase=ServerStop Result=Completed"), *RoomRunId.ToString(), OperationId);
	bOwnsServer = false;
	bAwaitingHostJoin = false;
	ServerPid = 0;
}

void USWRoomSubsystem::CancelPendingOperation()
{
	if (State == ESWRoomState::Playing) return;
	++OperationId;
	if (PendingRequest.IsValid()) PendingRequest->CancelRequest();
	PendingRequest.Reset();
	StopServer();
	SetState(ESWRoomState::Idle, FText::GetEmpty());
	ReturnToLobby();
}

void USWRoomSubsystem::LeaveRoom()
{
	++OperationId;
	StopServer();
	SetState(ESWRoomState::Idle, FText::GetEmpty());
	if (USWConnectionSubsystem* Connection = GetGameInstance()->GetSubsystem<USWConnectionSubsystem>()) Connection->DisconnectToDefaultMap();
}

void USWRoomSubsystem::ReturnToLobby()
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World || World->GetMapName().Contains(TEXT("ConnectionLobby")) || bReturningToLobby) return;
	bReturningToLobby = true;
	if (USWConnectionSubsystem* Connection = GetGameInstance()->GetSubsystem<USWConnectionSubsystem>())
	{
		if (Connection->GetConnectionState() != ESWConnectionState::Idle)
		{
			Connection->DisconnectToDefaultMap();
			return;
		}
	}
	UGameplayStatics::OpenLevel(World, FName(LobbyMap), true);
}

void USWRoomSubsystem::HandleConnectionChanged(ESWConnectionState Previous, ESWConnectionState Current, int32 AttemptId)
{
	if (State == ESWRoomState::Connecting || State == ESWRoomState::Playing)
		UE_LOG(LogSWConnection, Display, TEXT("Side=%s OperationId=%llu AttemptId=%d Phase=Connection State=%s"), bOwnsServer ? TEXT("HostClient") : TEXT("JoinClient"), OperationId, AttemptId, *UEnum::GetValueAsString(Current));
	if (Current == ESWConnectionState::Playing && State == ESWRoomState::Connecting)
		SetState(ESWRoomState::Playing, FText::FromString(TEXT("플레이 중")));
}

void USWRoomSubsystem::HandleConnectionFailed(FSWConnectionFailure Failure)
{
	if (State != ESWRoomState::Connecting && State != ESWRoomState::Playing) return;
	FText Text = FText::FromString(TEXT("기타 오류"));
	if (Failure.Reason == ESWConnectionFailureReason::ConnectionTimeout || Failure.Reason == ESWConnectionFailureReason::ConnectionLost)
		Text = FText::FromString(TEXT("서버에 연결할 수 없음"));
	else if (Failure.Reason == ESWConnectionFailureReason::ServerFull)
		Text = FText::FromString(TEXT("서버에서 접속 거절: 인원 초과"));
	else if (Failure.Reason == ESWConnectionFailureReason::VersionMismatch)
		Text = FText::FromString(TEXT("서버에서 접속 거절: 버전 불일치"));
	else if (Failure.Reason == ESWConnectionFailureReason::NetworkFailure
		&& (Failure.EngineMessage.Contains(TEXT("InvalidReconnectToken"))
			|| Failure.EngineMessage.Contains(TEXT("DuplicateReconnectToken"))
			|| Failure.EngineMessage.Contains(TEXT("ReconnectStateUnavailable"))
			|| Failure.EngineMessage.Contains(TEXT("Rejected"), ESearchCase::IgnoreCase)))
		Text = FText::FromString(TEXT("서버에서 접속 거절"));
	if (bOwnsServer) Text = FText::Format(FText::FromString(TEXT("방 생성 실패: {0}. NAT loopback과 UDP 7777 설정을 확인하세요.")), Text);
	UE_LOG(LogSWConnection, Warning, TEXT("Side=%s OperationId=%llu AttemptId=%d Phase=Connection Result=Failed Reason=%s Type=%s"), bOwnsServer ? TEXT("HostClient") : TEXT("JoinClient"), OperationId, Failure.AttemptId, *UEnum::GetValueAsString(Failure.Reason), *Failure.EngineFailureType);
	Fail(Text);
}
