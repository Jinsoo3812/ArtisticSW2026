// Fill out your copyright notice in the Description page of Project Settings.


#include "MultiGameMode.h"
#include "Respawn/SWRespawnControllerInterface.h"

#include "Network/SWNetworkLog.h"
#include "EngineUtils.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/WorldSettings.h"
#include "PlayerRespawnPointComponent.h"
#include "PlayerRespawnPoint.h"
#include "PlayerProgressSubsystem.h"
#include "RespawnHostInterface.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "SWRoomName.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Engine/NetDriver.h"
#include "IPAddress.h"
#include "Room/SWRoomProgressSubsystem.h"
#include "Room/SWRoomSaveGame.h"
#include "Room/SWLevelEntryPoint.h"
#include "Room/SWRoomReadyState.h"
#include "Containers/Ticker.h"

namespace
{
const TCHAR* GetNetModeName(const ENetMode NetMode)
{
    switch (NetMode)
    {
    case NM_Standalone:
        return TEXT("Standalone");
    case NM_DedicatedServer:
        return TEXT("DedicatedServer");
    case NM_ListenServer:
        return TEXT("ListenServer");
    case NM_Client:
        return TEXT("Client");
    default:
        return TEXT("Unknown");
    }
}
}

AMultiGameMode::AMultiGameMode()
{
    RequiredPlayerCount = 2;
    MaxPlayerCount = 2;
    bRequireAllPlayersReady = true;
    bAutoReadyOnPostLogin = false;
}

void AMultiGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
    Super::InitGame(MapName, Options, ErrorMessage);

    UE_LOG(
        LogSWConnection,
        Display,
        TEXT("InitGame Map=%s NetMode=%s RequiredPlayers=%d MaxPlayers=%d"),
        *MapName,
        GetNetModeName(GetNetMode()),
        RequiredPlayerCount,
        MaxPlayerCount
    );
}

void AMultiGameMode::StartPlay()
{
    Super::StartPlay();
	if (IsHostedRoom())
	{
		RoomReadyState = GetWorld()->SpawnActor<ASWRoomReadyState>();
		if (RoomReadyState)
			if (USWRoomProgressSubsystem* Room = GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>())
				RoomReadyState->RestoreGeneration = Room->AdvanceRestoreGeneration();
		int32 EntryCounts[3] = {0, 0, 0};
		for (TActorIterator<ASWLevelEntryPoint> It(GetWorld()); It; ++It)
			++EntryCounts[static_cast<int32>(It->EntryRole)];
		if (EntryCounts[0] != 1 || EntryCounts[1] != 1 || EntryCounts[2] != 1)
		{
			UE_LOG(LogSWConnection, Error, TEXT("Hosted room requires exactly one Host, Guest, and Ship entry point"));
			FPlatformMisc::RequestExit(false);
			return;
		}
		UpdateHostedRoomPause();
		if (USWRoomProgressSubsystem* Room = GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>(); Room && Room->HasStartupError())
		{
			UE_LOG(LogSWConnection, Error, TEXT("Hosted room save failed validation during startup"));
			FPlatformMisc::RequestExit(false);
			return;
		}
	}
	FString RunIdText;
	FString OwnerText;
	if (GetNetMode() == NM_DedicatedServer && FParse::Value(FCommandLine::Get(), TEXT("SWRoomRunId="), RunIdText)
		&& FParse::Value(FCommandLine::Get(), TEXT("SWRoomOwnerPid="), OwnerText)
		&& FGuid::Parse(RunIdText, RoomRunId))
	{
		if (RoomReadyState) RoomReadyState->RoomRunId = RoomRunId;
		RoomOwnerPid = static_cast<uint32>(FCString::Strtoui64(*OwnerText, nullptr, 10));
		UNetDriver* Driver = GetWorld() ? GetWorld()->GetNetDriver() : nullptr;
		const TSharedPtr<const FInternetAddr> LocalAddress = Driver ? Driver->GetLocalAddr() : nullptr;
		const int32 ListenPort = LocalAddress.IsValid() ? LocalAddress->GetPort() : 0;
		if (RoomOwnerPid && ListenPort == 7777)
		{
			const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RoomHost"));
			RoomReadyPath = FPaths::Combine(Directory, RoomRunId.ToString(EGuidFormats::DigitsWithHyphens) + TEXT(".ready"));
			IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
			if (Files.CreateDirectoryTree(*Directory))
				RoomOwnerTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &AMultiGameMode::TickRoomOwner), 0.5f);
		}
	}

    UE_LOG(
        LogSWConnection,
        Display,
        TEXT("StartPlay World=%s NetMode=%s PlayerCount=%d"),
        *GetNameSafe(GetWorld()),
        GetNetModeName(GetNetMode()),
        GetNumPlayers()
    );
}

void AMultiGameMode::SetHostedRoomWorldReady()
{
	if (IsHostedRoom() && RoomReadyState)
	{
		RoomReadyState->bWorldReady = true;
		RoomReadyState->ForceNetUpdate();
	}
}

bool AMultiGameMode::IsHostedRoom() const
{
	const USWRoomProgressSubsystem* Room = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>() : nullptr;
	return Room && Room->IsHostedRoom() && GetNetMode() == NM_DedicatedServer;
}

void AMultiGameMode::UpdateHostedRoomPause()
{
	if (!IsHostedRoom() || !GetWorld()) return;
	AWorldSettings* Settings = GetWorld()->GetWorldSettings();
	if (!Settings) return;
	if (GetConnectedPlayerCount() == 0)
	{
		if (!PauseSentinel)
		{
			PauseSentinel = GetWorld()->SpawnActor<APlayerState>();
			if (PauseSentinel)
			{
				PauseSentinel->SetReplicates(false);
				if (AGameStateBase* State = GetGameState<AGameStateBase>()) State->RemovePlayerState(PauseSentinel);
			}
		}
		if (PauseSentinel) Settings->SetPauserPlayerState(PauseSentinel);
	}
	else Settings->SetPauserPlayerState(nullptr);
}

void AMultiGameMode::MarkHostedRoomWorldReady()
{
	if (!IsHostedRoom() || bHostedWorldReady || RoomReadyPath.IsEmpty()) return;
	const USWRoomProgressSubsystem* Room = GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>();
	if (!Room || Room->HasStartupError()) return;
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	const FString TempPath = RoomReadyPath + TEXT(".tmp");
	const FString Contents = FString::Printf(TEXT("%s\n%u\n7777\n"), *RoomRunId.ToString(EGuidFormats::DigitsWithHyphens), FPlatformProcess::GetCurrentProcessId());
	if (FFileHelper::SaveStringToFile(Contents, *TempPath) && Files.MoveFile(*RoomReadyPath, *TempPath))
	{
		bHostedWorldReady = true;
		UE_LOG(LogSWConnection, Display, TEXT("Side=Server RoomRunId=%s Phase=ServerReady Result=Success Port=7777"), *RoomRunId.ToString());
	}
}

