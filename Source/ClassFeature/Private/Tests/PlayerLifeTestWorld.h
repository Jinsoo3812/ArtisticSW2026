#pragma once

#if WITH_DEV_AUTOMATION_TESTS
#include "BaseAttributeSet.h"
#include "BasePlayer.h"
#include "BasePlayerController.h"
#include "BasePlayerState.h"
#include "Components/BaseHealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "MultiGameMode.h"
#include "Settings_Item.h"
#include "UObject/StrongObjectPtr.h"

namespace PlayerLifeTests
{
struct FLife
{
	ABasePlayerController* Controller = nullptr;
	ABasePlayerState* State = nullptr;
	ABasePlayer* Player = nullptr;
};

/** Runs actual possession/death handlers without map content, travel, or BeginPlay. */
struct FWorld
{
	TGuardValue<TSoftObjectPtr<UDataTable>> CraftingTableGuard;
	TStrongObjectPtr<UWorld> World;
	TStrongObjectPtr<UGameInstance> GameInstance;
	AMultiGameMode* Mode = nullptr;

	FWorld()
		: CraftingTableGuard(GetMutableDefault<USettings_Item>()->CraftingRecipeDataTable, {})
		, World(UWorld::CreateWorld(EWorldType::Game, false))
		, GameInstance(NewObject<UGameInstance>(GEngine))
	{
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.SetCurrentWorld(World.Get());
		Context.OwningGameInstance = GameInstance.Get();
		World->SetGameInstance(GameInstance.Get());
		FURL URL;
		URL.AddOption(TEXT("game=/Script/ArtisticSWCore.MultiGameMode"));
		World->SetGameMode(URL);
		World->InitializeActorsForPlay(URL);
		Mode = World->GetAuthGameMode<AMultiGameMode>();
	}

	~FWorld()
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World.Get());
	}

	ABasePlayer* SpawnPlayer()
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ABasePlayer* Player = World->SpawnActor<ABasePlayer>(Spawn);
		if (Player)
		{
			Player->GetMesh()->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,
				TEXT("/Game/jiwon/Characters/SKM_Player_Woman.SKM_Player_Woman")));
			Player->GetHealthComponent()->OnDeathFinished.AddUniqueDynamic(Player, &ABasePlayer::HandleDeathFinished);
		}
		return Player;
	}

	FLife SpawnLife()
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		FLife Life;
		Life.Controller = World->SpawnActor<ABasePlayerController>(Spawn);
		Life.State = World->SpawnActor<ABasePlayerState>(Spawn);
		Life.Player = SpawnPlayer();
		if (Life.Controller && Life.State && Life.Player)
		{
			Life.State->GetAbilitySystemComponent()->AddAttributeSetSubobject(Life.State->GetAttributeSet());
			Life.Controller->PlayerState = Life.State;
			Life.Controller->Possess(Life.Player);
		}
		return Life;
	}
};
}
#endif
