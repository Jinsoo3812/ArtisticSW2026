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
#include "NPCDialogueData.h"
#include "StoryFacadeSubsystem.h"
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
	UDataTable* DataTable = LoadObject<UDataTable>(nullptr, *PackagePath);
	if (!DataTable)
	{
		UPackage* Package = CreatePackage(*PackagePath);
		Package->FullyLoad();
		FString AssetName = FPackageName::GetShortName(PackagePath);
		DataTable = NewObject<UDataTable>(Package, *AssetName, RF_Public | RF_Standalone);
		DataTable->RowStruct = RowStruct;
	}
	return DataTable;
}

static UChestDefinition* CreateOrGetChestDef(const FString& PackagePath)
{
	UChestDefinition* Def = LoadObject<UChestDefinition>(nullptr, *PackagePath);
	if (!Def)
	{
		UPackage* Package = CreatePackage(*PackagePath);
		Package->FullyLoad();
		FString AssetName = FPackageName::GetShortName(PackagePath);
		Def = NewObject<UChestDefinition>(Package, *AssetName, RF_Public | RF_Standalone);
	}
	return Def;
}

static URandomChestGroup* CreateOrGetRandomGroup(const FString& PackagePath)
{
	URandomChestGroup* Group = LoadObject<URandomChestGroup>(nullptr, *PackagePath);
	if (!Group)
	{
		UPackage* Package = CreatePackage(*PackagePath);
		Package->FullyLoad();
		FString AssetName = FPackageName::GetShortName(PackagePath);
		Group = NewObject<URandomChestGroup>(Package, *AssetName, RF_Public | RF_Standalone);
	}
	return Group;
}

