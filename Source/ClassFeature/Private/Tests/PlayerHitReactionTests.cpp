#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BasePlayer.h"
#include "GAS/Ability/GA_PlayerHitReaction.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlayerHitReactionConfigurationTest,
	"ArtisticSW.GAS.HitReaction.PlayerConfiguration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerHitReactionConfigurationTest::RunTest(const FString& Parameters)
{
	const UClass* HitReactionClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/GameplayAbilitySystem/Ability/GA_HitReact.GA_HitReact_C"));
	if (!TestNotNull(TEXT("Player HitReaction Blueprint class loads"), HitReactionClass))
	{
		return false;
	}

	TestEqual(
		TEXT("Player HitReaction Blueprint directly inherits the player-specific native ability"),
		HitReactionClass->GetSuperClass(),
		UGA_PlayerHitReaction::StaticClass());

	const UClass* PlayerClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/Blueprints/Player/BP_Player.BP_Player_C"));
	if (!TestNotNull(TEXT("Player Blueprint class loads"), PlayerClass))
	{
		return false;
	}

	const ABasePlayer* PlayerCDO = PlayerClass->GetDefaultObject<ABasePlayer>();
	if (!TestNotNull(TEXT("Player Blueprint CDO exists"), PlayerCDO))
	{
		return false;
	}

	const bool bGrantsHitReaction = PlayerCDO->DefaultGrantedAbilities.ContainsByPredicate(
		[HitReactionClass](const TSubclassOf<UGameplayAbility>& GrantedAbility)
		{
			return GrantedAbility.Get() == HitReactionClass;
		});
	TestTrue(
		TEXT("Player grants its HitReaction ability when possessed"),
		bGrantsHitReaction);

	return !HasAnyErrors();
}

#endif
