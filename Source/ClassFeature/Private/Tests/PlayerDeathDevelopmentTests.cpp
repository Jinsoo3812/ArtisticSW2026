#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "PlayerLifeTestWorld.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "Development/TestInput/SWDevTestInputComponent.h"
#include "HAL/IConsoleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerDeathDevelopmentTest,
	"ArtisticSW.Player.Death.LifeOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerDeathDevelopmentTest::RunTest(const FString& Parameters)
{
	PlayerLifeTests::FWorld Fixture;
	const PlayerLifeTests::FLife Life = Fixture.SpawnLife();
	if (!TestNotNull(TEXT("Mode"), Fixture.Mode) || !Life.Controller || !Life.State || !Life.Player) return false;
	Fixture.Mode->PlayerIndices.Add(Life.Controller, 0);
	UAbilitySystemComponent* ASC = Life.State->GetAbilitySystemComponent();
	UBaseHealthComponent* OldHealth = Life.Player->GetHealthComponent();
	USWDevTestInputComponent* Input = Life.Controller->DevTestInput;
	const int32 OriginalGeneration = Life.Controller->DeathFlowState.WaitingGeneration;
	FSWInventorySlotSnapshot Seed;
	Seed.Tab = static_cast<uint8>(EInventoryTab::Material);
	Seed.SlotIndex = 3;
	Seed.ItemTag = Item_Id_Material_WeaponMaterial_Wood;
	Seed.Count = 7;
	Life.Player->GetInventoryComponent()->RestoreProgressSnapshot({Seed});
	Input->ServerExecuteTest(ESWDevTestAction::KillSelf, 0, 1, Life.Player, OriginalGeneration);
	TestEqual(TEXT("Server rejects development death without an enabled room session"), Life.Player->GetHealthComponent()->GetHealth(), 100.0f);
	TestTrue(TEXT("Current life request matches"), Input->MatchesCurrentLife(Life.Player, OriginalGeneration));
	TestFalse(TEXT("Null life request rejected"), Input->MatchesCurrentLife(nullptr, OriginalGeneration));
	ABasePlayer* Replacement = Fixture.SpawnPlayer();
	if (!TestNotNull(TEXT("Replacement"), Replacement)) return false;
	TestFalse(TEXT("Another pawn cannot be selected for suicide"), Input->MatchesCurrentLife(Replacement, OriginalGeneration));
	const float Strength = ASC->GetNumericAttributeBase(UBaseAttributeSet::GetStrengthAttribute());
	ASC->SetNumericAttributeBase(UBaseAttributeSet::GetHealthAttribute(), 0.0f);
	TestTrue(TEXT("Existing death pipeline completes"), OldHealth->GetDeathState() == EBaseDeathState::DeathFinished);
	TestTrue(TEXT("Death tag remains on persistent ASC until new life"), ASC->HasMatchingGameplayTag(State_Dead));
	TestNull(TEXT("GameMode releases possession after capture"), Life.Controller->GetPawn());
	TestFalse(TEXT("Corpse no longer subscribes to shared health"),
		ASC->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetHealthAttribute()).IsBoundToObject(OldHealth));
	const FGameplayTag InteractionTags[] = {Interaction_PickUp, Interaction_ShipBoard, Interaction_CannonBoard};
	for (const FGameplayTag& Tag : InteractionTags)
		if (const auto* Callback = ASC->GenericGameplayEventCallbacks.Find(Tag))
			TestFalse(TEXT("Corpse interaction callback retired"), Callback->IsBoundToObject(Life.Player));
	TestFalse(TEXT("A request queued before death is rejected by life generation"),
		Input->MatchesCurrentLife(Life.Player, OriginalGeneration));

	// Exercise individual-respawn possession with the real captured snapshot.
	Fixture.Mode->IndividualRespawnInProgress.Add(Life.Controller);
	Life.Controller->Possess(Replacement);
	Fixture.Mode->IndividualRespawnInProgress.Remove(Life.Controller);
	TestTrue(TEXT("New life applies the captured progress successfully"), Life.Controller->WasLastLifeProgressApplySuccessful(Replacement));
	TestTrue(TEXT("New life leaves initialization with positive health"),
		!Replacement->GetHealthComponent()->IsLifeInitializing() && !Replacement->GetHealthComponent()->IsDead()
		&& Replacement->GetHealthComponent()->GetHealth() > 0);
	TestFalse(TEXT("New life clears persistent dead tag"), ASC->HasMatchingGameplayTag(State_Dead));
	TestEqual(TEXT("Respawn preserves strength"), ASC->GetNumericAttributeBase(UBaseAttributeSet::GetStrengthAttribute()), Strength);
	TestEqual(TEXT("Respawn restores a nonempty inventory"), Replacement->GetInventoryComponent()->GetItemCount(Seed.ItemTag), Seed.Count);
	TestEqual(TEXT("Respawn preserves original material slot"), Replacement->GetInventoryComponent()->GetSlots(EInventoryTab::Material)[Seed.SlotIndex].Count, Seed.Count);
	Life.Player->SetPlayerState(Life.State);
	Life.Player->OnRep_PlayerState();
	TestEqual(TEXT("Late corpse PlayerState cannot reclaim the ASC"), ASC->GetAvatarActor(), static_cast<AActor*>(Replacement));
	TestFalse(TEXT("Late corpse PlayerState cannot bind the retired health component"),
		ASC->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetHealthAttribute()).IsBoundToObject(OldHealth));
	TestTrue(TEXT("Replacement health remains subscribed"),
		ASC->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetHealthAttribute()).IsBoundToObject(Replacement->GetHealthComponent()));
	for (const FGameplayTag& Tag : InteractionTags)
	{
		const auto* Callback = ASC->GenericGameplayEventCallbacks.Find(Tag);
		TestTrue(TEXT("Late corpse update preserves replacement interaction binding"), Callback && Callback->IsBoundToObject(Replacement));
	}
	Life.Player->HandleDeathFinished(OldHealth);
	TestTrue(TEXT("Duplicate corpse death leaves the replacement possessed"), Life.Controller->GetPawn() == Replacement);
	TestFalse(TEXT("Old-pawn request cannot target replacement"),
		Input->MatchesCurrentLife(Life.Player, Life.Controller->DeathFlowState.WaitingGeneration));
	TestTrue(TEXT("New-pawn request uses the new life"),
		Input->MatchesCurrentLife(Replacement, Life.Controller->DeathFlowState.WaitingGeneration));
	Life.Player->Destroy();
	TestEqual(TEXT("Corpse destruction leaves the living ASC avatar intact"), ASC->GetAvatarActor(), static_cast<AActor*>(Replacement));
	TestTrue(TEXT("Corpse destruction preserves new health binding"),
		ASC->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetHealthAttribute()).IsBoundToObject(Replacement->GetHealthComponent()));
	TestEqual(TEXT("Corpse destruction cannot remove restored inventory"), Replacement->GetInventoryComponent()->GetItemCount(Seed.ItemTag), Seed.Count);
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	TestNotNull(TEXT("Existing development session command"), IConsoleManager::Get().FindConsoleObject(TEXT("SW.DevTest.Session")));
	TestNull(TEXT("Legacy global session variable removed"), IConsoleManager::Get().FindConsoleVariable(TEXT("SW.DevTest.Session")));
	TestNotNull(TEXT("Suicide alias registered"), IConsoleManager::Get().FindConsoleObject(TEXT("sw.DevTest.Suicide")));
#endif
	return !HasAnyErrors();
}
#endif