bool AMultiGameMode::RequestHostedRoomReturnTravel(bool bAfterGameOver)
{
	if (!IsHostedRoom() || bLevelRestartRequested || !GetWorld()) return false;
	bLevelRestartRequested = true;
	if (UPlayerProgressSubsystem* Progress = GetGameInstance()->GetSubsystem<UPlayerProgressSubsystem>()) Progress->ClearSnapshotsForHostedReturn();
	if (USWRoomProgressSubsystem* Room = GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>()) { Room->MarkReturnTravelPending(); if (bAfterGameOver) Room->MarkGameOverRetryTravelPending(); }
	if (!GetWorld()->ServerTravel(TEXT("?Restart"), false))
	{
		bLevelRestartRequested = false;
		if (USWRoomProgressSubsystem* Room = GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>()) { Room->ClearReturnTravelPending(); if (bAfterGameOver) Room->ClearGameOverRetryTravelPending(); }
		UE_LOG(LogSWConnection, Error, TEXT("Hosted room return travel failed"));
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			if (APlayerController* Controller = It->Get()) Controller->ClientMessage(TEXT("귀환 실패"));
		return false;
	}
	return true;
}

bool AMultiGameMode::RequestHostedRoomFinalDepartureTravel()
{
	if (!IsHostedRoom() || bLevelRestartRequested || !GetWorld()) return false;
	bLevelRestartRequested = true;
	if (UPlayerProgressSubsystem* Progress = GetGameInstance()->GetSubsystem<UPlayerProgressSubsystem>())
		Progress->ClearSnapshotsForHostedReturn();
	USWRoomProgressSubsystem* Room = GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>();
	if (Room) Room->MarkFinalDepartureTravelPending();
	const bool bTravelAccepted = GetWorld()->ServerTravel(TEXT("?Restart"), false);
	if (!bTravelAccepted)
	{
		bLevelRestartRequested = false;
		if (Room) Room->ClearFinalDepartureTravelPending();
	}
	return bTravelAccepted;
}

bool AMultiGameMode::TickRoomOwner(float DeltaTime)
{
	CheckRoomOwner();
	return true;
}

void AMultiGameMode::CheckRoomOwner()
{
	if (RoomOwnerPid && (!FPlatformProcess::IsApplicationRunning(RoomOwnerPid)
		|| (bHostedWorldReady && !RoomReadyPath.IsEmpty() && !FPlatformFileManager::Get().GetPlatformFile().FileExists(*RoomReadyPath))))
	{
		UE_LOG(LogSWConnection, Warning, TEXT("Side=Server RoomRunId=%s Phase=OwnerWatch Result=ShutdownRequested"), *RoomRunId.ToString());
		FPlatformMisc::RequestExit(false);
	}
}

void AMultiGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (auto& Pair : RespawnTimers) GetWorldTimerManager().ClearTimer(Pair.Value);
	RespawnTimers.Reset(); DeathFlowStates.Reset(); IndividualRespawnInProgress.Reset();
	if (RoomOwnerTickerHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(RoomOwnerTickerHandle);
	if (!RoomReadyPath.IsEmpty()) FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*RoomReadyPath);
	Super::EndPlay(EndPlayReason);
}

FString AMultiGameMode::InitNewPlayer(
    APlayerController* NewPlayerController,
    const FUniqueNetIdRepl& UniqueId,
    const FString& Options,
    const FString& Portal)
{
    if (!NewPlayerController)
    {
        return TEXT("InvalidPlayerController");
    }

    const USWRoomProgressSubsystem* Room = IsHostedRoom()
        ? GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>() : nullptr;
    FGuid SuppliedHostKey;
    const bool bIsHost = Room
        && FGuid::Parse(UGameplayStatics::ParseOption(Options, TEXT("SWHostKey")), SuppliedHostKey)
        && SuppliedHostKey == Room->GetHostKey();
    FString ParsedName;
    const FString NameHex = UGameplayStatics::ParseOption(Options, TEXT("SWNameHex"));
    if (Room && !FSWRoomName::FromHex(NameHex, ParsedName)) return TEXT("InvalidDisplayName");
    if (!Room && !NameHex.IsEmpty()) FSWRoomName::FromHex(NameHex, ParsedName);
    if (ParsedName.IsEmpty()) ParsedName = UGameplayStatics::ParseOption(Options, TEXT("Name"));
    const FString PlayerKey = Room
        ? (bIsHost ? FString(TEXT("H")) : FString(TEXT("G:")) + ParsedName)
        : (ParsedName.IsEmpty() ? FString() : FString(TEXT("M:")) + ParsedName);

    if (!AssignRoleToPlayer(NewPlayerController, bIsHost, PlayerKey))
    {
        return TEXT("ServerIsFull");
    }

    UE_LOG(
        LogSWConnection,
        Display,
        TEXT("InitNewPlayer Controller=%s PlayerIndex=%d"),
        *GetNameSafe(NewPlayerController),
        GetPlayerIndex(NewPlayerController)
    );

    const int32 AssignedPlayerIndex = GetPlayerIndex(NewPlayerController);
    const FString InitError = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
    if (InitError.IsEmpty())
    {
		if (NewPlayerController->PlayerState)
		{
			NewPlayerController->PlayerState->SetPlayerName(ParsedName.IsEmpty() ? TEXT("Player") : ParsedName);
			if (IsHostedRoom())
			{
				USWRoomProgressSubsystem* MutableRoom = GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>();
				if (bIsHost && MutableRoom && MutableRoom->GetMutableActiveRoom())
				{
					if (MutableRoom->GetMutableActiveRoom()->HostDisplayName.IsEmpty()) MutableRoom->GetMutableActiveRoom()->HostDisplayName = ParsedName;
					NewPlayerController->PlayerState->SetPlayerName(MutableRoom->GetMutableActiveRoom()->HostDisplayName);
				}
			}
		}
        return InitError;
    }

    PlayerRoles.Remove(NewPlayerController);
    PlayerIndices.Remove(NewPlayerController);
    PlayerReconnectKeys.Remove(NewPlayerController);
    ReadyPlayers.Remove(NewPlayerController);
    UE_LOG(
        LogSWConnection,
        Warning,
        TEXT("InitNewPlayer rolled back player slot. Controller=%s PlayerIndex=%d Reason=%s"),
        *GetNameSafe(NewPlayerController),
        AssignedPlayerIndex,
        *InitError
    );
    return InitError;
}

void AMultiGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
    Super::HandleStartingNewPlayer_Implementation(NewPlayer);

    APawn* Pawn = NewPlayer ? NewPlayer->GetPawn() : nullptr;
    const bool bPossessed = Pawn && Pawn->GetController() == NewPlayer;
    if (Pawn)
    {
        UE_LOG(
            LogSWConnection,
            Display,
            TEXT("HandleStartingNewPlayer Controller=%s PlayerIndex=%d PlayerState=%s Pawn=%s PawnClass=%s Possessed=%s"),
            *GetNameSafe(NewPlayer),
            GetPlayerIndex(NewPlayer),
            *GetNameSafe(NewPlayer ? NewPlayer->PlayerState : nullptr),
            *GetNameSafe(Pawn),
            *GetNameSafe(Pawn->GetClass()),
            bPossessed ? TEXT("true") : TEXT("false")
        );
    }
    else
    {
        UE_LOG(
            LogSWConnection,
            Warning,
            TEXT("HandleStartingNewPlayer Controller=%s PlayerIndex=%d PlayerState=%s Pawn=None PawnClass=None Possessed=false"),
            *GetNameSafe(NewPlayer),
            GetPlayerIndex(NewPlayer),
            *GetNameSafe(NewPlayer ? NewPlayer->PlayerState : nullptr)
        );
    }
}

void AMultiGameMode::PostLogin(APlayerController* NewPlayer)
{
    if (!NewPlayer)
    {
        return;
    }

    // Super::PostLogin 내부에서 PawnClass / PlayerStart를 물어볼 수 있으므로
    // 역할 배정은 Super 호출 전에 끝내는 것이 안전하다.
    const bool bRoleAssigned = PlayerIndices.Contains(NewPlayer);
    if (!bRoleAssigned)
    {
        UE_LOG(
            LogSWConnection,
            Error,
            TEXT("PostLogin failed to assign player slot. Controller=%s"),
            *GetNameSafe(NewPlayer)
        );
    }

    if (bRoleAssigned && bAutoReadyOnPostLogin)
    {
        ReadyPlayers.Add(NewPlayer);
    }

    Super::PostLogin(NewPlayer);

    const FName AssignedRole = GetPlayerRole(NewPlayer);
    const int32 PlayerIndex = GetPlayerIndex(NewPlayer);

    UE_LOG(
        LogSWConnection,
        Display,
        TEXT("PostLogin Controller=%s PlayerIndex=%d PlayerState=%s Pawn=%s RegisteredPlayers=%d"),
        *GetNameSafe(NewPlayer),
        PlayerIndex,
        *GetNameSafe(NewPlayer->PlayerState),
        *GetNameSafe(NewPlayer->GetPawn()),
        PlayerRoles.Num()
    );

    if (bRoleAssigned)
    {
        OnPlayerRoleAssigned.Broadcast(NewPlayer, AssignedRole, PlayerIndex);
    }

    if (bRoleAssigned && bAutoReadyOnPostLogin)
    {
        OnPlayerReadyChanged.Broadcast(NewPlayer, true);
    }

    TryNotifyReadinessState();
	UpdateHostedRoomPause();
	PublishLifePhase();
}

void AMultiGameMode::PreLogin(
    const FString& Options,
    const FString& Address,
    const FUniqueNetIdRepl& UniqueId,
    FString& ErrorMessage
)
{
    UE_LOG(
        LogSWConnection,
        Display,
        TEXT("PreLogin Begin PlayerCount=%d MaxPlayers=%d"),
        GetNumPlayers(),
        MaxPlayerCount
    );

    Super::PreLogin(Options, Address, UniqueId, ErrorMessage);

    if (!ErrorMessage.IsEmpty())
    {
        UE_LOG(LogSWConnection, Warning, TEXT("Side=Server RoomRunId=%s PreLogin Rejected Reason=%s"), *RoomRunId.ToString(), *ErrorMessage);
        return;
    }

	bool bIsHost = false;
	if (IsHostedRoom())
	{
		USWRoomProgressSubsystem* Room = GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>();
		FString ValidatedName;
		if (!FSWRoomName::FromHex(UGameplayStatics::ParseOption(Options, TEXT("SWNameHex")), ValidatedName))
		{
			ErrorMessage = TEXT("InvalidDisplayName");
			return;
		}
		const FString HostKeyText = UGameplayStatics::ParseOption(Options, TEXT("SWHostKey"));
		if (!HostKeyText.IsEmpty())
		{
			FGuid SuppliedKey;
			if (!FGuid::Parse(HostKeyText, SuppliedKey) || SuppliedKey != Room->GetHostKey())
			{
				ErrorMessage = TEXT("InvalidHostKey");
				return;
			}
			bIsHost = true;
		}
		if (bIsHost)
		{
			if (!Room || !Room->GetActiveRoom() || ValidatedName != Room->GetActiveRoom()->HostDisplayName)
			{ ErrorMessage = TEXT("HostNameMismatch"); return; }
			for (const TPair<TObjectPtr<AController>, int32>& Pair : PlayerIndices)
				if (Pair.Value == 0) { ErrorMessage = TEXT("HostSlotOccupied"); return; }
		}
		else
		{
			if (!Room || !Room->GetActiveRoom() || Room->IsNewRoomPending())
			{ ErrorMessage = TEXT("RoomNotReady"); return; }
			if (ValidatedName == Room->GetActiveRoom()->HostDisplayName)
			{ ErrorMessage = TEXT("HostNameReserved"); return; }
			for (const TPair<TObjectPtr<AController>, int32>& Pair : PlayerIndices)
				if (Pair.Key && Pair.Key->PlayerState && Pair.Key->PlayerState->GetPlayerName() == ValidatedName)
				{ ErrorMessage = TEXT("NameAlreadyConnected"); return; }
		}
	}

    if (!bIsHost && FindAvailablePlayerIndex() == INDEX_NONE)
    {
        ErrorMessage = TEXT("ServerIsFull");
        UE_LOG(LogSWConnection, Warning, TEXT("Side=Server RoomRunId=%s PreLogin Rejected Reason=ServerIsFull"), *RoomRunId.ToString());
        return;
    }

    UE_LOG(LogSWConnection, Display, TEXT("Side=Server RoomRunId=%s PreLogin Accepted"), *RoomRunId.ToString());
}

