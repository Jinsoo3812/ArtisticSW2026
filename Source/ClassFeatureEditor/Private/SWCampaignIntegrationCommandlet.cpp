#include "SWCampaignIntegrationCommandlet.h"

#include "BossAI/BossEncounterComponent.h"
#include "ShipAI/EnemyShip.h"
#include "StoryConditionalSpawner.h"
#include "BaseGameplayTags.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Item/ItemData.h"
#include "ItemSpawn/BossChestGuaranteedLootData.h"
#include "ItemSpawn/LootSpawnPoint.h"
#include "NPCDialogueData.h"
#include "Settings_Item.h"
#include "UObject/UnrealType.h"
#include "Components/ChildActorComponent.h"
#include "BaseManagerDialogueData.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"
#include "BaseEnemy.h"
#include "Spawning/CampaignBossSpawner.h"
#include "Components/CapsuleComponent.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "Room/SWFinalEncounterShipEntryPoint.h"
#include "Storage/StorageChest.h"

namespace
{
	FString ExportProperty(UObject* Object, const TCHAR* Name)
	{
		FString Value;
		if (FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), Name))
			Property->ExportText_InContainer(0, Value, Object, Object, Object, PPF_None);
		return Value;
	}

	FCraftingItemStack Stack(FGameplayTag Tag, int32 Quantity = 1)
	{
		FCraftingItemStack Item; Item.ItemTag = Tag; Item.Quantity = Quantity; return Item;
	}

	bool AssignProperty(UObject* Object, const TCHAR* Name, const FString& Value)
	{
		FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), Name);
		return Property && Property->ImportText_Direct(*Value, Property->ContainerPtrToValuePtr<void>(Object), Object, PPF_None);
	}

	bool ConfigureCampaignMap(UWorld* World, bool bApply)
	{
		AActor* Anchor = nullptr;
		AChestSpawnPoint* ExistingChest = nullptr;
		ACampaignBossSpawner* Boss = nullptr;
		int32 BossCount = 0, FinalEntries = 0, ShipGates = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel() == TEXT("BP_ESpawner_Z2")) Anchor = *It;
			if (It->GetActorLabel() == TEXT("BP_ChestSpawnPoint_Land_Guarded2")) ExistingChest = Cast<AChestSpawnPoint>(*It);
			if (ACampaignBossSpawner* Candidate = Cast<ACampaignBossSpawner>(*It)) { Boss = Candidate; ++BossCount; }
			if (It->IsA<ASWFinalEncounterShipEntryPoint>()) ++FinalEntries;
			if (AEnemyShip* Ship = Cast<AEnemyShip>(*It))
			{
				UBossEncounterComponent* Encounter = Ship->GetBossEncounterComponent();
				if (Encounter && Encounter->IsEncounterEnabled())
				{
					const bool bFirst = Ship->GetActorLabel() == TEXT("BP_ES_Mid_1");
					const bool bThird = Ship->GetActorLabel() == TEXT("BP_ES_Mid_3");
					if ((!bFirst && !bThird) || ExportProperty(Encounter, TEXT("RequiredStoryNode")) != (bFirst ? TEXT("ReconQuestAccepted") : TEXT("SuppressJapaneseForcesQuestAccepted"))
						|| ExportProperty(Encounter, TEXT("StopAfterStoryNode")) != (bFirst ? TEXT("MiddleBoss1Defeated") : TEXT("MiddleBoss3Defeated"))) return false;
					++ShipGates;
				}
			}
		}
		if (!Anchor || !ExistingChest || BossCount > 1 || FinalEntries != 1 || ShipGates != 2) return false;
		if (bApply)
		{
			UClass* BossClass = LoadClass<ABaseEnemy>(nullptr, TEXT("/Game/GameplayAbilitySystem/Enemy/Bosses/BP_Boss_Mid_2.BP_Boss_Mid_2_C"));
			UClass* ChestClass = LoadClass<AStorageChest>(nullptr, TEXT("/Game/Blueprints/Item/BP/Chest/BP_Storage_Chest.BP_Storage_Chest_C"));
			if (!BossClass || !ChestClass) return false;
			FVector BossLocation = Anchor->GetActorLocation();
			const FVector ChestLocation = ExistingChest->GetActorLocation();
			const ABaseEnemy* Defaults = BossClass->GetDefaultObject<ABaseEnemy>();
			BossLocation.Z += Defaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f;
			FActorSpawnParameters Params; Params.Name = TEXT("SWCampaignMid2BossSpawner");
			if (!Boss) Boss = World->SpawnActor<ACampaignBossSpawner>(ACampaignBossSpawner::StaticClass(), BossLocation, FRotator::ZeroRotator, Params);
			AChestSpawnPoint* Chest = ExistingChest;
			if (!Boss || !Chest) return false;
			Boss->SetActorLocation(BossLocation);
			Boss->SetActorLabel(TEXT("Campaign_Mid2_BossSpawner"));
			if (AChestSpawnPoint* TemporaryChest = Boss->RewardChest.Get(); TemporaryChest && TemporaryChest != Chest
				&& TemporaryChest->GetFName() == TEXT("SWCampaignMid2RewardChest") && TemporaryChest->GetClass() == AChestSpawnPoint::StaticClass())
				World->DestroyActor(TemporaryChest);
			Boss->RewardChest = Chest; Chest->GuardSpawners.AddUnique(Boss);
			if (!AssignProperty(Boss, TEXT("SpawnedActorClass"), BossClass->GetPathName())
				|| !AssignProperty(Boss, TEXT("RequiredStoryNode"), TEXT("SupplyPatrolQuestAccepted"))
				|| !AssignProperty(Boss, TEXT("StopAfterStoryNode"), TEXT("MiddleBoss2Defeated"))
				|| !AssignProperty(Boss, TEXT("bStopAfterStoryNode"), TEXT("True"))
				|| !AssignProperty(Chest, TEXT("ProgressionZone"), TEXT("Mid2"))
				|| !AssignProperty(Chest, TEXT("ProgressionKind"), TEXT("IslandGuarded"))
				|| !AssignProperty(Chest, TEXT("ChestClassOverride"), ChestClass->GetPathName())) return false;
			for (AActor* Actor : {static_cast<AActor*>(Boss), static_cast<AActor*>(Chest)})
			{
				USWRoomSnapshotComponent* Id = Actor->FindComponentByClass<USWRoomSnapshotComponent>();
				if (!Id) return false;
				if (!Id->StableId.IsValid()) Id->StableId = FGuid::NewGuid(); Id->bRequired = true;
			}
			const FString Map = FPackageName::LongPackageNameToFilename(TEXT("/Game/Level/Lvl_CY"), FPackageName::GetMapPackageExtension());
			const FString Backup = FPaths::ProjectSavedDir() / TEXT("CampaignIntegrationBackup") / FGuid::NewGuid().ToString(EGuidFormats::Digits) / TEXT("Lvl_CY.umap");
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
			if (IFileManager::Get().Copy(*Backup, *Map) != COPY_OK || !UEditorLoadingAndSavingUtils::SaveMap(World, TEXT("/Game/Level/Lvl_CY"))) return false;
			UE_LOG(LogTemp, Display, TEXT("CampaignApply MapSaved Backup=%s Boss=%s Chest=%s"), *Backup, *BossLocation.ToString(), *ChestLocation.ToString());
		}
		if (!Boss || !Boss->RewardChest || ExportProperty(Boss, TEXT("RequiredStoryNode")) != TEXT("SupplyPatrolQuestAccepted")
			|| ExportProperty(Boss, TEXT("StopAfterStoryNode")) != TEXT("MiddleBoss2Defeated")
			|| ExportProperty(Boss, TEXT("bStopAfterStoryNode")) != TEXT("True")
			|| !ExportProperty(Boss, TEXT("SpawnedActorClass")).Contains(TEXT("BP_Boss_Mid_2"))
			|| Boss->RewardChest != ExistingChest
			|| !Boss->RewardChest->GuardSpawners.Contains(Boss) || Boss->RewardChest->GetProgressionZone() != EProgressionZone::Mid2) return false;
		UE_LOG(LogTemp, Display, TEXT("CampaignMap Verified ShipGates=%d Mid2Spawner=1 FinalEntries=%d"), ShipGates, FinalEntries);
		return true;
	}

	bool SaveCampaignAsset(UObject* Object, const FString& Backup)
	{
		const FString File = FPackageName::LongPackageNameToFilename(Object->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension());
		const FString Copy = Backup / Object->GetPackage()->GetName().Mid(6) + FPackageName::GetAssetPackageExtension();
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Copy), true);
		if (IFileManager::Get().Copy(*Copy, *File) != COPY_OK) return false;
		Object->MarkPackageDirty();
		FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
		const bool bSaved = UPackage::SavePackage(Object->GetPackage(), Object, *File, Args);
		UE_LOG(LogTemp, Display, TEXT("CampaignApply Saved=%d Asset=%s Backup=%s"), bSaved, *Object->GetPathName(), *Copy);
		return bSaved;
	}

	bool ConfigureCampaignAssets(UItemData* Items, UDataTable* Features, UDataTable* Recipes,
		UNPCDialogueData* Dialogue, UBossChestGuaranteedLootData* Loot, bool bApply)
	{
		const FString Backup = FPaths::ProjectSavedDir() / TEXT("CampaignIntegrationBackup") / FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FGameplayTag Tags[] = {Item_Quest_CipherBook, Item_Quest_InvasionMap, Item_Quest_JapaneseCipher, Item_Quest_DecipheredCipher, Item_Quest_AirRaidInfo};
		const TCHAR* Names[] = {TEXT("암호 해독서"), TEXT("침공 지도"), TEXT("일본군 암호"), TEXT("해독된 암호"), TEXT("공습 정보")};
		const TCHAR* Legacy[] = {TEXT("Item.Id.Clue.Clue1"), TEXT("Item.Id.Clue.Clue2"), TEXT("Item.Id.Clue.Clue3"), TEXT("Item.Id.Clue.Clue3"), TEXT("Item.Id.Clue.Clue4")};
		bool bValid = true;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Tags); ++Index)
		{
			if (bApply)
			{
				const FItemDefinition* Template = Items->FindItemDefinition(FGameplayTag::RequestGameplayTag(Legacy[Index]));
				if (!Template) return false;
				FItemDefinition Definition = *Template;
				Definition.CategoryTag = Item_Category_Clue;
				Items->ItemDefinitions.Add(Tags[Index], Definition);
				FItemFeatureData Feature;
				Feature.ItemName = FText::FromString(Names[Index]); Feature.MaxStack = 99;
				Features->AddRow(Tags[Index].GetTagName(), Feature);
			}
			const FItemDefinition* Definition = Items->FindItemDefinition(Tags[Index]);
			const FItemFeatureData* Feature = Features->FindRow<FItemFeatureData>(Tags[Index].GetTagName(), TEXT("Campaign verify"), false);
			bValid &= Definition && Definition->CategoryTag == Item_Category_Clue && Feature && Feature->ItemName.ToString() == Names[Index];
		}
		const FGameplayTag SkillItems[] = {Item_Id_Skill_GravityVortex, Item_Id_Skill_WaterBomb, Item_Id_Skill_Bombardment};
		const FGameplayTag Gimmicks[] = {Item_Id_Material_SkillMaterial_RareSkill, Item_Id_Material_SkillMaterial_EpicSkill, Item_Id_Material_SkillMaterial_LegendarySkill};
		const TCHAR* RecipeNames[] = {TEXT("GravityVortex"), TEXT("WaterBomb"), TEXT("Bombbardment")};
		const TCHAR* SkillNames[] = {TEXT("해류 발생기 사용 아이템"), TEXT("물폭탄 사용 아이템"), TEXT("포탄세례 사용 아이템")};
		for (int32 Index = 0; Index < 3; ++Index)
		{
			FCraftingRecipeRow* Recipe = Recipes->FindRow<FCraftingRecipeRow>(RecipeNames[Index], TEXT("Campaign skill recipe"), false);
			FItemDefinition* Definition = Items->ItemDefinitions.Find(SkillItems[Index]);
			if (!Recipe || !Definition) return false;
			TArray<FCraftingItemStack> Costs = {Stack(Gimmicks[Index])};
			if (Index == 0) Costs.Add(Stack(Item_Id_Material_WeaponMaterial_Wood, 2));
			if (Index == 1) { Costs.Add(Stack(Item_Id_Material_WeaponMaterial_GoodWood)); Costs.Add(Stack(Item_Id_Material_WeaponMaterial_Iron)); }
			if (Index == 2) { Costs.Add(Stack(Item_Id_Material_WeaponMaterial_Iron)); Costs.Add(Stack(Item_Id_Material_WeaponMaterial_GoodIron)); }
			if (bApply)
			{
				Recipe->RequiredRecipeItemTag = FGameplayTag(); Recipe->bConsumeRecipeItem = false;
				Recipe->Ingredients = Costs; Recipe->ResultQuantity = 1; Recipe->bEnabled = true;
				Recipe->ProgressionTier = 0; Recipe->ProgressionTrack = EProgressionRecipeTrack::None;
				Definition->CategoryTag = Item_Category_Consumable; Definition->ProgressionKind = EItemProgressionKind::None; Definition->ProgressionTier = 0;
				FItemFeatureData Feature;
				if (const FItemFeatureData* Existing = Features->FindRow<FItemFeatureData>(SkillItems[Index].GetTagName(), TEXT("Skill names"), false)) Feature = *Existing;
				Feature.ItemName = FText::FromString(SkillNames[Index]); Feature.MaxStack = 99;
				Features->AddRow(SkillItems[Index].GetTagName(), Feature);
			}
			bValid &= Recipe->ResultItemTag == SkillItems[Index] && !Recipe->RequiredRecipeItemTag.IsValid()
				&& !Recipe->bConsumeRecipeItem && Recipe->ResultQuantity == 1 && Recipe->Ingredients.Num() == Costs.Num()
				&& Definition->CategoryTag == Item_Category_Consumable && Definition->ProgressionKind == EItemProgressionKind::None;
			for (int32 Cost = 0; Cost < Costs.Num() && Cost < Recipe->Ingredients.Num(); ++Cost)
				bValid &= Costs[Cost].ItemTag == Recipe->Ingredients[Cost].ItemTag && Costs[Cost].Quantity == Recipe->Ingredients[Cost].Quantity;
		}
		for (FGuaranteedBossLootEntry& Entry : Loot->Entries)
		{
			const FString Class = Entry.BossClass.ToString();
			TArray<FStorageItemEntry> Expected;
			auto Add = [&Expected](FGameplayTag Tag) { FStorageItemEntry& Item = Expected.AddDefaulted_GetRef(); Item.ItemTag = Tag; Item.Count = 1; };
			if (Class.Contains(TEXT("T2_BP_ShipBoss_Rogue"))) { Add(Item_Quest_CipherBook); Add(Item_Quest_InvasionMap); }
			else if (Class.Contains(TEXT("BP_Boss_Mid_2"))) Add(Item_Quest_JapaneseCipher);
			else if (Class.Contains(TEXT("T4_BP_ShipBoss_Samurai"))) Add(Item_Quest_AirRaidInfo);
			else continue;
			if (bApply) Entry.GuaranteedItems = Expected;
			for (const FStorageItemEntry& Required : Expected)
				bValid &= Entry.GuaranteedItems.ContainsByPredicate([&Required](const FStorageItemEntry& Item) { return Item.ItemTag == Required.ItemTag && Item.Count >= Required.Count; });
		}
		for (FNPCDialogueRule& Rule : Dialogue->Rules)
		{
			if (bApply)
			{
				for (TArray<FCraftingItemStack>* List : {&Rule.RequiredItems, &Rule.ConsumedItems, &Rule.RewardItems})
					for (FCraftingItemStack& Item : *List)
						for (int32 Index : {0, 1, 2, 4}) if (Item.ItemTag.ToString() == Legacy[Index]) { Item.ItemTag = Tags[Index]; break; }
				if (Rule.RuleId == TEXT("Rule_SupplyPatrol")) { Rule.RequiredItems = {Stack(Item_Quest_InvasionMap)}; Rule.ConsumedItems = Rule.RequiredItems; }
				if (Rule.RuleId == TEXT("Rule_SuppressForces")) { Rule.RequiredItems = {Stack(Item_Quest_DecipheredCipher)}; Rule.ConsumedItems = Rule.RequiredItems; }
				if (Rule.RuleId == TEXT("FinalDepartureOffer")) Rule.RequiredItems = {Stack(Item_Quest_AirRaidInfo)};
			}
			if (Rule.RuleId == TEXT("Rule_SupplyPatrol") || Rule.RuleId == TEXT("Rule_SuppressForces"))
				bValid &= Rule.bHideAfterStoryCompletion && Rule.bCompleteStoryNode && Rule.RequiredItems.Num() == 1 && Rule.ConsumedItems.Num() == 1;
		}
		if (!bValid) { UE_LOG(LogTemp, Error, TEXT("Campaign asset contract validation failed")); return false; }
		if (bApply)
		{
			UBaseManagerDialogueData* Base = LoadObject<UBaseManagerDialogueData>(nullptr, TEXT("/Game/Blueprints/NPC/DA_BaseManagerDialogue.DA_BaseManagerDialogue"));
			if (!Base) return false;
			const UBaseManagerDialogueData* Defaults = GetDefault<UBaseManagerDialogueData>();
			for (FNPCDialogueRule& Rule : Base->Rules)
				for (FNPCDialogueLine& Line : Rule.Lines)
					if (Line.LineId == TEXT("CurrentSuccess1") || Line.LineId == TEXT("WaterBombSuccess1") || Line.LineId == TEXT("BombardmentSuccess1"))
						if (const FNPCDialogueRule* DefaultRule = Defaults->FindRule(Rule.RuleId))
							if (const FNPCDialogueLine* DefaultLine = DefaultRule->Lines.FindByPredicate([&Line](const FNPCDialogueLine& Candidate) { return Candidate.LineId == Line.LineId; })) Line.Text = DefaultLine->Text;
			for (UObject* Object : {static_cast<UObject*>(Items), static_cast<UObject*>(Features), static_cast<UObject*>(Recipes), static_cast<UObject*>(Dialogue), static_cast<UObject*>(Loot), static_cast<UObject*>(Base)})
				if (!SaveCampaignAsset(Object, Backup)) return false;
		}
		UE_LOG(LogTemp, Display, TEXT("CampaignContract Verified Apply=%d"), bApply);
		return true;
	}
}

