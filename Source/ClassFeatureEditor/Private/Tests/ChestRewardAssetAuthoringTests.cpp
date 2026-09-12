#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "BaseGameplayTags.h"
#include "Engine/DataTable.h"
#include "HAL/FileManager.h"
#include "ItemSpawn/ChestSpawnData.h"
#include "ItemSpawn/LootSpawnTypes.h"
#include "Misc/AutomationTest.h"
#include "Storage/StorageChest.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace ChestRewardAuthoring
{
	constexpr TCHAR RootPath[] = TEXT("/Game/Blueprints/Item/Data");

	enum class ERegion : uint8
	{
		Land,
		Ocean,
		Ship
	};

	enum class EDifficulty : uint8
	{
		Mid1,
		Mid2,
		Mid3,
		Final
	};

	struct FRowDraft
	{
		FName Name;
		FGameplayTag ItemTag;
		int32 MinCount;
		int32 MaxCount;
		float Weight;
	};

	const TCHAR* RegionName(ERegion Region)
	{
		switch (Region)
		{
		case ERegion::Land: return TEXT("Land");
		case ERegion::Ocean: return TEXT("Ocean");
		case ERegion::Ship: return TEXT("Ship");
		default: return TEXT("Unknown");
		}
	}

	const TCHAR* DifficultyName(EDifficulty Difficulty)
	{
		switch (Difficulty)
		{
		case EDifficulty::Mid1: return TEXT("Mid_1");
		case EDifficulty::Mid2: return TEXT("Mid_2");
		case EDifficulty::Mid3: return TEXT("Mid_3");
		case EDifficulty::Final: return TEXT("Final");
		default: return TEXT("Unknown");
		}
	}

	int32 DifficultyIndex(EDifficulty Difficulty)
	{
		return static_cast<int32>(Difficulty);
	}

	FString AssetPath(const TCHAR* Folder, const TCHAR* Prefix, ERegion Region, EDifficulty Difficulty)
	{
		return FString::Printf(TEXT("%s/%s/%s_%s_%s"), RootPath, Folder, Prefix,
			RegionName(Region), DifficultyName(Difficulty));
	}

	bool SaveAsset(UObject* Asset)
	{
		if (!Asset)
		{
			return false;
		}

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
		const FString ObjectPath = PackagePath + TEXT(".") + AssetName;
		if (TAsset* Existing = LoadObject<TAsset>(nullptr, *ObjectPath))
		{
			return Existing;
		}

		UPackage* Package = CreatePackage(*PackagePath);
		Package->FullyLoad();
		return NewObject<TAsset>(Package, *AssetName, RF_Public | RF_Standalone);
	}

	void AddRow(TArray<FRowDraft>& Rows, const TCHAR* Name, const FGameplayTag& Tag,
		int32 MinCount, int32 MaxCount, float Weight)
	{
		Rows.Add({FName(Name), Tag, MinCount, MaxCount, Weight});
	}

	TArray<FRowDraft> BuildRows(ERegion Region, EDifficulty Difficulty)
	{
		TArray<FRowDraft> Rows;
		switch (Difficulty)
		{
		case EDifficulty::Mid1:
			if (Region == ERegion::Land)
			{
				AddRow(Rows, TEXT("Wood"), Item_Id_Material_WeaponMaterial_Wood, 3, 6, 100.f);
				AddRow(Rows, TEXT("Iron"), Item_Id_Material_WeaponMaterial_Iron, 2, 4, 80.f);
				AddRow(Rows, TEXT("Herbs"), Item_Id_Material_ConsumablesMaterial_Herbs, 2, 4, 65.f);
				AddRow(Rows, TEXT("Medicine"), Item_Id_Consumables_Heal_Medicine, 1, 2, 45.f);
			}
			else if (Region == ERegion::Ocean)
			{
				AddRow(Rows, TEXT("Gunpowder"), Item_Id_Material_Etc_Gunpowder, 2, 4, 100.f);
				AddRow(Rows, TEXT("WoodenPlank"), Item_Id_Material_ShipMaterials_WoodenPlank, 3, 6, 90.f);
				AddRow(Rows, TEXT("IronPlate"), Item_Id_Material_ShipMaterials_IronPlate, 2, 4, 70.f);
				AddRow(Rows, TEXT("Medicine"), Item_Id_Consumables_Heal_Medicine, 1, 2, 45.f);
			}
			else
			{
				AddRow(Rows, TEXT("WoodenPlank"), Item_Id_Material_ShipMaterials_WoodenPlank, 5, 9, 100.f);
				AddRow(Rows, TEXT("IronPlate"), Item_Id_Material_ShipMaterials_IronPlate, 3, 6, 90.f);
				AddRow(Rows, TEXT("Gunpowder"), Item_Id_Material_Etc_Gunpowder, 3, 5, 80.f);
				AddRow(Rows, TEXT("Medicine"), Item_Id_Consumables_Heal_Medicine, 1, 2, 40.f);
			}
			break;

		case EDifficulty::Mid2:
			if (Region == ERegion::Land)
			{
				AddRow(Rows, TEXT("GoodWood"), Item_Id_Material_WeaponMaterial_GoodWood, 3, 6, 100.f);
				AddRow(Rows, TEXT("GoodIron"), Item_Id_Material_WeaponMaterial_GoodIron, 2, 5, 85.f);
				AddRow(Rows, TEXT("GoodHerbs"), Item_Id_Material_ConsumablesMaterial_GoodHerbs, 2, 4, 60.f);
				AddRow(Rows, TEXT("Tangyak"), Item_Id_Consumables_Heal_Tangyak, 1, 2, 45.f);
				AddRow(Rows, TEXT("RareSkillMaterial"), Item_Id_Material_SkillMaterial_RareSkill, 1, 1, 20.f);
			}
			else if (Region == ERegion::Ocean)
			{
				AddRow(Rows, TEXT("GoodWoodenPlank"), Item_Id_Material_ShipMaterials_GoodWoodenPlank, 4, 8, 100.f);
				AddRow(Rows, TEXT("GoodIronPlate"), Item_Id_Material_ShipMaterials_GoodIronPlate, 3, 6, 85.f);
				AddRow(Rows, TEXT("Gunpowder"), Item_Id_Material_Etc_Gunpowder, 4, 7, 75.f);
				AddRow(Rows, TEXT("Tangyak"), Item_Id_Consumables_Heal_Tangyak, 1, 2, 45.f);
				AddRow(Rows, TEXT("GrapplingHook"), Item_Id_Material_ShipMaterials_GrapplingHook, 1, 1, 20.f);
			}
			else
			{
				AddRow(Rows, TEXT("GoodWoodenPlank"), Item_Id_Material_ShipMaterials_GoodWoodenPlank, 6, 10, 100.f);
				AddRow(Rows, TEXT("GoodIronPlate"), Item_Id_Material_ShipMaterials_GoodIronPlate, 4, 8, 90.f);
				AddRow(Rows, TEXT("Gunpowder"), Item_Id_Material_Etc_Gunpowder, 5, 8, 80.f);
				AddRow(Rows, TEXT("Tangyak"), Item_Id_Consumables_Heal_Tangyak, 1, 3, 45.f);
				AddRow(Rows, TEXT("GrapplingHook"), Item_Id_Material_ShipMaterials_GrapplingHook, 1, 1, 25.f);
			}
			break;

		case EDifficulty::Mid3:
			if (Region == ERegion::Land)
			{
				AddRow(Rows, TEXT("GoodWood"), Item_Id_Material_WeaponMaterial_GoodWood, 5, 9, 100.f);
				AddRow(Rows, TEXT("GoodIron"), Item_Id_Material_WeaponMaterial_GoodIron, 4, 7, 90.f);
				AddRow(Rows, TEXT("EpicMaterial"), Item_Id_Material_WeaponSpecialMaterial_EpicMaterial, 1, 2, 45.f);
				AddRow(Rows, TEXT("EpicRecipe"), Item_Id_Material_WeaponSpecialRecipe_EpicRecipe, 1, 1, 22.f);
				AddRow(Rows, TEXT("EpicSkillMaterial"), Item_Id_Material_SkillMaterial_EpicSkill, 1, 1, 22.f);
				AddRow(Rows, TEXT("Chungshimhwan"), Item_Id_Consumables_Buff_Chungshimhwan, 1, 2, 35.f);
			}
			else if (Region == ERegion::Ocean)
			{
				AddRow(Rows, TEXT("GoodWoodenPlank"), Item_Id_Material_ShipMaterials_GoodWoodenPlank, 6, 11, 100.f);
				AddRow(Rows, TEXT("GoodIronPlate"), Item_Id_Material_ShipMaterials_GoodIronPlate, 5, 9, 90.f);
				AddRow(Rows, TEXT("LuminousPearl"), Item_Id_Material_ShipMaterials_LuminousPearl, 1, 1, 28.f);
				AddRow(Rows, TEXT("EpicMaterial"), Item_Id_Material_WeaponSpecialMaterial_EpicMaterial, 1, 2, 40.f);
				AddRow(Rows, TEXT("EpicSkillMaterial"), Item_Id_Material_SkillMaterial_EpicSkill, 1, 1, 20.f);
				AddRow(Rows, TEXT("Chungshimhwan"), Item_Id_Consumables_Buff_Chungshimhwan, 1, 2, 35.f);
			}
			else
			{
				AddRow(Rows, TEXT("GoodWoodenPlank"), Item_Id_Material_ShipMaterials_GoodWoodenPlank, 8, 14, 100.f);
				AddRow(Rows, TEXT("GoodIronPlate"), Item_Id_Material_ShipMaterials_GoodIronPlate, 6, 11, 95.f);
				AddRow(Rows, TEXT("Gunpowder"), Item_Id_Material_Etc_Gunpowder, 7, 12, 75.f);
				AddRow(Rows, TEXT("LuminousPearl"), Item_Id_Material_ShipMaterials_LuminousPearl, 1, 2, 30.f);
				AddRow(Rows, TEXT("EpicMaterial"), Item_Id_Material_WeaponSpecialMaterial_EpicMaterial, 1, 3, 45.f);
				AddRow(Rows, TEXT("EpicRecipe"), Item_Id_Material_WeaponSpecialRecipe_EpicRecipe, 1, 1, 22.f);
			}
			break;

		case EDifficulty::Final:
			if (Region == ERegion::Land)
			{
				AddRow(Rows, TEXT("EpicMaterial"), Item_Id_Material_WeaponSpecialMaterial_EpicMaterial, 2, 4, 100.f);
				AddRow(Rows, TEXT("LegendaryMaterial"), Item_Id_Material_WeaponSpecialMaterial_LegendaryMaterial, 1, 2, 45.f);
				AddRow(Rows, TEXT("LegendaryRecipe"), Item_Id_Material_WeaponSpecialRecipe_LegendaryRecipe, 1, 1, 18.f);
				AddRow(Rows, TEXT("LegendarySkillMaterial"), Item_Id_Material_SkillMaterial_LegendarySkill, 1, 1, 18.f);
				AddRow(Rows, TEXT("Elixir"), Item_Id_Consumables_Heal_Elixir, 1, 2, 35.f);
				AddRow(Rows, TEXT("Gongjindan"), Item_Id_Consumables_Buff_Gongjindan, 1, 2, 30.f);
			}
			else if (Region == ERegion::Ocean)
			{
				AddRow(Rows, TEXT("LuminousPearl"), Item_Id_Material_ShipMaterials_LuminousPearl, 1, 3, 100.f);
				AddRow(Rows, TEXT("EpicMaterial"), Item_Id_Material_WeaponSpecialMaterial_EpicMaterial, 2, 4, 80.f);
				AddRow(Rows, TEXT("LegendaryMaterial"), Item_Id_Material_WeaponSpecialMaterial_LegendaryMaterial, 1, 2, 42.f);
				AddRow(Rows, TEXT("LegendarySkillMaterial"), Item_Id_Material_SkillMaterial_LegendarySkill, 1, 1, 18.f);
				AddRow(Rows, TEXT("Elixir"), Item_Id_Consumables_Heal_Elixir, 1, 2, 35.f);
				AddRow(Rows, TEXT("Gongjindan"), Item_Id_Consumables_Buff_Gongjindan, 1, 2, 30.f);
			}
			else
			{
				AddRow(Rows, TEXT("LuminousPearl"), Item_Id_Material_ShipMaterials_LuminousPearl, 2, 4, 100.f);
				AddRow(Rows, TEXT("GoodWoodenPlank"), Item_Id_Material_ShipMaterials_GoodWoodenPlank, 10, 16, 90.f);
				AddRow(Rows, TEXT("GoodIronPlate"), Item_Id_Material_ShipMaterials_GoodIronPlate, 8, 14, 85.f);
				AddRow(Rows, TEXT("LegendaryMaterial"), Item_Id_Material_WeaponSpecialMaterial_LegendaryMaterial, 1, 3, 50.f);
				AddRow(Rows, TEXT("LegendaryRecipe"), Item_Id_Material_WeaponSpecialRecipe_LegendaryRecipe, 1, 1, 20.f);
				AddRow(Rows, TEXT("LegendarySkillMaterial"), Item_Id_Material_SkillMaterial_LegendarySkill, 1, 1, 20.f);
				AddRow(Rows, TEXT("Elixir"), Item_Id_Consumables_Heal_Elixir, 1, 3, 35.f);
			}
			break;
		}
		return Rows;
	}

	UDataTable* AuthorLootTable(ERegion Region, EDifficulty Difficulty)
	{
		UDataTable* Table = CreateOrLoad<UDataTable>(
			AssetPath(TEXT("LootTable"), TEXT("DT_ChestLoot"), Region, Difficulty));
		Table->RowStruct = FChestInitialLootRow::StaticStruct();
		Table->EmptyTable();
		for (const FRowDraft& Draft : BuildRows(Region, Difficulty))
		{
			FChestInitialLootRow Row;
			Row.ItemTag = Draft.ItemTag;
			Row.MinCount = Draft.MinCount;
			Row.MaxCount = Draft.MaxCount;
			Row.Weight = Draft.Weight;
			Table->AddRow(Draft.Name, Row);
		}
		return SaveAsset(Table) ? Table : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChestRewardAssetAuthoringTest,
	"ArtisticSW.Chest.Authoring.GenerateRewardAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChestRewardAssetAuthoringTest::RunTest(const FString& Parameters)
{
	using namespace ChestRewardAuthoring;

	TSubclassOf<AStorageChest> ChestClass = StaticLoadClass(
		AStorageChest::StaticClass(), nullptr,
		TEXT("/Game/Blueprints/03_WorldObject/01_ItemStorage/BP_Storage_Chest.BP_Storage_Chest_C"));
	if (!ChestClass)
	{
		ChestClass = AStorageChest::StaticClass();
		AddWarning(TEXT("BP_Storage_Chest was not loadable; native AStorageChest was used."));
	}

	const ERegion Regions[] = {ERegion::Land, ERegion::Ocean, ERegion::Ship};
	const EDifficulty Difficulties[] = {
		EDifficulty::Mid1, EDifficulty::Mid2, EDifficulty::Mid3, EDifficulty::Final};
	TMap<FString, UChestDefinition*> Definitions;
	int32 LootTableCount = 0;
	int32 DefinitionCount = 0;
	int32 RandomGroupCount = 0;

	for (ERegion Region : Regions)
	{
		for (EDifficulty Difficulty : Difficulties)
		{
			UDataTable* LootTable = AuthorLootTable(Region, Difficulty);
			if (!TestNotNull(TEXT("Loot table was authored"), LootTable))
			{
				continue;
			}
			++LootTableCount;

			const FString DefinitionPath = AssetPath(
				TEXT("ChestDefinition"), TEXT("DA_Chest"), Region, Difficulty);
			UChestDefinition* Definition = CreateOrLoad<UChestDefinition>(DefinitionPath);
			Definition->ChestClass = ChestClass;
			Definition->LootTable = LootTable;
			Definition->RollCount = 3 + DifficultyIndex(Difficulty);
			Definition->SlotCount = 6 + DifficultyIndex(Difficulty) * 2;
			Definition->ColumnCount = 4;
			if (TestTrue(TEXT("Chest definition was saved"), SaveAsset(Definition)))
			{
				++DefinitionCount;
				Definitions.Add(FString::Printf(TEXT("%s_%s"), RegionName(Region), DifficultyName(Difficulty)), Definition);
			}
		}
	}

	for (ERegion Region : {ERegion::Land, ERegion::Ocean})
	{
		for (EDifficulty Difficulty : Difficulties)
		{
			const FString Key = FString::Printf(TEXT("%s_%s"), RegionName(Region), DifficultyName(Difficulty));
			URandomChestGroup* Group = CreateOrLoad<URandomChestGroup>(
				AssetPath(TEXT("RandomGroup"), TEXT("DA_RandomGroup"), Region, Difficulty));
			Group->ChestDefinition = Definitions.FindRef(Key);
			Group->SpawnCount = 2 + FMath::Min(DifficultyIndex(Difficulty), 2);
			if (TestTrue(TEXT("Random group was saved"), SaveAsset(Group)))
			{
				++RandomGroupCount;
			}
		}
	}

	TestEqual(TEXT("Twelve loot tables exist"), LootTableCount, 12);
	TestEqual(TEXT("Twelve chest definitions exist"), DefinitionCount, 12);
	TestEqual(TEXT("Eight land/ocean random groups exist"), RandomGroupCount, 8);
	return !HasAnyErrors();
}

#endif