static UNPCDialogueData* CreateOrGetDialogueData(const FString& PackagePath)
{
	UNPCDialogueData* DialogueData = LoadObject<UNPCDialogueData>(nullptr, *PackagePath);
	if (!DialogueData)
	{
		UPackage* Package = CreatePackage(*PackagePath);
		Package->FullyLoad();
		FString AssetName = FPackageName::GetShortName(PackagePath);
		DialogueData = NewObject<UNPCDialogueData>(Package, *AssetName, RF_Public | RF_Standalone);
	}
	return DialogueData;
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
	TSubclassOf<AStorageChest> DefaultChestClass = AStorageChest::StaticClass();

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
	DA_MB1->GuaranteedQuestItemTag = Item_Quest_InvasionMap;
	DA_MB1->GuaranteedQuestItemCount = 1;
	DA_MB1->RequiredStoryNodeForQuestItem = EStoryNode::ReconQuestAccepted;
	DA_MB1->bStopAfterStoryNode = true;
	DA_MB1->StopAfterStoryNodeForQuestItem = EStoryNode::MiddleBoss1Defeated;
	SaveAssetPackage(DA_MB1);

	UChestDefinition* DA_MB2 = CreateOrGetChestDef(TEXT("/Game/Campaign/DataAsset/Chest/DA_Chest_MidBoss2"));
	DA_MB2->ChestClass = DefaultChestClass; DA_MB2->LootTable = DT_MB2; DA_MB2->RollCount = 2; DA_MB2->SlotCount = 6; DA_MB2->ColumnCount = 4;
	DA_MB2->GuaranteedQuestItemTag = Item_Quest_JapaneseCipher;
	DA_MB2->GuaranteedQuestItemCount = 1;
	DA_MB2->RequiredStoryNodeForQuestItem = EStoryNode::SupplyPatrolQuestAccepted;
	DA_MB2->bStopAfterStoryNode = true;
	DA_MB2->StopAfterStoryNodeForQuestItem = EStoryNode::MiddleBoss2Defeated;
	SaveAssetPackage(DA_MB2);

	UChestDefinition* DA_MB3 = CreateOrGetChestDef(TEXT("/Game/Campaign/DataAsset/Chest/DA_Chest_MidBoss3"));
	DA_MB3->ChestClass = DefaultChestClass; DA_MB3->LootTable = DT_MB3; DA_MB3->RollCount = 2; DA_MB3->SlotCount = 6; DA_MB3->ColumnCount = 4;
	DA_MB3->GuaranteedQuestItemTag = Item_Quest_AirRaidInfo;
	DA_MB3->GuaranteedQuestItemCount = 1;
	DA_MB3->RequiredStoryNodeForQuestItem = EStoryNode::SuppressJapaneseForcesQuestAccepted;
	DA_MB3->bStopAfterStoryNode = true;
	DA_MB3->StopAfterStoryNodeForQuestItem = EStoryNode::MiddleBoss3Defeated;
	SaveAssetPackage(DA_MB3);

	UChestDefinition* DA_Cipher = CreateOrGetChestDef(TEXT("/Game/Campaign/DataAsset/Chest/DA_Chest_CipherBook"));
	DA_Cipher->ChestClass = DefaultChestClass; DA_Cipher->LootTable = DT_Cipher; DA_Cipher->RollCount = 2; DA_Cipher->SlotCount = 4; DA_Cipher->ColumnCount = 4;
	DA_Cipher->GuaranteedQuestItemTag = Item_Quest_CipherBook;
	DA_Cipher->GuaranteedQuestItemCount = 1;
	DA_Cipher->RequiredStoryNodeForQuestItem = EStoryNode::ReconQuestAccepted;
	DA_Cipher->bStopAfterStoryNode = true;
	DA_Cipher->StopAfterStoryNodeForQuestItem = EStoryNode::CipherBookAcquired;
	SaveAssetPackage(DA_Cipher);

	// ----------------------------------------------------
	// 3. Random Groups Authoring
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
	// 4. Dialogue DataAssets Authoring
	// ----------------------------------------------------
	UNPCDialogueData* DA_YiSunSin = CreateOrGetDialogueData(TEXT("/Game/Campaign/DataAsset/Dialogue/DA_YiSunSinDialogue"));
	DA_YiSunSin->DisplayName = FText::FromString(TEXT("이순신"));
	DA_YiSunSin->Rules.Reset();
	{
		FNPCDialogueRule R1; R1.RuleId = TEXT("Rule_ReconQuest"); R1.Priority = 200;
		R1.RequiredStoryNodes.Add(EStoryNode::FirstSailingCompleted);
		R1.BlockedStoryNodes.Add(EStoryNode::ReconQuestAccepted);
		R1.bCompleteStoryNode = true; R1.StoryNodeToComplete = EStoryNode::ReconQuestAccepted;
		FNPCDialogueLine L1; L1.LineId = TEXT("L1"); L1.Text = FText::FromString(TEXT("왜군의 움직임이 심상치 않소. 전방 해역을 정찰하고 적 선봉장(중간보스 1)을 처치하시오."));
		R1.Lines.Add(L1);
		DA_YiSunSin->Rules.Add(R1);

		FNPCDialogueRule R2; R2.RuleId = TEXT("Rule_SupplyPatrol"); R2.Priority = 210;
		R2.RequiredStoryNodes.Add(EStoryNode::MiddleBoss1Defeated);
		R2.BlockedStoryNodes.Add(EStoryNode::SupplyPatrolQuestAccepted);
		R2.bCompleteStoryNode = true; R2.StoryNodeToComplete = EStoryNode::SupplyPatrolQuestAccepted;
		FNPCDialogueLine L2; L2.LineId = TEXT("L1"); L2.Text = FText::FromString(TEXT("적의 침공 지도를 확보했군! 이제 놈들의 보급로를 차단하고 중간보스 2를 격파해야 하오."));
		R2.Lines.Add(L2);
		DA_YiSunSin->Rules.Add(R2);

		FNPCDialogueRule R3; R3.RuleId = TEXT("Rule_DecipherQuest"); R3.Priority = 220;
		R3.RequiredStoryNodes.Add(EStoryNode::MiddleBoss2Defeated);
		R3.BlockedStoryNodes.Add(EStoryNode::DecipherQuestAccepted);
		R3.bCompleteStoryNode = true; R3.StoryNodeToComplete = EStoryNode::DecipherQuestAccepted;
		FNPCDialogueLine L3; L3.LineId = TEXT("L1"); L3.Text = FText::FromString(TEXT("암호문을 노획했으나 해독서가 필요하오. 서브 해역에서 해독서를 찾아 암호를 해독해 오시오."));
		R3.Lines.Add(L3);
		DA_YiSunSin->Rules.Add(R3);

		FNPCDialogueRule R4; R4.RuleId = TEXT("Rule_SuppressForces"); R4.Priority = 230;
		R4.RequiredStoryNodes.Add(EStoryNode::DecipherQuestAccepted);
		R4.RequiredStoryNodes.Add(EStoryNode::CipherBookAcquired);
		R4.BlockedStoryNodes.Add(EStoryNode::SuppressJapaneseForcesQuestAccepted);
		R4.bCompleteStoryNode = true; R4.StoryNodeToComplete = EStoryNode::SuppressJapaneseForcesQuestAccepted;
		FNPCDialogueLine L4; L4.LineId = TEXT("L1"); L4.Text = FText::FromString(TEXT("훌륭하오! 해독된 정보에 따르면 왜군 본대의 공습이 임박했소. 중간보스 3을 먼저 저지하시오."));
		R4.Lines.Add(L4);
		DA_YiSunSin->Rules.Add(R4);

		FNPCDialogueRule R5; R5.RuleId = TEXT("Rule_UldolmokBattle"); R5.Priority = 240;
		R5.RequiredStoryNodes.Add(EStoryNode::MiddleBoss3Defeated);
		R5.BlockedStoryNodes.Add(EStoryNode::UldolmokBattleQuestAccepted);
		R5.bCompleteStoryNode = true; R5.StoryNodeToComplete = EStoryNode::UldolmokBattleQuestAccepted;
		FNPCDialogueLine L5; L5.LineId = TEXT("L1"); L5.Text = FText::FromString(TEXT("적의 총공세가 시작되었소. 울돌목의 좁은 해협으로 적 최종 함대를 유인해 격멸합시다!"));
		R5.Lines.Add(L5);
		DA_YiSunSin->Rules.Add(R5);

		FNPCDialogueRule R6; R6.RuleId = TEXT("Rule_Ending"); R6.Priority = 250;
		R6.RequiredStoryNodes.Add(EStoryNode::FinalBossDefeated);
		R6.BlockedStoryNodes.Add(EStoryNode::EndingDialogueCompleted);
		R6.bCompleteStoryNode = true; R6.StoryNodeToComplete = EStoryNode::EndingDialogueCompleted;
		FNPCDialogueLine L6; L6.LineId = TEXT("L1"); L6.Text = FText::FromString(TEXT("신에게는 아직 12척의 배가 남아있었소... 왜적을 완파하고 조선의 바다를 지켜내었소!"));
		R6.Lines.Add(L6);
		DA_YiSunSin->Rules.Add(R6);

		FNPCDialogueRule R_Amb; R_Amb.RuleId = TEXT("Rule_Ambient"); R_Amb.Priority = 0;
		FNPCDialogueLine L_Amb; L_Amb.LineId = TEXT("L1"); L_Amb.Text = FText::FromString(TEXT("바다를 지키는 일은 한 치의 방심도 허용되지 않소."));
		R_Amb.Lines.Add(L_Amb);
		DA_YiSunSin->Rules.Add(R_Amb);
	}
	SaveAssetPackage(DA_YiSunSin);

	// Base Helper Dialogue
	UNPCDialogueData* DA_BaseHelper = CreateOrGetDialogueData(TEXT("/Game/Campaign/DataAsset/Dialogue/DA_BaseNPCDialogue"));
	DA_BaseHelper->DisplayName = FText::FromString(TEXT("조선 수군 군관"));
	DA_BaseHelper->Rules.Reset();
	{
		FNPCDialogueRule R1; R1.RuleId = TEXT("Rule_UnlockCurrent"); R1.Priority = 100;
		R1.RequiredStoryNodes.Add(EStoryNode::SupplyPatrolQuestAccepted);
		R1.BlockedStoryNodes.Add(EStoryNode::CurrentGeneratorUnlocked);
		R1.bCompleteStoryNode = true; R1.StoryNodeToComplete = EStoryNode::CurrentGeneratorUnlocked;
		FNPCDialogueLine L1; L1.LineId = TEXT("L1"); L1.Text = FText::FromString(TEXT("보급로 작전을 지원하기 위해 [해류 발생기] 장치를 활성화했습니다!"));
		R1.Lines.Add(L1);
		DA_BaseHelper->Rules.Add(R1);

		FNPCDialogueRule R2; R2.RuleId = TEXT("Rule_UnlockWaterBomb"); R2.Priority = 110;
		R2.RequiredStoryNodes.Add(EStoryNode::SuppressJapaneseForcesQuestAccepted);
		R2.BlockedStoryNodes.Add(EStoryNode::WaterBombUnlocked);
		R2.bCompleteStoryNode = true; R2.StoryNodeToComplete = EStoryNode::WaterBombUnlocked;
		FNPCDialogueLine L2; L2.LineId = TEXT("L1"); L2.Text = FText::FromString(TEXT("왜군 원군 저지를 위해 강력한 [물폭탄] 기술을 지급합니다."));
		R2.Lines.Add(L2);
		DA_BaseHelper->Rules.Add(R2);

		FNPCDialogueRule R3; R3.RuleId = TEXT("Rule_UnlockBombard"); R3.Priority = 120;
		R3.RequiredStoryNodes.Add(EStoryNode::UldolmokBattleQuestAccepted);
		R3.BlockedStoryNodes.Add(EStoryNode::BombardmentUnlocked);
		R3.bCompleteStoryNode = true; R3.StoryNodeToComplete = EStoryNode::BombardmentUnlocked;
		FNPCDialogueLine L3; L3.LineId = TEXT("L1"); L3.Text = FText::FromString(TEXT("울돌목 결전을 위해 함포 화력을 극대화한 [포탄세례] 기술을 해금합니다!"));
		R3.Lines.Add(L3);
		DA_BaseHelper->Rules.Add(R3);
	}
	SaveAssetPackage(DA_BaseHelper);

	// Also generate DA_TestNPCDialogue for legacy test
	UNPCDialogueData* DA_TestNPC = CreateOrGetDialogueData(TEXT("/Game/New/NPC/Data/DA_TestNPCDialogue"));
	DA_TestNPC->DisplayName = FText::FromString(TEXT("테스트 NPC"));
	DA_TestNPC->Rules = DA_YiSunSin->Rules;
	SaveAssetPackage(DA_TestNPC);

	// ----------------------------------------------------
	// 5. Automated Validation Checks
	// ----------------------------------------------------
	TestNotNull(TEXT("DT_Early is valid"), DT_Early);
	TestNotNull(TEXT("DT_MB1 is valid"), DT_MB1);
	TestNotNull(TEXT("DA_MB1 is valid"), DA_MB1);
	TestNotNull(TEXT("DA_YiSunSin is valid"), DA_YiSunSin);
	TestNotNull(TEXT("RG_LandEarly is valid"), RG_LandEarly);

	// Validation: Test 3-stage Story-Gated Guaranteed Item on DA_MB1
	TArray<FStorageItemEntry> RolledBeforeStory = DA_MB1->RollInitialItems(1234, nullptr);
	bool bHasMapItem = RolledBeforeStory.ContainsByPredicate([](const FStorageItemEntry& E) {
		return E.ItemTag == Item_Quest_InvasionMap;
	});
	TestTrue(TEXT("DA_MB1 includes InvasionMap tag when condition met"), bHasMapItem);

	// Validation: Dialogue rule count
	TestEqual(TEXT("YiSunSin dialogue has 7 rules"), DA_YiSunSin->Rules.Num(), 7);
	TestEqual(TEXT("BaseHelper dialogue has 3 rules"), DA_BaseHelper->Rules.Num(), 3);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS