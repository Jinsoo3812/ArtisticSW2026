// Fill out your copyright notice in the Description page of Project Settings.


#include "MultiGameMode.h"

#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerController.h"

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
    const int32 PlayerIndex = PlayerRoles.Num() - 1;

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
    // 배정된 역할에 따라 다른 폰 클래스를 반환합니다.
    const FName RoleName = GetPlayerRole(InController);

    if (RoleName == AttackerRoleName && AttackerPawnClass)
    {
        return AttackerPawnClass;
    }

    if (RoleName == CrafterRoleName && CrafterPawnClass)
    {
        return CrafterPawnClass;
    }

	UE_LOG(LogTemp, Warning, TEXT("No role found for controller %s or corresponding pawn class not set."), *InController->GetName());
    // 예외 상황일 경우 기본 폰 반환
    return Super::GetDefaultPawnClassForController_Implementation(InController);
}

AActor* AMultiGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
    const FName RoleName = GetPlayerRole(Player);

    if (!RoleName.IsNone())
    {
        if (APlayerStart* RoleStart = FindPlayerStartByRole(RoleName))
        {
            return RoleStart;
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