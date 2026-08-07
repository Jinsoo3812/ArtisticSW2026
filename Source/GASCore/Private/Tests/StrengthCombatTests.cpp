#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "GASCombatLibrary.h"
#include "GASDamageInstantGameplayEffect.h"
#include "GASStrengthEquipmentGameplayEffect.h"
#include "Item/BaseItem.h"
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
	Request.DamageEffectClass = UGASDamageInstantGameplayEffect::StaticClass();
	Request.AttackCoefficient = 1.5f;
	Request.ChargeMultiplier = 2.0f;
	Request.InstigatorActor = SourceActor;
	Request.EffectCauser = SourceActor;
	const FGameplayEffectSpecHandle DamageSpec = UGASCombatLibrary::MakeStrengthDamageEffectSpec(Request);

	if (!TestTrue(TEXT("Strength damage spec is valid"), DamageSpec.IsValid() && DamageSpec.Data.IsValid()))
	{
		return false;
	}

	TestEqual(TEXT("Spec snapshots the final launch-time damage"),
		DamageSpec.Data->GetSetByCallerMagnitude(Data_Damage, false, 0.0f), 36.0f);
	Attributes->InitStrength(99.0f);
	TestEqual(TEXT("Changing Strength does not mutate an existing spec"),
		DamageSpec.Data->GetSetByCallerMagnitude(Data_Damage, false, 0.0f), 36.0f);

	const UGASDamageInstantGameplayEffect* DamageEffectCDO = GetDefault<UGASDamageInstantGameplayEffect>();
	if (TestEqual(TEXT("Common damage GE has exactly one modifier"), DamageEffectCDO->Modifiers.Num(), 1))
	{
		TestTrue(TEXT("Common damage GE writes only to the Damage meta attribute"),
			DamageEffectCDO->Modifiers[0].Attribute == UBaseAttributeSet::GetDamageAttribute());
	}

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

	TestTrue(TEXT("Item accepts a pre-equip Strength bonus"), Item->SetStrengthBonus(5.0f));
	TestTrue(TEXT("Equip Strength GE is applied"),
		Item->ApplyStrengthBonusEffect(ASC, UGASStrengthEquipmentGameplayEffect::StaticClass()));
	TestEqual(TEXT("Strength 10 plus weapon 5 equals 15"), Attributes->GetStrength(), 15.0f);

	TestTrue(TEXT("Applying the same item twice is treated as an idempotent success"),
		Item->ApplyStrengthBonusEffect(ASC, UGASStrengthEquipmentGameplayEffect::StaticClass()));
	TestEqual(TEXT("Duplicate equip does not stack Strength"), Attributes->GetStrength(), 15.0f);
	TestFalse(TEXT("An active item bonus cannot be mutated"), Item->SetStrengthBonus(20.0f));

	TestTrue(TEXT("Unequip removes the exact active GE handle"), Item->RemoveStrengthBonusEffect());
	TestEqual(TEXT("Unequip restores base Strength"), Attributes->GetStrength(), 10.0f);
	TestFalse(TEXT("Unequipped item no longer owns an active handle"), Item->HasActiveStrengthBonusEffect());
	return true;
}

#endif
