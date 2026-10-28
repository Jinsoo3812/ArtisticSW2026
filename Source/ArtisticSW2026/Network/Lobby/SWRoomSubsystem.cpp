#include "Network/Lobby/SWRoomSubsystem.h"

#include "Network/Lobby/SWRoomCode.h"
#include "Network/SWConnectionSubsystem.h"
#include "Network/SWNetworkLog.h"
#include "Network/SWRoomLoadDiagnostics.h"
#include "Misc/CommandLine.h"
#include "Misc/NetworkVersion.h"
#include "Misc/Parse.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "SWRoomName.h"
#include "Room/SWRoomSaveGame.h"
#include "Room/SWRoomSaveStore.h"
#include "Room/SWRoomRuntimePaths.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Kismet/GameplayStatics.h"
#include "GameMapsSettings.h"

namespace
{
constexpr uint16 RoomPort = 7777;
constexpr float ServerStartTimeoutSeconds = 90.0f;
const TCHAR* LobbyMap = TEXT("/Game/Level/ConnectionLobby");
TAutoConsoleVariable<int32> CVarRoomServerBackend(TEXT("sw.Room.ServerBackend"), 0,
	TEXT("Local room server: 0=UnrealEditor, 1=packaged dedicated server. Applies to the next hosting request."));
FString GRoomServerExeOverride;
FAutoConsoleVariableRef CVarRoomServerExe(TEXT("sw.Room.ServerExe"), GRoomServerExeOverride,
	TEXT("Packaged server internal executable path. Empty uses SWRoomServerExe or SWLocalPackagedServer settings."));

FString GetPackagedServerProjectDirectory(const FString& Executable)
{
	return FPaths::GetPath(FPaths::GetPath(FPaths::GetPath(Executable)));
}

bool IsLocalRoomTestEnabled()
{
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
	return false;
#else
	return FParse::Param(FCommandLine::Get(), TEXT("SWRoomLocalTest"));
#endif
}

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
// Disabled: use the normal room UI and sw.Room.ServerBackend for development tests.
/*
FAutoConsoleCommandWithWorldAndArgs LocalHostCommand(
	TEXT("SW.DevTest.Local.Host"), TEXT("Create a local room and auto-join: SW.DevTest.Local.Host [Name]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || !World->IsGameWorld() || !World->GetGameInstance() || Args.Num() > 1) return;
		if (USWRoomSubsystem* Room = World->GetGameInstance()->GetSubsystem<USWRoomSubsystem>())
			if (!Room->HostLocalTestRoom(Args.IsEmpty() ? TEXT("Host") : Args[0]))
				UE_LOG(LogSWConnection, Warning, TEXT("Local.Host failed. Check room state, name and server paths."));
	}));
FAutoConsoleCommandWithWorldAndArgs LocalJoinCommand(
	TEXT("SW.DevTest.Local.Join"), TEXT("Join a local room: SW.DevTest.Local.Join [Name] [IPv4; default 127.0.0.1]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || !World->IsGameWorld() || !World->GetGameInstance() || Args.Num() > 2) return;
		if (USWRoomSubsystem* Room = World->GetGameInstance()->GetSubsystem<USWRoomSubsystem>())
			if (!Room->JoinLocalTestRoom(Args.IsEmpty() ? TEXT("Guest") : Args[0], Args.Num() < 2 ? TEXT("127.0.0.1") : Args[1]))
				UE_LOG(LogSWConnection, Warning, TEXT("Local.Join failed. Use an idle client, valid name and private/loopback IPv4."));
	}));
*/
#endif

bool NormalizeLocalFilePath(FString& Path)
{
	Path.TrimStartAndEndInline();
	if (Path.Len() < 3 || !FChar::IsAlpha(Path[0]) || Path[1] != ':'
		|| (Path[2] != '/' && Path[2] != '\\') || FPaths::IsRelative(Path)
		|| Path.Contains(TEXT("\"")) || Path.Contains(TEXT("\r")) || Path.Contains(TEXT("\n"))) return false;
	FPaths::NormalizeFilename(Path);
	return FPaths::CollapseRelativeDirectories(Path);
}

bool ResolveLocalEditorServer(FString& OutExecutable, FString& OutProject, FString& OutError)
{
	OutExecutable.Reset();
	OutProject.Reset();
	OutError.Reset();
#if PLATFORM_WINDOWS
	auto Resolve = [](const TCHAR* Argument, const TCHAR* Key, FString& OutPath)
	{
		const bool bParsed = FParse::Value(FCommandLine::Get(), Argument, OutPath);
		if (bParsed || FString(FCommandLine::Get()).Contains(FString(TEXT("-")) + Argument, ESearchCase::IgnoreCase)) return;
		if (GConfig) GConfig->GetString(TEXT("SWLocalEditorServer"), Key, OutPath, GGameIni);
#if WITH_EDITOR
		if (OutPath.TrimStartAndEnd().IsEmpty())
			OutPath = FCString::Stricmp(Key, TEXT("EditorExecutable")) == 0
				? FString(FPlatformProcess::ExecutablePath()) : FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
#endif
	};
	Resolve(TEXT("SWRoomEditorExe="), TEXT("EditorExecutable"), OutExecutable);
	Resolve(TEXT("SWRoomProject="), TEXT("ProjectFile"), OutProject);
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	if (!NormalizeLocalFilePath(OutExecutable)
		|| !FPaths::GetCleanFilename(OutExecutable).Equals(TEXT("UnrealEditor.exe"), ESearchCase::IgnoreCase)
		|| !Files.FileExists(*OutExecutable))
	{
		OutError = TEXT("UnrealEditor.exe 경로를 확인하세요. SWRoomEditorExe 또는 SWLocalEditorServer.EditorExecutable 설정이 필요합니다.");
		return false;
	}
	if (!NormalizeLocalFilePath(OutProject)
		|| !FPaths::GetExtension(OutProject).Equals(TEXT("uproject"), ESearchCase::IgnoreCase)
		|| !Files.FileExists(*OutProject))
	{
		OutError = TEXT("원본 .uproject 경로를 확인하세요. SWRoomProject 또는 SWLocalEditorServer.ProjectFile 설정이 필요합니다.");
		return false;
	}
	return true;
#else
	OutError = TEXT("로컬 에디터 서버 실행은 Windows에서만 지원됩니다.");
	return false;
#endif
}

bool ResolveLocalServer(FString& OutExecutable, FString& OutProject, bool& bOutPackaged, FString& OutError)
{
	OutExecutable.Reset();
	OutProject.Reset();
	OutError.Reset();
	bOutPackaged = false;
	const int32 Backend = CVarRoomServerBackend.GetValueOnGameThread();
	if (Backend == 0) return ResolveLocalEditorServer(OutExecutable, OutProject, OutError);
	if (Backend != 1)
	{
		OutError = TEXT("서버 실행 방식은 0(에디터) 또는 1(패키징 서버)을 선택하세요.");
		return false;
	}
#if PLATFORM_WINDOWS
	bOutPackaged = true;
	if (!GRoomServerExeOverride.IsEmpty())
	{
		OutExecutable = GRoomServerExeOverride.TrimStartAndEnd();
		if (OutExecutable.Len() >= 2 && OutExecutable.StartsWith(TEXT("\"")) && OutExecutable.EndsWith(TEXT("\"")))
			OutExecutable = OutExecutable.Mid(1, OutExecutable.Len() - 2);
	}
	else
	{
		const bool bExplicit = FParse::Value(FCommandLine::Get(), TEXT("SWRoomServerExe="), OutExecutable)
			|| FString(FCommandLine::Get()).Contains(TEXT("-SWRoomServerExe="), ESearchCase::IgnoreCase);
		if (!bExplicit && GConfig)
			GConfig->GetString(TEXT("SWLocalPackagedServer"), TEXT("ServerExecutable"), OutExecutable, GGameIni);
	}
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	if (!NormalizeLocalFilePath(OutExecutable)
		|| !FPaths::GetCleanFilename(OutExecutable).Equals(TEXT("ArtisticSW2026Server.exe"), ESearchCase::IgnoreCase)
		|| !FPaths::GetCleanFilename(FPaths::GetPath(OutExecutable)).Equals(TEXT("Win64"), ESearchCase::IgnoreCase)
		|| !FPaths::GetCleanFilename(FPaths::GetPath(FPaths::GetPath(OutExecutable))).Equals(TEXT("Binaries"), ESearchCase::IgnoreCase)
		|| !Files.FileExists(*OutExecutable)
		|| !Files.DirectoryExists(*FPaths::Combine(GetPackagedServerProjectDirectory(OutExecutable), TEXT("Content"), TEXT("Paks"))))
	{
		OutError = TEXT("패키징 서버의 내부 Binaries/Win64/ArtisticSW2026Server.exe와 Content/Paks 폴더를 확인하세요.");
		return false;
	}
	return true;
#else
	OutError = TEXT("로컬 패키징 서버 실행은 Windows에서만 지원됩니다.");
	return false;
#endif
}

void RefreshServerLaunchSettings()
{
	static int32 PreviousBackend = 0;
	static FString PreviousExecutable;
	const int32 Backend = CVarRoomServerBackend.GetValueOnGameThread();
	if (Backend == PreviousBackend && GRoomServerExeOverride == PreviousExecutable) return;
	PreviousBackend = Backend;
	PreviousExecutable = GRoomServerExeOverride;
	if (!GEngine) return;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (UGameInstance* Instance = Context.OwningGameInstance.Get())
			if (USWRoomSubsystem* Room = Instance->GetSubsystem<USWRoomSubsystem>())
				Room->OnRoomChanged.Broadcast(Room->GetRoomState(), Room->GetRoomMessage());
	}
}

