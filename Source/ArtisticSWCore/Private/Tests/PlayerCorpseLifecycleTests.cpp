#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "MultiGameMode.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Settings_Item.h"
#include "TimerManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerCorpseLifecycleTest,
	"ArtisticSW.GameMode.PlayerCorpseLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerCorpseLifecycleTest::RunTest(const FString& Parameters)
{
	// Keep this lifecycle test independent of unrelated crafting-table content validation.
	TGuardValue<TSoftObjectPtr<UDataTable>> CraftingTableGuard(GetMutableDefault<USettings_Item>()->CraftingRecipeDataTable, {});
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("PlayerCorpseLifecycleTest"));
	if (!TestNotNull(TEXT("Corpse test world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->InitializeActorsForPlay(FURL());
	AMultiGameMode* Mode = World->SpawnActor<AMultiGameMode>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	APawn* Corpse = World->SpawnActor<APawn>();
	APawn* Replacement = World->SpawnActor<APawn>();
	if (!Mode || !Controller || !Corpse || !Replacement) return false;
	Mode->RequiredPlayerCount = 2;
	Mode->PlayerIndices.Add(Controller, 0);
	Controller->Possess(Corpse);
	Corpse->SetLifeSpan(1.0f); // Registration must cancel any earlier cleanup policy.
	Mode->NotifyPlayerDeathFinished(Corpse);
	TestNull(TEXT("Death registration releases possession"), Controller->GetPawn());
	TestEqual(TEXT("Waiting corpse has no destructive lifespan"), Corpse->GetLifeSpan(), 0.0f);
	TestEqual(TEXT("One record per dead controller"), Mode->PendingPlayerRespawns.Num(), 1);
	Mode->NotifyPlayerDeathFinished(Corpse);
	TestEqual(TEXT("Duplicate completion does not add waiting records"), Mode->PendingPlayerRespawns.Num(), 1);
	Mode->TryRespawnPlayer(Controller); // No available host in this world.
	TestTrue(TEXT("Missing spawn point schedules retry"), World->GetTimerManager().IsTimerActive(Mode->PendingPlayerRespawns[Controller].Timer));
	TestFalse(TEXT("Missing spawn point preserves the observed corpse"), Corpse->IsActorBeingDestroyed());
	TestEqual(TEXT("Waiting is not converted into corpse expiry"), Corpse->GetLifeSpan(), 0.0f);
	Mode->IndividualRespawnDelay = 0.0f;
	Mode->SchedulePlayerRespawn(Controller, 0.0f);
	TestTrue(TEXT("Zero delay is scheduled for next tick"), Mode->PendingPlayerRespawns[Controller].Timer.IsValid());
	Mode->PendingPlayerRespawns[Controller].DeathFinishedTime = World->GetTimeSeconds() - 20.0;
	Controller->Possess(Replacement); // An externally completed respawn uses the same cleanup path.
	Mode->TryRespawnPlayer(Controller);
	TestEqual(TEXT("Successful possession completes waiting"), Mode->PendingPlayerRespawns.Num(), 0);
	TestEqual(TEXT("Cleanup honors minimum time since death"), Corpse->GetLifeSpan(), 10.0f);
	TestTrue(TEXT("Cleanup does not remove the replacement pawn"), Controller->GetPawn() == Replacement);

	APlayerController* WaitingController = World->SpawnActor<APlayerController>();
	APawn* WaitingCorpse = World->SpawnActor<APawn>();
	if (!WaitingController || !WaitingCorpse) return false;
	Mode->PlayerIndices.Add(WaitingController, 1);
	WaitingController->Possess(WaitingCorpse);
	Mode->NotifyPlayerDeathFinished(WaitingCorpse);
	Mode->PendingPlayerRespawns[WaitingController].DeathFinishedTime = World->GetTimeSeconds() - 100.0;
	APawn* LateReplacement = World->SpawnActor<APawn>();
	WaitingController->Possess(LateReplacement);
	Mode->TryRespawnPlayer(WaitingController);
	TestEqual(TEXT("Long wait still leaves a post-respawn grace period"), WaitingCorpse->GetLifeSpan(), Mode->CorpseLifetimeAfterRespawn);
	Mode->PlayerIndices.Add(Controller, 0);
	Controller->Possess(Corpse);
	Mode->NotifyPlayerDeathFinished(Corpse);
	const FTimerHandle PendingTimer = Mode->PendingPlayerRespawns[Controller].Timer;
	Mode->CancelPlayerRespawn(Controller, true);
	TestFalse(TEXT("Disconnect cleanup clears retry timer"), World->GetTimerManager().TimerExists(PendingTimer));
	TestTrue(TEXT("Disconnect cleanup destroys its waiting corpse"), Corpse->IsActorBeingDestroyed());
	TestFalse(TEXT("Disconnect cleanup preserves other corpses"), WaitingCorpse->IsActorBeingDestroyed());
	TestEqual(TEXT("No waiting record leaks after cancellation"), Mode->PendingPlayerRespawns.Num(), 0);
	return !HasAnyErrors();
}

#endif
