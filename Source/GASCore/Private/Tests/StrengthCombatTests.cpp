#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "GASCombatLibrary.h"
#include "GAS/SWCombatEffectContextLibrary.h"
#include "GASAttributeDamageGameplayEffect.h"
#include "GASDamageInstantGameplayEffect.h"
#include "GASStrengthEquipmentGameplayEffect.h"
#include "Item/BaseItem.h"
#include "Components/EquipmentStatComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace StrengthCombatTests
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;

		FScopedTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("StrengthCombatTestWorld"));
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
	FStrengthDamageFormulaTest,
	"ArtisticSW.GAS.Strength.DamageFormula",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrengthDamageFormulaTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Default Strength is 10"), GetDefault<UBaseAttributeSet>()->GetStrength(), 10.0f);
	TestEqual(TEXT("Strength times attack coefficient"), UGASCombatLibrary::CalculateStrengthDamage(10.0f, 1.5f), 15.0f);
	TestEqual(TEXT("Charge multiplier participates in the same formula"),
		UGASCombatLibrary::CalculateStrengthDamage(10.0f, 1.5f, 2.0f), 30.0f);
	TestEqual(TEXT("Damage has a minimum of one"), UGASCombatLibrary::CalculateStrengthDamage(0.0f, 0.0f, 0.0f), 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FStrengthDamageSpecSnapshotTest,
	"ArtisticSW.GAS.Strength.DamageSpecSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrengthDamageSpecSnapshotTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("QuestItem (has an invalid ResultItemTag|contains an invalid ingredient)"),
		EAutomationExpectedErrorFlags::Contains, 0);
	StrengthCombatTests::FScopedTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient game world is created"), TestWorld.World))
	{
		return false;
	}

	AActor* SourceActor = TestWorld.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Source actor is spawned"), SourceActor))
	{
		return false;
	}

	UAbilitySystemComponent* SourceASC = NewObject<UAbilitySystemComponent>(SourceActor, TEXT("StrengthTestASC"));
	SourceASC->RegisterComponent();
	SourceASC->InitAbilityActorInfo(SourceActor, SourceActor);
	UBaseAttributeSet* Attributes = NewObject<UBaseAttributeSet>(SourceActor);
	SourceASC->AddAttributeSetSubobject(Attributes);
	Attributes->InitStrength(12.0f);

	FStrengthDamageRequest Request;
	Request.SourceASC = SourceASC;


	Request.AttackCoefficient = 1.5f;
	Request.ChargeMultiplier = 2.0f;
	Request.InstigatorActor = SourceActor;
	Request.EffectCauser = SourceActor;
	const FGameplayEffectSpecHandle DamageSpec = UGASCombatLibrary::MakeStrengthDamageEffectSpec(Request);

	if (!TestTrue(TEXT("Strength damage spec is valid"), DamageSpec.IsValid() && DamageSpec.Data.IsValid()))
	{
		return false;
	}

	TestTrue(TEXT("Only native execution GE is selected"), DamageSpec.Data->Def->GetClass() == UGASAttributeDamageGameplayEffect::StaticClass());
	TestFalse(TEXT("Legacy final damage input is absent"), DamageSpec.Data->SetByCallerTagMagnitudes.Contains(Data_Damage));
	TestEqual(TEXT("Native GE has no direct modifiers"), DamageSpec.Data->Def->Modifiers.Num(), 0);
	TestEqual(TEXT("Native GE has one execution"), DamageSpec.Data->Def->Executions.Num(), 1);

	const UGASStrengthEquipmentGameplayEffect* StrengthEffectCDO = GetDefault<UGASStrengthEquipmentGameplayEffect>();
	TestEqual(TEXT("Equipment Strength GE is infinite"),
		StrengthEffectCDO->DurationPolicy, EGameplayEffectDurationType::Infinite);
	if (TestEqual(TEXT("Equipment Strength GE has exactly one modifier"), StrengthEffectCDO->Modifiers.Num(), 1))
	{
		TestTrue(TEXT("Equipment Strength GE modifies Strength"),
			StrengthEffectCDO->Modifiers[0].Attribute == UBaseAttributeSet::GetStrengthAttribute());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAttributeDamageExecutionTest,
	"ArtisticSW.GAS.Strength.AttributeDamageExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAttributeDamageExecutionTest::RunTest(const FString& Parameters)
{
	StrengthCombatTests::FScopedTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient game world is created"), TestWorld.World))
	{
		return false;
	}

	AActor* SourceActor = TestWorld.World->SpawnActor<AActor>();
	AActor* TargetActor = TestWorld.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Source actor is spawned"), SourceActor)
		|| !TestNotNull(TEXT("Target actor is spawned"), TargetActor))
	{
		return false;
	}

	UAbilitySystemComponent* SourceASC = NewObject<UAbilitySystemComponent>(SourceActor);
	SourceASC->RegisterComponent();
	SourceASC->InitAbilityActorInfo(SourceActor, SourceActor);
	UBaseAttributeSet* SourceAttributes = NewObject<UBaseAttributeSet>(SourceActor);
	SourceASC->AddAttributeSetSubobject(SourceAttributes);
	SourceAttributes->InitStrength(12.0f);

	UAbilitySystemComponent* TargetASC = NewObject<UAbilitySystemComponent>(TargetActor);
	TargetASC->RegisterComponent();
	TargetASC->InitAbilityActorInfo(TargetActor, TargetActor);
	UBaseAttributeSet* TargetAttributes = NewObject<UBaseAttributeSet>(TargetActor);
	TargetASC->AddAttributeSetSubobject(TargetAttributes);
	TargetAttributes->InitMaxHealth(100.0f);
	TargetAttributes->InitHealth(100.0f);

	FStrengthDamageRequest Request;
	Request.SourceASC = SourceASC;

	Request.AttackCoefficient = 1.5f;
	Request.ChargeMultiplier = 2.0f;
	Request.InstigatorActor = SourceActor;
	Request.EffectCauser = SourceActor;
	const FGameplayEffectSpecHandle DamageSpec = UGASCombatLibrary::MakeStrengthDamageEffectSpec(Request);
	if (!TestTrue(TEXT("Attribute damage spec is valid"), DamageSpec.IsValid() && DamageSpec.Data.IsValid()))
	{
		return false;
	}

	TestEqual(TEXT("Weapon coefficient is carried by the spec"),
		DamageSpec.Data->GetSetByCallerMagnitude(Data_AttackCoefficient, false, 0.0f), 1.5f);
	TestEqual(TEXT("Charge multiplier is carried by the spec"),
		DamageSpec.Data->GetSetByCallerMagnitude(Data_ChargeMultiplier, false, 0.0f), 2.0f);
	TestEqual(TEXT("Strength damage is explicitly classified as a direct hit"),
		static_cast<uint8>(USWCombatEffectContextLibrary::GetDamageDeliveryType(
			DamageSpec.Data->GetContext())),
		static_cast<uint8>(ESWDamageDeliveryType::DirectHit));

	// Source captures are snapshotted at spec creation, so an already-fired
	// projectile or open melee window cannot change damage retroactively.
	SourceAttributes->SetStrength(99.0f);
	TargetASC->ApplyGameplayEffectSpecToSelf(*DamageSpec.Data.Get());
	TestEqual(TEXT("Execution uses the launch-time Strength snapshot"), TargetAttributes->GetHealth(), 64.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FStrengthEquipmentLifecycleTest,
	"ArtisticSW.GAS.Strength.EquipmentLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrengthEquipmentLifecycleTest::RunTest(const FString& Parameters)
{
	StrengthCombatTests::FScopedTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient game world is created"), TestWorld.World))
	{
		return false;
	}

	AActor* OwnerActor = TestWorld.World->SpawnActor<AActor>();
	ABaseItem* Item = TestWorld.World->SpawnActor<ABaseItem>();
	if (!TestNotNull(TEXT("Owner actor is spawned"), OwnerActor)
		|| !TestNotNull(TEXT("Equipment item is spawned"), Item))
	{
		return false;
	}

	UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(OwnerActor, TEXT("EquipmentTestASC"));
	ASC->RegisterComponent();
	ASC->InitAbilityActorInfo(OwnerActor, OwnerActor);
	UBaseAttributeSet* Attributes = NewObject<UBaseAttributeSet>(OwnerActor);
	ASC->AddAttributeSetSubobject(Attributes);
	Attributes->InitStrength(10.0f);

	Item->SetOwner(OwnerActor);
	auto* Equipment = UEquipmentStatComponent::GetOrCreate(OwnerActor);
	TestTrue(TEXT("Item accepts a pre-equip Strength bonus"), Item->SetStrengthBonus(5.0f));
	TestTrue(TEXT("Equip Strength GE is applied"),
		Equipment->Equip(ASC, Item, Item->GetStrengthBonus()));
	TestEqual(TEXT("Strength 10 plus weapon 5 equals 15"), Attributes->GetStrength(), 15.0f);

	TestTrue(TEXT("Applying the same item twice is treated as an idempotent success"),
		Equipment->Equip(ASC, Item, Item->GetStrengthBonus()));
	TestEqual(TEXT("Duplicate equip does not stack Strength"), Attributes->GetStrength(), 15.0f);
	TestFalse(TEXT("An active item bonus cannot be mutated"), Item->SetStrengthBonus(20.0f));

	TestTrue(TEXT("Unequip removes the exact active GE handle"), Equipment->Clear());
	TestEqual(TEXT("Unequip restores base Strength"), Attributes->GetStrength(), 10.0f);
	TestFalse(TEXT("Unequipped item no longer owns an active handle"), Item->HasActiveStrengthBonusEffect());
	return true;
}

#endif