void AMultiGameMode::Logout(AController* Exiting)
{
    const int32 ReleasedPlayerIndex = GetPlayerIndex(Exiting);
	if (IsHostedRoom()) UE_LOG(LogSWRoom, Display, TEXT("Flow=Disconnect Side=Server PlayerIndex=%d Host=%d Phase=Logout NoDiskWrite=1"),
		ReleasedPlayerIndex, IsHostController(Exiting) ? 1 : 0);
	const bool bWasHost = IsHostController(Exiting);
	const FString ReconnectKey = GetReconnectKey(Exiting);
	if (Exiting && !ReconnectKey.IsEmpty() && !bLevelRestartRequested)
	{
		if (APawn* ExitingPawn = Exiting->GetPawn())
		{
			if (UFunction* CaptureFunction = ExitingPawn->FindFunction(TEXT("CaptureReconnectProgress")))
			{
				ExitingPawn->ProcessEvent(CaptureFunction, nullptr);
			}
		}
	}

    if (Exiting)
    {
        PlayerRoles.Remove(Exiting);
        ReadyPlayers.Remove(Exiting);
		PlayerIndices.Remove(Exiting);
		PlayerReconnectKeys.Remove(Exiting);
		HostControllers.Remove(Exiting);
		FinishedDeadPlayers.Remove(Exiting);
		if (FTimerHandle* Timer = RespawnTimers.Find(Exiting)) GetWorldTimerManager().ClearTimer(*Timer);
		RespawnTimers.Remove(Exiting);
		IndividualRespawnInProgress.Remove(Exiting); DeathFlowStates.Remove(Exiting); RespawnFailureLogTimes.Remove(Exiting);
    }

    // 플레이어가 나가면 다시 조건을 만족할 수 있도록 플래그를 갱신한다.
    if (!AreRequiredPlayersJoined())
    {
        bRequiredPlayersJoinedNotified = false;
    }

    if (!AreAllPlayersReady())
    {
        bAllPlayersReadyNotified = false;
    }

    Super::Logout(Exiting);
	RefreshSpectatorTargets();
	UpdateHostedRoomPause();
	if (bWasHost && !bLevelRestartRequested) FPlatformMisc::RequestExit(false);

    UE_LOG(
        LogSWConnection,
        Display,
        TEXT("Logout Controller=%s ReleasedPlayerIndex=%d RegisteredPlayers=%d"),
        *GetNameSafe(Exiting),
        ReleasedPlayerIndex,
        PlayerRoles.Num()
    );
}

UClass* AMultiGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
    // 공통 표준 플레이어 폰 클래스가 지정되어 있으면 우선 반환합니다.
    if (CommonPlayerPawnClass)
    {
        return CommonPlayerPawnClass;
    }

    // 기본 DefaultPawnClass가 지정되어 있으면 반환합니다.
    if (DefaultPawnClass)
    {
        return DefaultPawnClass;
    }

    // [LEGACY 호환] 기존 세팅이 남아있는 경우의 폴백
    const FName RoleName = GetPlayerRole(InController);
    if (RoleName == AttackerRoleName && AttackerPawnClass)
    {
        return AttackerPawnClass;
    }
    if (RoleName == CrafterRoleName && CrafterPawnClass)
    {
        return CrafterPawnClass;
    }

    return Super::GetDefaultPawnClassForController_Implementation(InController);
}

AActor* AMultiGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
    const int32 PlayerIndex = FMath::Max(0, GetPlayerIndex(Player));
	if (IsHostedRoom())
	{
		const ESWLevelEntryRole Wanted = PlayerIndex == 0 ? ESWLevelEntryRole::Host : ESWLevelEntryRole::Guest;
		for (TActorIterator<ASWLevelEntryPoint> It(GetWorld()); It; ++It)
			if (It->EntryRole == Wanted) return *It;
	}
	const USWRoomProgressSubsystem* Room = GetGameInstance() ? GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>() : nullptr;
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UPlayerProgressSubsystem* Progress = GI->GetSubsystem<UPlayerProgressSubsystem>();
			(Progress && Progress->HasSnapshot(PlayerIndex))
			|| (Room && (Room->IsReturnTravelPending() || Room->IsFinalDepartureTravelPending())))
		{
			APlayerRespawnPoint* Fallback = nullptr;
			for (TActorIterator<APlayerRespawnPoint> It(GetWorld()); It; ++It)
			{
				if (It->PlayerSlot == static_cast<ESWPlayerSlot>(PlayerIndex)) return *It;
				if (It->PlayerSlot == ESWPlayerSlot::Any) Fallback = *It;
			}
			if (Fallback) return Fallback;
		}
	}

    // 2. Player_0, Player_1 등의 인덱스 태그를 가진 PlayerStart 우선 검색
    const FName IndexTag = *FString::Printf(TEXT("Player_%d"), PlayerIndex);
    if (APlayerStart* IndexStart = FindPlayerStartByRole(IndexTag))
    {
        return IndexStart;
    }

    // 3. 레거시 역할 태그 검색
    const FName RoleName = GetPlayerRole(Player);
    if (!RoleName.IsNone())
    {
        if (APlayerStart* RoleStart = FindPlayerStartByRole(RoleName))
        {
            return RoleStart;
        }
    }

    // 4. 레벨에 배치된 PlayerStart 목록 중 인덱스 기반 순차 배정
    if (UWorld* World = GetWorld())
    {
        TArray<APlayerStart*> AllStarts;
        for (TActorIterator<APlayerStart> It(World); It; ++It)
        {
            if (IsValid(*It))
            {
                AllStarts.Add(*It);
            }
        }

        if (!AllStarts.IsEmpty())
        {
            const int32 TargetIdx = FMath::Clamp(PlayerIndex, 0, AllStarts.Num() - 1);
            return AllStarts[TargetIdx];
        }
    }

    return Super::ChoosePlayerStart_Implementation(Player);
}

void AMultiGameMode::RestartPlayer(AController* NewPlayer)
{
	FTransform ReconnectTransform;
	if (ResolveReconnectSpawnTransform(NewPlayer, ReconnectTransform))
	{
		RestartPlayerAtTransform(NewPlayer, ReconnectTransform);
		return;
	}

	Super::RestartPlayer(NewPlayer);
}

