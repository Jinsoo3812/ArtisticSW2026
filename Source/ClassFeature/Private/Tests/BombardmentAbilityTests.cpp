#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Attacker/GA_Bombardment.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "BasePlayerState.h"
#include "Bombardment.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/InventoryComponent.h"
#include "Ship.h"
#include "Skills/PlayerSkillComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBombardmentAbilityConfigurationTest,
	"ArtisticSW.Bombardment.GameplayAbilityConfiguration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBombardmentAbilityConfigurationTest::RunTest(const FString& Parameters)
{
	const UGA_Bombardment* Ability = GetDefault<UGA_Bombardment>();
	TestNotNull(TEXT("Bombardment GA CDO exists"), Ability);
	TestTrue(TEXT("Bombardment GA owns its activation tag"),
		Ability->GetAssetTags().HasTagExact(GameplayAbility_Skill_Bombardment));
	TestTrue(TEXT("Bombardment GA has an execution actor class"),
		Ability->BombardmentClass != nullptr);

	const ABasePlayer* Player = GetDefault<ABasePlayer>();
	TestTrue(TEXT("Player grants Bombardment by default"), Player->bGrantBombardmentAbility);
	TestTrue(TEXT("Player default Bombardment class is the native GA"),
		Player->BombardmentAbilityClass == UGA_Bombardment::StaticClass());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBombardmentShipModeIntegrationTest,
	"ArtisticSW.Bombardment.GameplayAbilityShipIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBombardmentShipModeIntegrationTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("BombardmentAbilityShipWorld"));
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
	AShip* Ship = World->SpawnActor<AShip>();
	if (!TestNotNull(TEXT("PlayerState is spawned"), PlayerState)
		|| !TestNotNull(TEXT("PlayerController is spawned"), PlayerController)
		|| !TestNotNull(TEXT("Player is spawned"), Player)
		|| !TestNotNull(TEXT("Ship is spawned"), Ship))
	{
		CleanupWorld();
		return false;
	}

	Player->SetPlayerState(PlayerState);
	PlayerController->Possess(Player);
	UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("Player ASC is initialized by possession"), ASC))
	{
		CleanupWorld();
		return false;
	}

	Ship->Board(Player);
	Ship->SetPlayerState(PlayerState);
	TestTrue(TEXT("Player controller possesses the ship"), Ship->IsPlayerControlled());
	TestTrue(TEXT("Ship tracks the riding player"), Ship->GetRidingPlayer() == Player);

	FGameplayTagContainer AbilityTags(GameplayAbility_Skill_Bombardment);
	UPlayerSkillComponent* SkillComponent = PlayerState->GetPlayerSkillComponent();
	TestNotNull(TEXT("Player skill component exists"), SkillComponent);
	TestFalse(TEXT("Locked Bombardment cannot activate"),
		ASC->TryActivateAbilitiesByTag(AbilityTags, true));
	TestTrue(TEXT("Bombardment is unlocked for the targeting test"),
		SkillComponent && SkillComponent->UnlockSkill(GameplayAbility_Skill_Bombardment));
	TestEqual(TEXT("One Bombardment material is added"),
		Player->GetInventoryComponent()->AddItem(Item_Id_Material_SkillMaterial_LegendarySkill, 1), 1);
	TestTrue(TEXT("Bombardment GA activates by ability tag"),
		ASC->TryActivateAbilitiesByTag(AbilityTags, true));
	TestTrue(TEXT("GA activation enters ship targeting mode"), Ship->IsBombardmentTargeting());

	ASC->CancelAbilities(&AbilityTags);
	TestFalse(TEXT("Cancelling the GA exits ship targeting mode"), Ship->IsBombardmentTargeting());

	CleanupWorld();
	return true;
}

#endif
