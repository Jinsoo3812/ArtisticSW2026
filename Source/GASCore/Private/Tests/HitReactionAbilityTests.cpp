#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Abilities/BaseHitReactionGameplayAbility.h"
#include "Abilities/GameplayAbility.h"
#include "BaseGameplayTags.h"
#include "UObject/UnrealType.h"

namespace HitReactionAbilityTests
{
	const FGameplayTagContainer* GetTagContainerProperty(
		const UGameplayAbility* Ability,
		const FName PropertyName)
	{
		const FStructProperty* Property = FindFProperty<FStructProperty>(
			UGameplayAbility::StaticClass(),
			PropertyName);
		return Property && Ability
			? Property->ContainerPtrToValuePtr<FGameplayTagContainer>(Ability)
			: nullptr;
	}

	bool GetBoolProperty(
		const UGameplayAbility* Ability,
		const FName PropertyName,
		bool& OutValue)
	{
		const FBoolProperty* Property = FindFProperty<FBoolProperty>(
			UGameplayAbility::StaticClass(),
			PropertyName);
		if (!Property || !Ability)
		{
			return false;
		}

		OutValue = Property->GetPropertyValue_InContainer(Ability);
		return true;
	}

	bool VerifyPolicy(
		FAutomationTestBase& Test,
		const UBaseHitReactionGameplayAbility* Ability,
		const FString& Context)
	{
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s CDO exists"), *Context), Ability))
		{
			return false;
		}

		bool bRetriggerInstancedAbility = false;
		Test.TestTrue(
			*FString::Printf(TEXT("%s exposes the retrigger property"), *Context),
			GetBoolProperty(Ability, TEXT("bRetriggerInstancedAbility"), bRetriggerInstancedAbility));
		Test.TestTrue(
			*FString::Printf(TEXT("%s retriggers on every non-lethal hit"), *Context),
			bRetriggerInstancedAbility);

		Test.TestTrue(
			*FString::Printf(TEXT("%s has the hit-reaction asset tag"), *Context),
			Ability->GetAssetTags().HasTagExact(GameplayAbility_HitReaction));

		const FGameplayTagContainer* CancelTags = GetTagContainerProperty(Ability, TEXT("CancelAbilitiesWithTag"));
		const FGameplayTagContainer* BlockTags = GetTagContainerProperty(Ability, TEXT("BlockAbilitiesWithTag"));
		const FGameplayTagContainer* OwnedTags = GetTagContainerProperty(Ability, TEXT("ActivationOwnedTags"));
		const FGameplayTagContainer* BlockedTags = GetTagContainerProperty(Ability, TEXT("ActivationBlockedTags"));

		Test.TestTrue(
			*FString::Printf(TEXT("%s cancels interruptible attacks"), *Context),
			CancelTags && CancelTags->HasTagExact(GameplayAbility_InterruptibleByHit));
		Test.TestTrue(
			*FString::Printf(TEXT("%s blocks new interruptible attacks"), *Context),
			BlockTags && BlockTags->HasTagExact(GameplayAbility_InterruptibleByHit));
		Test.TestTrue(
			*FString::Printf(TEXT("%s owns State.Damaged while active"), *Context),
			OwnedTags && OwnedTags->HasTagExact(State_Damaged));
		Test.TestTrue(
			*FString::Printf(TEXT("%s cannot activate after death"), *Context),
			BlockedTags && BlockedTags->HasTagExact(State_Dead));

		return !Test.HasAnyErrors();
	}

	void VerifyInterruptibleAttackPolicy(
		FAutomationTestBase& Test,
		const TCHAR* AbilityClassPath,
		const FString& Context)
	{
		const UClass* AbilityClass = LoadObject<UClass>(nullptr, AbilityClassPath);
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s Blueprint class loads"), *Context), AbilityClass))
		{
			return;
		}

		const UGameplayAbility* Ability = AbilityClass->GetDefaultObject<UGameplayAbility>();
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s CDO exists"), *Context), Ability))
		{
			return;
		}

		Test.TestTrue(
			*FString::Printf(TEXT("%s is interruptible by hit reactions"), *Context),
			Ability->GetAssetTags().HasTagExact(GameplayAbility_InterruptibleByHit));

		const FGameplayTagContainer* BlockedTags = GetTagContainerProperty(Ability, TEXT("ActivationBlockedTags"));
		Test.TestTrue(
			*FString::Printf(TEXT("%s cannot restart while damaged"), *Context),
			BlockedTags && BlockedTags->HasTagExact(State_Damaged));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHitReactionAbilityPolicyTest,
	"ArtisticSW.GAS.HitReaction.Policy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHitReactionAbilityPolicyTest::RunTest(const FString& Parameters)
{
	HitReactionAbilityTests::VerifyPolicy(
		*this,
		GetDefault<UBaseHitReactionGameplayAbility>(),
		TEXT("Native HitReaction"));

	const UClass* PlayerAbilityClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/GameplayAbilitySystem/Ability/GA_HitReact.GA_HitReact_C"));
	if (TestNotNull(TEXT("Player HitReaction Blueprint class loads"), PlayerAbilityClass))
	{
		HitReactionAbilityTests::VerifyPolicy(
			*this,
			PlayerAbilityClass->GetDefaultObject<UBaseHitReactionGameplayAbility>(),
			TEXT("Player HitReaction Blueprint"));
	}

	const UClass* EnemyAbilityClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/GameplayAbilitySystem/Ability/Enemy/BPGA_EnemyHitreaction.BPGA_EnemyHitreaction_C"));
	if (TestNotNull(TEXT("Enemy HitReaction Blueprint class loads"), EnemyAbilityClass))
	{
		HitReactionAbilityTests::VerifyPolicy(
			*this,
			EnemyAbilityClass->GetDefaultObject<UBaseHitReactionGameplayAbility>(),
			TEXT("Enemy HitReaction Blueprint"));
	}

	HitReactionAbilityTests::VerifyInterruptibleAttackPolicy(
		*this,
		TEXT("/Game/GameplayAbilitySystem/Ability/Player/BPGA_BasicAttack.BPGA_BasicAttack_C"),
		TEXT("Player basic attack"));
	HitReactionAbilityTests::VerifyInterruptibleAttackPolicy(
		*this,
		TEXT("/Game/GameplayAbilitySystem/Ability/Enemy/BPGA_MeleeAttack.BPGA_MeleeAttack_C"),
		TEXT("Enemy melee attack"));
	HitReactionAbilityTests::VerifyInterruptibleAttackPolicy(
		*this,
		TEXT("/Game/GameplayAbilitySystem/Ability/Enemy/BPGA_RangedAttack.BPGA_RangedAttack_C"),
		TEXT("Enemy ranged attack"));

	return !HasAnyErrors();
}

#endif