bool AMultiGameMode::ResolveReconnectSpawnTransform(
	AController* Controller,
	FTransform& OutTransform)
{
	const FString ReconnectKey = GetReconnectKey(Controller);
	const UPlayerProgressSubsystem* Progress = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UPlayerProgressSubsystem>()
		: nullptr;
	FSWPlayerProgressSnapshot Snapshot;
	if (ReconnectKey.IsEmpty() || !Progress || !Progress->PeekReconnectSnapshot(ReconnectKey, Snapshot))
	{
		return false;
	}
	if (Snapshot.bHasLastValidWorldTransform)
	{
		FTransform SavedTransform = Snapshot.LastValidWorldTransform;
		if (AActor* MovementHost = Snapshot.LastMovementHost.Get())
		{
			if (MovementHost->GetWorld() == GetWorld())
				SavedTransform = Snapshot.LastMovementHostRelativeTransform * MovementHost->GetActorTransform();
		}
		if (IsReconnectTransformSafe(Controller, SavedTransform))
		{
			OutTransform = SavedTransform;
			return true;
		}
	}

	const int32 PlayerIndex = GetPlayerIndex(Controller);
	APawn* TeammatePawn = nullptr;
	AActor* TeammateMovementHost = nullptr;
	for (const TPair<TObjectPtr<AController>, int32>& Pair : PlayerIndices)
	{
		if (Pair.Key == Controller || !Pair.Key)
		{
			continue;
		}

		TeammatePawn = Pair.Key->GetPawn();
		const ACharacter* TeammateCharacter = Cast<ACharacter>(TeammatePawn);
		const UPrimitiveComponent* MovementBase = TeammateCharacter && TeammateCharacter->GetCharacterMovement()
			? TeammateCharacter->GetCharacterMovement()->GetMovementBase()
			: nullptr;
		TeammateMovementHost = MovementBase ? MovementBase->GetOwner() : nullptr;
		if (TeammateMovementHost)
		{
			break;
		}
	}

	if (TeammatePawn && TeammateMovementHost)
	{
		UPlayerRespawnPointComponent* BestPoint = nullptr;
		double BestDistanceSquared = TNumericLimits<double>::Max();
		for (TObjectIterator<UPlayerRespawnPointComponent> It; It; ++It)
		{
			UPlayerRespawnPointComponent* Point = *It;
			if (!Point || Point->GetWorld() != GetWorld() || !Point->IsRegistered()
				|| Point->GetOwner() != TeammateMovementHost
				|| (Point->PlayerSlot != static_cast<ESWPlayerSlot>(PlayerIndex)
					&& Point->PlayerSlot != ESWPlayerSlot::Any))
			{
				continue;
			}

			AActor* Host = Point->GetOwner();
			if (!Host->GetClass()->ImplementsInterface(URespawnHostInterface::StaticClass())
				|| !IRespawnHostInterface::Execute_IsAvailableForPlayerRespawn(Host)
				|| !IsReconnectTransformSafe(Controller, Point->GetComponentTransform()))
			{
				continue;
			}

			const double DistanceSquared = FVector::DistSquared(
				Point->GetComponentLocation(),
				TeammatePawn->GetActorLocation());
			if (DistanceSquared < BestDistanceSquared)
			{
				BestDistanceSquared = DistanceSquared;
				BestPoint = Point;
			}
		}

		if (BestPoint)
		{
			OutTransform = BestPoint->GetComponentTransform();
			return true;
		}
	}

	if (Snapshot.bHasLastValidWorldTransform)
	{
		FTransform CandidateTransform = Snapshot.LastValidWorldTransform;
		if (UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
		{
			FNavLocation ProjectedLocation;
			if (NavigationSystem->ProjectPointToNavigation(
				CandidateTransform.GetLocation(),
				ProjectedLocation,
				FVector(100.0f, 100.0f, 200.0f)))
			{
				CandidateTransform.SetLocation(ProjectedLocation.Location);
				if (IsReconnectTransformSafe(Controller, CandidateTransform))
				{
					OutTransform = CandidateTransform;
					return true;
				}
			}
		}
	}

	return false;
}

bool AMultiGameMode::IsReconnectTransformSafe(
	AController* Controller,
	const FTransform& Transform)
{
	UWorld* World = GetWorld();
	const UClass* PawnClass = GetDefaultPawnClassForController(Controller);
	const ACharacter* CharacterDefaultObject = PawnClass
		? Cast<ACharacter>(PawnClass->GetDefaultObject())
		: nullptr;
	const UCapsuleComponent* Capsule = CharacterDefaultObject
		? CharacterDefaultObject->GetCapsuleComponent()
		: nullptr;
	if (!World || !Capsule || Transform.GetLocation().ContainsNaN())
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SWReconnectSpawn), false);
	return !World->OverlapBlockingTestByChannel(
		Transform.GetLocation(),
		Transform.GetRotation(),
		ECC_Pawn,
		FCollisionShape::MakeCapsule(
			Capsule->GetScaledCapsuleRadius(),
			Capsule->GetScaledCapsuleHalfHeight()),
		QueryParams);
}

void AMultiGameMode::SetPlayerReady(AController* Controller, bool bReady)
{
	if (!HasAuthority() || !Controller)
	{
		return;
	}

	if (!PlayerRoles.Contains(Controller))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ASWMultiGameMode] SetPlayerReady ignored. Unknown Controller=%s"),
			*GetNameSafe(Controller)
		);
		return;
	}

	// ReadyPlayer가 Controller를 할당 받고 있었는지?
	const bool bWasReady = ReadyPlayers.Contains(Controller);

	if (bReady)
	{
		ReadyPlayers.Add(Controller);
	}
	else
	{
		ReadyPlayers.Remove(Controller);
	}

	if (bWasReady != bReady)
	{
		OnPlayerReadyChanged.Broadcast(Controller, bReady);
	}

	TryNotifyReadinessState();
}

bool AMultiGameMode::IsPlayerReady(AController* Controller) const
{
	if (!Controller)
	{
		return false;
	}

	return ReadyPlayers.Contains(Controller);
}

FName AMultiGameMode::GetPlayerRole(AController* Controller) const
{
	if (!Controller)
	{
		return NAME_None;
	}

	if (const FName* FoundRole = PlayerRoles.Find(Controller))
	{
		return *FoundRole;
	}

	return NAME_None;
}

int32 AMultiGameMode::GetConnectedPlayerCount() const
{
	return PlayerRoles.Num();
}

//  필요한 Player들이 모두 존재?
bool AMultiGameMode::AreRequiredPlayersJoined() const
{
	return RequiredPlayerCount > 0 && PlayerRoles.Num() >= RequiredPlayerCount;
}