FAutoConsoleVariableSink RoomServerSettingsSink(FConsoleCommandDelegate::CreateStatic(&RefreshServerLaunchSettings));
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
	FString Executable, Project, Root, Error;
	bool bPackaged = false;
	return ResolveLocalServer(Executable, Project, bPackaged, Error) && FSWRoomRuntimePaths::TryResolveRoot(Root, Error);
}

bool USWRoomSubsystem::HasSavedRoom() const
{
	return FSWRoomSaveStore::HasValidCurrentRoom() || FSWRoomSaveStore::HasLegacyRoomFile();
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
	UE_LOG(LogSWRoom, Error, TEXT("Flow=RoomSetup OperationId=%llu Mode=%s RoomId=%s Result=Failed Reason=%s"),
		OperationId, bContinuingRoom ? TEXT("Continue") : TEXT("New"), *SavedRoomId.ToString(), *FailureMessage.ToString());
	++OperationId;
	if (PendingRequest.IsValid()) PendingRequest->CancelRequest();
	PendingRequest.Reset();
	StopServer();
	if (!bContinuingRoom) FSWRoomSaveStore::DiscardStagedNewRoom();
	SetState(ESWRoomState::Failed, FailureMessage);
	ReturnToLobby();
}

bool USWRoomSubsystem::CreateRoom(const FString& Name, const FString& OptionalPublicIPv4)
{
	return BeginHosting(Name, OptionalPublicIPv4, false);
}

