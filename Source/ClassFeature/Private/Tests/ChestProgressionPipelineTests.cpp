#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BaseGameplayTags.h"
#include "Settings_Item.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"
#include "Item/ItemData.h"
#include "ItemSpawn/GlobalLootSpawnManager.h"
#include "ItemSpawn/LootSpawnPoint.h"
#include "Storage/StorageChest.h"
#include "Upgrade/ShipUpgradeTreeDataAsset.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChestProgressionAuthoredAssetsTest,
	"ArtisticSW.Chest.Progression.AuthoredAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChestProgressionAuthoredAssetsTest::RunTest(const FString& Parameters)
{
	const USettings_Item* Settings = GetDefault<USettings_Item>();
	const UItemData* Items = Settings->ItemAssetRegistry.LoadSynchronous();
	const UDataTable* Recipes = Settings->CraftingRecipeDataTable.LoadSynchronous();
	const UProgressionBalanceData* Balance = UProgressionBalanceData::LoadConfigured();
	const UShipUpgradeTreeDataAsset* Tree = LoadObject<UShipUpgradeTreeDataAsset>(nullptr,
		TEXT("/Game/Blueprints/Item/Data/ShipUpgrade/DA_ShipUpgradeTree.DA_ShipUpgradeTree"));
	if (!TestNotNull(TEXT("Configured item definitions"), Items)
		|| !TestNotNull(TEXT("Configured recipes"), Recipes)
		|| !TestNotNull(TEXT("Configured progression"), Balance)
		|| !TestNotNull(TEXT("Ship tree"), Tree)) return false;
	const FArrayProperty* ZonePlansProperty = FindFProperty<FArrayProperty>(UProgressionBalanceData::StaticClass(), GET_MEMBER_NAME_CHECKED(UProgressionBalanceData, ZonePlans));
	if (!TestNotNull(TEXT("Zone Plans property exists"), ZonePlansProperty)) return false;
	TestTrue(TEXT("Zone Plans is editable on the actual Progression DA"), ZonePlansProperty->HasAnyPropertyFlags(CPF_Edit));
	const auto CheckPlanField = [this](FName FieldName, bool bShouldBeEditable)
	{
		const FProperty* Field = FProgressionZonePlan::StaticStruct()->FindPropertyByName(FieldName);
		if (TestNotNull(*FString::Printf(TEXT("Zone Plan field %s"), *FieldName.ToString()), Field))
		{
			TestEqual(*FString::Printf(TEXT("Zone Plan field %s editor visibility"), *FieldName.ToString()),
				Field->HasAnyPropertyFlags(CPF_Edit), bShouldBeEditable);
		}
	};
	CheckPlanField(GET_MEMBER_NAME_CHECKED(FProgressionZonePlan, Zone), true);
	CheckPlanField(GET_MEMBER_NAME_CHECKED(FProgressionZonePlan, OceanActiveChests), true);
	CheckPlanField(GET_MEMBER_NAME_CHECKED(FProgressionZonePlan, IslandActiveChests), true);
	CheckPlanField(GET_MEMBER_NAME_CHECKED(FProgressionZonePlan, ShipSquads), false);
	CheckPlanField(GET_MEMBER_NAME_CHECKED(FProgressionZonePlan, ShipsPerSquad), false);
	CheckPlanField(GET_MEMBER_NAME_CHECKED(FProgressionZonePlan, IslandGuardSquads), false);
	CheckPlanField(GET_MEMBER_NAME_CHECKED(FProgressionZonePlan, WeaponCraftCount), false);
	CheckPlanField(GET_MEMBER_NAME_CHECKED(FProgressionZonePlan, ConsumableCraftCount), false);
	CheckPlanField(GET_MEMBER_NAME_CHECKED(FProgressionZonePlan, ShipUpgradeNodeCount), false);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const EProgressionZone Zone = static_cast<EProgressionZone>(Index);
		const FProgressionZonePlan* Plan = Balance->FindZone(Zone);
		if (TestNotNull(*FString::Printf(TEXT("Actual DA Zone %d plan"), Index), Plan))
		{
			TestEqual(TEXT("Ocean activation reads actual DA plan"), Balance->GetActiveCount(Zone, EProgressionChestKind::OceanRandom), Plan->OceanActiveChests);
			TestEqual(TEXT("Island activation reads actual DA plan"), Balance->GetActiveCount(Zone, EProgressionChestKind::IslandRandom), Plan->IslandActiveChests);
			UE_LOG(LogTemp, Display, TEXT("Actual Progression DA zone=%d ocean=%d island=%d"), Index, Plan->OceanActiveChests, Plan->IslandActiveChests);
		}
		const FProgressionZoneTarget* Target = Balance->FindTarget(Zone);
		if (!TestNotNull(*FString::Printf(TEXT("Zone %d target"), Index), Target)) continue;
		TestEqual(*FString::Printf(TEXT("Zone %d clear target"), Index), Target->FullClears, 1);
		TestEqual(*FString::Printf(TEXT("Zone %d weapon target"), Index), Target->WeaponCrafts, 1);
		TestEqual(*FString::Printf(TEXT("Zone %d consumable target"), Index), Target->ConsumableCrafts, 2);
		TestEqual(*FString::Printf(TEXT("Zone %d ship target"), Index), Target->ShipUpgrades, 3);
		TArray<FProgressionComputedDrop> Drops;
		TestTrue(*FString::Printf(TEXT("Zone %d calculates material drops from real assets"), Index),
			AGlobalLootSpawnManager::CalculateZoneDrops(Zone, *Target, 5, Items, Recipes, Tree, Drops));
		TestTrue(*FString::Printf(TEXT("Zone %d has material drops"), Index), !Drops.IsEmpty());
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChestProgressionPipelineTest,
	"ArtisticSW.Chest.Progression.DefinitionRecipeTargetManager",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChestProgressionPipelineTest::RunTest(const FString& Parameters)
{
	// Creating the transient game world initializes the project's existing recipe fixture.
	AddExpectedError(TEXT("QuestItem has an invalid ResultItemTag"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("QuestItem contains an invalid ingredient"), EAutomationExpectedErrorFlags::Contains, 2);
	UItemData* Items = NewObject<UItemData>();
	const auto Define = [Items](FGameplayTag Tag, EItemProgressionKind Kind, int32 Tier)
	{
		FItemDefinition& Definition = Items->ItemDefinitions.Add(Tag);
		Definition.ProgressionKind = Kind;
		Definition.ProgressionTier = Tier;
	};
	Define(Item_Id_Weapon_Sword_SwordA2, EItemProgressionKind::Weapon, 1);
	Define(Item_Id_Weapon_Sword_SwordB2, EItemProgressionKind::Weapon, 1);
	Define(Item_Id_Consumables_Heal_Medicine, EItemProgressionKind::Consumable, 1);
	Define(Item_Id_Material_WeaponMaterial_Wood, EItemProgressionKind::WeaponMaterial, 1);
	Define(Item_Id_Material_WeaponMaterial_GoodWood, EItemProgressionKind::WeaponMaterial, 1);
	Define(Item_Id_Material_WeaponMaterial_GoodIron, EItemProgressionKind::WeaponMaterial, 4);
	Define(Item_Id_Material_ConsumablesMaterial_Pear, EItemProgressionKind::ConsumableMaterial, 1);
	Define(Item_Id_Material_ShipMaterials_WoodenPlank, EItemProgressionKind::ShipMaterial, 1);
	const TArray<FGameplayTag> WeaponOptions = Items->GetCraftingMaterialOptions(Item_Id_Weapon_Sword_SwordA2);
	TestTrue(TEXT("Weapon picker includes weapon materials"), WeaponOptions.Contains(Item_Id_Material_WeaponMaterial_Wood));
	TestFalse(TEXT("Weapon picker excludes consumable materials"), WeaponOptions.Contains(Item_Id_Material_ConsumablesMaterial_Pear));
	TestFalse(TEXT("Weapon picker excludes future-tier materials"), WeaponOptions.Contains(Item_Id_Material_WeaponMaterial_GoodIron));

	UDataTable* Recipes = NewObject<UDataTable>();
	Recipes->RowStruct = FCraftingRecipeRow::StaticStruct();
	const auto AddRecipe = [Recipes](FName Name, FGameplayTag Result, FGameplayTag Material, int32 Quantity)
	{
		FCraftingRecipeRow Row;
		Row.ResultItemTag = Result;
		FCraftingItemStack& Ingredient = Row.Ingredients.AddDefaulted_GetRef();
		Ingredient.ItemTag = Material;
		Ingredient.Quantity = Quantity;
		Recipes->AddRow(Name, Row);
	};
	AddRecipe(TEXT("SwordA2"), Item_Id_Weapon_Sword_SwordA2, Item_Id_Material_WeaponMaterial_Wood, 4);
	AddRecipe(TEXT("SwordB2"), Item_Id_Weapon_Sword_SwordB2, Item_Id_Material_WeaponMaterial_GoodWood, 6);
	AddRecipe(TEXT("Medicine"), Item_Id_Consumables_Heal_Medicine, Item_Id_Material_ConsumablesMaterial_Pear, 3);

	UShipUpgradeTreeDataAsset* Tree = NewObject<UShipUpgradeTreeDataAsset>();
	for (int32 Quantity : {4, 8})
	{
		FShipUpgradeNodeDefinition& Node = Tree->Nodes.AddDefaulted_GetRef();
		Node.StatTrack = Quantity == 4 ? EShipUpgradeStatTrack::Hull : EShipUpgradeStatTrack::Mobility;
		Node.TrackLevel = 1;
		FCraftingItemStack& Cost = Node.ActivationCosts.AddDefaulted_GetRef();
		Cost.ItemTag = Item_Id_Material_ShipMaterials_WoodenPlank;
		Cost.Quantity = Quantity;
	}
	FProgressionZoneTarget Target;
	Target.Zone = EProgressionZone::Mid1;
	Target.FullClears = 2;
	Target.WeaponCrafts = 2;
	Target.ConsumableCrafts = 3;
	Target.ShipUpgrades = 2;
	TArray<FProgressionComputedDrop> Drops;
	TestTrue(TEXT("Definitions + recipes + ship nodes + target calculate"),
		AGlobalLootSpawnManager::CalculateZoneDrops(EProgressionZone::Mid1, Target, 5, Items, Recipes, Tree, Drops));
	const auto FindDrop = [&Drops](FGameplayTag Tag) -> const FProgressionComputedDrop*
	{
		return Drops.FindByPredicate([Tag](const FProgressionComputedDrop& Drop) { return Drop.ItemTag == Tag; });
	};
	const FProgressionComputedDrop* Wood = FindDrop(Item_Id_Material_WeaponMaterial_Wood);
	const FProgressionComputedDrop* Pear = FindDrop(Item_Id_Material_ConsumablesMaterial_Pear);
	const FProgressionComputedDrop* Plank = FindDrop(Item_Id_Material_ShipMaterials_WoodenPlank);
	TestNotNull(TEXT("Weapon material included"), Wood);
	TestNotNull(TEXT("Consumable material included"), Pear);
	TestNotNull(TEXT("Ship material included"), Plank);
	if (Wood) TestTrue(TEXT("Weapon average supports two crafts"), FMath::IsNearlyEqual(5 * 2 * Wood->Chance * Wood->MinCount, 4.f, .001f));
	if (Pear) TestTrue(TEXT("Consumable average supports three crafts"), FMath::IsNearlyEqual(5 * 2 * Pear->Chance * Pear->MinCount, 9.f, .001f));
	if (Plank) TestTrue(TEXT("Ship node average supports two upgrades"), FMath::IsNearlyEqual(5 * 2 * Plank->Chance * Plank->MinCount, 12.f, .001f));

	TArray<FProgressionComputedDrop> TenChestDrops;
	TestTrue(TEXT("Manager count recalculates drops"),
		AGlobalLootSpawnManager::CalculateZoneDrops(EProgressionZone::Mid1, Target, 10, Items, Recipes, Tree, TenChestDrops));
	if (Wood)
	{
		const FProgressionComputedDrop* MoreChests = TenChestDrops.FindByPredicate([](const FProgressionComputedDrop& Drop)
		{ return Drop.ItemTag == Item_Id_Material_WeaponMaterial_Wood; });
		if (MoreChests) TestTrue(TEXT("More chests reduce each chest's expected value"),
			MoreChests->Chance * MoreChests->MinCount < Wood->Chance * Wood->MinCount);
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ChestProgressionPipelineWorld"));
	if (TestNotNull(TEXT("Spawn-point test world"), World))
	{
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.SetCurrentWorld(World);
		AChestSpawnPoint* Point = World->SpawnActor<AChestSpawnPoint>();
		FChestSpawnPointChestSettings ChestSettings;
		ChestSettings.ProgressionZone = EProgressionZone::Mid1;
		ChestSettings.ProgressionKind = EProgressionChestKind::OceanRandom;
		ChestSettings.SpawnMode = EChestSpawnMode::Random;
		FChestSpawnPointLootSettings LootSettings;
		Point->ApplyAuthoringSettings(ChestSettings, LootSettings);
		TestEqual(TEXT("Point reports its zone"), Point->GetProgressionZone(), EProgressionZone::Mid1);
		TestEqual(TEXT("Point reports its chest kind"), Point->GetProgressionKind(), EProgressionChestKind::OceanRandom);
		AStorageChest* Chest = Point->SpawnConfiguredChest(nullptr, 77);
		if (TestNotNull(TEXT("Point spawns without loot DA/DT"), Chest))
		{
			FProgressionComputedDrop Certain;
			Certain.ItemTag = Item_Id_Material_WeaponMaterial_Wood;
			Certain.Chance = 1.f;
			Certain.MinCount = 3;
			Certain.MaxCount = 3;
			Chest->ReplaceProgressionLoot({Certain}, Items, 77);
			bool bFound = false;
			for (const FInventorySlot& Slot : Chest->GetStorageComponent()->GetSlots())
			{
				bFound |= Slot.ItemTag == Certain.ItemTag && Slot.Count == 3;
			}
			TestTrue(TEXT("Calculated material reaches chest storage"), bFound);
			UProgressionBalanceData* Balance = NewObject<UProgressionBalanceData>();
			FProgressionZoneTarget* Mid1 = Balance->ZoneTargets.FindByPredicate([](const FProgressionZoneTarget& Entry)
			{ return Entry.Zone == EProgressionZone::Mid1; });
			if (TestNotNull(TEXT("Four fixed zone target rows exist"), Mid1))
			{
				*Mid1 = Target;
				AGlobalLootSpawnManager* Manager = World->SpawnActor<AGlobalLootSpawnManager>();
				Manager->SetInitializeOnBeginPlayForTesting(false);
				TestTrue(TEXT("Manager discovers point and applies calculated loot"),
					Manager->RebalanceSpawnedChestsWithData(Balance, Items, Recipes, Tree));
				TestEqual(TEXT("Manager census counts only the active point"),
					Manager->GetLastActiveChestCount(EProgressionZone::Mid1), 1);
				FStorageItemEntry Extra;
				Extra.ItemTag = Item_Id_Material_WeaponSpecialMaterial_LegendaryMaterial;
				Extra.Count = 1;
				Chest->AppendFixedLoot({Extra});
				bool bHasBaseMaterial = false;
				bool bHasSpecialMaterial = false;
				for (const FInventorySlot& Slot : Chest->GetStorageComponent()->GetSlots())
				{
					bHasBaseMaterial |= Slot.ItemTag == Item_Id_Material_WeaponMaterial_Wood;
					bHasSpecialMaterial |= Slot.ItemTag == Item_Id_Material_WeaponSpecialMaterial_LegendaryMaterial;
				}
				TestTrue(TEXT("Fixed drop append preserves automated material"), bHasBaseMaterial);
				TestTrue(TEXT("Fixed drop append stores special material"), bHasSpecialMaterial);
				TArray<FProgressionComputedDrop> SunkDrops;
				TestTrue(TEXT("Sunk chest reuses finalized deck-zone drops"),
					Manager->GetSunkChestDrops(EProgressionZone::Mid1, SunkDrops));
				const UProgressionBalanceData* ConfiguredBalance = UProgressionBalanceData::LoadConfigured();
				const FProgressionComputedDrop* SunkWood = SunkDrops.FindByPredicate([](const FProgressionComputedDrop& Drop)
				{ return Drop.ItemTag == Item_Id_Material_WeaponMaterial_Wood; });
				if (TestNotNull(TEXT("Sunk wood entry"), SunkWood) && ConfiguredBalance)
				{
					TArray<FProgressionComputedDrop> DeckDrops;
					AGlobalLootSpawnManager::CalculateZoneDrops(EProgressionZone::Mid1, Target, 1, Items, Recipes, Tree, DeckDrops);
					const FProgressionComputedDrop* DeckWood = DeckDrops.FindByPredicate([](const FProgressionComputedDrop& Drop)
					{ return Drop.ItemTag == Item_Id_Material_WeaponMaterial_Wood; });
					if (TestNotNull(TEXT("Deck wood entry"), DeckWood))
					{
						TestTrue(TEXT("Sunk chance is deck chance times configured ratio"),
							FMath::IsNearlyEqual(SunkWood->Chance,
								DeckWood->Chance * ConfiguredBalance->SunkChestExpectedValueRatio));
						TestEqual(TEXT("Sunk stack size equals deck stack size"), SunkWood->MinCount, DeckWood->MinCount);
					}
				}
				TestFalse(TEXT("One-shot finalization prevents loot reroll"),
					Manager->RebalanceSpawnedChestsWithData(Balance, Items, Recipes, Tree));
			}
		}
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
	return !HasAnyErrors();
}

#endif
