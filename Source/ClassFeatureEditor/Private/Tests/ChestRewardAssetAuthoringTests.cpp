#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "BaseGameplayTags.h"
#include "IAssetTools.h"
#include "Balance/ProgressionBalanceData.h"
#include "Crafting/CraftingRecipeTypes.h"
#include "Engine/DataTable.h"
#include "Item/ItemData.h"
#include "Settings_Item.h"
#include "HAL/FileManager.h"
#include "ItemSpawn/ChestSpawnData.h"
#include "ItemSpawn/LootSpawnTypes.h"
#include "Misc/AutomationTest.h"
#include "Storage/StorageChest.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Upgrade/ShipUpgradeTreeDataAsset.h"
#include "Ship.h"

namespace ChestRewardAuthoring
{
	constexpr TCHAR RootPath[] = TEXT("/Game/Blueprints/Item/Data");

	const TCHAR* ZoneName(EProgressionZone Zone)
	{
		switch (Zone)
		{
		case EProgressionZone::Mid1: return TEXT("Mid_1");
		case EProgressionZone::Mid2: return TEXT("Mid_2");
		case EProgressionZone::Mid3: return TEXT("Mid_3");
		case EProgressionZone::Final: return TEXT("Final");
		default: return TEXT("Unknown");
		}
	}

	// Preserve the existing Land/Ocean/Ship names so placed points retain their references.
	const TCHAR* KindName(EProgressionChestKind Kind)
	{
		switch (Kind)
		{
		case EProgressionChestKind::ShipGuarded: return TEXT("Ship");
		case EProgressionChestKind::IslandGuarded: return TEXT("IslandGuarded");
		case EProgressionChestKind::OceanRandom: return TEXT("Ocean");
		case EProgressionChestKind::IslandRandom: return TEXT("Land");
		default: return TEXT("Unknown");
		}
	}

	FString AssetPath(const TCHAR* Folder, const TCHAR* Prefix,
		EProgressionChestKind Kind, EProgressionZone Zone)
	{
		return FString::Printf(TEXT("%s/%s/%s_%s_%s"), RootPath, Folder, Prefix,
			KindName(Kind), ZoneName(Zone));
	}

	bool SaveAsset(UObject* Asset)
	{
		if (!Asset) return false;
		UPackage* Package = Asset->GetOutermost();
		Package->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Asset);
		const FString FileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(FileName), true);
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Package, Asset, *FileName, SaveArgs);
	}

	template <typename TAsset>
	TAsset* CreateOrLoad(const FString& PackagePath)
	{
		const FString AssetName = FPackageName::GetShortName(PackagePath);
		if (TAsset* Existing = LoadObject<TAsset>(nullptr, *(PackagePath + TEXT(".") + AssetName)))
		{
			return Existing;
		}
		UPackage* Package = CreatePackage(*PackagePath);
		Package->FullyLoad();
		return NewObject<TAsset>(Package, *AssetName, RF_Public | RF_Standalone);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChestRewardAssetAuthoringTest,
	"ArtisticSW.Chest.Authoring.GenerateRewardAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChestRewardAssetAuthoringTest::RunTest(const FString& Parameters)
{
	// Retired: reward assets are no longer generated or populated from static pool snapshots.
	// Keeping this test name as a harmless compatibility alias prevents old automation commands
	// from rewriting designer-owned DA/DT values.
	AddWarning(TEXT("GenerateRewardAssets is retired. Configure DA_ItemData, DT_CraftingRecipes and ZoneTargets instead."));
	return true;

	using namespace ChestRewardAuthoring;

	UProgressionBalanceData* Balance = CreateOrLoad<UProgressionBalanceData>(
		FString(RootPath) + TEXT("/DA_ProgressionBalance"));
	if (!TestNotNull(TEXT("Progression balance asset"), Balance)) return false;
	TArray<FString> Errors;
	TestTrue(TEXT("Four zone plans and recipe-derived demand are valid"), Balance->ValidateBalance(Errors));
	for (const FString& Error : Errors) AddError(Error);
	for (int32 ZoneIndex = 0; ZoneIndex < 4; ++ZoneIndex)
	{
		const EProgressionZone Zone = static_cast<EProgressionZone>(ZoneIndex);
		TMap<FGameplayTag, int32> Demand;
		Balance->GetTierDemand(Zone, Demand);
		for (const TPair<FGameplayTag, int32>& Pair : Demand)
		{
			const float Expected = Balance->GetExpectedQuantity(Zone, Pair.Key, Balance->TargetFullClearsPerZone);
			TestTrue(*FString::Printf(TEXT("Expected demand %s zone %d: %.3f >= %d"),
				*Pair.Key.ToString(), ZoneIndex + 1, Expected, Pair.Value), Expected + .001f >= Pair.Value);
		}
	}
	if (const FProgressionZonePlan* BasePlan = Balance->FindZone(EProgressionZone::Mid1))
	{
		UProgressionBalanceData* Probe = DuplicateObject<UProgressionBalanceData>(Balance, GetTransientPackage());
		TArray<FProgressionComputedDrop> Before;
		Probe->GetComputedDrops(EProgressionZone::Mid1, EProgressionChestKind::ShipGuarded, Before);
		Probe->TargetFullClearsPerZone += 1;
		TArray<FProgressionComputedDrop> AfterTarget;
		Probe->GetComputedDrops(EProgressionZone::Mid1, EProgressionChestKind::ShipGuarded, AfterTarget);
		if (!Before.IsEmpty() && !AfterTarget.IsEmpty())
		{
			TestTrue(TEXT("Target clear count changes computed drops"),
				!FMath::IsNearlyEqual(Before[0].Chance, AfterTarget[0].Chance)
				|| Before[0].MinCount != AfterTarget[0].MinCount);
		}
		Probe->TargetFullClearsPerZone = Balance->TargetFullClearsPerZone;
		if (FProgressionZonePlan* Plan = Probe->ZonePlans.FindByPredicate([](const FProgressionZonePlan& Entry)
			{ return Entry.Zone == EProgressionZone::Mid1; }))
		{
			Plan->ShipSquads = FMath::Max(1, BasePlan->ShipSquads + 1);
			Plan->ShipsPerSquad = FMath::Max(1, BasePlan->ShipsPerSquad);
			TArray<FProgressionComputedDrop> AfterSpawn;
			Probe->GetComputedDrops(EProgressionZone::Mid1, EProgressionChestKind::ShipGuarded, AfterSpawn);
			if (!Before.IsEmpty() && !AfterSpawn.IsEmpty())
			{
				TestTrue(TEXT("Zone ship count changes computed drops"),
					!FMath::IsNearlyEqual(Before[0].Chance, AfterSpawn[0].Chance)
					|| Before[0].MinCount != AfterSpawn[0].MinCount);
			}
		}
	}
	// Never save the user's progression asset from an authoring test: it is the input.

	TSubclassOf<AStorageChest> ChestClass = StaticLoadClass(AStorageChest::StaticClass(), nullptr,
		TEXT("/Game/Blueprints/Item/BP/Chest/BP_Storage_Chest.BP_Storage_Chest_C"));
	if (!ChestClass) ChestClass = StaticLoadClass(AStorageChest::StaticClass(), nullptr,
		TEXT("/Game/Blueprints/03_WorldObject/01_ItemStorage/BP_Storage_Chest.BP_Storage_Chest_C"));
	if (!ChestClass)
	{
		ChestClass = AStorageChest::StaticClass();
		AddWarning(TEXT("BP_Storage_Chest was not loadable; native AStorageChest was used."));
	}

	int32 LootTableCount = 0;
	int32 DefinitionCount = 0;
	int32 RandomGroupCount = 0;
	for (int32 ZoneIndex = 0; ZoneIndex < 4; ++ZoneIndex)
	{
		const EProgressionZone Zone = static_cast<EProgressionZone>(ZoneIndex);
		for (int32 KindIndex = 0; KindIndex < 4; ++KindIndex)
		{
			const EProgressionChestKind Kind = static_cast<EProgressionChestKind>(KindIndex);
			TArray<FProgressionComputedDrop> Drops;
			Balance->GetComputedDrops(Zone, Kind, Drops);

			UDataTable* Table = CreateOrLoad<UDataTable>(AssetPath(TEXT("LootTable"), TEXT("DT_ChestLoot"), Kind, Zone));
			Table->RowStruct = FChestInitialLootRow::StaticStruct();
			Table->EmptyTable();
			for (const FProgressionComputedDrop& Entry : Drops)
			{
				FChestInitialLootRow Row;
				Row.ItemTag = Entry.ItemTag;
				Row.MinCount = Entry.MinCount;
				Row.MaxCount = Entry.MaxCount;
				Row.Weight = Entry.Chance; // Inspection snapshot; runtime rolls entries independently.
				Table->AddRow(Entry.ItemTag.GetTagName(), Row);
			}
			if (TestTrue(TEXT("Loot table saved"), SaveAsset(Table))) ++LootTableCount;

			UChestDefinition* Definition = CreateOrLoad<UChestDefinition>(
				AssetPath(TEXT("ChestDefinition"), TEXT("DA_Chest"), Kind, Zone));
			Definition->ChestClass = ChestClass;
			Definition->LootTable = Table; // Readable snapshot; runtime reads BalanceProfile.
			Definition->RollCount = Drops.Num();
			Definition->SlotCount = FMath::Max(8, Drops.Num());
			Definition->BalanceProfile = Balance;
			Definition->BalanceZone = Zone;
			Definition->BalanceKind = Kind;
			if (TestTrue(TEXT("Chest definition saved"), SaveAsset(Definition))) ++DefinitionCount;

			if (Kind == EProgressionChestKind::OceanRandom || Kind == EProgressionChestKind::IslandRandom)
			{
				URandomChestGroup* Group = CreateOrLoad<URandomChestGroup>(
					AssetPath(TEXT("RandomGroup"), TEXT("DA_RandomGroup"), Kind, Zone));
				Group->ChestDefinition = Definition;
				Group->SpawnCount = Balance->GetActiveCount(Zone, Kind);
				Group->BalanceProfile = Balance;
				Group->BalanceZone = Zone;
				Group->BalanceKind = Kind;
				if (TestTrue(TEXT("Random group saved"), SaveAsset(Group))) ++RandomGroupCount;
			}
		}
	}

	// Add tier-four consumables without rewriting existing user-authored recipes or item data.
	const USettings_Item* ItemSettings = GetDefault<USettings_Item>();
	UDataTable* Recipes = ItemSettings ? ItemSettings->CraftingRecipeDataTable.LoadSynchronous() : nullptr;
	UItemData* ItemRegistry = ItemSettings ? ItemSettings->ItemAssetRegistry.LoadSynchronous() : nullptr;
	UDataTable* Features = ItemSettings ? ItemSettings->ItemFeatureDataTable.LoadSynchronous() : nullptr;
	bool bRecipesChanged = false;
	bool bRegistryChanged = false;
	bool bFeaturesChanged = false;
	struct FConsumableUpgrade { FGameplayTag Source; FGameplayTag Target; const TCHAR* RecipeName; const TCHAR* DisplayName; };
	const FConsumableUpgrade Upgrades[] = {
		{Item_Id_Consumables_Heal_Elixir, Item_Id_Consumables_Heal_Panacea, TEXT("Panacea"), TEXT("만병통치약")},
		{Item_Id_Consumables_Buff_Gongjindan, Item_Id_Consumables_Buff_RoyalGongjindan, TEXT("RoyalGongjindan"), TEXT("황실 공진단")}
	};
	for (const FConsumableUpgrade& Upgrade : Upgrades)
	{
		if (ItemRegistry && !ItemRegistry->ItemDefinitions.Contains(Upgrade.Target))
		{
			if (const FItemDefinition* Source = ItemRegistry->ItemDefinitions.Find(Upgrade.Source))
			{
				ItemRegistry->ItemDefinitions.Add(Upgrade.Target, *Source);
				bRegistryChanged = true;
			}
		}
		if (Features && !Features->GetRowMap().Contains(Upgrade.Target.GetTagName()))
		{
			if (const FItemFeatureData* Source = Features->FindRow<FItemFeatureData>(Upgrade.Source.GetTagName(), TEXT("Tier 4 consumable"), false))
			{
				FItemFeatureData NewFeature = *Source;
				NewFeature.ItemName = FText::FromString(Upgrade.DisplayName);
				Features->AddRow(Upgrade.Target.GetTagName(), NewFeature);
				bFeaturesChanged = true;
			}
		}
		if (Recipes && !Recipes->GetRowMap().Contains(FName(Upgrade.RecipeName)))
		{
			FCraftingRecipeRow NewRecipe;
			bool bFoundSource = false;
			for (const FName RowName : Recipes->GetRowNames())
			{
				if (const FCraftingRecipeRow* Source = Recipes->FindRow<FCraftingRecipeRow>(RowName, TEXT("Tier 4 consumable"), false))
				{
					if (Source->ResultItemTag == Upgrade.Source)
					{
						NewRecipe = *Source;
						bFoundSource = true;
						break;
					}
				}
			}
			if (bFoundSource)
			{
				NewRecipe.ResultItemTag = Upgrade.Target;
				NewRecipe.ProgressionTier = 4;
				NewRecipe.ProgressionTrack = EProgressionRecipeTrack::Consumable;
				NewRecipe.Ingredients.Reset();
				FCraftingItemStack& Base = NewRecipe.Ingredients.AddDefaulted_GetRef();
				Base.ItemTag = Item_Id_Material_ConsumablesMaterial_Herbs;
				Base.Quantity = 3;
				FCraftingItemStack& Premium = NewRecipe.Ingredients.AddDefaulted_GetRef();
				Premium.ItemTag = Item_Id_Material_ConsumablesMaterial_GoodHerbs;
				Premium.Quantity = 4;
				NewRecipe.bEnabled = true;
				Recipes->AddRow(FName(Upgrade.RecipeName), NewRecipe);
				bRecipesChanged = true;
			}
		}
		TestTrue(*FString::Printf(TEXT("Tier-four item exists: %s"), Upgrade.RecipeName),
			ItemRegistry && ItemRegistry->ItemDefinitions.Contains(Upgrade.Target));
		TestTrue(*FString::Printf(TEXT("Tier-four recipe exists: %s"), Upgrade.RecipeName),
			Recipes && Recipes->GetRowMap().Contains(FName(Upgrade.RecipeName)));
	}
	if (bRegistryChanged) TestTrue(TEXT("Tier 4 item registry saved"), SaveAsset(ItemRegistry));
	if (bFeaturesChanged) TestTrue(TEXT("Tier 4 item names saved"), SaveAsset(Features));
	if (bRecipesChanged) TestTrue(TEXT("Tier 4 recipes saved"), SaveAsset(Recipes));
	if (bRecipesChanged)
	{
		for (int32 KindIndex = 0; KindIndex < 4; ++KindIndex)
		{
			const EProgressionChestKind Kind = static_cast<EProgressionChestKind>(KindIndex);
			const FString TablePath = AssetPath(TEXT("LootTable"), TEXT("DT_ChestLoot"), Kind, EProgressionZone::Final);
			if (UDataTable* Table = CreateOrLoad<UDataTable>(TablePath))
			{
				Table->EmptyTable();
				TArray<FProgressionComputedDrop> Drops;
				Balance->GetComputedDrops(EProgressionZone::Final, Kind, Drops);
				for (const FProgressionComputedDrop& Entry : Drops)
				{
					FChestInitialLootRow Row;
					Row.ItemTag = Entry.ItemTag;
					Row.MinCount = Entry.MinCount;
					Row.MaxCount = Entry.MaxCount;
					Row.Weight = Entry.Chance;
					Table->AddRow(Entry.ItemTag.GetTagName(), Row);
				}
				TestTrue(TEXT("Final loot snapshot saved"), SaveAsset(Table));
			}
		}
	}

	UShipUpgradeTreeDataAsset* Tree = LoadObject<UShipUpgradeTreeDataAsset>(nullptr,
		TEXT("/Game/Blueprints/Item/Data/ShipUpgrade/DA_ShipUpgradeTree.DA_ShipUpgradeTree"));
	if (!Tree) Tree = LoadObject<UShipUpgradeTreeDataAsset>(nullptr,
		TEXT("/Game/Blueprints/Ship/Data/DA_ShipUpgradeTree.DA_ShipUpgradeTree"));
	if (Tree)
	{
		Tree->BalanceProfile = Balance;
		TArray<FShipUpgradeNodeDefinition> FourthTierNodes;
		for (const FShipUpgradeNodeDefinition& Node : Tree->Nodes)
		{
			if (Node.StatTrack == EShipUpgradeStatTrack::LegacyModifiers || Node.TrackLevel != 3) continue;
			const bool bExists = Tree->Nodes.ContainsByPredicate([&Node](const FShipUpgradeNodeDefinition& Other)
			{
				return Other.StatTrack == Node.StatTrack && Other.TrackLevel == 4;
			});
			if (bExists) continue;
			FShipUpgradeNodeDefinition Fourth = Node;
			Fourth.NodeId = FName(*(Node.NodeId.ToString() + TEXT("_IV")));
			Fourth.DisplayName = FText::FromString(Node.DisplayName.ToString() + TEXT(" IV"));
			Fourth.TrackLevel = 4;
			Fourth.GraphPosition.X += 350.f;
			Fourth.PrerequisiteNodeIds = {Node.NodeId};
			Fourth.TargetStatRowName = FName(*(Node.TargetStatRowName.ToString() + TEXT("_IV")));
			if (Tree->ShipStatTable && !Tree->ShipStatTable->GetRowMap().Contains(Fourth.TargetStatRowName))
			{
				if (const FShipStatRow* Source = Tree->ShipStatTable->FindRow<FShipStatRow>(Node.TargetStatRowName, TEXT("Tier 4 ship stat"), false))
				{
					FShipStatRow FourthStats = *Source;
					FourthStats.MaxHealth *= 1.2f;
					FourthStats.ForwardPropulsionMultiplier *= 1.15f;
					FourthStats.TurnTorqueMultiplier *= 1.15f;
					FourthStats.CannonDamage *= 1.2f;
					FourthStats.CannonFireCooldown *= .9f;
					FourthStats.CannonballSpeed *= 1.1f;
					Tree->ShipStatTable->AddRow(Fourth.TargetStatRowName, FourthStats);
				}
			}
			FourthTierNodes.Add(Fourth);
		}
		if (!FourthTierNodes.IsEmpty() && Tree->ShipStatTable)
		{
			TestTrue(TEXT("Fourth ship stat rows saved"), SaveAsset(Tree->ShipStatTable));
		}
		Tree->Nodes.Append(FourthTierNodes);
		for (const FShipUpgradeNodeDefinition& Existing : Tree->Nodes)
		{
			if (Existing.StatTrack == EShipUpgradeStatTrack::LegacyModifiers || Existing.TrackLevel != 3) continue;
			TestTrue(*FString::Printf(TEXT("Ship track %d has fourth node"), static_cast<int32>(Existing.StatTrack)),
				Tree->Nodes.ContainsByPredicate([&Existing](const FShipUpgradeNodeDefinition& Node)
				{
					return Node.StatTrack == Existing.StatTrack && Node.TrackLevel == 4;
				}));
		}
		for (FShipUpgradeNodeDefinition& Node : Tree->Nodes)
		{
			if (Node.StatTrack != EShipUpgradeStatTrack::LegacyModifiers)
			{
				Balance->GetIngredients(EProgressionMaterialTrack::Ship, Node.TrackLevel, Node.ActivationCosts);
			}
		}
		TestTrue(TEXT("Ship upgrade tree linked"), SaveAsset(Tree));
	}
	else
	{
		AddWarning(TEXT("Ship upgrade tree missing; central material costs were not linked to nodes."));
	}

	TestEqual(TEXT("Sixteen loot tables"), LootTableCount, 16);
	TestEqual(TEXT("Sixteen chest definitions"), DefinitionCount, 16);
	TestEqual(TEXT("Eight random groups"), RandomGroupCount, 8);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConsolidateItemAssetsTest,
	"ArtisticSW.Chest.Authoring.ConsolidateItemAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConsolidateItemAssetsTest::RunTest(const FString& Parameters)
{
	struct FMove { const TCHAR* Source; const TCHAR* DestinationFolder; };
	const FMove Moves[] = {
		{TEXT("/Game/Blueprints/03_WorldObject/01_ItemStorage/BP_Storage_Chest"), TEXT("/Game/Blueprints/Item/BP/Chest")},
		{TEXT("/Game/Blueprints/03_WorldObject/01_ItemStorage/BP_StorageChest"), TEXT("/Game/Blueprints/Item/BP/Chest")},
		{TEXT("/Game/Blueprints/03_WorldObject/01_ItemStorage/BP_SharedStorageChest"), TEXT("/Game/Blueprints/Item/BP/Chest")},
		{TEXT("/Game/Blueprints/03_WorldObject/02_LootSpawnManager/BP_ChestSpawnPoint"), TEXT("/Game/Blueprints/Item/BP/LootSpawn")},
		{TEXT("/Game/Blueprints/03_WorldObject/02_LootSpawnManager/BP_GlobalLootSpawnManager"), TEXT("/Game/Blueprints/Item/BP/LootSpawn")},
		{TEXT("/Game/Blueprints/03_WorldObject/02_LootSpawnManager/BP_LooseLootItem"), TEXT("/Game/Blueprints/Item/BP/LootSpawn")},
		{TEXT("/Game/Blueprints/03_WorldObject/02_LootSpawnManager/BP_LooseLootSpawnPoint"), TEXT("/Game/Blueprints/Item/BP/LootSpawn")},
		{TEXT("/Game/Blueprints/03_WorldObject/02_LootSpawnManager/BP_LootZoneSpawnManager"), TEXT("/Game/Blueprints/Item/BP/LootSpawn")},
		{TEXT("/Game/Blueprints/03_WorldObject/02_LootSpawnManager/DT_Zone01_ChestLootItems"), TEXT("/Game/Blueprints/Item/Data/LegacyLoot")},
		{TEXT("/Game/Blueprints/03_WorldObject/02_LootSpawnManager/DT_Zone01_LootItemS"), TEXT("/Game/Blueprints/Item/Data/LegacyLoot")},
		{TEXT("/Game/Blueprints/03_WorldObject/02_LootSpawnManager/DT_Zone02_ChestLootItems"), TEXT("/Game/Blueprints/Item/Data/LegacyLoot")},
		{TEXT("/Game/Blueprints/03_WorldObject/02_LootSpawnManager/DT_Zone02_LootItems"), TEXT("/Game/Blueprints/Item/Data/LegacyLoot")},
		{TEXT("/Game/Blueprints/Ship/Data/DA_ShipUpgradeTree"), TEXT("/Game/Blueprints/Item/Data/ShipUpgrade")},
		{TEXT("/Game/Blueprints/Ship/Data/DT_ShipStat"), TEXT("/Game/Blueprints/Item/Data/ShipUpgrade")},
	};
	TArray<FAssetRenameData> Renames;
	for (const FMove& Move : Moves)
	{
		const FString SourcePath(Move.Source);
		const FString AssetName = FPackageName::GetShortName(SourcePath);
		const FString Destination = FString(Move.DestinationFolder) / AssetName;
		if (LoadObject<UObject>(nullptr, *(Destination + TEXT(".") + AssetName))) continue;
		UObject* Asset = LoadObject<UObject>(nullptr, *(SourcePath + TEXT(".") + AssetName));
		if (!TestNotNull(*FString::Printf(TEXT("Source asset %s"), Move.Source), Asset)) continue;
		Renames.Emplace(Asset, FString(Move.DestinationFolder), AssetName);
	}
	if (!Renames.IsEmpty())
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		TestTrue(TEXT("Item assets moved without breaking references"), AssetTools.RenameAssets(Renames));
	}
	return !HasAnyErrors();
}

#endif