bool USWRoomSubsystem::ContinueRoom(const FString& Name, const FString& OptionalPublicIPv4)
{
	return BeginHosting(Name, OptionalPublicIPv4, true);
}

bool USWRoomSubsystem::BeginHosting(const FString& Name, const FString& OptionalPublicIPv4, bool bContinue, bool bLocalTest)
{
	SWRoomLoadDiagnostics::Mark(TEXT("Host.Requested"));
	if (State != ESWRoomState::Idle && State != ESWRoomState::Failed) return false;
	bLocalCommandHost = bLocalTest;
	UE_LOG(LogSWRoom, Display, TEXT("Flow=RoomSetup OperationId=%llu Mode=%s Phase=Requested"), OperationId + 1,
		bContinue ? TEXT("Continue") : TEXT("New"));
	bReturningToLobby = false;
	FString Executable, Project, Root, Error;
	bool bPackaged = false;
	if (!ResolveLocalServer(Executable, Project, bPackaged, Error) || !FSWRoomRuntimePaths::TryResolveRoot(Root, Error))
	{
		Fail(FText::FromString(Error));
		return false;
	}
	PendingServerExecutable = Executable;
	PendingServerProject = Project;
	bPendingPackagedServer = bPackaged;
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	if (!Files.CreateDirectoryTree(*FSWRoomRuntimePaths::GetSaveDirectory())
		|| !Files.CreateDirectoryTree(*FSWRoomRuntimePaths::GetHostDirectory()))
	{
		Fail(FText::FromString(TEXT("방 저장 폴더를 만들 수 없습니다.")));
		return false;
	}
	bContinuingRoom = bContinue;
	bUsingBackup = false;
	if (bContinue)
	{
		USWRoomSaveGame* Saved = FSWRoomSaveStore::LoadCurrentRoom(this);
		if (!Saved)
		{
			Fail(FText::FromString(FSWRoomSaveStore::HasLegacyRoomFile()
				? TEXT("이전 방 저장 형식(v3)은 이 빌드에서 지원되지 않음")
				: TEXT("유효한 방 저장이 없습니다.")));
			return false;
		}
		SavedRoomId = Saved->RoomId;
		DisplayName = Saved->HostDisplayName;
		bUsingBackup = Saved->bRecoveredFromBackup;
	}
	else
	{
		if (!FSWRoomName::Normalize(Name, DisplayName)) { Fail(FText::FromString(TEXT("이름은 1~16자, UTF-8 48바이트 이하여야 합니다."))); return false; }
		SavedRoomId = FGuid::NewGuid();
		USWRoomSaveGame* Staged = NewObject<USWRoomSaveGame>(this);
		Staged->RoomId = SavedRoomId;
		Staged->HostDisplayName = DisplayName;
		Staged->ContentContractVersion = USWRoomSaveGame::CurrentContentContractVersion;
		Staged->MapPath = FSoftObjectPath(UGameMapsSettings::GetGameDefaultMap(EDefaultMapRequestType::Server));
		if (Staged->MapPath.IsNull() || Staged->MapPath.ToString().Contains(TEXT("ConnectionLobby")))
		{
			Fail(FText::FromString(TEXT("서버 기본 맵이 유효하지 않습니다.")));
			return false;
		}
		if (!FSWRoomSaveStore::StageNewRoom(Staged)) { Fail(FText::FromString(TEXT("새 방 저장 준비에 실패했습니다."))); return false; }
	}
	HostKey = FGuid::NewGuid();
	++OperationId;
	UE_LOG(LogSWRoom, Display, TEXT("Flow=RoomSetup OperationId=%llu Mode=%s RoomId=%s Phase=SaveSelected Backup=%d"),
		OperationId, bContinue ? TEXT("Continue") : TEXT("New"), *SavedRoomId.ToString(), bUsingBackup ? 1 : 0);
	DisplayCode.Empty();
	PublicAddress.Empty();
	bAwaitingHostJoin = false;
	bOwnsServer = false;
	if (bLocalCommandHost)
	{
		bNeedsManualPublicIP = false;
		StartServer(TEXT("127.0.0.1"));
		return State != ESWRoomState::Failed;
	}
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
	const FString Directory = FSWRoomRuntimePaths::GetHostDirectory();
	return Directory.IsEmpty() ? FString() : FPaths::Combine(Directory, RoomRunId.ToString(EGuidFormats::DigitsWithHyphens) + TEXT(".ready"));
}

void USWRoomSubsystem::StartServer(const FString& Address)
{
	SWRoomLoadDiagnostics::Mark(TEXT("Host.PublicIPResolved"));
#if PLATFORM_WINDOWS
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	if (PendingServerExecutable.IsEmpty() || !Files.FileExists(*PendingServerExecutable)
		|| (bPendingPackagedServer
			? !Files.DirectoryExists(*FPaths::Combine(GetPackagedServerProjectDirectory(PendingServerExecutable), TEXT("Content"), TEXT("Paks")))
			: !Files.FileExists(*PendingServerProject)))
	{
		Fail(FText::FromString(TEXT("선택한 서버 실행 파일 또는 콘텐츠를 확인할 수 없습니다.")));
		return;
	}
	FString Root, Error;
	if (!FSWRoomRuntimePaths::TryResolveRoot(Root, Error))
	{
		Fail(FText::FromString(Error));
		return;
	}
	PublicAddress = Address;
	RoomRunId = FGuid::NewGuid();
	const FString Run = RoomRunId.ToString(EGuidFormats::DigitsWithHyphens);
	const FString LogDir = FPaths::Combine(Root, TEXT("Logs"), TEXT("SWRoom"), Run);
	const FString MarkerDir = FSWRoomRuntimePaths::GetHostDirectory();
	if (!Files.CreateDirectoryTree(*LogDir) || !Files.CreateDirectoryTree(*MarkerDir)) { Fail(FText::FromString(TEXT("서버 로그 폴더를 만들 수 없습니다."))); return; }
	Files.DeleteFile(*MarkerPath());
	const USWRoomSaveGame* Saved = bContinuingRoom ? FSWRoomSaveStore::LoadCurrentRoom(this) : FSWRoomSaveStore::LoadStagedNewRoom(this);
	if (!Saved || Saved->RoomId != SavedRoomId || Saved->MapPath.IsNull()) { Fail(FText::FromString(TEXT("방 맵을 확인할 수 없습니다."))); return; }
	const FString MapName = Saved->MapPath.GetLongPackageName();
	bool bInvalidMap = !FPackageName::IsValidLongPackageName(MapName) || MapName.Contains(TEXT("\""));
	for (TCHAR Character : MapName) bInvalidMap |= FChar::IsWhitespace(Character);
	if (bInvalidMap) { Fail(FText::FromString(TEXT("방 맵 경로가 올바르지 않습니다."))); return; }
	const FString LaunchPrefix = bPendingPackagedServer ? MapName
		: FString::Printf(TEXT("\"%s\" %s -server"), *PendingServerProject, *MapName);
	const FString Params = FString::Printf(TEXT("%s -unattended -NoSound -NullRHI -port=7777 -log -abslog=\"%s\" -SWRoomRunId=%s -SWRoomOwnerPid=%u -SWRoomMode=%s -SWRoomId=%s -SWRoomHostKey=%s"), *LaunchPrefix, *FPaths::Combine(LogDir, TEXT("Server.log")), *Run, FPlatformProcess::GetCurrentProcessId(), bContinuingRoom ? TEXT("Continue") : TEXT("New"), *SavedRoomId.ToString(EGuidFormats::DigitsWithHyphens), *HostKey.ToString(EGuidFormats::DigitsWithHyphens));
	FString DiagnosticParams = SWRoomLoadDiagnostics::IsEnabled() ? Params + TEXT(" -SWRoomLoadDiag") : Params;
	if (bPendingPackagedServer)
		DiagnosticParams += FString::Printf(TEXT(" -networkversionoverride=%u"), FNetworkVersion::GetNetworkCompatibleChangelist());
	DiagnosticParams += FString::Printf(TEXT(" -SWRoomRoot=\"%s\""), *Root);
	if (SWRoomLogging::IsDetailedEnabled()) DiagnosticParams += TEXT(" -SWRoomDetailedLog");
	if (FParse::Param(FCommandLine::Get(), TEXT("SWRoomSaveAudit"))) DiagnosticParams += TEXT(" -SWRoomSaveAudit");
	if (SWRoomLoadDiagnostics::IsEnabled() && FParse::Param(FCommandLine::Get(), TEXT("SWRoomLoadTrace")))
		DiagnosticParams += FString::Printf(TEXT(" -trace=cpu,loadtime,file,bookmark,frame -statnamedevents -tracefile=\"%s\""), *FPaths::Combine(LogDir, TEXT("Server.utrace")));
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	const TCHAR* DiagnosticSwitches[] = { TEXT("EnemyNetProfile"), TEXT("EnemyShipTorpedoBuoyancyTest"),
		TEXT("EnemyShipObstacleBuoyancyTest"), TEXT("EnemyShipTimeStopTest") };
	for (const TCHAR* Switch : DiagnosticSwitches)
		if (FParse::Param(FCommandLine::Get(), Switch)) DiagnosticParams += FString::Printf(TEXT(" -%s"), Switch);
	if (FParse::Param(FCommandLine::Get(), TEXT("EnemyNetProfile")))
	{
		int32 Count, Seed;
		float SpawnDelay;
		FString Mode;
		if (FParse::Value(FCommandLine::Get(), TEXT("EnemyNetProfileCount="), Count))
			DiagnosticParams += FString::Printf(TEXT(" -EnemyNetProfileCount=%d"), FMath::Clamp(Count, 0, 128));
		if (FParse::Value(FCommandLine::Get(), TEXT("EnemyNetProfileSeed="), Seed))
			DiagnosticParams += FString::Printf(TEXT(" -EnemyNetProfileSeed=%d"), Seed);
		if (FParse::Value(FCommandLine::Get(), TEXT("EnemyNetProfileSpawnDelay="), SpawnDelay) && FMath::IsFinite(SpawnDelay))
			DiagnosticParams += FString::Printf(TEXT(" -EnemyNetProfileSpawnDelay=%.3f"), FMath::Clamp(SpawnDelay, 0.0f, 60.0f));
		if (FParse::Value(FCommandLine::Get(), TEXT("EnemyNetProfileMode="), Mode))
		{
			if (Mode.Equals(TEXT("Idle"), ESearchCase::IgnoreCase)) DiagnosticParams += TEXT(" -EnemyNetProfileMode=Idle");
			else if (Mode.Equals(TEXT("Combat"), ESearchCase::IgnoreCase)) DiagnosticParams += TEXT(" -EnemyNetProfileMode=Combat");
		}
	}
#endif
	SWRoomLoadDiagnostics::Mark(TEXT("Host.CreateProc"));
	const FString WorkingDirectory = bPendingPackagedServer ? FPaths::GetPath(PendingServerExecutable) : FString();
	ServerHandle = FPlatformProcess::CreateProc(*PendingServerExecutable, *DiagnosticParams, true, true, true, &ServerPid, 0,
		bPendingPackagedServer ? *WorkingDirectory : nullptr, nullptr);
	if (!ServerHandle.IsValid()) { Fail(FText::FromString(TEXT("서버를 시작할 수 없습니다."))); return; }
	bOwnsServer = true;
	ServerStartElapsed = 0;
	SetState(ESWRoomState::StartingServer, FText::FromString(TEXT("서버 준비 중...")));
	UE_LOG(LogSWConnection, Display, TEXT("Side=HostClient RoomRunId=%s OperationId=%llu Phase=ServerStart Result=Started Backend=%s Executable=%s Pid=%u Port=%d Root=%s"), *Run, OperationId,
		bPendingPackagedServer ? TEXT("Packaged") : TEXT("Editor"), *PendingServerExecutable, ServerPid, RoomPort, *Root);
#else
	Fail(FText::FromString(TEXT("로컬 서버 실행은 Windows에서만 지원됩니다.")));
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
	FString StartupFailure;
	if (FFileHelper::LoadFileToString(StartupFailure, *(MarkerPath() + TEXT(".failed"))))
	{
		Fail(FText::FromString(StartupFailure));
		return;
	}
	if (!ServerHandle.IsValid() || !FPlatformProcess::IsProcRunning(ServerHandle)) { Fail(FText::FromString(TEXT("서버가 준비되기 전에 종료되었습니다."))); return; }
	FString Marker;
	if (FFileHelper::LoadFileToString(Marker, *MarkerPath()))
	{
		TArray<FString> Parts;
		Marker.ParseIntoArrayLines(Parts, true);
		if ((Parts.Num() == 3 || (Parts.Num() == 4 && Parts[3] == TEXT("WorldRecovered")))
			&& Parts[0] == RoomRunId.ToString(EGuidFormats::DigitsWithHyphens)
			&& FCString::Strtoui64(*Parts[1], nullptr, 10) == ServerPid && Parts[2] == TEXT("7777"))
		{
			if (!bLocalCommandHost && !FSWRoomCode::Encode(PublicAddress, RoomPort, DisplayCode)) { Fail(FText::FromString(TEXT("참가 코드를 만들 수 없습니다."))); return; }
			UE_LOG(LogSWConnection, Display, TEXT("Side=HostClient RoomRunId=%s OperationId=%llu Phase=ServerReady Result=Success ElapsedMs=%d Port=%d"), *RoomRunId.ToString(), OperationId, FMath::RoundToInt(ServerStartElapsed * 1000), RoomPort);
			bAwaitingHostJoin = true;
			if (bLocalCommandHost) { ConnectHostedRoom(); return; }
			SWRoomLoadDiagnostics::Mark(TEXT("Host.ReadyMarkerReceived"));
			SetState(ESWRoomState::StartingServer, FText::FromString(Parts.Num() == 4
				? TEXT("월드 복원에 실패하여 초기화했습니다. 진행 데이터는 유지됩니다. 서버 접속을 누르세요.") : bUsingBackup
				? TEXT("복구본 사용. 방 준비 완료. 참가 코드를 복사한 뒤 서버 접속을 누르세요.")
				: TEXT("방 준비 완료. 참가 코드를 복사한 뒤 서버 접속을 누르세요.")));
			return;
		}
	}
	if (ServerStartElapsed >= ServerStartTimeoutSeconds) Fail(FText::FromString(TEXT("서버 준비 시간이 초과되었습니다.")));
}

