#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BaseEnemy.h"
#include "BaseGameplayTags.h"
#include "GAS/Ability/GA_EnemyMoveSpeedBoost.h"
#include "GAS/EnemyAttributeSet.h"
#include "RangedEnemy/RangedEnemy.h"
#include "Task/BTT_ActivateBossAbility.h"
#include "Task/BTT_ActivateEnemyAbilityByTag.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyMovementSpeedResolutionTest,
	"ArtisticSW.Enemy.AbilityInfrastructure.MovementSpeedResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyMovementSpeedResolutionTest::RunTest(const FString& Parameters)
{
	TestEqual(
		TEXT("Wave multiplier is applied before additive GAS bonus"),
		ABaseEnemy::ResolveMovementSpeed(500.0f, 1.2f, 150.0f, 2000.0f),
		750.0f);
	TestEqual(
		TEXT("Idle remains stopped even while the buff attribute is active"),
		ABaseEnemy::ResolveMovementSpeed(0.0f, 1.2f, 150.0f, 2000.0f),
		0.0f);
	TestEqual(
		TEXT("Resolved speed respects the safety cap"),
		ABaseEnemy::ResolveMovementSpeed(1800.0f, 2.0f, 500.0f, 2000.0f),
		2000.0f);
	TestEqual(
		TEXT("Duration slow multiplies the complete enemy movement result"),
		ABaseEnemy::ResolveMovementSpeed(500.0f, 1.2f, 150.0f, 2000.0f, 0.5f),
		375.0f);
	TestEqual(
		TEXT("A stopped locomotion mode remains stopped while slowed"),
		ABaseEnemy::ResolveMovementSpeed(0.0f, 1.2f, 150.0f, 2000.0f, 0.5f),
		0.0f);
	TestEqual(
		TEXT("Attack-speed slow multiplies a ranged attack's authored montage rate"),
		ARangedEnemy::ResolveAttackMontagePlayRate(1.2f, 0.5f),
		0.6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyMoveSpeedBonusAttributeTest,
	"ArtisticSW.Enemy.AbilityInfrastructure.MoveSpeedBonusAttribute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyMoveSpeedBonusAttributeTest::RunTest(const FString& Parameters)
{
	const UEnemyAttributeSet* Attributes = GetDefault<UEnemyAttributeSet>();
	TestEqual(TEXT("MoveSpeedBonus defaults to zero"), Attributes->GetMoveSpeedBonus(), 0.0f);

	const FProperty* BonusProperty = FindFProperty<FProperty>(
		UEnemyAttributeSet::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UEnemyAttributeSet, MoveSpeedBonus));
	TestTrue(TEXT("MoveSpeedBonus is a replicated property"),
		BonusProperty && BonusProperty->HasAnyPropertyFlags(CPF_Net));

	const UEnemyMoveSpeedBoostEffect* Effect = GetDefault<UEnemyMoveSpeedBoostEffect>();
	TestEqual(TEXT("Move-speed GE is duration based"),
		Effect->DurationPolicy, EGameplayEffectDurationType::HasDuration);
	TestEqual(TEXT("A source refreshes one stack instead of accumulating itself"),
		Effect->GetStackLimitCount(), 1);
	TestEqual(TEXT("Move-speed GE owns one modifier"), Effect->Modifiers.Num(), 1);
	if (Effect->Modifiers.Num() == 1)
	{
		TestTrue(TEXT("GE modifies the Enemy-specific MoveSpeedBonus attribute"),
			Effect->Modifiers[0].Attribute == UEnemyAttributeSet::GetMoveSpeedBonusAttribute());
		TestEqual(TEXT("GE modifier is additive"),
			Effect->Modifiers[0].ModifierOp, EGameplayModOp::Additive);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyAbilityCommonBTContractTest,
	"ArtisticSW.Enemy.AbilityInfrastructure.CommonBTContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyAbilityCommonBTContractTest::RunTest(const FString& Parameters)
{
	const UGA_EnemyMoveSpeedBoost* Ability = GetDefault<UGA_EnemyMoveSpeedBoost>();
	TestTrue(TEXT("Move-speed buff has the common exact asset tag"),
		Ability->GetAssetTags().HasTagExact(GameplayAbility_Enemy_Buff_MoveSpeed));
	TestEqual(TEXT("Enemy AI ability runs only on authority"),
		Ability->GetNetExecutionPolicy(), EGameplayAbilityNetExecutionPolicy::ServerOnly);

	const UBTT_ActivateEnemyAbilityByTag* CommonTask =
		GetDefault<UBTT_ActivateEnemyAbilityByTag>();
	TestTrue(TEXT("Common task cancels an active ability when its BT branch aborts"),
		CommonTask->GetCancelAbilityOnAbort());

	const UBTT_ActivateBossAbility* BossTask = GetDefault<UBTT_ActivateBossAbility>();
	TestTrue(TEXT("Existing Boss task is a specialization of the common API"),
		BossTask->IsA<UBTT_ActivateEnemyAbilityByTag>());
	return true;
}

#endif
