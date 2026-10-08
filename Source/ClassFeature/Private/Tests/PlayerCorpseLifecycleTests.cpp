#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "PlayerLifeTestWorld.h"
#include "BaseAttributeSet.h"
#include "TimerManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerCorpseLifecycleTest,
	"ArtisticSW.GameMode.PlayerCorpseLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerCorpseLifecycleTest::RunTest(const FString& Parameters)
{
	PlayerLifeTests::FWorld Fixture;
	AMultiGameMode* Mode = Fixture.Mode;
	const PlayerLifeTests::FLife Life = Fixture.SpawnLife();
	if (!TestNotNull(TEXT("Mode"), Mode) || !Life.Controller || !Life.State || !Life.Player) return false;
	Mode->PlayerIndices.Add(Life.Controller, 0);
	Life.State->GetAbilitySystemComponent()->SetNumericAttributeBase(UBaseAttributeSet::GetHealthAttribute(), 0);
	TestNull(TEXT("Death registration releases possession after snapshot"), Life.Controller->GetPawn());
	TestTrue(TEXT("Death progress survives independently of corpse"), Life.Controller->HasPendingLifeProgress());
	TestEqual(TEXT("Master corpse lifetime retained"), Life.Player->GetLifeSpan(), AMultiGameMode::IndividualRespawnDelay);
	TestEqual(TEXT("One record per dead controller"), Mode->FinishedDeadPlayers.Num(), 1);
	const int32 Generation = Mode->DeathFlowStates.FindChecked(Life.Controller).WaitingGeneration;
	const FTimerHandle OriginalTimer = Mode->RespawnTimers.FindChecked(Life.Controller);
	Mode->NotifyPlayerDeathFinished(Life.Player);
	TestEqual(TEXT("Duplicate death does not advance waiting generation"), Mode->DeathFlowStates.FindChecked(Life.Controller).WaitingGeneration, Generation);
	Mode->TryRespawnPlayer(Life.Controller, Generation - 1);
	TestTrue(TEXT("Stale callback does not replace current timer"), Mode->RespawnTimers.FindChecked(Life.Controller) == OriginalTimer);
	Mode->TryRespawnPlayer(Life.Controller, Generation);
	TestTrue(TEXT("Missing ship point schedules retry"), Fixture.World->GetTimerManager().IsTimerActive(Mode->RespawnTimers.FindChecked(Life.Controller)));
	TestTrue(TEXT("Failed respawn preserves captured progress"), Life.Controller->HasPendingLifeProgress());
	TestTrue(TEXT("Player death alone does not cause game over"), Mode->GetSessionLifePhase() == ESWSessionLifePhase::Playing);

	const PlayerLifeTests::FLife OtherLife = Fixture.SpawnLife();
	if (!OtherLife.Controller || !OtherLife.State || !OtherLife.Player) return false;
	Mode->PlayerIndices.Add(OtherLife.Controller, 1);
	OtherLife.State->GetAbilitySystemComponent()->SetNumericAttributeBase(UBaseAttributeSet::GetHealthAttribute(), 0);
	TestEqual(TEXT("Both dead players keep separate records"), Mode->FinishedDeadPlayers.Num(), 2);
	TestTrue(TEXT("Both dead players still use ship lifecycle policy"), Mode->GetSessionLifePhase() == ESWSessionLifePhase::Playing);
	const FTimerHandle LogoutTimer = Mode->RespawnTimers.FindChecked(Life.Controller);
	Mode->Logout(Life.Controller);
	TestFalse(TEXT("Disconnect clears only the disconnected retry"), Fixture.World->GetTimerManager().TimerExists(LogoutTimer));
	TestFalse(TEXT("Disconnect removes dead-player state"), Mode->DeathFlowStates.Contains(Life.Controller));
	TestTrue(TEXT("Other player retry remains active"), Fixture.World->GetTimerManager().IsTimerActive(Mode->RespawnTimers.FindChecked(OtherLife.Controller)));
	AActor* Ship = Fixture.World->SpawnActor<AActor>();
	Mode->PlayerRespawnShip = Ship;
	Mode->NotifyPlayerShipSinking(Ship);
	TestTrue(TEXT("Sinking cancels all respawn timers"), Mode->RespawnTimers.IsEmpty());
	TestTrue(TEXT("Sinking starts separate session phase"), Mode->GetSessionLifePhase() == ESWSessionLifePhase::ShipSinking);
	Mode->TryRespawnPlayer(OtherLife.Controller, Mode->DeathFlowStates.FindChecked(OtherLife.Controller).WaitingGeneration);
	TestTrue(TEXT("Late retry cannot restart during sinking"), Mode->RespawnTimers.IsEmpty());
	TestTrue(TEXT("Captured inventory survives sinking"), OtherLife.Controller->HasPendingLifeProgress());
	return !HasAnyErrors();
}
#endif
