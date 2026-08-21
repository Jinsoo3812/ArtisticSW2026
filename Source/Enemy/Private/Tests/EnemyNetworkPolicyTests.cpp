#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "Abilities/BaseDeathGameplayAbility.h"
#include "Animation/AnimNotify_DeathRagdoll.h"
#include "Animation/AnimMontage.h"
#include "BaseEnemy.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "BossAI/ShipBossEnemy.h"
#include "Camera/CameraShakeBase.h"
#include "Components/BaseHealthComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameplayCue/SWGameplayCueNotify_BurstFeedback.h"
#include "HAL/IConsoleManager.h"
#include "MeleeEnemy/MeleeEnemy.h"
#include "RangedEnemy/RangedEnemy.h"
#include "RangedEnemy/RangedEnemyProjectile.h"
#include "UObject/UnrealType.h"
#include "Weapon/BaseWeapon.h"
#include "Weapon/BaseWeaponComponent.h"

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
		TestNull(TEXT("Regular enemies do not own a death GA"),
			EnemyCDO->GetDeathAbilityClass().Get());

		TestNotNull(TEXT("Enemy ASC exists"), EnemyCDO->GetAbilitySystemComponent());
		TestEqual(TEXT("Enemy ASC uses Minimal replication"),
			EnemyCDO->GetASCReplicationMode(), EGameplayEffectReplicationMode::Minimal);
	}

	const UClass* BossBlueprintClass = LoadObject<UClass>(nullptr,
		TEXT("/Game/GameplayAbilitySystem/Enemy/BP_Ship_BossEnemy.BP_Ship_BossEnemy_C"));
	const AShipBossEnemy* BossBlueprintCDO = BossBlueprintClass
		? Cast<AShipBossEnemy>(BossBlueprintClass->GetDefaultObject())
		: nullptr;
	if (TestNotNull(TEXT("Boss Blueprint loads for death policy"), BossBlueprintCDO))
	{
		const TSubclassOf<UBaseDeathGameplayAbility> BossDeathAbility = BossBlueprintCDO->GetDeathAbilityClass();
		TestTrue(TEXT("Boss owns the dedicated BPGA_BossDeath ability"),
			BossDeathAbility
			&& BossDeathAbility->IsChildOf(UBaseDeathGameplayAbility::StaticClass())
			&& BossDeathAbility->GetPathName().Contains(TEXT("BPGA_BossDeath")));
	}

	const UClass* PlayerBlueprintClass = LoadObject<UClass>(nullptr,
		TEXT("/Game/Blueprints/Player/BP_Player.BP_Player_C"));
	const ABasePlayer* PlayerBlueprintCDO = PlayerBlueprintClass
		? Cast<ABasePlayer>(PlayerBlueprintClass->GetDefaultObject())
		: nullptr;
	if (TestNotNull(TEXT("Player Blueprint loads for death policy"), PlayerBlueprintCDO))
	{
		int32 PlayerDeathAbilityCount = 0;
		for (const TSubclassOf<UGameplayAbility>& AbilityClass : PlayerBlueprintCDO->DefaultGrantedAbilities)
		{
			if (AbilityClass && AbilityClass->IsChildOf(UBaseDeathGameplayAbility::StaticClass()))
			{
				++PlayerDeathAbilityCount;
				TestTrue(TEXT("Player uses the dedicated BPGA_PlayerDeath ability"),
					AbilityClass->GetPathName().Contains(TEXT("BPGA_PlayerDeath")));
			}
		}
		TestEqual(TEXT("Player owns exactly one death ability"), PlayerDeathAbilityCount, 1);
	}

	const UAnimMontage* DeathMontage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Characters/Mannequins/Anims/Death/AM_Death.AM_Death"));
	if (TestNotNull(TEXT("Shared death montage loads"), DeathMontage))
	{
		bool bHasRagdollNotify = false;
		for (const FAnimNotifyEvent& NotifyEvent : DeathMontage->Notifies)
		{
			if (NotifyEvent.Notify && NotifyEvent.Notify->IsA<UAnimNotify_DeathRagdoll>())
			{
				bHasRagdollNotify = true;
				break;
			}
		}
		TestTrue(TEXT("AM_Death contains the authored ragdoll transition notify"), bHasRagdollNotify);
	}

	struct FEnemyCueExpectation
	{
		const ABaseEnemy* Archetype;
		FGameplayTag ExpectedCue;
	};
	const FEnemyCueExpectation EnemyArchetypes[] =
	{
		{ GetDefault<ABaseEnemy>(), GameplayCue_Enemy_Hit },
		{ GetDefault<AMeleeEnemy>(), GameplayCue_Enemy_Hit },
		{ GetDefault<ARangedEnemy>(), GameplayCue_Enemy_Hit },
		{ GetDefault<ADeckRangedEnemy>(), GameplayCue_Enemy_Hit },
		{ GetDefault<AShipBossEnemy>(), GameplayCue_Boss_Hit }
	};
	for (const FEnemyCueExpectation& Expectation : EnemyArchetypes)
	{
		if (TestNotNull(TEXT("Enemy archetype owns a health component"),
			Expectation.Archetype ? Expectation.Archetype->GetHealthComponent() : nullptr))
		{
			TestEqual(
				*FString::Printf(TEXT("%s uses its intended confirmed-damage cue"),
					*GetNameSafe(Expectation.Archetype)),
				Expectation.Archetype->GetHealthComponent()->GetDamageGameplayCueTag(),
				Expectation.ExpectedCue);
			TestTrue(
				*FString::Printf(TEXT("%s health component replicates"),
					*GetNameSafe(Expectation.Archetype)),
				Expectation.Archetype->GetHealthComponent()->GetIsReplicated());
		}
	}

	struct FEnemyBlueprintCueExpectation
	{
		const TCHAR* Path;
		FGameplayTag ExpectedCue;
	};
	const FEnemyBlueprintCueExpectation EnemyBlueprints[] =
	{
		{ TEXT("/Game/GameplayAbilitySystem/Enemy/BP_MeleeEnemy.BP_MeleeEnemy_C"), GameplayCue_Enemy_Hit },
		{ TEXT("/Game/GameplayAbilitySystem/Enemy/BP_RangedEnemy.BP_RangedEnemy_C"), GameplayCue_Enemy_Hit },
		{ TEXT("/Game/GameplayAbilitySystem/Enemy/BP_DeckRangedEnemy.BP_DeckRangedEnemy_C"), GameplayCue_Enemy_Hit },
		{ TEXT("/Game/GameplayAbilitySystem/Enemy/BP_Ship_BossEnemy.BP_Ship_BossEnemy_C"), GameplayCue_Boss_Hit }
	};
	for (const FEnemyBlueprintCueExpectation& Expectation : EnemyBlueprints)
	{
		const UClass* BlueprintClass = LoadObject<UClass>(nullptr, Expectation.Path);
		const ABaseEnemy* BlueprintCDO = BlueprintClass
			? Cast<ABaseEnemy>(BlueprintClass->GetDefaultObject())
			: nullptr;
		if (TestNotNull(*FString::Printf(TEXT("Enemy Blueprint loads: %s"), Expectation.Path), BlueprintCDO))
		{
			TestEqual(
				*FString::Printf(TEXT("%s uses its intended confirmed-damage cue"), Expectation.Path),
				BlueprintCDO->GetHealthComponent()->GetDamageGameplayCueTag(),
				Expectation.ExpectedCue);
		}
	}
	TestTrue(TEXT("Regular enemy confirmed-damage cue is registered"), GameplayCue_Enemy_Hit.GetTag().IsValid());
	TestTrue(TEXT("Boss confirmed-damage cue is registered"), GameplayCue_Boss_Hit.GetTag().IsValid());
	TestNotEqual(TEXT("Regular enemy and boss confirmed-damage cues stay distinct"),
		GameplayCue_Enemy_Hit.GetTag(), GameplayCue_Boss_Hit.GetTag());
	const UClass* EnemyHitCueClass = LoadClass<USWGameplayCueNotify_BurstFeedback>(
		nullptr, TEXT("/Game/GameplayCues/Enemy/GCN_Enemy_Hit.GCN_Enemy_Hit_C"));
	if (TestNotNull(TEXT("Regular enemy hit cue Blueprint loads"), EnemyHitCueClass))
	{
		const USWGameplayCueNotify_BurstFeedback* EnemyHitCue =
			Cast<USWGameplayCueNotify_BurstFeedback>(EnemyHitCueClass->GetDefaultObject());
		if (TestNotNull(TEXT("Regular enemy hit cue uses the multiplayer-safe feedback parent"),
			EnemyHitCue))
		{
			TestEqual(TEXT("Regular enemy hit uses a lighter camera shake than the boss"),
				EnemyHitCue->GetCameraShakeScale(), 0.35f);
			TestTrue(TEXT("Regular enemy hit feedback is limited to the attacking local player"),
				EnemyHitCue->GetCameraShakeRecipient()
					== ESWGameplayCueCameraShakeRecipient::InstigatorLocalPlayer);

			const TSubclassOf<UCameraShakeBase> ShakeClass = EnemyHitCue->GetCameraShakeClass();
			const UCameraShakeBase* ShakeDefaults = ShakeClass
				? ShakeClass->GetDefaultObject<UCameraShakeBase>()
				: nullptr;
			if (TestNotNull(TEXT("Regular enemy hit has a camera shake class"), ShakeDefaults))
			{
				TestTrue(TEXT("Repeated hits restart one shake instead of stacking instances"),
					ShakeDefaults->bSingleInstance);
				const UCameraShakePattern* Pattern = ShakeDefaults->GetRootShakePattern();
				if (TestNotNull(TEXT("Hit camera shake has a root pattern"), Pattern))
				{
					auto TestPatternFloat = [this, Pattern](
						const TCHAR* Description,
						const FName PropertyName,
						const float ExpectedValue)
					{
						const FFloatProperty* Property =
							FindFProperty<FFloatProperty>(Pattern->GetClass(), PropertyName);
						if (TestNotNull(Description, Property))
						{
							TestEqual(Description,
								Property->GetPropertyValue_InContainer(Pattern), ExpectedValue);
						}
					};
					TestPatternFloat(TEXT("Hit shake is a single short pulse"),
						TEXT("Duration"), 0.16f);
					TestPatternFloat(TEXT("Hit shake avoids high-frequency buzzing"),
						TEXT("LocationFrequencyMultiplier"), 3.0f);
					TestPatternFloat(TEXT("Hit shake uses restrained movement amplitude"),
						TEXT("LocationAmplitudeMultiplier"), 1.25f);
				}
			}
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

	const FProperty* WeaponLifecycleProperty = FindFProperty<FProperty>(
		UBaseWeaponComponent::StaticClass(), TEXT("WeaponLifecycleState"));
	if (TestNotNull(TEXT("Weapon lifecycle state exists"), WeaponLifecycleProperty))
	{
		TestTrue(TEXT("Weapon lifecycle state replicates"),
			WeaponLifecycleProperty->HasAnyPropertyFlags(CPF_Net));
		TestEqual(TEXT("Weapon lifecycle state has one RepNotify"),
			WeaponLifecycleProperty->RepNotifyFunc, FName(TEXT("OnRep_WeaponLifecycleState")));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyDamageGameplayCueAuthorityTest,
	"ArtisticSW.Enemy.Network.DamageGameplayCueAuthority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyDamageGameplayCueAuthorityTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("EnemyDamageGameplayCueAuthorityWorld"));
	if (!TestNotNull(TEXT("Transient game world is created"), World))
	{
		return false;
	}

	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	ABaseEnemy* Enemy = World->SpawnActor<ABaseEnemy>();
	if (!TestNotNull(TEXT("Authority-test enemy spawns"), Enemy))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		return false;
	}

	UBaseHealthComponent* HealthComponent = Enemy->GetHealthComponent();
	HealthComponent->InitializeWithAbilitySystem(Enemy->GetAbilitySystemComponent());
	TestTrue(TEXT("Server authority is eligible to execute confirmed-damage cues"),
		HealthComponent->ShouldExecuteConfirmedDamageGameplayCues(10.0f, FGameplayTag()));
	TestFalse(TEXT("Zero damage never executes confirmed-damage cues"),
		HealthComponent->ShouldExecuteConfirmedDamageGameplayCues(0.0f, FGameplayTag()));

	// The transient fixture starts with zero Health. Initialization therefore
	// exercises the no-death-GA path synchronously.
	TestEqual(TEXT("A zero-health regular enemy without a death GA finishes death immediately"),
		HealthComponent->GetDeathState(), EBaseDeathState::DeathFinished);

	Enemy->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("Simulated client cannot execute confirmed-damage cues"),
		HealthComponent->ShouldExecuteConfirmedDamageGameplayCues(10.0f, FGameplayTag()));

	Enemy->SetRole(ROLE_Authority);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

#endif