bool USWRoomSubsystem::HostLocalTestRoom(const FString& Name)
{
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
	return false;
#else
	return BeginHosting(Name, FString(), false, true);
#endif
}

bool USWRoomSubsystem::JoinLocalTestRoom(const FString& Name, const FString& Address)
{
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
	return false;
#else
	if (State != ESWRoomState::Idle && State != ESWRoomState::Failed) return false;
	TArray<FString> Parts;
	Address.ParseIntoArray(Parts, TEXT("."), false);
	if (Parts.Num() != 4) return false;
	int32 Octets[4];
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (Parts[Index].IsEmpty() || Parts[Index].Len() > 3) return false;
		for (TCHAR Ch : Parts[Index]) if (Ch < '0' || Ch > '9') return false;
		Octets[Index] = FCString::Atoi(*Parts[Index]);
		if (Octets[Index] > 255) return false;
	}
	if (!(Octets[0] == 127 || Octets[0] == 10 || (Octets[0] == 192 && Octets[1] == 168)
		|| (Octets[0] == 172 && Octets[1] >= 16 && Octets[1] <= 31))) return false;
	if (!FSWRoomName::Normalize(Name, DisplayName)) return false;
	++OperationId;
	bReturningToLobby = false;
	bOwnsServer = false;
	bAwaitingHostJoin = false;
	bLocalCommandHost = false;
	DisplayCode.Empty();
	SetState(ESWRoomState::Connecting, FText::FromString(TEXT("Local server connecting...")));
	UE_LOG(LogSWConnection, Display, TEXT("Side=JoinClient Phase=LocalCommand Address=%s Port=%d"), *Address, RoomPort);
	USWConnectionSubsystem* Connection = GetGameInstance()->GetSubsystem<USWConnectionSubsystem>();
	if (!Connection || !Connection->ConnectDirectWithName(FString::Printf(TEXT("%s:%d"), *Address, RoomPort), DisplayName))
	{
		Fail(FText::FromString(TEXT("Local server connection could not start.")));
		return false;
	}
	return true;
