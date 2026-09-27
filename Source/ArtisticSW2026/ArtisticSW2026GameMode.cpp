// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArtisticSW2026GameMode.h"


AArtisticSW2026GameMode::AArtisticSW2026GameMode()
{
	// stub
}

FString AArtisticSW2026GameMode::InitNewPlayer(
	APlayerController* NewPlayerController,
	const FUniqueNetIdRepl& UniqueId,
	const FString& Options,
	const FString& Portal)
{
	// Super::InitNewPlayer 내부에서 PlayerStart를 찾으므로 그 전에 순번을 확정해야 한다.
	AssignRoleToPlayer(NewPlayerController);
	return Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
}

UClass* AArtisticSW2026GameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	switch (GetPlayerIndex(InController))
	{
	case 0:
		if (FirstPlayerPawnClass)
		{
			return FirstPlayerPawnClass;
		}
		break;

	case 1:
		if (SecondPlayerPawnClass)
		{
			return SecondPlayerPawnClass;
		}
		break;

	default:
		break;
	}

	// 순번별 클래스가 비어 있으면 기존 CommonPlayerPawnClass / DefaultPawnClass 설정을 유지한다.
	return Super::GetDefaultPawnClassForController_Implementation(InController);
}
