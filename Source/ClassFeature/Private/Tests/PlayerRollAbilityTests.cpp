#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Abilities/GameplayAbility.h"
#include "BaseGameplayTags.h"
#include "GAS/Ability/GA_PlayerRoll.h"
#include "Roll/RollTypes.h"
#include "UObject/UnrealType.h"

namespace PlayerRollAbilityTests
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlayerRollAbilityPolicyTest,
	"ArtisticSW.GAS.Roll.AbilityPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerRollAbilityPolicyTest::RunTest(const FString& Parameters)
{
	const UGA_PlayerRoll* Ability = GetDefault<UGA_PlayerRoll>();
	if (!TestNotNull(TEXT("Native player roll CDO exists"), Ability))
	{
		return false;
	}

	TestTrue(TEXT("Roll has its identifying asset tag"),
		Ability->GetAssetTags().HasTagExact(GameplayAbility_Player_Roll));
	TestEqual(TEXT("Roll input uses the default C-key tag hierarchy"),
		Key_Default_C.GetTag().GetTagName(), FName(TEXT("Key.Default.C")));
	TestEqual(TEXT("Roll recovery uses a stable gameplay event contract"),
		Event_Ability_Roll_Recovery.GetTag().GetTagName(),
		FName(TEXT("Event.Ability.Roll.Recovery")));
	TestTrue(TEXT("Roll can be interrupted by confirmed hit reactions"),
		Ability->GetAssetTags().HasTagExact(GameplayAbility_InterruptibleByHit));
	TestEqual(TEXT("Roll keeps one ability instance per avatar"),
		Ability->GetInstancingPolicy(), EGameplayAbilityInstancingPolicy::InstancedPerActor);
	TestEqual(TEXT("Roll is locally predicted"),
		Ability->GetNetExecutionPolicy(), EGameplayAbilityNetExecutionPolicy::LocalPredicted);

	const FGameplayTagContainer* OwnedTags = PlayerRollAbilityTests::GetTagContainerProperty(
		Ability, TEXT("ActivationOwnedTags"));
	const FGameplayTagContainer* BlockedTags = PlayerRollAbilityTests::GetTagContainerProperty(
		Ability, TEXT("ActivationBlockedTags"));
	const FGameplayTagContainer* BlockAbilityTags = PlayerRollAbilityTests::GetTagContainerProperty(
		Ability, TEXT("BlockAbilitiesWithTag"));

	TestTrue(TEXT("Roll owns State.Rolling for its full ability lifetime"),
		OwnedTags && OwnedTags->HasTagExact(State_Rolling));
	TestTrue(TEXT("Dead actors cannot roll"),
		BlockedTags && BlockedTags->HasTagExact(State_Dead));
	TestTrue(TEXT("Damaged actors cannot start a roll"),
		BlockedTags && BlockedTags->HasTagExact(State_Damaged));
	TestTrue(TEXT("Roll cannot retrigger while already rolling"),
		BlockedTags && BlockedTags->HasTagExact(State_Rolling));
	TestTrue(TEXT("Interruptible attacks are blocked while rolling"),
		BlockAbilityTags && BlockAbilityTags->HasTagExact(GameplayAbility_InterruptibleByHit));

	const FRollIntent DefaultIntent;
	TestTrue(TEXT("Roll intent has a deterministic forward fallback"),
		DefaultIntent.WorldDirection.Equals(FVector::ForwardVector));
	TestFalse(TEXT("Default roll intent does not claim movement input"),
		DefaultIntent.bHasMovementInput);

	return !HasAnyErrors();
}

#endif