#endif
}

bool USWRoomSubsystem::ConnectHostedRoom()
{
	if (!bOwnsServer || !bAwaitingHostJoin || State != ESWRoomState::StartingServer) return false;
	UE_LOG(LogSWRoom, Display, TEXT("Flow=HostJoin OperationId=%llu RoomRunId=%s Phase=Requested"), OperationId, *RoomRunId.ToString());
	bAwaitingHostJoin = false;
	SetState(ESWRoomState::Connecting, FText::FromString(TEXT("서버 연결 중...")));
	const FString ConnectAddress = IsLocalRoomTestEnabled() ? TEXT("127.0.0.1") : PublicAddress;
	UE_LOG(LogSWConnection, Display, TEXT("Side=HostClient Phase=ConnectTarget Address=%s Port=%d LocalTest=%d"), *ConnectAddress, RoomPort, IsLocalRoomTestEnabled());
	USWConnectionSubsystem* Connection = GetGameInstance()->GetSubsystem<USWConnectionSubsystem>();
	if (!Connection || !Connection->ConnectDirectWithName(FString::Printf(TEXT("%s:%d"), *ConnectAddress, RoomPort), DisplayName, HostKey))
	{
		Fail(FText::FromString(TEXT("방 생성 실패: 서버 연결을 시작할 수 없습니다.")));
		return false;
	}
	return true;
}

