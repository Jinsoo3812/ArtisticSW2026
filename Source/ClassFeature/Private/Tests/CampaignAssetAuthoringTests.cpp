#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/DataTable.h"
#include "UObject/SavePackage.h"
#include "UObject/Package.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ItemSpawn/ChestSpawnData.h"
#include "ItemSpawn/LootSpawnTypes.h"
#include "Storage/StorageChest.h"
#include "ItemSpawn/LootSpawnPoint.h"
#include "BaseCharacter.h"
#include "Components/BaseHealthComponent.h"
#include "NPCDialogueData.h"
#include "StoryFacadeSubsystem.h"
#include "StorySubsystem.h"
#include "Crafting/CraftingRecipeTypes.h"
#include "BaseGameplayTags.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCampaignAssetsAuthoringAndValidationTest,
	"ArtisticSW.Campaign.AssetsAuthoringAndValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

static bool SaveAssetPackage(UObject* Asset)
{
	if (!Asset) return false;
	UPackage* Package = Asset->GetOutermost();
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Asset);

	FString PackageFileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(), FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	return UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs);
}

static UDataTable* CreateOrGetDataTable(const FString& PackagePath, UScriptStruct* RowStruct)
{
	FString AssetName = FPackageName::GetShortName(PackagePath);
	FString ObjectPath = PackagePath + TEXT(".") + AssetName;
	if (UObject* LoadedObj = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath))
	{
		if (UObjectRedirector* Redirector = Cast<UObjectRedirector>(LoadedObj))
		{
			LoadedObj = Redirector->DestinationObject;
		}
		if (UDataTable* LoadedDT = Cast<UDataTable>(LoadedObj))
		{
			return LoadedDT;
		}
	}

	UPackage* Package = CreatePackage(*PackagePath);
	Package->FullyLoad();
	if (UObject* Existing = FindObject<UObject>(Package, *AssetName))
	{
		if (UDataTable* ExistingDT = Cast<UDataTable>(Existing))
		{
			return ExistingDT;
		}
		if (UObjectRedirector* Redirector = Cast<UObjectRedirector>(Existing))
		{
			if (UDataTable* DestDT = Cast<UDataTable>(Redirector->DestinationObject))
			{
				return DestDT;
			}
		}
		Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders | REN_NonTransactional);
	}

	UDataTable* DataTable = NewObject<UDataTable>(Package, *AssetName, RF_Public | RF_Standalone);
	DataTable->RowStruct = RowStruct;
	return DataTable;
}

static UChestDefinition* CreateOrGetChestDef(const FString& PackagePath)
{
	FString AssetName = FPackageName::GetShortName(PackagePath);
	FString ObjectPath = PackagePath + TEXT(".") + AssetName;
	if (UObject* LoadedObj = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath))
	{
		if (UObjectRedirector* Redirector = Cast<UObjectRedirector>(LoadedObj))
		{
			LoadedObj = Redirector->DestinationObject;
		}
		if (UChestDefinition* Def = Cast<UChestDefinition>(LoadedObj))
		{
			return Def;
		}
	}

	UPackage* Package = CreatePackage(*PackagePath);
	Package->FullyLoad();
	if (UObject* Existing = FindObject<UObject>(Package, *AssetName))
	{
		if (UChestDefinition* ExistingDef = Cast<UChestDefinition>(Existing))
		{
			return ExistingDef;
		}
		if (UObjectRedirector* Redirector = Cast<UObjectRedirector>(Existing))
		{
			if (UChestDefinition* DestDef = Cast<UChestDefinition>(Redirector->DestinationObject))
			{
				return DestDef;
			}
		}
		Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders | REN_NonTransactional);
	}

	return NewObject<UChestDefinition>(Package, *AssetName, RF_Public | RF_Standalone);
}

static URandomChestGroup* CreateOrGetRandomGroup(const FString& PackagePath)
{
	FString AssetName = FPackageName::GetShortName(PackagePath);
	FString ObjectPath = PackagePath + TEXT(".") + AssetName;
	if (UObject* LoadedObj = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath))
	{
		if (UObjectRedirector* Redirector = Cast<UObjectRedirector>(LoadedObj))
		{
			LoadedObj = Redirector->DestinationObject;
		}
		if (URandomChestGroup* Group = Cast<URandomChestGroup>(LoadedObj))
		{
			return Group;
		}
	}

	UPackage* Package = CreatePackage(*PackagePath);
	Package->FullyLoad();
	if (UObject* Existing = FindObject<UObject>(Package, *AssetName))
	{
		if (URandomChestGroup* ExistingGroup = Cast<URandomChestGroup>(Existing))
		{
			return ExistingGroup;
		}
		if (UObjectRedirector* Redirector = Cast<UObjectRedirector>(Existing))
		{
			if (URandomChestGroup* DestGroup = Cast<URandomChestGroup>(Redirector->DestinationObject))
			{
				return DestGroup;
			}
		}
		Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders | REN_NonTransactional);
	}

	return NewObject<URandomChestGroup>(Package, *AssetName, RF_Public | RF_Standalone);
}

