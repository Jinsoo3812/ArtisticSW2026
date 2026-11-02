#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Skills/Abilities/GA_WaterBombCannonMode.h"
#include "AbilitySystemComponent.h"
#include "PlayerLifeTestWorld.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "BasePlayerState.h"
#include "Cannon.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/InventoryComponent.h"
#include "Skills/PlayerSkillComponent.h"
#include "WaterBombCannonball.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWaterBombAbilityConfigurationTest,
	"ArtisticSW.WaterBomb.GameplayAbilityConfiguration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWaterBombAbilityConfigurationTest::RunTest(const FString& Parameters)
{
	const UGA_WaterBombCannonMode* Ability = GetDefault<UGA_WaterBombCannonMode>();
	TestNotNull(TEXT("Water Bomb GA CDO exists"), Ability);
	TestTrue(TEXT("Water Bomb GA owns its activation tag"),
		Ability->GetAssetTags().HasTagExact(GameplayAbility_Skill_WaterBomb));
	TestTrue(TEXT("Water Bomb GA has a projectile class"), Ability->ProjectileClass != nullptr);
	TestTrue(TEXT("Configured projectile derives from AWaterBombCannonball"),
		Ability->ProjectileClass && Ability->ProjectileClass->IsChildOf(AWaterBombCannonball::StaticClass()));
	TestEqual(TEXT("Default effect duration comes from GA"), Ability->EffectDurationSeconds, 5.0f);
	TestEqual(TEXT("Default attack-speed multiplier comes from GA"), Ability->AttackSpeedMultiplier, 0.5f);

	const ABasePlayer* Player = GetDefault<ABasePlayer>();
	TestTrue(TEXT("Player grants Water Bomb ability by default"), Player->bGrantWaterBombAbility);
	TestTrue(TEXT("Player default Water Bomb class is the native GA"),
		Player->WaterBombAbilityClass == UGA_WaterBombCannonMode::StaticClass());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWaterBombAbilityCannonIntegrationTest,
	"ArtisticSW.WaterBomb.GameplayAbilityCannonIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWaterBombAbilityCannonIntegrationTest::RunTest(const FString& Parameters)
{
	// Use the real controller/life-ownership initialization. A bare PlayerController
	// cannot pass the server's CanMutateLifeGameplay policy after cannon possession.
	PlayerLifeTests::FWorld Fixture;
	const auto Life = Fixture.SpawnLife();
	UWorld* World = Fixture.World.Get();
	ABasePlayerState* PlayerState = Life.State;
	ABasePlayerController* PlayerController = Life.Controller;
	ABasePlayer* Player = Life.Player;
	ACannon* Cannon = World->SpawnActor<ACannon>();
	if (!TestNotNull(TEXT("Cannon is spawned"), Cannon)) return false;
	UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("Player ASC is initialized by possession"), ASC)) return false;

	FGameplayTagContainer AbilityTags(GameplayAbility_Skill_WaterBomb);
	bool bGranted = false;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->GetAssetTags().HasTagExact(GameplayAbility_Skill_WaterBomb))
		{
			bGranted = true;
			break;
		}
	}
	TestTrue(TEXT("Player possession grants the Water Bomb GA"), bGranted);

	Cannon->Board(Player);
	// A normally logged-in PlayerController owns this PlayerState and possession copies it
	// to the cannon. The transient test controller has no login flow, so mirror that step.
	Cannon->SetPlayerState(PlayerState);
	TestTrue(TEXT("Cannon tracks the riding player"), Cannon->GetRidingPlayer() == Player);
	TestTrue(TEXT("Player controller possesses the cannon"), Cannon->IsPlayerControlled());

	UPlayerSkillComponent* SkillComponent = PlayerState->GetPlayerSkillComponent();
	TestNotNull(TEXT("Player skill component exists"), SkillComponent);
	TestFalse(TEXT("Locked Water Bomb cannot activate"),
		ASC->TryActivateAbilitiesByTag(AbilityTags, true));
	TestTrue(TEXT("Water Bomb is unlocked for the execution test"),
		SkillComponent && SkillComponent->UnlockSkill(GameplayAbility_Skill_WaterBomb));
	TestEqual(TEXT("One Water Bomb material is added"),
		Player->GetInventoryComponent()->AddItem(Item_Id_Material_SkillMaterial_EpicSkill, 1), 1);

	TestTrue(TEXT("Water Bomb GA activates by ability tag"), ASC->TryActivateAbilitiesByTag(AbilityTags, true));
	TestTrue(TEXT("GA activation changes cannon to Water Bomb mode"), Cannon->IsWaterBombMode());

	TestTrue(TEXT("Water Bomb mode fires successfully"), Cannon->FireCannon());
	TestEqual(TEXT("Water Bomb fire consumes one material"),
		Player->GetInventoryComponent()->GetItemCount(Item_Id_Material_SkillMaterial_EpicSkill), 0);
	AWaterBombCannonball* FiredProjectile = nullptr;
	for (TActorIterator<AWaterBombCannonball> It(World); It; ++It)
	{
		FiredProjectile = *It;
		break;
	}
	TestNotNull(TEXT("GA-selected Water Bomb projectile is spawned"), FiredProjectile);
	if (FiredProjectile)
	{
		TestEqual(TEXT("GA duration reaches the spawned projectile"),
			FiredProjectile->GetEffectDurationSeconds(), 5.0f);
		TestEqual(TEXT("GA slow multiplier reaches the spawned projectile"),
			FiredProjectile->GetAttackSpeedMultiplier(), 0.5f);
	}

	TestFalse(TEXT("Firing the last available material automatically exits Water Bomb mode"),
		Cannon->IsWaterBombMode());

	TestEqual(TEXT("Another Water Bomb material is added"),
		Player->GetInventoryComponent()->AddItem(Item_Id_Material_SkillMaterial_EpicSkill, 1), 1);
	TestTrue(TEXT("Water Bomb mode can be entered again"), ASC->TryActivateAbilitiesByTag(AbilityTags, true));
	TestTrue(TEXT("The reactivated GA changes the cannon mode"), Cannon->IsWaterBombMode());
	TestTrue(TEXT("The material can disappear through another inventory path"),
		Player->GetInventoryComponent()->RemoveItem(Item_Id_Material_SkillMaterial_EpicSkill, 1));
	Cannon->Tick(0.016f);
	TestFalse(TEXT("Server tick exits Water Bomb mode when its material runs out externally"),
		Cannon->IsWaterBombMode());

	return true;
}

#endif