bool USWRoomSubsystem::JoinRoom(const FString& Name, const FString& Code)
{
	if (State != ESWRoomState::Idle && State != ESWRoomState::Failed) return false;
	UE_LOG(LogSWRoom, Display, TEXT("Flow=GuestJoin OperationId=%llu Phase=Requested"), OperationId + 1);
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
	if (!FSWRoomCode::Encode(Address, Port, DisplayCode))
	{
		Fail(FText::FromString(TEXT("참가 코드를 표시할 수 없습니다.")));
		return false;
	}
	++OperationId;
	bOwnsServer = false;
	bAwaitingHostJoin = false;
	UE_LOG(LogSWConnection, Display, TEXT("Side=JoinClient OperationId=%llu Phase=CodeValidation Result=Success Port=%u"), OperationId, Port);
	UE_LOG(LogSWRoom, Display, TEXT("Flow=GuestJoin OperationId=%llu Phase=CodeValidated Port=%u"), OperationId, Port);
	SetState(ESWRoomState::Connecting, FText::FromString(TEXT("서버 연결 중...")));
	const FString ConnectAddress = IsLocalRoomTestEnabled() ? TEXT("127.0.0.1") : Address;
	UE_LOG(LogSWConnection, Display, TEXT("Side=JoinClient Phase=ConnectTarget Address=%s Port=%u LocalTest=%d"), *ConnectAddress, Port, IsLocalRoomTestEnabled());
	USWConnectionSubsystem* Connection = GetGameInstance()->GetSubsystem<USWConnectionSubsystem>();
	if (!Connection || !Connection->ConnectDirectWithName(FString::Printf(TEXT("%s:%u"), *ConnectAddress, Port), DisplayName))
	{
		Fail(FText::FromString(TEXT("기타 오류")));
		return false;
	}
	return true;
}

