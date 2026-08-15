#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BaseGameplayTags.h"
#include "BossAI/BossDeckPointSelector.h"
#include "BossAI/BossEncounterComponent.h"
#include "BossAI/EnemyItemBox.h"
#include "BossAI/ShipBossAIController.h"
#include "BossAI/ShipBossEnemy.h"
#include "GAS/Ability/Boss/BossGameplayAbility.h"
#include "GAS/Ability/Boss/GA_BossBasicAttack.h"
#include "GAS/Ability/Boss/GA_BossDashSlash.h"
#include "GAS/Ability/Boss/GA_BossKnockback.h"
#include "GAS/Ability/Boss/GA_BossVanish.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Interactable/InteractableComponent.h"
#include "ShipAI/EnemyShip.h"
#include "Task/BTT_ActivateBossAbility.h"
#include "Task/BTT_SelectBossDestinationPoint.h"

namespace BossMVPTests
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;

		FScopedTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("BossMVPTestWorld"));
			if (World)
			{
				FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
				WorldContext.SetCurrentWorld(World);
			}
		}

		~FScopedTestWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBossMVPDefaultsTest,
	"ArtisticSW.Enemy.BossMVP.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBossMVPDefaultsTest::RunTest(const FString& Parameters)
{
	const AShipBossEnemy* BossCDO = GetDefault<AShipBossEnemy>();
	const UBossEncounterComponent* EncounterCDO = GetDefault<UBossEncounterComponent>();
	const AEnemyItemBox* ItemBoxCDO = GetDefault<AEnemyItemBox>();
	const UBTT_SelectBossDestinationPoint* SelectTaskCDO = GetDefault<UBTT_SelectBossDestinationPoint>();
	const UBTT_ActivateBossAbility* ActivateTaskCDO = GetDefault<UBTT_ActivateBossAbility>();

	if (TestNotNull(TEXT("Ship boss class exists"), BossCDO))
	{
		TestTrue(TEXT("Boss uses its native fallback controller"),
			BossCDO->AIControllerClass == AShipBossAIController::StaticClass());
		TestTrue(TEXT("Boss is server-replicated"), BossCDO->GetIsReplicated());
		TestEqual(TEXT("Boss does not walk point-by-point in the MVP"),
			BossCDO->GetCharacterMovement()->MaxWalkSpeed, 0.0f);
	}
	if (TestNotNull(TEXT("Encounter component exists"), EncounterCDO))
	{
		TestTrue(TEXT("Encounter state replicates through its component"), EncounterCDO->GetIsReplicated());
		TestEqual(TEXT("Encounter waits for first box interaction"),
			EncounterCDO->GetEncounterState(), EBossEncounterState::Waiting);
	}
	const AEnemyShip* EnemyShipCDO = GetDefault<AEnemyShip>();
	if (TestNotNull(TEXT("EnemyShip CDO exists"), EnemyShipCDO))
	{
		TestNotNull(TEXT("Every EnemyShip owns an optional boss encounter component"),
			EnemyShipCDO->GetBossEncounterComponent());
		TestFalse(TEXT("Existing EnemyShips do not enable boss encounters accidentally"),
			EnemyShipCDO->GetBossEncounterComponent()->IsEncounterEnabled());
	}
	if (TestNotNull(TEXT("Enemy item box exists"), ItemBoxCDO))
	{
		TestFalse(TEXT("Ship-mounted item box does not simulate buoyancy"),
			ItemBoxCDO->IsPhysicsAndBuoyancyEnabled());
	}
	TestNotNull(TEXT("Shared destination BT task exists"), SelectTaskCDO);
	TestNotNull(TEXT("Generic boss ability BT task exists"), ActivateTaskCDO);

	const UGA_BossKnockback* Knockback = GetDefault<UGA_BossKnockback>();
	const UGA_BossVanish* Vanish = GetDefault<UGA_BossVanish>();
	const UGA_BossDashSlash* Dash = GetDefault<UGA_BossDashSlash>();
	const UGA_BossBasicAttack* Basic = GetDefault<UGA_BossBasicAttack>();
	const UBossAbilityCooldownEffect* CooldownEffect = GetDefault<UBossAbilityCooldownEffect>();

	if (TestNotNull(TEXT("Boss knockback ability exists"), Knockback))
	{
		TestTrue(TEXT("Knockback has its independent cooldown tag"),
			Knockback->GetCooldownTags()->HasTagExact(Cooldown_Boss_Knockback));
		TestTrue(TEXT("Knockback exposes its boss ability asset tag"),
			Knockback->GetAssetTags().HasTagExact(GameplayAbility_Boss_Knockback));
	}
	if (TestNotNull(TEXT("Boss vanish ability exists"), Vanish))
	{
		TestTrue(TEXT("Vanish has its independent cooldown tag"),
			Vanish->GetCooldownTags()->HasTagExact(Cooldown_Boss_Vanish));
	}
	if (TestNotNull(TEXT("Boss dash ability exists"), Dash))
	{
		TestTrue(TEXT("Dash has its independent cooldown tag"),
			Dash->GetCooldownTags()->HasTagExact(Cooldown_Boss_DashSlash));
	}
	if (TestNotNull(TEXT("Boss basic attack specialization exists"), Basic))
	{
		TestTrue(TEXT("Basic attack remains independent from knockback cooldown"),
			Basic->GetCooldownTags()->HasTagExact(Cooldown_Enemy_BasicAttack));
	}
	if (TestNotNull(TEXT("Native boss cooldown effect exists"), CooldownEffect))
	{
		TestEqual(TEXT("Boss cooldown effect has duration policy"),
			CooldownEffect->DurationPolicy, EGameplayEffectDurationType::HasDuration);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBossPointMathTest,
	"ArtisticSW.Enemy.BossMVP.PointMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBossPointMathTest::RunTest(const FString& Parameters)
{
	const FVector TargetLocation = FVector::ZeroVector;
	const FVector TargetForward = FVector::ForwardVector;
	const FVector DeckUp = FVector::UpVector;

	TestTrue(TEXT("Point in rear half-plane is accepted"),
		UBossDeckPointSelector::IsPointBehindTarget(
			TargetLocation, TargetForward, FVector(-300.0f, 50.0f, 0.0f), DeckUp, 0.0f));
	TestFalse(TEXT("Point in front of player is rejected"),
		UBossDeckPointSelector::IsPointBehindTarget(
			TargetLocation, TargetForward, FVector(300.0f, 0.0f, 0.0f), DeckUp, 0.0f));
	TestTrue(TEXT("Dash segment crossing the target is accepted"),
		UBossDeckPointSelector::DoesSegmentPassTarget(
			FVector(400.0f, 0.0f, 0.0f), FVector(-400.0f, 0.0f, 0.0f), TargetLocation, 120.0f));
	TestFalse(TEXT("Dash segment missing the target corridor is rejected"),
		UBossDeckPointSelector::DoesSegmentPassTarget(
			FVector(400.0f, 300.0f, 0.0f), FVector(-400.0f, 300.0f, 0.0f), TargetLocation, 120.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBossEncounterSingleTriggerTest,
	"ArtisticSW.Enemy.BossMVP.EncounterSingleTrigger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBossEncounterSingleTriggerTest::RunTest(const FString& Parameters)
{
	BossMVPTests::FScopedTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient boss test world is created"), TestWorld.World))
	{
		return false;
	}

	AEnemyShip* Ship = TestWorld.World->SpawnActor<AEnemyShip>();
	AEnemyItemBox* ItemBox = TestWorld.World->SpawnActor<AEnemyItemBox>();
	AActor* Interactor = TestWorld.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Enemy ship is spawned"), Ship)
		|| !TestNotNull(TEXT("Enemy item box is spawned"), ItemBox)
		|| !TestNotNull(TEXT("Interactor is spawned"), Interactor))
	{
		return false;
	}

	UBossEncounterComponent* Encounter = Ship->GetBossEncounterComponent();
	if (!TestNotNull(TEXT("Ship encounter component is available"), Encounter))
	{
		return false;
	}
	Encounter->ConfigureEncounter(ItemBox, nullptr, -1);
	TestTrue(TEXT("Configured encounter locks the item box before combat"), ItemBox->IsLocked());

	ItemBox->GetInteractableComponent()->Interact(Interactor);
	TestEqual(TEXT("Invalid authoring fails atomically after the first interaction"),
		Encounter->GetEncounterState(), EBossEncounterState::Failed);
	TestNull(TEXT("Failed encounter does not leave a partial boss"), Encounter->GetSpawnedBoss());

	ItemBox->GetInteractableComponent()->Interact(Interactor);
	TestEqual(TEXT("A second interaction cannot restart a terminal encounter"),
		Encounter->GetEncounterState(), EBossEncounterState::Failed);
	TestTrue(TEXT("Failed encounter keeps the collectible locked"), ItemBox->IsLocked());
	return true;
}

#endif