static UNPCDialogueData* CreateOrGetDialogueData(const FString& PackagePath)
{
	FString AssetName = FPackageName::GetShortName(PackagePath);
	FString ObjectPath = PackagePath + TEXT(".") + AssetName;
	if (UObject* LoadedObj = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath))
	{
		if (UObjectRedirector* Redirector = Cast<UObjectRedirector>(LoadedObj))
		{
			LoadedObj = Redirector->DestinationObject;
		}
		if (UNPCDialogueData* Data = Cast<UNPCDialogueData>(LoadedObj))
		{
			return Data;
		}
	}

	UPackage* Package = CreatePackage(*PackagePath);
	Package->FullyLoad();
	if (UObject* Existing = FindObject<UObject>(Package, *AssetName))
	{
		if (UNPCDialogueData* ExistingData = Cast<UNPCDialogueData>(Existing))
		{
			return ExistingData;
		}
		if (UObjectRedirector* Redirector = Cast<UObjectRedirector>(Existing))
		{
			if (UNPCDialogueData* DestData = Cast<UNPCDialogueData>(Redirector->DestinationObject))
			{
				return DestData;
			}
		}
		Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders | REN_NonTransactional);
	}

	return NewObject<UNPCDialogueData>(Package, *AssetName, RF_Public | RF_Standalone);
}

