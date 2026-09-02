#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BaseGameplayTags.h"
#include "Repair/ShipLeakDamageGameplayEffect.h"
#include "Repair/ShipRepairPointComponent.h"
#include "Repair/ShipRepairTypes.h"
#include "Ship.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShipRepairRulesTest,
	"ArtisticSW.Ship.Repair.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipRepairRulesTest::RunTest(const FString& Parameters)
{
	const TArray<float> Thresholds = { 0.75f, 0.50f, 0.25f };
	TestEqual(TEXT("Above 75% requires no leak"), FShipRepairSpawnRules::GetRequiredLeakCount(0.76f, Thresholds, 3), 0);
	TestEqual(TEXT("At 75% requires one leak"), FShipRepairSpawnRules::GetRequiredLeakCount(0.75f, Thresholds, 3), 1);
	TestEqual(TEXT("At 50% requires two leaks"), FShipRepairSpawnRules::GetRequiredLeakCount(0.50f, Thresholds, 3), 2);
	TestEqual(TEXT("At 25% requires three leaks"), FShipRepairSpawnRules::GetRequiredLeakCount(0.25f, Thresholds, 3), 3);
	TestTrue(TEXT("A missing required leak bypasses chance"),
		FShipRepairSpawnRules::ShouldCreateLeak(1, 2, 2, 0.0f, 1.0f));
	TestTrue(TEXT("A roll inside 10 percent creates a leak"),
		FShipRepairSpawnRules::ShouldCreateLeak(0, 3, 0, 0.10f, 0.0999f));
	TestFalse(TEXT("A roll above 10 percent does not create a leak"),
		FShipRepairSpawnRules::ShouldCreateLeak(0, 3, 0, 0.10f, 0.1001f));
	TestFalse(TEXT("No inactive point means no leak"),
		FShipRepairSpawnRules::ShouldCreateLeak(3, 0, 3, 1.0f, 0.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShipRepairAuthoringDefaultsTest,
	"ArtisticSW.Ship.Repair.AuthoringDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipRepairAuthoringDefaultsTest::RunTest(const FString& Parameters)
{
	const AShip* Ship = GetDefault<AShip>();
	TestNotNull(TEXT("Repair point 1 exists"), Ship->RepairPoint1.Get());
	TestNotNull(TEXT("Repair point 2 exists"), Ship->RepairPoint2.Get());
	TestNotNull(TEXT("Repair point 3 exists"), Ship->RepairPoint3.Get());
	TestTrue(TEXT("Leak damage uses a gameplay effect"),
		Ship->LeakDamageGameplayEffectClass == UShipLeakDamageGameplayEffect::StaticClass());

	float Amount = 0.0f;
	TestTrue(TEXT("Wooden plank is accepted"), Ship->ResolveRepairMaterial(Item_Id_Material_ShipMaterials_WoodenPlank, Amount));
	TestEqual(TEXT("Wooden plank restores 20"), Amount, 20.0f);
	TestTrue(TEXT("Iron plate is accepted"), Ship->ResolveRepairMaterial(Item_Id_Material_ShipMaterials_IronPlate, Amount));
	TestEqual(TEXT("Iron plate restores 40"), Amount, 40.0f);
	return true;
}

#endif