bool AMultiGameMode::AreAllPlayersReady() const
{
	if (!AreRequiredPlayersJoined())
	{
		return false;
	}

	// Ready 체크가 필요 없는 경우 항상 true 반환
	if (!bRequireAllPlayersReady)
	{
		return true;
	}

	for (const TPair<TObjectPtr<AController>, FName>& Pair : PlayerRoles)
	{
		AController* Controller = Pair.Key.Get();

		if (!Controller)
		{
			continue;
		}

		if (!ReadyPlayers.Contains(Controller))
		{
			return false;
		}
	}

	return true;
}

int32 AMultiGameMode::GetPlayerIndex(AController* Controller) const
{
	if (const int32* Index = PlayerIndices.Find(Controller)) return *Index;
	return INDEX_NONE;
}

void AMultiGameMode::PublishLifePhase()
{
 if (RoomReadyState) { RoomReadyState->SessionLifePhase = SessionLifePhase; RoomReadyState->ForceNetUpdate(); }
 for (const auto& Pair : PlayerIndices)
  if (ISWRespawnControllerInterface* Flow = Cast<ISWRespawnControllerInterface>(Pair.Key.Get()))
  {
   FSWDeathFlowState& State = DeathFlowStates.FindOrAdd(Pair.Key);
   State.RestoreGeneration = RoomReadyState ? RoomReadyState->RestoreGeneration : 0;
   State.bHostMayRetry = IsHostController(Pair.Key) && SessionLifePhase == ESWSessionLifePhase::GameOver;
   Flow->SetDeathFlowState(State);
  }
}
bool AMultiGameMode::RegisterPlayerRespawnShip(AActor* Ship)
{
 if (!HasAuthority() || !IsValid(Ship) || Ship->GetWorld() != GetWorld() || Ship->ActorHasTag(TEXT("Enemy"))
  || !Ship->GetClass()->ImplementsInterface(URespawnHostInterface::StaticClass())) return false;
 if (PlayerRespawnShip.Get() == Ship) return true;
 if (PlayerRespawnShip.IsValid()) { UE_LOG(LogSWRoom, Error, TEXT("RespawnShipMissing: duplicate player ship %s"), *GetNameSafe(Ship)); return false; }
 if (SessionLifePhase != ESWSessionLifePhase::Playing) return false;
 if (bPlayerRespawnShipRegistered && (!RoomReadyState || RoomReadyState->bWorldReady)) return false;
 PlayerRespawnShip = Ship; bPlayerRespawnShipRegistered = true;
 return true;
}
bool AMultiGameMode::CanMutateGameplay(AController* Controller) const
{
 return Controller && SessionLifePhase != ESWSessionLifePhase::GameOver && SessionLifePhase != ESWSessionLifePhase::ReturningAfterGameOver
  && !FinishedDeadPlayers.Contains(Controller) && !IndividualRespawnInProgress.Contains(Controller)
  && (!IsHostedRoom() || (RoomReadyState && RoomReadyState->bWorldReady));
}
bool AMultiGameMode::CanHostRequestGameOverRetry(AController* Controller) const
{
 return HasAuthority() && IsHostController(Controller) && SessionLifePhase == ESWSessionLifePhase::GameOver && !bLevelRestartRequested;
}
void AMultiGameMode::SetGameOverRetryTransitionPending(bool bPending)
{
 if (!HasAuthority()) return;
 if (bPending && SessionLifePhase == ESWSessionLifePhase::GameOver) SessionLifePhase = ESWSessionLifePhase::ReturningAfterGameOver;
 else if (!bPending && SessionLifePhase == ESWSessionLifePhase::ReturningAfterGameOver) SessionLifePhase = ESWSessionLifePhase::GameOver;
 PublishLifePhase();
}
void AMultiGameMode::NotifyPlayerShipSinking(AActor* Ship)
{
 if (!HasAuthority() || Ship != PlayerRespawnShip.Get() || SessionLifePhase != ESWSessionLifePhase::Playing || bLevelRestartRequested) return;
 SessionLifePhase = ESWSessionLifePhase::ShipSinking;
 for (auto& Pair : RespawnTimers) GetWorldTimerManager().ClearTimer(Pair.Value);
 RespawnTimers.Reset();
 for (auto& Pair : DeathFlowStates) Pair.Value.RespawnEndServerTime = 0;
 UE_LOG(LogSWRoom, Display, TEXT("ShipSinkingStarted Ship=%s"), *GetNameSafe(Ship));
 PublishLifePhase();
}
void AMultiGameMode::NotifyPlayerShipRemovedBySinking(AActor* Ship)
{
 // Weak Get() excludes pending-kill actors; retain identity with Get(true).
 if (!HasAuthority() || !Ship || Ship != PlayerRespawnShip.Get(true) || Ship->GetWorld() != GetWorld()
  || bLevelRestartRequested || GetWorld()->bIsTearingDown || SessionLifePhase != ESWSessionLifePhase::ShipSinking) return;
 SessionLifePhase = ESWSessionLifePhase::GameOver;
 for (const auto& Pair : PlayerIndices)
  if (ISWRespawnControllerInterface* Flow = Cast<ISWRespawnControllerInterface>(Pair.Key.Get())) Flow->FreezeLifeProgressForGameOver();
 PublishLifePhase();
 UE_LOG(LogSWRoom, Display, TEXT("ShipRemovedGameOver Ship=%s"), *GetNameSafe(Ship));
 OnGameOverRequested.Broadcast();
}
void AMultiGameMode::RefreshSpectatorTargets()
{
 // Controller owns camera publication and life-character checks; refreshing state also handles late joins.
 PublishLifePhase();
}
void AMultiGameMode::NotifyPlayerDeathFinished(APawn* DeadPawn)
{
 UE_LOG(LogSWRoom, Display, TEXT("[SWLifeDiag] Event=ModeDeathFinishedEntered Pawn=%s Authority=%d Session=%d"), *GetNameSafe(DeadPawn), HasAuthority(), static_cast<int32>(SessionLifePhase));
 if (!HasAuthority() || !DeadPawn || DeadPawn->GetWorld() != GetWorld()) return;
 AController* Controller = DeadPawn->GetController();
 if (!Controller && DeadPawn->GetPlayerState()) Controller = DeadPawn->GetPlayerState()->GetOwningController();
 if (!Controller || !PlayerIndices.Contains(Controller) || FinishedDeadPlayers.Contains(Controller)
  || SessionLifePhase == ESWSessionLifePhase::GameOver || SessionLifePhase == ESWSessionLifePhase::ReturningAfterGameOver)
 {
  UE_LOG(LogSWRoom, Display, TEXT("[SWLifeDiag] Event=ModeDeathFinishedRejected Controller=%s Registered=%d AlreadyDead=%d Session=%d"), *GetNameSafe(Controller), PlayerIndices.Contains(Controller), FinishedDeadPlayers.Contains(Controller), static_cast<int32>(SessionLifePhase));
  return;
 }
 ISWRespawnControllerInterface* Flow = Cast<ISWRespawnControllerInterface>(Controller);
 const bool bCaptured = Flow && Flow->CaptureLatestLifeProgress(DeadPawn);
 FinishedDeadPlayers.Add(Controller);
 FSWDeathFlowState& State = DeathFlowStates.FindOrAdd(Controller);
 State.Phase = ESWPersonalLifePhase::WaitingForRespawn;
 ++State.WaitingGeneration;
 State.RespawnEndServerTime = SessionLifePhase == ESWSessionLifePhase::Playing
  ? (GetGameState<AGameStateBase>() ? GetGameState<AGameStateBase>()->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds()) + FMath::Max(0.f, IndividualRespawnDelay) : 0;
 Controller->UnPossess();
 UE_LOG(LogSWRoom, Display, TEXT("[SWLifeDiag] Event=RespawnWaiting Controller=%s Slot=%d Captured=%d WaitingGeneration=%d EndServerTime=%.3f Delay=%.3f Session=%d"), *GetNameSafe(Controller), GetPlayerIndex(Controller), bCaptured, State.WaitingGeneration, State.RespawnEndServerTime, IndividualRespawnDelay, static_cast<int32>(SessionLifePhase));
 DeadPawn->SetLifeSpan(FMath::Max(IndividualRespawnDelay + 2.f, 10.f));
 if (!bCaptured) UE_LOG(LogSWRoom, Error, TEXT("RespawnProgressMissing Index=%d Generation=%d"), GetPlayerIndex(Controller), State.WaitingGeneration);
 if (SessionLifePhase == ESWSessionLifePhase::Playing && bCaptured)
 {
  FTimerDelegate Delegate = FTimerDelegate::CreateUObject(this, &AMultiGameMode::TryRespawnPlayer, Controller, State.WaitingGeneration);
  if (IndividualRespawnDelay <= 0) RespawnTimers.Add(Controller, GetWorldTimerManager().SetTimerForNextTick(Delegate));
  else GetWorldTimerManager().SetTimer(RespawnTimers.FindOrAdd(Controller), Delegate, IndividualRespawnDelay, false);
 }
 RefreshSpectatorTargets();
}
void AMultiGameMode::TryRespawnPlayer(AController* Controller, int32 ExpectedGeneration)
{
 UE_LOG(LogSWRoom, Display, TEXT("[SWLifeDiag] Event=RespawnTimerFired Controller=%s ExpectedGeneration=%d Session=%d Dead=%d"), *GetNameSafe(Controller), ExpectedGeneration, static_cast<int32>(SessionLifePhase), FinishedDeadPlayers.Contains(Controller));
 if (!IsValid(Controller) || !FinishedDeadPlayers.Contains(Controller) || SessionLifePhase != ESWSessionLifePhase::Playing
  || !DeathFlowStates.Contains(Controller) || DeathFlowStates.FindChecked(Controller).WaitingGeneration != ExpectedGeneration) return;
 ISWRespawnControllerInterface* Flow = Cast<ISWRespawnControllerInterface>(Controller);
 UPlayerRespawnPointComponent* Point = FindShipRespawnPoint(GetPlayerIndex(Controller));
 UE_LOG(LogSWRoom, Display, TEXT("[SWLifeDiag] Event=RespawnPrerequisites Controller=%s Slot=%d Point=%s Flow=%d PendingProgress=%d Ship=%s"), *GetNameSafe(Controller), GetPlayerIndex(Controller), *GetNameSafe(Point), Flow != nullptr, Flow && Flow->HasPendingLifeProgress(), *GetNameSafe(PlayerRespawnShip.Get()));
 bool bSuccess = false;
 if (Point && Flow && Flow->HasPendingLifeProgress())
 {
  IndividualRespawnInProgress.Add(Controller);
  RestartPlayerAtTransform(Controller, Point->GetComponentTransform());
  bSuccess = Flow->WasLastLifeProgressApplySuccessful(Controller->GetPawn());
  IndividualRespawnInProgress.Remove(Controller);
 }
 if (!bSuccess)
 {
  if (APawn* Partial = Controller->GetPawn()) { Controller->UnPossess(); Partial->Destroy(); }
  double& LastLog = RespawnFailureLogTimes.FindOrAdd(Controller);
  if (GetWorld()->GetTimeSeconds() - LastLog >= 1)
  {
   UE_LOG(LogSWRoom, Warning, TEXT("%s Index=%d Generation=%d"), Point ? TEXT("RespawnSpawnFailed") : TEXT("RespawnSlotMissing"), GetPlayerIndex(Controller), DeathFlowStates.FindOrAdd(Controller).WaitingGeneration);
   LastLog = GetWorld()->GetTimeSeconds();
  }
  GetWorldTimerManager().SetTimer(RespawnTimers.FindOrAdd(Controller), FTimerDelegate::CreateUObject(this, &AMultiGameMode::TryRespawnPlayer, Controller, ExpectedGeneration), .5f, false);
  return;
 }
 FinishedDeadPlayers.Remove(Controller);
 UE_LOG(LogSWRoom, Display, TEXT("[SWLifeDiag] Event=RespawnSucceeded Controller=%s Pawn=%s Generation=%d"), *GetNameSafe(Controller), *GetNameSafe(Controller->GetPawn()), ExpectedGeneration);
 RespawnTimers.Remove(Controller);
 DeathFlowStates.FindOrAdd(Controller).Phase = ESWPersonalLifePhase::Alive;
 Flow->CaptureLatestLifeProgress(Controller->GetPawn());
 if (UPlayerProgressSubsystem* Progress = GetGameInstance()->GetSubsystem<UPlayerProgressSubsystem>())
 { Progress->ClearSnapshot(GetPlayerIndex(Controller)); Progress->ClearReconnectSnapshot(GetReconnectKey(Controller)); }
 RefreshSpectatorTargets();
}
UPlayerRespawnPointComponent* AMultiGameMode::FindShipRespawnPoint(int32 PlayerIndex) const
{
 AActor* Ship = PlayerRespawnShip.Get();
 if (!Ship || PlayerIndex < 0 || PlayerIndex > 1 || SessionLifePhase != ESWSessionLifePhase::Playing
  || !IRespawnHostInterface::Execute_IsAvailableForPlayerRespawn(Ship)) return nullptr;
 TArray<UPlayerRespawnPointComponent*> Points;
 Ship->GetComponents(Points);
 UPlayerRespawnPointComponent* Result = nullptr;
 for (UPlayerRespawnPointComponent* Point : Points)
  if (Point->PlayerSlot == static_cast<ESWPlayerSlot>(PlayerIndex) && Point->IsRegistered())
  {
   if (Result) { UE_LOG(LogSWRoom, Error, TEXT("RespawnSlotDuplicate Index=%d"), PlayerIndex); return nullptr; }
   if (Point->GetComponentTransform().ContainsNaN()) return nullptr;
   Result = Point;
  }
 return Result;
}
void AMultiGameMode::HandleAllPlayersDeathFinished() {}
void AMultiGameMode::RequestGameOverAndLevelRestart()
{
 UE_LOG(LogSWRoom, Warning, TEXT("Legacy GameOver ignored: actual sinking removal notification required"));
}
void AMultiGameMode::CapturePlayerProgressForLevelRestart()
{
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		if (UFunction* CaptureFunction = It->FindFunction(TEXT("CaptureRespawnProgress")))
		{
			It->ProcessEvent(CaptureFunction, nullptr);
		}
	}
}