bool FCampaignAssetsAuthoringAndValidationTest::RunTest(const FString& Parameters)
{
	UScriptStruct* LootRowStruct = FChestInitialLootRow::StaticStruct();

	// ----------------------------------------------------
	// 1. Data Tables Authoring
	// ----------------------------------------------------
	UDataTable* DT_Early = CreateOrGetDataTable(TEXT("/Game/Campaign/DataTable/DT_Loot_EarlyTier"), LootRowStruct);
	DT_Early->EmptyTable();
	{
		FChestInitialLootRow Row1; Row1.ItemTag = Item_Id_Material_WeaponMaterial_Wood; Row1.MinCount = 3; Row1.MaxCount = 6; Row1.Weight = 100.f;
		FChestInitialLootRow Row2; Row2.ItemTag = Item_Id_Material_WeaponMaterial_Iron; Row2.MinCount = 2; Row2.MaxCount = 4; Row2.Weight = 80.f;
		FChestInitialLootRow Row3; Row3.ItemTag = Item_Id_Material_Etc_Gunpowder; Row3.MinCount = 1; Row3.MaxCount = 2; Row3.Weight = 40.f;
		FChestInitialLootRow Row4; Row4.ItemTag = Item_Id_Consumables_Heal_Medicine; Row4.MinCount = 1; Row4.MaxCount = 2; Row4.Weight = 50.f;
		DT_Early->AddRow(TEXT("Wood"), Row1);
		DT_Early->AddRow(TEXT("Iron"), Row2);
		DT_Early->AddRow(TEXT("Gunpowder"), Row3);
		DT_Early->AddRow(TEXT("Medicine"), Row4);
	}
	SaveAssetPackage(DT_Early);

	UDataTable* DT_Mid = CreateOrGetDataTable(TEXT("/Game/Campaign/DataTable/DT_Loot_MidTier"), LootRowStruct);
	DT_Mid->EmptyTable();
	{
		FChestInitialLootRow Row1; Row1.ItemTag = Item_Id_Material_WeaponMaterial_GoodWood; Row1.MinCount = 4; Row1.MaxCount = 8; Row1.Weight = 100.f;
		FChestInitialLootRow Row2; Row2.ItemTag = Item_Id_Material_WeaponMaterial_GoodIron; Row2.MinCount = 3; Row2.MaxCount = 6; Row2.Weight = 80.f;
		FChestInitialLootRow Row3; Row3.ItemTag = Item_Id_Material_Etc_Gunpowder; Row3.MinCount = 3; Row3.MaxCount = 5; Row3.Weight = 70.f;
		DT_Mid->AddRow(TEXT("GoodWood"), Row1);
		DT_Mid->AddRow(TEXT("GoodIron"), Row2);
		DT_Mid->AddRow(TEXT("Gunpowder"), Row3);
	}
	SaveAssetPackage(DT_Mid);

	UDataTable* DT_Late = CreateOrGetDataTable(TEXT("/Game/Campaign/DataTable/DT_Loot_LateTier"), LootRowStruct);
	DT_Late->EmptyTable();
	{
		FChestInitialLootRow Row1; Row1.ItemTag = Item_Id_Material_WeaponSpecialMaterial_EpicMaterial; Row1.MinCount = 1; Row1.MaxCount = 3; Row1.Weight = 100.f;
		FChestInitialLootRow Row2; Row2.ItemTag = Item_Id_Material_WeaponSpecialMaterial_LegendaryMaterial; Row2.MinCount = 1; Row2.MaxCount = 2; Row2.Weight = 50.f;
		DT_Late->AddRow(TEXT("EpicMaterial"), Row1);
		DT_Late->AddRow(TEXT("LegendaryMaterial"), Row2);
	}
	SaveAssetPackage(DT_Late);

	UDataTable* DT_Sunk = CreateOrGetDataTable(TEXT("/Game/Campaign/DataTable/DT_Loot_EnemyShipSunk"), LootRowStruct);
	DT_Sunk->EmptyTable();
	{
		FChestInitialLootRow Row1; Row1.ItemTag = Item_Id_Material_ShipMaterials_WoodenPlank; Row1.MinCount = 5; Row1.MaxCount = 10; Row1.Weight = 100.f;
		FChestInitialLootRow Row2; Row2.ItemTag = Item_Id_Material_ShipMaterials_IronPlate; Row2.MinCount = 3; Row2.MaxCount = 6; Row2.Weight = 80.f;
		DT_Sunk->AddRow(TEXT("WoodenPlank"), Row1);
		DT_Sunk->AddRow(TEXT("IronPlate"), Row2);
	}
	SaveAssetPackage(DT_Sunk);

	UDataTable* DT_MB1 = CreateOrGetDataTable(TEXT("/Game/Campaign/DataTable/DT_Loot_MidBoss1"), LootRowStruct);
	DT_MB1->EmptyTable();
	{
		FChestInitialLootRow Row1; Row1.ItemTag = Item_Id_Material_WeaponMaterial_GoodIron; Row1.MinCount = 3; Row1.MaxCount = 5; Row1.Weight = 100.f;
		DT_MB1->AddRow(TEXT("Bonus_GoodIron"), Row1);
	}
	SaveAssetPackage(DT_MB1);

	UDataTable* DT_MB2 = CreateOrGetDataTable(TEXT("/Game/Campaign/DataTable/DT_Loot_MidBoss2"), LootRowStruct);
	DT_MB2->EmptyTable();
	{
		FChestInitialLootRow Row1; Row1.ItemTag = Item_Id_Material_WeaponMaterial_GoodWood; Row1.MinCount = 4; Row1.MaxCount = 6; Row1.Weight = 100.f;
		DT_MB2->AddRow(TEXT("Bonus_GoodWood"), Row1);
	}
	SaveAssetPackage(DT_MB2);

	UDataTable* DT_MB3 = CreateOrGetDataTable(TEXT("/Game/Campaign/DataTable/DT_Loot_MidBoss3"), LootRowStruct);
	DT_MB3->EmptyTable();
	{
		FChestInitialLootRow Row1; Row1.ItemTag = Item_Id_Material_WeaponSpecialMaterial_EpicMaterial; Row1.MinCount = 2; Row1.MaxCount = 4; Row1.Weight = 100.f;
		DT_MB3->AddRow(TEXT("Bonus_EpicMaterial"), Row1);
	}
	SaveAssetPackage(DT_MB3);

	UDataTable* DT_Cipher = CreateOrGetDataTable(TEXT("/Game/Campaign/DataTable/DT_Loot_CipherBookSub"), LootRowStruct);
	DT_Cipher->EmptyTable();
	{
		FChestInitialLootRow Row1; Row1.ItemTag = Item_Id_Consumables_Heal_Medicine; Row1.MinCount = 2; Row1.MaxCount = 4; Row1.Weight = 100.f;
		DT_Cipher->AddRow(TEXT("Bonus_Supplies"), Row1);
	}
	SaveAssetPackage(DT_Cipher);

	// ----------------------------------------------------
	// 2. Chest Definitions Authoring
	// ----------------------------------------------------
	TSubclassOf<AStorageChest> DefaultChestClass = StaticLoadClass(
		AStorageChest::StaticClass(),
		nullptr,
		TEXT("/Game/Blueprints/03_WorldObject/01_ItemStorage/BP_Storage_Chest.BP_Storage_Chest_C"));
	if (!DefaultChestClass)
	{
		DefaultChestClass = StaticLoadClass(
			AStorageChest::StaticClass(),
			nullptr,
			TEXT("/Game/Blueprints/03_WorldObject/01_ItemStorage/BP_StorageChest.BP_StorageChest_C"));
	}
	if (!DefaultChestClass)
	{
		DefaultChestClass = AStorageChest::StaticClass();
	}

	UChestDefinition* DA_LandEarly = CreateOrGetChestDef(TEXT("/Game/Campaign/DataAsset/Chest/DA_Chest_Land_Early"));
	DA_LandEarly->ChestClass = DefaultChestClass; DA_LandEarly->LootTable = DT_Early; DA_LandEarly->RollCount = 3; DA_LandEarly->SlotCount = 5; DA_LandEarly->ColumnCount = 4;
	SaveAssetPackage(DA_LandEarly);

	UChestDefinition* DA_OceanEarly = CreateOrGetChestDef(TEXT("/Game/Campaign/DataAsset/Chest/DA_Chest_Ocean_Early"));
	DA_OceanEarly->ChestClass = DefaultChestClass; DA_OceanEarly->LootTable = DT_Early; DA_OceanEarly->RollCount = 3; DA_OceanEarly->SlotCount = 5; DA_OceanEarly->ColumnCount = 4;
	SaveAssetPackage(DA_OceanEarly);

	UChestDefinition* DA_LandMid = CreateOrGetChestDef(TEXT("/Game/Campaign/DataAsset/Chest/DA_Chest_Land_Mid"));
	DA_LandMid->ChestClass = DefaultChestClass; DA_LandMid->LootTable = DT_Mid; DA_LandMid->RollCount = 3; DA_LandMid->SlotCount = 5; DA_LandMid->ColumnCount = 4;
	SaveAssetPackage(DA_LandMid);

	UChestDefinition* DA_OceanMid = CreateOrGetChestDef(TEXT("/Game/Campaign/DataAsset/Chest/DA_Chest_Ocean_Mid"));
	DA_OceanMid->ChestClass = DefaultChestClass; DA_OceanMid->LootTable = DT_Mid; DA_OceanMid->RollCount = 3; DA_OceanMid->SlotCount = 5; DA_OceanMid->ColumnCount = 4;
	SaveAssetPackage(DA_OceanMid);

	UChestDefinition* DA_LandLate = CreateOrGetChestDef(TEXT("/Game/Campaign/DataAsset/Chest/DA_Chest_Land_Late"));
	DA_LandLate->ChestClass = DefaultChestClass; DA_LandLate->LootTable = DT_Late; DA_LandLate->RollCount = 4; DA_LandLate->SlotCount = 6; DA_LandLate->ColumnCount = 4;
	SaveAssetPackage(DA_LandLate);

	UChestDefinition* DA_OceanLate = CreateOrGetChestDef(TEXT("/Game/Campaign/DataAsset/Chest/DA_Chest_Ocean_Late"));
	DA_OceanLate->ChestClass = DefaultChestClass; DA_OceanLate->LootTable = DT_Late; DA_OceanLate->RollCount = 4; DA_OceanLate->SlotCount = 6; DA_OceanLate->ColumnCount = 4;
	SaveAssetPackage(DA_OceanLate);

	UChestDefinition* DA_ShipDeck = CreateOrGetChestDef(TEXT("/Game/Campaign/DataAsset/Chest/DA_Chest_ShipDeck"));
	DA_ShipDeck->ChestClass = DefaultChestClass; DA_ShipDeck->LootTable = DT_Mid; DA_ShipDeck->RollCount = 3; DA_ShipDeck->SlotCount = 5; DA_ShipDeck->ColumnCount = 4;
	SaveAssetPackage(DA_ShipDeck);

	UChestDefinition* DA_ShipSunk = CreateOrGetChestDef(TEXT("/Game/Campaign/DataAsset/Chest/DA_Chest_ShipSunk"));
	DA_ShipSunk->ChestClass = DefaultChestClass; DA_ShipSunk->LootTable = DT_Sunk; DA_ShipSunk->RollCount = 4; DA_ShipSunk->SlotCount = 6; DA_ShipSunk->ColumnCount = 4;
	SaveAssetPackage(DA_ShipSunk);

	// Story Boss Chest Definitions
	UChestDefinition* DA_MB1 = CreateOrGetChestDef(TEXT("/Game/Campaign/DataAsset/Chest/DA_Chest_MidBoss1"));
	DA_MB1->ChestClass = DefaultChestClass; DA_MB1->LootTable = DT_MB1; DA_MB1->RollCount = 2; DA_MB1->SlotCount = 6; DA_MB1->ColumnCount = 4;
	SaveAssetPackage(DA_MB1);

	// Phase_1 path compatibility
	UChestDefinition* DA_MB1_P1 = CreateOrGetChestDef(TEXT("/Game/Campaign/DataAsset/Chest/Chest_Definition/Phase_1/DA_Chest_MidBoss1"));
	DA_MB1_P1->ChestClass = DefaultChestClass; DA_MB1_P1->LootTable = DT_MB1; DA_MB1_P1->RollCount = 2; DA_MB1_P1->SlotCount = 6; DA_MB1_P1->ColumnCount = 4;
	SaveAssetPackage(DA_MB1_P1);

	UChestDefinition* DA_MB2 = CreateOrGetChestDef(TEXT("/Game/Campaign/DataAsset/Chest/DA_Chest_MidBoss2"));
	DA_MB2->ChestClass = DefaultChestClass; DA_MB2->LootTable = DT_MB2; DA_MB2->RollCount = 2; DA_MB2->SlotCount = 6; DA_MB2->ColumnCount = 4;
	SaveAssetPackage(DA_MB2);

	// Phase_2 path compatibility
	UChestDefinition* DA_MB2_P2 = CreateOrGetChestDef(TEXT("/Game/Campaign/DataAsset/Chest/Chest_Definition/Phase_2/DA_Chest_MidBoss2"));
	DA_MB2_P2->ChestClass = DefaultChestClass; DA_MB2_P2->LootTable = DT_MB2; DA_MB2_P2->RollCount = 2; DA_MB2_P2->SlotCount = 6; DA_MB2_P2->ColumnCount = 4;
	SaveAssetPackage(DA_MB2_P2);

	UChestDefinition* DA_MB3 = CreateOrGetChestDef(TEXT("/Game/Campaign/DataAsset/Chest/DA_Chest_MidBoss3"));
	DA_MB3->ChestClass = DefaultChestClass; DA_MB3->LootTable = DT_MB3; DA_MB3->RollCount = 2; DA_MB3->SlotCount = 6; DA_MB3->ColumnCount = 4;
	SaveAssetPackage(DA_MB3);

	// Phase_3 path compatibility
	UChestDefinition* DA_MB3_P3 = CreateOrGetChestDef(TEXT("/Game/Campaign/DataAsset/Chest/Chest_Definition/Phase_3/DA_Chest_MidBoss3"));
	DA_MB3_P3->ChestClass = DefaultChestClass; DA_MB3_P3->LootTable = DT_MB3; DA_MB3_P3->RollCount = 2; DA_MB3_P3->SlotCount = 6; DA_MB3_P3->ColumnCount = 4;
	SaveAssetPackage(DA_MB3_P3);

	// Phase_1 Land Early path compatibility
	UChestDefinition* DA_LandEarly_P1 = CreateOrGetChestDef(TEXT("/Game/Campaign/DataAsset/Chest/Chest_Definition/Phase_1/DA_Chest_Land_Early"));
	DA_LandEarly_P1->ChestClass = DefaultChestClass; DA_LandEarly_P1->LootTable = DT_Early; DA_LandEarly_P1->RollCount = 2; DA_LandEarly_P1->SlotCount = 4; DA_LandEarly_P1->ColumnCount = 4;
	SaveAssetPackage(DA_LandEarly_P1);

	UChestDefinition* DA_Cipher = CreateOrGetChestDef(TEXT("/Game/Campaign/DataAsset/Chest/DA_Chest_CipherBook"));
	DA_Cipher->ChestClass = DefaultChestClass; DA_Cipher->LootTable = DT_Cipher; DA_Cipher->RollCount = 2; DA_Cipher->SlotCount = 4; DA_Cipher->ColumnCount = 4;
	SaveAssetPackage(DA_Cipher);

	// ----------------------------------------------------
	// 3. Crafting Recipes Authoring
	// ----------------------------------------------------
	UDataTable* DT_Crafting = CreateOrGetDataTable(TEXT("/Game/Blueprints/Item/DT_CraftingRecipes"), FCraftingRecipeRow::StaticStruct());
	if (DT_Crafting)
	{
		FCraftingRecipeRow Recipe;
		Recipe.ResultItemTag = Item_Quest_DecipheredCipher;
		Recipe.ResultQuantity = 1;
		Recipe.bEnabled = true;
		Recipe.SortOrder = 100;
		
		FCraftingItemStack Ing1;
		Ing1.ItemTag = Item_Quest_CipherBook;
		Ing1.Quantity = 1;
		Recipe.Ingredients.Add(Ing1);

		FCraftingItemStack Ing2;
		Ing2.ItemTag = Item_Quest_JapaneseCipher;
		Ing2.Quantity = 1;
		Recipe.Ingredients.Add(Ing2);

		DT_Crafting->AddRow(TEXT("Recipe_DecipherCipher"), Recipe);
		SaveAssetPackage(DT_Crafting);
	}

	// ----------------------------------------------------
	// 4. Random Groups Authoring
	// ----------------------------------------------------
	URandomChestGroup* RG_LandEarly = CreateOrGetRandomGroup(TEXT("/Game/Campaign/DataAsset/Chest/DA_RandomGroup_Land_Early"));
	RG_LandEarly->ChestDefinition = DA_LandEarly; RG_LandEarly->SpawnCount = 2;
	SaveAssetPackage(RG_LandEarly);

	URandomChestGroup* RG_OceanEarly = CreateOrGetRandomGroup(TEXT("/Game/Campaign/DataAsset/Chest/DA_RandomGroup_Ocean_Early"));
	RG_OceanEarly->ChestDefinition = DA_OceanEarly; RG_OceanEarly->SpawnCount = 2;
	SaveAssetPackage(RG_OceanEarly);

	URandomChestGroup* RG_LandMid = CreateOrGetRandomGroup(TEXT("/Game/Campaign/DataAsset/Chest/DA_RandomGroup_Land_Mid"));
	RG_LandMid->ChestDefinition = DA_LandMid; RG_LandMid->SpawnCount = 3;
	SaveAssetPackage(RG_LandMid);

	URandomChestGroup* RG_OceanMid = CreateOrGetRandomGroup(TEXT("/Game/Campaign/DataAsset/Chest/DA_RandomGroup_Ocean_Mid"));
	RG_OceanMid->ChestDefinition = DA_OceanMid; RG_OceanMid->SpawnCount = 2;
	SaveAssetPackage(RG_OceanMid);

	URandomChestGroup* RG_LandLate = CreateOrGetRandomGroup(TEXT("/Game/Campaign/DataAsset/Chest/DA_RandomGroup_Land_Late"));
	RG_LandLate->ChestDefinition = DA_LandLate; RG_LandLate->SpawnCount = 2;
	SaveAssetPackage(RG_LandLate);

	URandomChestGroup* RG_OceanLate = CreateOrGetRandomGroup(TEXT("/Game/Campaign/DataAsset/Chest/DA_RandomGroup_Ocean_Late"));
	RG_OceanLate->ChestDefinition = DA_OceanLate; RG_OceanLate->SpawnCount = 2;
	SaveAssetPackage(RG_OceanLate);

	// ----------------------------------------------------
	// 5. Dialogue DataAssets Authoring (Single Unified YiSunSin Dialogue)
	// ----------------------------------------------------
	UNPCDialogueData* DA_YiSunSin = CreateOrGetDialogueData(TEXT("/Game/Campaign/DataAsset/Dialogue/DA_YiSunSinDialogue"));
	DA_YiSunSin->DisplayName = FText::FromString(TEXT("이순신"));
	DA_YiSunSin->Rules.Reset();
	{
		// 1. 정찰 퀘스트 수락 (GameStarted 상태에서 바로 시작)
		FNPCDialogueRule R1; R1.RuleId = TEXT("Rule_ReconQuest"); R1.Priority = 200;
		R1.RequiredStoryNodes.Add(EStoryNode::GameStarted);
		R1.BlockedStoryNodes.Add(EStoryNode::ReconQuestAccepted);
		R1.bCompleteStoryNode = true; R1.StoryNodeToComplete = EStoryNode::ReconQuestAccepted;
		FNPCDialogueLine L1; L1.LineId = TEXT("L1"); L1.Text = FText::FromString(TEXT("왜군의 움직임이 심상치 않소. 전방 해역을 정찰하고 적 암호 해독서를 확보하며 적 선봉장(중간보스 1)을 격파하시오."));
		R1.Lines.Add(L1);
		DA_YiSunSin->Rules.Add(R1);

		// 2. 보급로 차단 퀘스트 & 해류 발생기 장치 해금
		FNPCDialogueRule R2; R2.RuleId = TEXT("Rule_SupplyPatrol"); R2.Priority = 210;
		R2.RequiredStoryNodes.Add(EStoryNode::MiddleBoss1Defeated);
		R2.BlockedStoryNodes.Add(EStoryNode::SupplyPatrolQuestAccepted);
		R2.bCompleteStoryNode = true; R2.StoryNodeToComplete = EStoryNode::SupplyPatrolQuestAccepted;
		FNPCDialogueLine L2; L2.LineId = TEXT("L1"); L2.Text = FText::FromString(TEXT("적 선봉장을 격파했군! 이제 놈들의 보급로를 차단하고 중간보스 2를 격파하여 일본군 암호를 노획하시오."));
		R2.Lines.Add(L2);
		DA_YiSunSin->Rules.Add(R2);

		// 3. 암호문 노획 후 해독서 조합 퀘스트
		FNPCDialogueRule R3; R3.RuleId = TEXT("Rule_DecipherQuest"); R3.Priority = 220;
		R3.RequiredStoryNodes.Add(EStoryNode::MiddleBoss2Defeated);
		R3.BlockedStoryNodes.Add(EStoryNode::SuppressJapaneseForcesQuestAccepted);
		R3.bCompleteStoryNode = true; R3.StoryNodeToComplete = EStoryNode::DecipherQuestAccepted;
		FNPCDialogueLine L3; L3.LineId = TEXT("L1"); L3.Text = FText::FromString(TEXT("일본군 암호를 노획했군! 제작대(작업대)에서 암호 해독서와 조합하여 [해독된 암호]를 제작해 오시오."));
		R3.Lines.Add(L3);
		DA_YiSunSin->Rules.Add(R3);

		// 4. 왜군 본대 저지 퀘스트 (인벤토리에 해독된 암호문이 있을 때 즉시 발동)
		FNPCDialogueRule R4; R4.RuleId = TEXT("Rule_SuppressForces"); R4.Priority = 230;
		R4.RequiredStoryNodes.Add(EStoryNode::MiddleBoss2Defeated);
		R4.BlockedStoryNodes.Add(EStoryNode::SuppressJapaneseForcesQuestAccepted);
		FCraftingItemStack ReqDeciphered;
		ReqDeciphered.ItemTag = Item_Quest_DecipheredCipher;
		ReqDeciphered.Quantity = 1;
		R4.RequiredItems.Add(ReqDeciphered);
		R4.bCompleteStoryNode = true; R4.StoryNodeToComplete = EStoryNode::SuppressJapaneseForcesQuestAccepted;
		FNPCDialogueLine L4; L4.LineId = TEXT("L1"); L4.Text = FText::FromString(TEXT("훌륭하오! 해독된 정보에 따르면 왜군 본대의 공습이 임박했소. 중간보스 3을 먼저 저지하시오."));
		R4.Lines.Add(L4);
		DA_YiSunSin->Rules.Add(R4);

		// 5. 울돌목 결전 퀘스트
		FNPCDialogueRule R5; R5.RuleId = TEXT("Rule_UldolmokBattle"); R5.Priority = 240;
		R5.RequiredStoryNodes.Add(EStoryNode::MiddleBoss3Defeated);
		R5.BlockedStoryNodes.Add(EStoryNode::UldolmokBattleQuestAccepted);
		R5.bCompleteStoryNode = true; R5.StoryNodeToComplete = EStoryNode::UldolmokBattleQuestAccepted;
		FNPCDialogueLine L5; L5.LineId = TEXT("L1"); L5.Text = FText::FromString(TEXT("적의 총공세가 시작되었소. 울돌목의 좁은 해협으로 적 최종 함대를 유인해 격멸합시다!"));
		R5.Lines.Add(L5);
		DA_YiSunSin->Rules.Add(R5);

		// 6. 최종 엔딩 대사
		FNPCDialogueRule R6; R6.RuleId = TEXT("Rule_Ending"); R6.Priority = 250;
		R6.RequiredStoryNodes.Add(EStoryNode::FinalBossDefeated);
		R6.BlockedStoryNodes.Add(EStoryNode::EndingDialogueCompleted);
		R6.bCompleteStoryNode = true; R6.StoryNodeToComplete = EStoryNode::EndingDialogueCompleted;
		FNPCDialogueLine L6; L6.LineId = TEXT("L1"); L6.Text = FText::FromString(TEXT("신에게는 아직 12척의 배가 남아있었소... 왜적을 완파하고 조선의 바다를 지켜내었소!"));
		R6.Lines.Add(L6);
		DA_YiSunSin->Rules.Add(R6);

		// 7. 기본 대기 대사
		FNPCDialogueRule R_Amb; R_Amb.RuleId = TEXT("Rule_Ambient"); R_Amb.Priority = 0;
		FNPCDialogueLine L_Amb; L_Amb.LineId = TEXT("L1"); L_Amb.Text = FText::FromString(TEXT("바다를 지키는 일은 한 치의 방심도 허용되지 않소."));
		R_Amb.Lines.Add(L_Amb);
		DA_YiSunSin->Rules.Add(R_Amb);
	}
	SaveAssetPackage(DA_YiSunSin);

	// Also generate DA_TestNPCDialogue for legacy test
	UNPCDialogueData* DA_TestNPC = CreateOrGetDialogueData(TEXT("/Game/New/NPC/Data/DA_TestNPCDialogue"));
	DA_TestNPC->DisplayName = FText::FromString(TEXT("테스트 NPC"));
	DA_TestNPC->Rules = DA_YiSunSin->Rules;
	{
		FNPCDialogueRule AmbientRule;
		AmbientRule.RuleId = TEXT("Ambient_Default");
		AmbientRule.Priority = 0;
		FNPCDialogueLine L1; L1.LineId = TEXT("Ambient_01"); L1.Text = FText::FromString(TEXT("안녕하세요."));
		FNPCDialogueLine L2; L2.LineId = TEXT("Ambient_02"); L2.Text = FText::FromString(TEXT("선택지를 고르세요."));
		FNPCDialogueReply Rep1; Rep1.ReplyId = TEXT("R1"); Rep1.Text = FText::FromString(TEXT("1번"));
		FNPCDialogueReply Rep2; Rep2.ReplyId = TEXT("R2"); Rep2.Text = FText::FromString(TEXT("2번"));
		L2.Replies.Add(Rep1);
		L2.Replies.Add(Rep2);
		AmbientRule.Lines.Add(L1);
		AmbientRule.Lines.Add(L2);
		DA_TestNPC->Rules.Add(AmbientRule);
	}
	SaveAssetPackage(DA_TestNPC);

	// ----------------------------------------------------
	// 6. Automated Validation Checks & Full E2E Scenario Simulation
	// ----------------------------------------------------
	TestNotNull(TEXT("DT_Early is valid"), DT_Early);
	TestNotNull(TEXT("DT_MB1 is valid"), DT_MB1);
	TestNotNull(TEXT("DA_MB1 is valid"), DA_MB1);
	TestNotNull(TEXT("DA_YiSunSin is valid"), DA_YiSunSin);
	TestNotNull(TEXT("RG_LandEarly is valid"), RG_LandEarly);

	// 1. Validation: AChestSpawnPoint with Boss Guard injects quest item
	AChestSpawnPoint* TestPoint = NewObject<AChestSpawnPoint>();
	TestPoint->bIsBossChest = true;
	TestPoint->GuaranteedBossQuestItemTag = Item_Quest_CipherBook;
	TestPoint->GuaranteedBossQuestItemCount = 1;

	// Without matching guard, HasMatchingBossGuard is false
	TestFalse(TEXT("No guard -> HasMatchingBossGuard is false"), TestPoint->HasMatchingBossGuard());

	// With matching boss guard
	ABaseCharacter* DummyBoss = NewObject<ABaseCharacter>();
	const FGameplayTag BossTag = Enemy_Type_Boss_Mid1;
	DummyBoss->Tags.Add(BossTag.GetTagName());
	TestPoint->RequiredBossTag = BossTag;
	TArray<ABaseCharacter*> GuardList;
	GuardList.Add(DummyBoss);
	TestPoint->ConfigureGuardedSpawn(DA_MB1, GuardList);
	TestTrue(TEXT("With Boss guard -> HasMatchingBossGuard is true"), TestPoint->HasMatchingBossGuard());

	// If RequiredBossTag is cleared, it should not act as a boss chest
	TestPoint->RequiredBossTag = FGameplayTag();
	TestFalse(TEXT("Cleared RequiredBossTag -> HasMatchingBossGuard is false"), TestPoint->HasMatchingBossGuard());

	// 1-B. Validation: Dynamic Guard Registration & 3 Lock States requested by User
	// Scenario 1: No mobs initially -> chest starts unlocked
	AStorageChest* DynamicChest = NewObject<AStorageChest>();
	DynamicChest->ConfigureGuarding(true, {}, nullptr);
	TestFalse(TEXT("Scenario 1: Chest without initial guards is unlocked"), DynamicChest->IsLocked());

	// Boss spawns dynamically -> AddGuardCharacter locks chest
	ABaseCharacter* DynamicBoss = NewObject<ABaseCharacter>();
	UBaseHealthComponent* BossHealth = NewObject<UBaseHealthComponent>(DynamicBoss);
	DynamicBoss->AddInstanceComponent(BossHealth);
	DynamicChest->AddGuardCharacter(DynamicBoss);
	TestTrue(TEXT("Scenario 1: Dynamically spawned boss locks the chest"), DynamicChest->IsLocked());

	// Boss dies -> Chest unlocks
	DynamicChest->HandleTrackedHealthDeath(BossHealth);
	TestFalse(TEXT("Scenario 1: Boss death unlocks the chest"), DynamicChest->IsLocked());

	// Scenario 2: Mob guard + delayed boss spawn
	AStorageChest* MultiGuardChest = NewObject<AStorageChest>();
	ABaseCharacter* NormalMob = NewObject<ABaseCharacter>();
	UBaseHealthComponent* MobHealth = NewObject<UBaseHealthComponent>(NormalMob);
	NormalMob->AddInstanceComponent(MobHealth);
	MultiGuardChest->ConfigureGuarding(true, { NormalMob }, nullptr);
	TestTrue(TEXT("Scenario 2: Chest with normal mob starts locked"), MultiGuardChest->IsLocked());

	// Mob dies before boss spawns -> unlocks
	MultiGuardChest->HandleTrackedHealthDeath(MobHealth);
	TestFalse(TEXT("Scenario 2: Mob dies -> chest unlocks"), MultiGuardChest->IsLocked());

	// Later boss spawns -> relocks
	ABaseCharacter* DelayedBoss = NewObject<ABaseCharacter>();
	UBaseHealthComponent* DelayedBossHealth = NewObject<UBaseHealthComponent>(DelayedBoss);
	DelayedBoss->AddInstanceComponent(DelayedBossHealth);
	MultiGuardChest->AddGuardCharacter(DelayedBoss);
	TestTrue(TEXT("Scenario 2: Delayed boss spawns -> chest relocks"), MultiGuardChest->IsLocked());

	// Delayed boss dies -> unlocks
	MultiGuardChest->HandleTrackedHealthDeath(DelayedBossHealth);
	TestFalse(TEXT("Scenario 2: Delayed boss dies -> chest unlocks again"), MultiGuardChest->IsLocked());

	// 2. Validation: Full 8-Step Story E2E Scenario
	UGameInstance* GI = NewObject<UGameInstance>();
	UStorySubsystem* StoryState = NewObject<UStorySubsystem>(GI);
	UStoryFacadeSubsystem* Story = NewObject<UStoryFacadeSubsystem>(GI);
	Story->ConfigureForUseCase(StoryState);

	TestTrue(TEXT("Step 0: Game starts automatically"), Story->StartNewCampaign());
	TestTrue(TEXT("Step 1: First YiSunSin talk gives Recon quest"), Story->CompleteStoryNode(EStoryNode::ReconQuestAccepted));
	TestTrue(TEXT("Step 2: Defeat MidBoss 1 (CipherBook acquired)"), Story->CompleteStoryNode(EStoryNode::MiddleBoss1Defeated));
	TestTrue(TEXT("CipherBook acquired recorded"), Story->CompleteStoryNode(EStoryNode::CipherBookAcquired));
	TestTrue(TEXT("Step 3: Talk to YiSunSin gives SupplyPatrol quest"), Story->CompleteStoryNode(EStoryNode::SupplyPatrolQuestAccepted));
	TestTrue(TEXT("Step 4: Defeat MidBoss 2 (JapaneseCipher acquired)"), Story->CompleteStoryNode(EStoryNode::MiddleBoss2Defeated));
	TestTrue(TEXT("Step 5: Talk to YiSunSin gives Decipher quest"), Story->CompleteStoryNode(EStoryNode::DecipherQuestAccepted));
	TestTrue(TEXT("Step 6: Crafting complete -> Talk gives Suppress forces quest"), Story->CompleteStoryNode(EStoryNode::SuppressJapaneseForcesQuestAccepted));
	TestTrue(TEXT("Step 7: Defeat MidBoss 3 (AirRaidInfo acquired)"), Story->CompleteStoryNode(EStoryNode::MiddleBoss3Defeated));
	TestTrue(TEXT("Step 8: Talk gives Uldolmok battle quest"), Story->CompleteStoryNode(EStoryNode::UldolmokBattleQuestAccepted));
	TestTrue(TEXT("Step 9: Defeat Final Boss"), Story->CompleteStoryNode(EStoryNode::FinalBossDefeated));
	TestTrue(TEXT("Step 10: Talk to YiSunSin gives Ending"), Story->CompleteStoryNode(EStoryNode::EndingDialogueCompleted));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS