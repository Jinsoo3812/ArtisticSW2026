// Fill out your copyright notice in the Description page of Project Settings.


#include "MultiGameMode.h"

#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "PlayerRespawnPointComponent.h"
#include "PlayerRespawnPoint.h"
#include "PlayerProgressSubsystem.h"
#include "RespawnHostInterface.h"
#include "TimerManager.h"

AMultiGameMode::AMultiGameMode()
{
    RequiredPlayerCount = 2;
    MaxPlayerCount = 2;
    bRequireAllPlayersReady = true;
    bAutoReadyOnPostLogin = false;
}

void AMultiGameMode::PostLogin(APlayerController* NewPlayer)
{
    if (!NewPlayer)
    {
        return;
    }

    // Super::PostLogin 내부에서 PawnClass / PlayerStart를 물어볼 수 있으므로
    // 역할 배정은 Super 호출 전에 끝내는 것이 안전하다.
    AssignRoleToPlayer(NewPlayer);

    if (bAutoReadyOnPostLogin)
    {
        ReadyPlayers.Add(NewPlayer);
    }

    Super::PostLogin(NewPlayer);

    const FName AssignedRole = GetPlayerRole(NewPlayer);
    const int32 PlayerIndex = GetPlayerIndex(NewPlayer);

    OnPlayerRoleAssigned.Broadcast(NewPlayer, AssignedRole, PlayerIndex);

    if (bAutoReadyOnPostLogin)
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
    Super::PreLogin(Options, Address, UniqueId, ErrorMessage);

    if (!ErrorMessage.IsEmpty())
    {
        return;
    }

    if (MaxPlayerCount > 0 && GetNumPlayers() >= MaxPlayerCount)
    {
        ErrorMessage = TEXT("ServerIsFull");
    }
}

void AMultiGameMode::Logout(AController* Exiting)
{
    if (Exiting)
    {
        PlayerRoles.Remove(Exiting);
        ReadyPlayers.Remove(Exiting);
		PlayerIndices.Remove(Exiting);
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

void AMultiGameMode::AssignRoleToPlayer(AController* Controller)
{
	if (!Controller)
	{
		return;
	}

	if (PlayerRoles.Contains(Controller))
	{
		return;
	}

	const int32 PlayerIndex = PlayerRoles.Num();
	const FName AssignedRole = GetRoleForPlayerIndex(PlayerIndex);

	PlayerRoles.Add(Controller, AssignedRole);
	PlayerIndices.Add(Controller, PlayerIndex);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[ASWMultiGameMode] Role assigned. Controller=%s Role=%s PlayerIndex=%d"),
		*GetNameSafe(Controller),
		*AssignedRole.ToString(),
		PlayerIndex
	);
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
