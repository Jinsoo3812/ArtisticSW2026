#include "Development/SWEnemyShipDebugTeleport.h"

#include "BasePlayerController.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWEnemyShipDebugRequest, Log, All);

FSWEnemyShipDebugTeleport& SWEnemyShipDebug::GetTeleportHandler()
{
	static FSWEnemyShipDebugTeleport Handler;
	return Handler;
}

void ABasePlayerController::RequestEnemyShipDebugTeleport(const FString& ShipTag, const FString& ArrivalTag)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (IsLocalController())
	{
		ServerEnemyShipDebugTeleport(ShipTag, ArrivalTag);
	}
#endif
}

void ABasePlayerController::ServerEnemyShipDebugTeleport_Implementation(const FString& ShipTag, const FString& ArrivalTag)
{
// Keep reflected RPC declarations in every build, but never execute debug moves in release builds.
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (!HasAuthority() || !GetWorld() || !GetWorld()->IsGameWorld()) return;
	const double Now = FPlatformTime::Seconds();
	if (Now < NextEnemyShipDebugTeleportTime) return;
	NextEnemyShipDebugTeleportTime = Now + 0.25;
	if (ShipTag.TrimStartAndEnd().IsEmpty() || ArrivalTag.TrimStartAndEnd().IsEmpty()
		|| ShipTag.Len() > 128 || ArrivalTag.Len() > 128)
	{
		ClientEnemyShipDebugTeleportResult(TEXT("Tags must contain 1-128 characters."));
		return;
	}
	if (!CanMutateGameplay() || !IsLifeCharacterAlive() || IsDevelopmentTestInputBlockedByServerUI())
	{
		ClientEnemyShipDebugTeleportResult(TEXT("Teleport unavailable during death, transitions or active interactions."));
		return;
	}
	FSWEnemyShipDebugTeleport& Handler = SWEnemyShipDebug::GetTeleportHandler();
	ClientEnemyShipDebugTeleportResult(Handler.IsBound()
		? Handler.Execute(this, ShipTag, ArrivalTag)
		: TEXT("Enemy ship debug teleport handler is unavailable."));
#endif
}

void ABasePlayerController::ClientEnemyShipDebugTeleportResult_Implementation(const FString& Message)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	UE_LOG(LogSWEnemyShipDebugRequest, Display, TEXT("%s"), *Message);
	ClientMessage(Message);
#endif
}
