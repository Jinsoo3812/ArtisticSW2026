#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "BaseEnemy.h"
#include "BaseGameplayTags.h"
#include "BossAI/ShipBossEnemy.h"
#include "Components/BaseHealthComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "HAL/IConsoleManager.h"
#include "MeleeEnemy/MeleeEnemy.h"
#include "RangedEnemy/RangedEnemy.h"
#include "RangedEnemy/RangedEnemyProjectile.h"
#include "Weapon/BaseWeapon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyNetworkPolicyDefaultsTest,
	"ArtisticSW.Enemy.Network.MVPPolicyDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyNetworkPolicyDefaultsTest::RunTest(const FString& Parameters)
{
	const ABaseEnemy* EnemyCDO = GetDefault<ABaseEnemy>();
	if (TestNotNull(TEXT("BaseEnemy CDO exists"), EnemyCDO))
	{
		TestTrue(TEXT("Enemy actor replicates"), EnemyCDO->GetIsReplicated());
		TestTrue(TEXT("Enemy movement replicates"), EnemyCDO->IsReplicatingMovement());
		TestFalse(TEXT("Enemy uses distance relevancy instead of AlwaysRelevant"), EnemyCDO->bAlwaysRelevant);
		TestEqual(TEXT("Enemy maximum update rate is 30 Hz"), EnemyCDO->GetNetUpdateFrequency(), 30.0f);
		TestEqual(TEXT("Enemy adaptive floor is 5 Hz"), EnemyCDO->GetMinNetUpdateFrequency(), 5.0f);
		TestEqual(
			TEXT("Enemy cull distance is 150 metres"),
			EnemyCDO->GetNetCullDistanceSquared(),
			FMath::Square(15000.0f));
		TestTrue(TEXT("Finished corpses are automatically retired"),
			EnemyCDO->ShouldDestroyAfterDeathFinished());
		TestEqual(TEXT("Finished corpse grace period is five seconds"),
			EnemyCDO->GetCorpseLifetimeAfterDeathFinished(), 5.0f);

		TestNotNull(TEXT("Enemy ASC exists"), EnemyCDO->GetAbilitySystemComponent());
		TestEqual(TEXT("Enemy ASC uses Minimal replication"),
			EnemyCDO->GetASCReplicationMode(), EGameplayEffectReplicationMode::Minimal);
	}

	const ABaseEnemy* EnemyArchetypes[] =
	{
		GetDefault<ABaseEnemy>(),
		GetDefault<AMeleeEnemy>(),
		GetDefault<ARangedEnemy>(),
		GetDefault<ADeckRangedEnemy>(),
		GetDefault<AShipBossEnemy>()
	};
	for (const ABaseEnemy* EnemyArchetype : EnemyArchetypes)
	{
		if (TestNotNull(TEXT("Enemy archetype owns a health component"),
			EnemyArchetype ? EnemyArchetype->GetHealthComponent() : nullptr))
		{
			TestEqual(
				*FString::Printf(TEXT("%s inherits the shared confirmed-damage cue"), *GetNameSafe(EnemyArchetype)),
				EnemyArchetype->GetHealthComponent()->GetDamageGameplayCueTag(),
				GameplayCue_Boss_Hit.GetTag());
		}
	}

	const TCHAR* EnemyBlueprintPaths[] =
	{
		TEXT("/Game/GameplayAbilitySystem/Enemy/BP_MeleeEnemy.BP_MeleeEnemy_C"),
		TEXT("/Game/GameplayAbilitySystem/Enemy/BP_RangedEnemy.BP_RangedEnemy_C"),
		TEXT("/Game/GameplayAbilitySystem/Enemy/BP_DeckRangedEnemy.BP_DeckRangedEnemy_C"),
		TEXT("/Game/GameplayAbilitySystem/Enemy/BP_Ship_BossEnemy.BP_Ship_BossEnemy_C")
	};
	for (const TCHAR* BlueprintPath : EnemyBlueprintPaths)
	{
		const UClass* BlueprintClass = LoadObject<UClass>(nullptr, BlueprintPath);
		const ABaseEnemy* BlueprintCDO = BlueprintClass
			? Cast<ABaseEnemy>(BlueprintClass->GetDefaultObject())
			: nullptr;
		if (TestNotNull(*FString::Printf(TEXT("Enemy Blueprint loads: %s"), BlueprintPath), BlueprintCDO))
		{
			TestEqual(
				*FString::Printf(TEXT("%s inherits the shared confirmed-damage cue"), BlueprintPath),
				BlueprintCDO->GetHealthComponent()->GetDamageGameplayCueTag(),
				GameplayCue_Boss_Hit.GetTag());
		}
	}

	const ABaseWeapon* WeaponCDO = GetDefault<ABaseWeapon>();
	if (TestNotNull(TEXT("BaseWeapon CDO exists"), WeaponCDO))
	{
		TestTrue(TEXT("Weapon actor replicates"), WeaponCDO->GetIsReplicated());
		TestTrue(TEXT("Weapon relevancy follows its owner"), WeaponCDO->bNetUseOwnerRelevancy);
		TestFalse(TEXT("Attached weapon does not replicate movement independently"),
			WeaponCDO->IsReplicatingMovement());
		TestEqual(TEXT("Weapon maximum update rate is 10 Hz"),
			WeaponCDO->GetNetUpdateFrequency(), 10.0f);
		TestEqual(TEXT("Weapon adaptive floor is 1 Hz"),
			WeaponCDO->GetMinNetUpdateFrequency(), 1.0f);
	}

	const ARangedEnemyProjectile* ProjectileCDO = GetDefault<ARangedEnemyProjectile>();
	if (TestNotNull(TEXT("RangedEnemyProjectile CDO exists"), ProjectileCDO))
	{
		TestTrue(TEXT("Enemy projectile replicates"), ProjectileCDO->GetIsReplicated());
		TestTrue(TEXT("Enemy projectile movement replicates"), ProjectileCDO->IsReplicatingMovement());
		TestEqual(TEXT("Enemy projectile maximum update rate is 30 Hz"),
			ProjectileCDO->GetNetUpdateFrequency(), 30.0f);
		TestEqual(TEXT("Enemy projectile adaptive floor is 20 Hz"),
			ProjectileCDO->GetMinNetUpdateFrequency(), 20.0f);
		TestEqual(TEXT("Enemy projectile retires after ten seconds"),
			ProjectileCDO->InitialLifeSpan, 10.0f);
	}

	const IConsoleVariable* AdaptiveNetUpdate =
		IConsoleManager::Get().FindConsoleVariable(TEXT("net.UseAdaptiveNetUpdateFrequency"));
	if (TestNotNull(TEXT("Adaptive net update CVar exists"), AdaptiveNetUpdate))
	{
		TestEqual(TEXT("Adaptive net update is enabled by project config"),
			AdaptiveNetUpdate->GetInt(), 1);
	}

	return true;
}

#endif
