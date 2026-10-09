#include "ShipAI/EnemyShipDebugCommands.h"

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
#include "ShipAI/EnemyShip.h"
#include "BasePlayerController.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogEnemyShipDebug, Log, All);

namespace EnemyShipDebug
{
void TeleportPlayer(const TArray<FString>& Args, UWorld* World)
{
	if (!World || !World->IsGameWorld())
	{
		UE_LOG(LogEnemyShipDebug, Warning, TEXT("Run in a player game window."));
		return;
	}
	if (Args.Num() != 2 || Args[0].IsEmpty() || Args[1].IsEmpty())
	{
		UE_LOG(LogEnemyShipDebug, Warning, TEXT("Usage: SW.EnemyShip.Teleport <ShipActorTag> <ArrivalComponentTag>"));
		return;
	}

	APlayerController* Controller = nullptr;
	int32 ControllerCount = 0;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* Candidate = It->Get(); Candidate && Candidate->IsLocalPlayerController())
		{
			Controller = Candidate;
			++ControllerCount;
		}
	}
	ABasePlayerController* Requester = Cast<ABasePlayerController>(Controller);
	if (ControllerCount != 1 || !Requester)
	{
		UE_LOG(LogEnemyShipDebug, Warning, TEXT("Requires exactly one local BasePlayerController."));
		return;
	}
	Requester->RequestEnemyShipDebugTeleport(Args[0], Args[1]);
}

FString ExecuteTeleport(APlayerController* Controller, const FString& ShipTag, const FString& ArrivalTag)
{
	UWorld* World = IsValid(Controller) ? Controller->GetWorld() : nullptr;
	if (!World || !World->IsGameWorld() || !Controller->HasAuthority() || World->GetNetMode() == NM_Client)
	{
		return TEXT("Teleport must execute on the requesting player's server controller.");
	}
	ACharacter* Player = Cast<ACharacter>(Controller->GetPawn());
	UCharacterMovementComponent* Movement = Player ? Player->GetCharacterMovement() : nullptr;
	if (!Player || !Player->HasAuthority() || Player->GetController() != Controller || !Movement
		|| Movement->MovementMode == MOVE_None || Player->GetAttachParentActor())
	{
		return TEXT("Requires your possessed, movable character. Exit the helm/cannon before teleporting.");
	}

	AEnemyShip* Ship = nullptr;
	int32 ShipCount = 0;
	for (TActorIterator<AEnemyShip> It(World); It; ++It)
	{
		if (It->ActorHasTag(FName(*ShipTag)))
		{
			Ship = *It;
			++ShipCount;
		}
	}
	if (ShipCount != 1 || !IsValid(Ship))
	{
		return FString::Printf(TEXT("Expected one enemy ship with Actor Tag '%s'; found %d."), *ShipTag, ShipCount);
	}
	if (Ship->IsDeathHandled() || Ship->IsSinking() || Ship->IsStoryGateDormant()
		|| Ship->IsDistanceOptimizationDormant() || !Ship->GetActorEnableCollision())
	{
		return TEXT("Ship is inactive, dormant or sinking. Activate/approach the ship before teleporting.");
	}

	TArray<USceneComponent*> Components;
	Ship->GetComponents(Components);
	USceneComponent* Arrival = nullptr;
	int32 ArrivalCount = 0;
	for (USceneComponent* Component : Components)
	{
		if (IsValid(Component) && Component->IsRegistered() && Component->ComponentHasTag(FName(*ArrivalTag)))
		{
			Arrival = Component;
			++ArrivalCount;
		}
	}
	if (ArrivalCount != 1 || Arrival->GetComponentTransform().ContainsNaN())
	{
		return FString::Printf(TEXT("Expected one registered scene component with Component Tag '%s'; found %d."), *ArrivalTag, ArrivalCount);
	}

	// Authored at capsule-center height, parented to the moving deck mesh.
	const FVector Destination = Arrival->GetComponentLocation();
	const FRotator Facing(0.f, Arrival->GetComponentRotation().Yaw, 0.f);
	if (!Player->TeleportTo(Destination, Facing, false, false))
	{
		return FString::Printf(TEXT("Teleport failed: check capsule clearance at %s."), *Arrival->GetPathName());
	}

	Movement->StopMovementImmediately();
	Movement->ClearAccumulatedForces();
	Player->SetBase(nullptr);
	// Let normal landing find the actual deck collision and establish its movement base.
	Movement->SetMovementMode(MOVE_Falling);
	Controller->SetControlRotation(Facing);
	Controller->ClientSetRotation(Facing, true);
	Player->ForceNetUpdate();
	UE_LOG(LogEnemyShipDebug, Display, TEXT("Teleported %s to %s / %s at %s."),
		*Player->GetName(), *Ship->GetName(), *Arrival->GetName(), *Player->GetActorLocation().ToString());
	return FString::Printf(TEXT("Teleported %s to %s / %s."), *Player->GetName(), *Ship->GetName(), *Arrival->GetName());
}

FAutoConsoleCommandWithWorldAndArgs TeleportCommand(
	TEXT("SW.EnemyShip.Teleport"),
	TEXT("Ask the server to teleport your player: SW.EnemyShip.Teleport <ShipActorTag> <ArrivalComponentTag>. Development only."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TeleportPlayer));
}
#endif
