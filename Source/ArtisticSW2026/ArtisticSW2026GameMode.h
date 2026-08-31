// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MultiGameMode.h"
#include "ArtisticSW2026GameMode.generated.h"

class AController;
class APawn;

/**
 *  Simple GameMode for a third person game
 */
UCLASS(abstract)
class AArtisticSW2026GameMode : public AMultiGameMode
{
	GENERATED_BODY()

public:
	
	/** Constructor */
	AArtisticSW2026GameMode();

	/** 접속 순서가 첫 번째인 플레이어에게 사용할 Pawn 클래스 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Multiplayer|Player Pawns")
	TSubclassOf<APawn> FirstPlayerPawnClass;

	/** 접속 순서가 두 번째인 플레이어에게 사용할 Pawn 클래스 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Multiplayer|Player Pawns")
	TSubclassOf<APawn> SecondPlayerPawnClass;

protected:
	/** PlayerStart가 선택되기 전에 접속 순번을 배정한다. */
	virtual FString InitNewPlayer(
		APlayerController* NewPlayerController,
		const FUniqueNetIdRepl& UniqueId,
		const FString& Options,
		const FString& Portal) override;

	/** AMultiGameMode가 PostLogin 전에 배정한 플레이어 인덱스로 Pawn 클래스를 선택한다. */
	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;
};