USWCampaignIntegrationCommandlet::USWCampaignIntegrationCommandlet()
{
	IsClient = false; IsServer = false; IsEditor = true; LogToConsole = true;
}

int32 USWCampaignIntegrationCommandlet::Main(const FString& Params)
{
	const USettings_Item* Settings = GetDefault<USettings_Item>();
	UItemData* Items = Settings->ItemAssetRegistry.LoadSynchronous();
	UDataTable* Recipes = Settings->CraftingRecipeDataTable.LoadSynchronous();
	UNPCDialogueData* Dialogue = LoadObject<UNPCDialogueData>(nullptr, TEXT("/Game/Campaign/DataAsset/Dialogue/DA_YiSunSinDialogue.DA_YiSunSinDialogue"));
	UBossChestGuaranteedLootData* Loot = LoadObject<UBossChestGuaranteedLootData>(nullptr, TEXT("/Game/Blueprints/Item/Data/DA_BossChestGuaranteedLoot.DA_BossChestGuaranteedLoot"));
	if (!Items || !Recipes || !Dialogue || !Loot) return 1;
	if (FParse::Param(*Params, TEXT("Apply")) || FParse::Param(*Params, TEXT("Verify")))
	{
		UDataTable* Features = Settings->ItemFeatureDataTable.LoadSynchronous();
		if (!Features || !ConfigureCampaignAssets(Items, Features, Recipes, Dialogue, Loot, FParse::Param(*Params, TEXT("Apply")))) return 3;
	}
	UE_LOG(LogTemp, Display, TEXT("CampaignAudit Items=%d Recipes=%s Features=%s"), Items->ItemDefinitions.Num(), *Recipes->GetPathName(), *Settings->ItemFeatureDataTable.ToString());
	for (FGameplayTag Tag : {Item_Quest_CipherBook.GetTag(), Item_Quest_InvasionMap.GetTag(), Item_Quest_JapaneseCipher.GetTag(), Item_Quest_DecipheredCipher.GetTag(), Item_Quest_AirRaidInfo.GetTag()})
		UE_LOG(LogTemp, Display, TEXT("CampaignAudit Item=%s Defined=%d"), *Tag.ToString(), Items->FindItemDefinition(Tag) != nullptr);
	for (const FNPCDialogueRule& Rule : Dialogue->Rules)
	{
		FString Value;
		FNPCDialogueRule::StaticStruct()->ExportText(Value, &Rule, nullptr, Dialogue, PPF_None, nullptr);
		UE_LOG(LogTemp, Display, TEXT("CampaignAudit Dialogue=%s"), *Value);
	}
	UE_LOG(LogTemp, Display, TEXT("CampaignAudit BossLoot=%s"), *ExportProperty(Loot, TEXT("Entries")));
	if (UClass* Boss2Class = LoadClass<ABaseEnemy>(nullptr, TEXT("/Game/GameplayAbilitySystem/Enemy/Bosses/BP_Boss_Mid_2.BP_Boss_Mid_2_C")))
		UE_LOG(LogTemp, Display, TEXT("CampaignAudit Boss2Parent=%s DeathEnabled=%s DeathNode=%s"), *Boss2Class->GetSuperClass()->GetPathName(),
			*ExportProperty(Boss2Class->GetDefaultObject(), TEXT("bCompleteStoryNodeOnDeath")), *ExportProperty(Boss2Class->GetDefaultObject(), TEXT("CompletedStoryNodeOnDeath")));
	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/Level/Lvl_CY"));
	if (!World) return 2;
	if ((FParse::Param(*Params, TEXT("ApplyMap")) || FParse::Param(*Params, TEXT("VerifyMap")))
		&& !ConfigureCampaignMap(World, FParse::Param(*Params, TEXT("ApplyMap")))) return 4;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		const FString Label = It->GetActorLabel();
		if (Label.Contains(TEXT("Spawn")) || Label.Contains(TEXT("Boss")) || Label.Contains(TEXT("NPC")) || Label.Contains(TEXT("Mid_2")) || Label.StartsWith(TEXT("Z2")))
			UE_LOG(LogTemp, Display, TEXT("CampaignAudit Actor=%s Class=%s Location=%s"), *Label, *It->GetClass()->GetPathName(), *It->GetActorLocation().ToString());
	}
	for (TActorIterator<AEnemyShip> It(World); It; ++It)
	{
		UBossEncounterComponent* Boss = It->GetBossEncounterComponent();
		if (!Boss || !Boss->IsEncounterEnabled()) continue;
		UE_LOG(LogTemp, Display, TEXT("CampaignAudit Ship=%s Class=%s Boss=%s Required=%s Stop=%s Chest=%s"),
			*It->GetActorLabel(), *It->GetClass()->GetPathName(), *ExportProperty(Boss, TEXT("BossClass")),
			*ExportProperty(Boss, TEXT("RequiredStoryNode")), *ExportProperty(Boss, TEXT("StopAfterStoryNode")),
			*ExportProperty(*It, TEXT("ChestSpawnPointChestSettings")));
	}
	for (TActorIterator<AStoryConditionalSpawner> It(World); It; ++It)
		UE_LOG(LogTemp, Display, TEXT("CampaignAudit StorySpawner=%s Class=%s Required=%s Stop=%s StopEnabled=%s Location=%s"),
			*It->GetActorLabel(), *ExportProperty(*It, TEXT("SpawnedActorClass")), *ExportProperty(*It, TEXT("RequiredStoryNode")),
			*ExportProperty(*It, TEXT("StopAfterStoryNode")), *ExportProperty(*It, TEXT("bStopAfterStoryNode")), *It->GetActorLocation().ToString());
	for (TActorIterator<AChestSpawnPoint> It(World); It; ++It)
		if (ExportProperty(*It, TEXT("bIsBossChest")) == TEXT("True"))
			UE_LOG(LogTemp, Display, TEXT("CampaignAudit BossChest=%s Tag=%s Guard=%s Location=%s"), *It->GetActorLabel(),
				*ExportProperty(*It, TEXT("GuaranteedBossQuestItemTag")), *ExportProperty(*It, TEXT("RequiredBossClass")), *It->GetActorLocation().ToString());
	UE_LOG(LogTemp, Display, TEXT("CampaignAudit Complete"));
	return 0;
}
