// Fill out your copyright notice in the Description page of Project Settings.


#include "MultiGameMode.h"

#include "Network/SWNetworkLog.h"
#include "EngineUtils.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
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
	FString RunIdText;
	FString OwnerText;
	if (GetNetMode() == NM_DedicatedServer && FParse::Value(FCommandLine::Get(), TEXT("SWRoomRunId="), RunIdText)
		&& FParse::Value(FCommandLine::Get(), TEXT("SWRoomOwnerPid="), OwnerText)
		&& FGuid::Parse(RunIdText, RoomRunId))
	{
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
			{
				const FString TempPath = RoomReadyPath + TEXT(".tmp");
				const FString Contents = FString::Printf(TEXT("%s\n%u\n7777\n"), *RoomRunId.ToString(EGuidFormats::DigitsWithHyphens), FPlatformProcess::GetCurrentProcessId());
				if (FFileHelper::SaveStringToFile(Contents, *TempPath) && Files.MoveFile(*RoomReadyPath, *TempPath))
				{
					UE_LOG(LogSWConnection, Display, TEXT("Side=Server RoomRunId=%s Phase=ServerReady Result=Success Port=7777"), *RoomRunId.ToString());
					GetWorldTimerManager().SetTimer(RoomOwnerTimer, this, &AMultiGameMode::CheckRoomOwner, 5.0f, true);
				}
			}
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

void AMultiGameMode::CheckRoomOwner()
{
	if (RoomOwnerPid && (!FPlatformProcess::IsApplicationRunning(RoomOwnerPid)
		|| (!RoomReadyPath.IsEmpty() && !FPlatformFileManager::Get().GetPlatformFile().FileExists(*RoomReadyPath))))
	{
		UE_LOG(LogSWConnection, Warning, TEXT("Side=Server RoomRunId=%s Phase=OwnerWatch Result=ShutdownRequested"), *RoomRunId.ToString());
		FPlatformMisc::RequestExit(false);
	}
}

void AMultiGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(RoomOwnerTimer);
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

    FGuid ReconnectToken;
    const bool bHasReconnectToken = ParseReconnectToken(Options, ReconnectToken);
    if (!bHasReconnectToken && GetNetMode() != NM_Standalone)
    {
        return TEXT("InvalidReconnectToken");
    }

    UPlayerProgressSubsystem* Progress = GetGameInstance()
        ? GetGameInstance()->GetSubsystem<UPlayerProgressSubsystem>()
        : nullptr;
    FSWReconnectRecord ExistingRecord;
    const double CurrentTimeSeconds = FPlatformTime::Seconds();
    const bool bExistingReconnectRecord = bHasReconnectToken && Progress
        && Progress->FindReconnectRecord(ReconnectToken, CurrentTimeSeconds, ExistingRecord);

    if (!AssignRoleToPlayer(NewPlayerController, bHasReconnectToken ? &ReconnectToken : nullptr))
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
		FString ParsedName;
		const FString NameHex = UGameplayStatics::ParseOption(Options, TEXT("SWNameHex"));
		if (NewPlayerController->PlayerState)
		{
			if (!NameHex.IsEmpty() && !FSWRoomName::FromHex(NameHex, ParsedName))
				UE_LOG(LogSWConnection, Warning, TEXT("Side=Server RoomRunId=%s Phase=DisplayName Result=Invalid"), *RoomRunId.ToString());
			NewPlayerController->PlayerState->SetPlayerName(ParsedName.IsEmpty() ? TEXT("Player") : ParsedName);
		}
        return InitError;
    }

    PlayerRoles.Remove(NewPlayerController);
    PlayerIndices.Remove(NewPlayerController);
    PlayerReconnectTokens.Remove(NewPlayerController);
    ReadyPlayers.Remove(NewPlayerController);
    if (bHasReconnectToken && Progress)
    {
        Progress->CancelReconnectActivation(ReconnectToken, !bExistingReconnectRecord);
    }
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
    const bool bRoleAssigned = AssignRoleToPlayer(NewPlayer);
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

    FGuid ReconnectToken;
    if (!ParseReconnectToken(Options, ReconnectToken))
    {
        ErrorMessage = TEXT("InvalidReconnectToken");
        UE_LOG(LogSWConnection, Warning, TEXT("Side=Server RoomRunId=%s PreLogin Rejected Reason=InvalidReconnectToken"), *RoomRunId.ToString());
        return;
    }

    UPlayerProgressSubsystem* Progress = GetGameInstance()
        ? GetGameInstance()->GetSubsystem<UPlayerProgressSubsystem>()
        : nullptr;
    if (!Progress)
    {
        ErrorMessage = TEXT("ReconnectStateUnavailable");
        UE_LOG(LogSWConnection, Warning, TEXT("Side=Server RoomRunId=%s PreLogin Rejected Reason=ReconnectStateUnavailable"), *RoomRunId.ToString());
        return;
    }

    FSWReconnectRecord ExistingRecord;
    const bool bKnownToken = Progress->FindReconnectRecord(
        ReconnectToken,
        FPlatformTime::Seconds(),
        ExistingRecord);
    if (bKnownToken && ExistingRecord.bConnectionActive)
    {
        ErrorMessage = TEXT("DuplicateReconnectToken");
        UE_LOG(LogSWConnection, Warning, TEXT("Side=Server RoomRunId=%s PreLogin Rejected Reason=DuplicateReconnectToken"), *RoomRunId.ToString());
        return;
    }

    if (!bKnownToken && FindAvailablePlayerIndex() == INDEX_NONE)
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
	const FGuid ReconnectToken = Exiting && PlayerReconnectTokens.Contains(Exiting)
		? PlayerReconnectTokens.FindChecked(Exiting)
		: FGuid();
	bool bSnapshotStored = false;
	if (Exiting && ReconnectToken.IsValid())
	{
		if (APawn* ExitingPawn = Exiting->GetPawn())
		{
			if (UFunction* CaptureFunction = ExitingPawn->FindFunction(TEXT("CaptureReconnectProgress")))
			{
				ExitingPawn->ProcessEvent(CaptureFunction, nullptr);
				bSnapshotStored = true;
			}
		}
		if (!bSnapshotStored)
		{
			if (UPlayerProgressSubsystem* Progress = GetGameInstance()
				? GetGameInstance()->GetSubsystem<UPlayerProgressSubsystem>()
				: nullptr)
			{
				Progress->MarkReconnectDisconnected(
					ReconnectToken,
					ReleasedPlayerIndex,
					FPlatformTime::Seconds(),
					ReconnectReservationSeconds);
			}
		}
	}

    if (Exiting)
    {
        PlayerRoles.Remove(Exiting);
        ReadyPlayers.Remove(Exiting);
		PlayerIndices.Remove(Exiting);
		PlayerReconnectTokens.Remove(Exiting);
		FinishedDeadPlayers.Remove(Exiting);
		if (FTimerHandle* Timer = RespawnTimers.Find(Exiting)) GetWorldTimerManager().ClearTimer(*Timer);
		RespawnTimers.Remove(Exiting);
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
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UPlayerProgressSubsystem* Progress = GI->GetSubsystem<UPlayerProgressSubsystem>(); Progress && Progress->HasSnapshot(PlayerIndex))
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
	const FGuid* ReconnectToken = PlayerReconnectTokens.Find(Controller);
	const UPlayerProgressSubsystem* Progress = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UPlayerProgressSubsystem>()
		: nullptr;
	FSWPlayerProgressSnapshot Snapshot;
	if (!ReconnectToken || !Progress || !Progress->PeekReconnectSnapshot(*ReconnectToken, Snapshot))
	{
		return false;
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
			if (!NavigationSystem->ProjectPointToNavigation(
				CandidateTransform.GetLocation(),
				ProjectedLocation,
				FVector(100.0f, 100.0f, 200.0f)))
			{
				return false;
			}
			CandidateTransform.SetLocation(ProjectedLocation.Location);
		}
		else
		{
			return false;
		}

		if (IsReconnectTransformSafe(Controller, CandidateTransform))
		{
			OutTransform = CandidateTransform;
			return true;
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

void AMultiGameMode::NotifyPlayerDeathFinished(APawn* DeadPawn)
{
	if (!HasAuthority() || !DeadPawn) return;
	AController* DeadController = DeadPawn->GetController();
	if (!DeadController && DeadPawn->GetPlayerState()) DeadController = DeadPawn->GetPlayerState()->GetOwningController();
	if (!DeadController || !PlayerIndices.Contains(DeadController) || FinishedDeadPlayers.Contains(DeadController)) return;

	FinishedDeadPlayers.Add(DeadController);
	DeadController->UnPossess();
	DeadPawn->SetLifeSpan(FMath::Max(IndividualRespawnDelay + 2.0f, 10.0f));
	if (FinishedDeadPlayers.Num() >= RequiredPlayerCount)
	{
		for (TPair<TObjectPtr<AController>, FTimerHandle>& Pair : RespawnTimers) GetWorldTimerManager().ClearTimer(Pair.Value);
		RespawnTimers.Reset();
		HandleAllPlayersDeathFinished();
		return;
	}

	FTimerDelegate Delegate;
	Delegate.BindUObject(this, &AMultiGameMode::TryRespawnPlayer, DeadController);
	GetWorldTimerManager().SetTimer(RespawnTimers.FindOrAdd(DeadController), Delegate, IndividualRespawnDelay, false);
}

void AMultiGameMode::TryRespawnPlayer(AController* Controller)
{
	RespawnTimers.Remove(Controller);
	if (!Controller || !FinishedDeadPlayers.Contains(Controller)) return;
	UPlayerRespawnPointComponent* Point = FindShipRespawnPoint(GetPlayerIndex(Controller));
	if (!Point) return;
	FinishedDeadPlayers.Remove(Controller);
	RestartPlayerAtTransform(Controller, Point->GetComponentTransform());
}

UPlayerRespawnPointComponent* AMultiGameMode::FindShipRespawnPoint(int32 PlayerIndex) const
{
	UWorld* World = GetWorld();
	if (!World) return nullptr;
	UPlayerRespawnPointComponent* Fallback = nullptr;
	for (TObjectIterator<UPlayerRespawnPointComponent> It; It; ++It)
	{
		UPlayerRespawnPointComponent* Point = *It;
		if (!Point || Point->GetWorld() != World || !Point->IsRegistered()) continue;
		AActor* Host = Point->GetOwner();
		if (!Host || !Host->GetClass()->ImplementsInterface(URespawnHostInterface::StaticClass())
			|| !IRespawnHostInterface::Execute_IsAvailableForPlayerRespawn(Host)) continue;
		if (Point->PlayerSlot == static_cast<ESWPlayerSlot>(PlayerIndex)) return Point;
		if (Point->PlayerSlot == ESWPlayerSlot::Any) Fallback = Point;
	}
	return Fallback;
}

void AMultiGameMode::HandleAllPlayersDeathFinished()
{
	if (!HasAuthority()) return;
	RequestGameOverAndLevelRestart();
}

void AMultiGameMode::RequestGameOverAndLevelRestart()
{
	if (!HasAuthority() || bLevelRestartRequested) return;
	bLevelRestartRequested = true;
	for (TPair<TObjectPtr<AController>, FTimerHandle>& Pair : RespawnTimers)
	{
		GetWorldTimerManager().ClearTimer(Pair.Value);
	}
	RespawnTimers.Reset();
	CapturePlayerProgressForLevelRestart();
	OnGameOverRequested.Broadcast();
	if (UWorld* World = GetWorld())
	{
		World->ServerTravel(TEXT("?Restart"), false);
	}
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

    for (int32 CandidateIndex = 0; CandidateIndex < MaxPlayerCount; ++CandidateIndex)
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

        UPlayerProgressSubsystem* Progress = GetGameInstance()
            ? GetGameInstance()->GetSubsystem<UPlayerProgressSubsystem>()
            : nullptr;
        const bool bReserved = Progress
            && Progress->IsPlayerIndexReserved(CandidateIndex, FPlatformTime::Seconds());
        if (!bAlreadyUsed && !bReserved)
        {
            return CandidateIndex;
        }
    }

    return INDEX_NONE;
}

bool AMultiGameMode::AssignRoleToPlayer(AController* Controller, const FGuid* RequestedReconnectToken)
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
	bool bExistingReconnectRecord = false;
	UPlayerProgressSubsystem* Progress = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UPlayerProgressSubsystem>()
		: nullptr;
	if (RequestedReconnectToken && RequestedReconnectToken->IsValid() && Progress)
	{
		FSWReconnectRecord ExistingRecord;
		bExistingReconnectRecord = Progress->FindReconnectRecord(
			*RequestedReconnectToken,
			FPlatformTime::Seconds(),
			ExistingRecord);
		if (bExistingReconnectRecord && !ExistingRecord.bConnectionActive)
		{
			PlayerIndex = ExistingRecord.PlayerIndex;
		}
	}
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
	if (RequestedReconnectToken && RequestedReconnectToken->IsValid())
	{
		if (!Progress || !Progress->ActivateReconnectRecord(
			*RequestedReconnectToken,
			PlayerIndex,
			FPlatformTime::Seconds()))
		{
			PlayerRoles.Remove(Controller);
			PlayerIndices.Remove(Controller);
			return false;
		}
		PlayerReconnectTokens.Add(Controller, *RequestedReconnectToken);
	}

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

bool AMultiGameMode::ParseReconnectToken(const FString& Options, FGuid& OutReconnectToken) const
{
	const FString TokenString = UGameplayStatics::ParseOption(Options, TEXT("ReconnectToken"));
	return FGuid::Parse(TokenString, OutReconnectToken) && OutReconnectToken.IsValid();
}

bool AMultiGameMode::StoreReconnectSnapshotForController(
	AController* Controller,
	const FSWPlayerProgressSnapshot& Snapshot)
{
	const FGuid* ReconnectToken = PlayerReconnectTokens.Find(Controller);
	const int32 PlayerIndex = GetPlayerIndex(Controller);
	UPlayerProgressSubsystem* Progress = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UPlayerProgressSubsystem>()
		: nullptr;
	return ReconnectToken && PlayerIndex != INDEX_NONE && Progress
		&& Progress->StoreReconnectSnapshot(
			*ReconnectToken,
			PlayerIndex,
			Snapshot,
			FPlatformTime::Seconds(),
			ReconnectReservationSeconds);
}

bool AMultiGameMode::ConsumeReconnectSnapshotForController(
	AController* Controller,
	FSWPlayerProgressSnapshot& OutSnapshot)
{
	const FGuid* ReconnectToken = PlayerReconnectTokens.Find(Controller);
	UPlayerProgressSubsystem* Progress = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UPlayerProgressSubsystem>()
		: nullptr;
	return ReconnectToken && Progress
		&& Progress->ConsumeReconnectSnapshot(*ReconnectToken, OutSnapshot);
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
