#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GAS/Ability/GA_PlayerRoll.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "SwimmingComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSwimmingExternalExitTest, "ArtisticSW.Swimming.ExternalExit.ClearsRollBlock",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSwimmingExternalExitTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false, TEXT("SwimmingExternalExitWorld"));
	if (!TestNotNull(TEXT("World"), World))
		return false;
	GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
	ACharacter* Character = World->SpawnActor<ACharacter>();
	USwimmingComponent* Swim = NewObject<USwimmingComponent>(Character);
	Swim->RegisterComponent();
	UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(Character);
	ASC->RegisterComponent();
	Character->DispatchBeginPlay();
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	// EditorPreview does not initialize movement automatically like a game world.
	if (!Movement->HasBeenInitialized())
		Movement->InitializeComponent();
	Movement->SetUpdatedComponent(Character->GetCapsuleComponent());
	TestTrue(TEXT("Fixture movement has a valid character and updated component"), Movement->HasValidData());
	const UGA_PlayerRoll* Roll = GetDefault<UGA_PlayerRoll>();
	for (int32 Cycle = 0; Cycle < 3; ++Cycle)
		for (const EMovementMode ExitMode : {MOVE_Walking, MOVE_Falling, MOVE_None})
		{
			Movement->SetMovementMode(MOVE_Custom, uint8(ECustomMovementMode::CMOVE_Swimming));
			Swim->ApplySwimmingGameplayState(true);
			Swim->SetRawVerticalSwimInput(true, false);
			TestFalse(TEXT("Swimming still blocks roll before an exit"), Roll->DoesAbilitySatisfyTagRequirements(*ASC));
			// Same mode change performed by sea boarding; no cabin mask or water-exit query.
			Movement->SetMovementMode(ExitMode);
			TestFalse(TEXT("An external movement exit clears the stale swimming blocker immediately"),
			          ASC->HasMatchingGameplayTag(State_Swimming));
			TestFalse(TEXT("External exit clears held swim input"), Swim->HasVerticalSwimInput());
			TestTrue(TEXT("Roll's swimming tag requirement recovers"), Roll->DoesAbilitySatisfyTagRequirements(*ASC));
			Swim->CheckWaterTransitions(0.f);
			TestFalse(TEXT("Repeated dry updates do not resurrect swimming"),
			          ASC->HasMatchingGameplayTag(State_Swimming));
		}
	ASC->AddLooseGameplayTag(State_Damaged);
	Movement->SetMovementMode(MOVE_Custom, uint8(ECustomMovementMode::CMOVE_Swimming));
	Swim->ApplySwimmingGameplayState(true);
	Movement->SetMovementMode(MOVE_Walking);
	TestTrue(TEXT("Exiting swimming preserves unrelated combat blockers"), ASC->HasMatchingGameplayTag(State_Damaged));
	TestFalse(TEXT("Being damaged still prevents rolling"), Roll->DoesAbilitySatisfyTagRequirements(*ASC));
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return !HasAnyErrors();
}
#endif