void AMultiGameMode::HandleRequiredPlayersJoined()
{
	// 하위 GameMode에서 필요하면 override.
}

void AMultiGameMode::HandleAllPlayersReady()
{
	// 하위 GameMode에서 필요하면 override.
}

FName AMultiGameMode::GetRoleForPlayerIndex(int32 PlayerIndex) const
{
	if (PlayerIndex == 0)
	{
		return AttackerRoleName;
	}

	if (PlayerIndex == 1)
	{
		return CrafterRoleName;
	}

	return NAME_None;
}

void AMultiGameMode::TryNotifyReadinessState()
{
	if (AreRequiredPlayersJoined() && !bRequiredPlayersJoinedNotified)
	{
		bRequiredPlayersJoinedNotified = true;

		OnRequiredPlayersJoined.Broadcast();
		HandleRequiredPlayersJoined();
	}

	if (AreAllPlayersReady() && !bAllPlayersReadyNotified)
	{
		bAllPlayersReadyNotified = true;

		OnAllPlayersReady.Broadcast();
		HandleAllPlayersReady();
	}
}

int32 AMultiGameMode::FindAvailablePlayerIndex()
{
    if (MaxPlayerCount <= 0)
    {
        return INDEX_NONE;
    }

    for (int32 CandidateIndex = IsHostedRoom() ? 1 : 0; CandidateIndex < MaxPlayerCount; ++CandidateIndex)
    {
        bool bAlreadyUsed = false;
        for (const TPair<TObjectPtr<AController>, int32>& Pair : PlayerIndices)
        {
            if (Pair.Value == CandidateIndex)
            {
                bAlreadyUsed = true;
                break;
            }
        }

        if (!bAlreadyUsed)
        {
            return CandidateIndex;
        }
    }

    return INDEX_NONE;
}

