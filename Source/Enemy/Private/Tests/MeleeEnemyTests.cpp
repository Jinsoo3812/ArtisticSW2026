#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BaseGameplayTags.h"
#include "Decorator/BTD_IsMeleeAttackReady.h"
#include "Decorator/BTD_TargetDistance.h"
#include "GAS/Ability/GA_BasicAttack.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MeleeEnemy/MeleeEnemy.h"
#include "Task/BTT_MoveToWeaponRange.h"
#include "Weapon/WeaponDataAsset.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMeleeEnemyMVPDefaultsTest,
	"ArtisticSW.Enemy.MeleeEnemy.MVPDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMeleeEnemyMVPDefaultsTest::RunTest(const FString& Parameters)
{
	const AMeleeEnemy* EnemyCDO = GetDefault<AMeleeEnemy>();
	const UCharacterMovementComponent* Movement = EnemyCDO ? EnemyCDO->GetCharacterMovement() : nullptr;
	const UGA_BasicAttack* AttackCDO = GetDefault<UGA_BasicAttack>();
	const UEnemyBasicAttackCooldownEffect* CooldownEffectCDO =
		GetDefault<UEnemyBasicAttackCooldownEffect>();
	const UBTD_IsMeleeAttackReady* ReadyDecorator = GetDefault<UBTD_IsMeleeAttackReady>();
	const UBTT_MoveToWeaponRange* MoveTask = GetDefault<UBTT_MoveToWeaponRange>();
	const UBTD_TargetDistance* DistanceDecorator = GetDefault<UBTD_TargetDistance>();

	if (TestNotNull(TEXT("MeleeEnemy CDO exists"), EnemyCDO))
	{
		TestTrue(TEXT("MeleeEnemy consumes controller yaw for focus rotation"),
			EnemyCDO->bUseControllerRotationYaw);
		TestFalse(TEXT("MeleeEnemy starts with a holstered loadout"),
			EnemyCDO->ShouldEquipWeaponOnSpawn());
	}
	if (TestNotNull(TEXT("MeleeEnemy movement component exists"), Movement))
	{
		TestFalse(TEXT("MeleeEnemy does not rotate toward movement while strafing"),
			Movement->bOrientRotationToMovement);
		TestFalse(TEXT("MeleeEnemy uses direct controller yaw instead of desired-rotation smoothing"),
			Movement->bUseControllerDesiredRotation);
	}

	if (TestNotNull(TEXT("Basic attack ability CDO exists"), AttackCDO))
	{
		const FGameplayTagContainer* CooldownTags = AttackCDO->GetCooldownTags();
		TestTrue(TEXT("Basic attack owns the native melee cooldown tag"),
			CooldownTags && CooldownTags->HasTagExact(Cooldown_Enemy_BasicAttack));
		TestTrue(TEXT("Basic attack cooldown duration is positive"),
			AttackCDO->GetAttackCooldownDuration() > 0.0f);
	}
	if (TestNotNull(TEXT("Native basic attack cooldown GE exists"), CooldownEffectCDO))
	{
		TestEqual(TEXT("Basic attack cooldown GE has duration policy"),
			CooldownEffectCDO->DurationPolicy, EGameplayEffectDurationType::HasDuration);
	}

	if (TestNotNull(TEXT("Melee readiness decorator exists"), ReadyDecorator))
	{
		TestEqual(TEXT("Melee readiness watches the basic attack cooldown"),
			ReadyDecorator->GetObservedCooldownTag(), FGameplayTag(Cooldown_Enemy_BasicAttack));
		TestEqual(TEXT("Cooldown completion can abort the lower-priority strafe branch"),
			ReadyDecorator->GetFlowAbortMode(), EBTFlowAbortMode::LowerPriority);
	}
	if (TestNotNull(TEXT("Move-to-weapon-range task exists"), MoveTask))
	{
		TestEqual(TEXT("Move-to-weapon-range defaults to TargetActor"),
			MoveTask->GetSelectedBlackboardKey(), FName(TEXT("TargetActor")));
		TestTrue(TEXT("Move-to-weapon-range keeps an inset from the exact weapon boundary"),
			MoveTask->GetAcceptanceRangeInset() > 0.0f);
		TestEqual(TEXT("Acceptance remains inside a normal weapon range"),
			UBTT_MoveToWeaponRange::ResolveAcceptanceRange(180.0f, 15.0f, 25.0f),
			165.0f);
		TestEqual(TEXT("A short weapon range is never exceeded by the minimum"),
			UBTT_MoveToWeaponRange::ResolveAcceptanceRange(20.0f, 15.0f, 25.0f),
			20.0f);
	}
	if (TestNotNull(TEXT("Target-distance decorator exists"), DistanceDecorator))
	{
		TestEqual(TEXT("Target-distance defaults to the reapproach condition"),
			DistanceDecorator->GetDistanceQuery(), ETargetDistanceQuery::Outside);
		TestEqual(TEXT("Target-distance default outer threshold"),
			DistanceDecorator->GetDistanceThreshold(), 500.0f);
		TestEqual(TEXT("Distance changes can abort either active branch"),
			DistanceDecorator->GetFlowAbortMode(), EBTFlowAbortMode::Both);
	}

	const FProperty* AttackRangeProperty =
		FWeaponCombatData::StaticStruct()->FindPropertyByName(TEXT("AttackRange"));
	TestNotNull(TEXT("Weapon combat data exposes one AttackRange property"), AttackRangeProperty);
	TestNull(TEXT("Legacy MinAttackRange was removed"),
		FWeaponCombatData::StaticStruct()->FindPropertyByName(TEXT("MinAttackRange")));
	TestNull(TEXT("Legacy MaxAttackRange was redirected and removed"),
		FWeaponCombatData::StaticStruct()->FindPropertyByName(TEXT("MaxAttackRange")));
	TestNull(TEXT("Weapon data no longer owns attack cooldown"),
		FWeaponCombatData::StaticStruct()->FindPropertyByName(TEXT("AttackCooldown")));

	const UWeaponDataAsset* WeaponData = LoadObject<UWeaponDataAsset>(
		nullptr,
		TEXT("/Game/GameplayAbilitySystem/Enemy/Weapon/DA_Weapon.DA_Weapon"));
	if (TestNotNull(TEXT("Existing enemy weapon data loads after property migration"), WeaponData))
	{
		for (const FWeaponDefinition& Definition : WeaponData->WeaponDefinitions)
		{
			TestTrue(
				FString::Printf(TEXT("%s has a positive AttackRange"), *Definition.WeaponTag.ToString()),
				Definition.CombatData.AttackRange > 0.0f);
		}
	}

	const UClass* MeleeAbilityClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/GameplayAbilitySystem/Ability/Enemy/BPGA_MeleeAttack.BPGA_MeleeAttack_C"));
	if (TestNotNull(TEXT("Existing melee attack Blueprint still loads"), MeleeAbilityClass))
	{
		const UGA_BasicAttack* BlueprintAttackCDO =
			MeleeAbilityClass->GetDefaultObject<UGA_BasicAttack>();
		TestTrue(TEXT("Melee attack Blueprint inherits the native cooldown tag"),
			BlueprintAttackCDO
			&& BlueprintAttackCDO->GetCooldownTags()
			&& BlueprintAttackCDO->GetCooldownTags()->HasTagExact(Cooldown_Enemy_BasicAttack));
	}

	return true;
}

#endif
