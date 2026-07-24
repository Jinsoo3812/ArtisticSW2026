#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "BasePlayerState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Skills/Abilities/GA_GravityVortexThrow.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGravityVortexQuickSlotToggleTest,
	"ArtisticSW.GravityVortex.QuickSlotToggle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGravityVortexQuickSlotToggleTest::RunTest(const FString& Parameters)
{
	const UGA_GravityVortexThrow* AbilityDefaults = GetDefault<UGA_GravityVortexThrow>();
	TestEqual(
		TEXT("Gravity Vortex uses one persistent ability instance per player"),
		AbilityDefaults->GetInstancingPolicy(),
		EGameplayAbilityInstancingPolicy::InstancedPerActor);
	TestEqual(
		TEXT("Gravity Vortex is locally predicted and confirmed by the server"),
		AbilityDefaults->GetNetExecutionPolicy(),
		EGameplayAbilityNetExecutionPolicy::LocalPredicted);

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GravityVortexQuickSlotWorld"));
	if (!TestNotNull(TEXT("Transient game world is created"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	auto CleanupWorld = [World]()
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};

	ABasePlayerState* PlayerState = World->SpawnActor<ABasePlayerState>();
	APlayerController* PlayerController = World->SpawnActor<APlayerController>();
	ABasePlayer* Player = World->SpawnActor<ABasePlayer>();
	if (!TestNotNull(TEXT("PlayerState is spawned"), PlayerState)
		|| !TestNotNull(TEXT("PlayerController is spawned"), PlayerController)
		|| !TestNotNull(TEXT("Player is spawned"), Player))
	{
		CleanupWorld();
		return false;
	}

	Player->bBypassSkillRequirementsForTesting = true;
	Player->SetPlayerState(PlayerState);
	PlayerController->Possess(Player);
	UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("Player ASC is initialized"), ASC))
	{
		CleanupWorld();
		return false;
	}

	Player->OnGravityVortexSkillPressed();
	TestTrue(
		TEXT("One press enters Gravity Vortex aiming mode"),
		ASC->HasMatchingGameplayTag(GameplayAbility_Skill_GravityVortex));

	Player->OnGravityVortexSkillPressed();
	TestFalse(
		TEXT("A second press cancels Gravity Vortex aiming mode"),
		ASC->HasMatchingGameplayTag(GameplayAbility_Skill_GravityVortex));

	CleanupWorld();
	return true;
}

#endif