bool AMultiGameMode::IsHostController(AController* Controller) const
{
	return HostControllers.Contains(Controller);
}

bool AMultiGameMode::AssignRoleToPlayer(AController* Controller, bool bIsHost, const FString& PlayerKey)
{
	if (!Controller)
	{
		return false;
	}

	if (PlayerIndices.Contains(Controller))
	{
		return true;
	}

	int32 PlayerIndex = INDEX_NONE;
	if (bIsHost) PlayerIndex = 0;
	if (PlayerIndex == INDEX_NONE)
	{
		PlayerIndex = FindAvailablePlayerIndex();
	}
	if (PlayerIndex == INDEX_NONE)
	{
		UE_LOG(
			LogSWConnection,
			Warning,
			TEXT("Player slot assignment failed. Controller=%s MaxPlayers=%d"),
			*GetNameSafe(Controller),
			MaxPlayerCount
		);
		return false;
	}

	const FName AssignedRole = GetRoleForPlayerIndex(PlayerIndex);

	PlayerRoles.Add(Controller, AssignedRole);
	PlayerIndices.Add(Controller, PlayerIndex);
	if (bIsHost) HostControllers.Add(Controller);
	if (!PlayerKey.IsEmpty()) PlayerReconnectKeys.Add(Controller, PlayerKey);

	UE_LOG(
		LogSWConnection,
		Display,
		TEXT("Player slot assigned. Controller=%s Role=%s PlayerIndex=%d"),
		*GetNameSafe(Controller),
		*AssignedRole.ToString(),
		PlayerIndex
	);

	return true;
}

FString AMultiGameMode::GetReconnectKey(AController* Controller) const
{
	if (const FString* Key = PlayerReconnectKeys.Find(Controller)) return *Key;
	return FString();
}

bool AMultiGameMode::StoreReconnectSnapshotForController(
	AController* Controller,
	const FSWPlayerProgressSnapshot& Snapshot)
{
	const FString ReconnectKey = GetReconnectKey(Controller);
	UPlayerProgressSubsystem* Progress = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UPlayerProgressSubsystem>()
		: nullptr;
	if (ReconnectKey.IsEmpty() || !Progress) return false;
	Progress->StoreReconnectSnapshot(ReconnectKey, Snapshot);
	return true;
}

bool AMultiGameMode::ConsumeReconnectSnapshotForController(
	AController* Controller,
	FSWPlayerProgressSnapshot& OutSnapshot)
{
	const FString ReconnectKey = GetReconnectKey(Controller);
	UPlayerProgressSubsystem* Progress = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UPlayerProgressSubsystem>()
		: nullptr;
	return !ReconnectKey.IsEmpty() && Progress
		&& Progress->ConsumeReconnectSnapshot(ReconnectKey, OutSnapshot);
}

APlayerStart* AMultiGameMode::FindPlayerStartByRole(FName RoleName) const
{
	if (RoleName.IsNone())
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		APlayerStart* PlayerStart = *It;
		if (!PlayerStart)
		{
			continue;
		}

		if (PlayerStart->PlayerStartTag == RoleName)
		{
			return PlayerStart;
		}
	}

	return nullptr;
}