void USWRoomSubsystem::StopServer()
{
	PendingServerExecutable.Reset();
	PendingServerProject.Reset();
	bPendingPackagedServer = false;
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
	if (!bContinuingRoom) FSWRoomSaveStore::DiscardStagedNewRoom();
	SetState(ESWRoomState::Idle, FText::GetEmpty());
	ReturnToLobby();
}

void USWRoomSubsystem::LeaveRoom()
{
	UE_LOG(LogSWRoom, Display, TEXT("Flow=LeaveRoom OperationId=%llu RoomRunId=%s Phase=Requested"), OperationId + 1, *RoomRunId.ToString());
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
	{
		if (bOwnsServer && !bContinuingRoom)
		{
			const USWRoomSaveGame* Saved = FSWRoomSaveStore::LoadCurrentRoom(this);
			if (!Saved || Saved->RoomId != SavedRoomId || Saved->SaveKind != ESWRoomSaveKind::New)
			{
				Fail(FText::FromString(TEXT("새 방 확정 저장에 실패했습니다.")));
				return;
			}
		}
		SetState(ESWRoomState::Playing, FText::FromString(TEXT("플레이 중")));
		UE_LOG(LogSWRoom, Display, TEXT("Flow=%s OperationId=%llu RoomRunId=%s Result=Playing"),
			bOwnsServer ? (bContinuingRoom ? TEXT("Continue") : TEXT("New")) : TEXT("GuestJoin"),
			OperationId, *RoomRunId.ToString());
	}
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
		&& Failure.EngineMessage.Contains(TEXT("Rejected"), ESearchCase::IgnoreCase))
		Text = FText::FromString(TEXT("서버에서 접속 거절"));
	if (bOwnsServer) Text = FText::Format(FText::FromString(TEXT("방 생성 실패: {0}")), Text);
	UE_LOG(LogSWConnection, Warning, TEXT("Side=%s OperationId=%llu AttemptId=%d Phase=Connection Result=Failed Reason=%s Type=%s"), bOwnsServer ? TEXT("HostClient") : TEXT("JoinClient"), OperationId, Failure.AttemptId, *UEnum::GetValueAsString(Failure.Reason), *Failure.EngineFailureType);
	Fail(Text);
}
