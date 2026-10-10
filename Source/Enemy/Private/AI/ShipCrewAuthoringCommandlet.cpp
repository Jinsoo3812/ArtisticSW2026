#include "AI/ShipCrewAuthoringCommandlet.h"

#if WITH_EDITOR
#include "BossAI/BossEncounterComponent.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipArchetypeData.h"
#include "Components/ChildActorComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "ItemSpawn/BossChestGuaranteedLootData.h"
#include "ItemSpawn/GlobalLootSpawnManager.h"
#include "ItemSpawn/LootSpawnPoint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace
{
bool SaveCrewAsset(UObject* Object)
{
	Object->MarkPackageDirty();
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	Args.SaveFlags = SAVE_NoError;
	return UPackage::SavePackage(Object->GetPackage(), Object,
		*FPackageName::LongPackageNameToFilename(Object->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension()), Args);
}

bool ConfigureCrewBoss(AEnemyShip* Ship, int32 Tier, UEnemyShipArchetypeData* Normal)
{
	UBossEncounterComponent* Boss = Ship->GetBossEncounterComponent();
	if (!Boss || !Normal) return false;
	FEnumProperty* Required = FindFProperty<FEnumProperty>(Boss->GetClass(), TEXT("RequiredStoryNode"));
	FEnumProperty* Stop = FindFProperty<FEnumProperty>(Boss->GetClass(), TEXT("StopAfterStoryNode"));
	if (!Required || !Stop) return false;
	Ship->NormalFallbackArchetype = Normal;
	Required->GetUnderlyingProperty()->SetIntPropertyValue(Required->ContainerPtrToValuePtr<void>(Boss),
		static_cast<int64>(Tier == 1 ? EStoryNode::ReconQuestAccepted : EStoryNode::SuppressJapaneseForcesQuestAccepted));
	Stop->GetUnderlyingProperty()->SetIntPropertyValue(Stop->ContainerPtrToValuePtr<void>(Boss),
		static_cast<int64>(Tier == 1 ? EStoryNode::MiddleBoss1Defeated : EStoryNode::MiddleBoss3Defeated));
	return true;
}

bool CrewBossMatches(AEnemyShip* Ship, int32 Tier, UEnemyShipArchetypeData* Normal)
{
	UBossEncounterComponent* Boss = Ship->GetBossEncounterComponent();
	FEnumProperty* Required = Boss ? FindFProperty<FEnumProperty>(Boss->GetClass(), TEXT("RequiredStoryNode")) : nullptr;
	FEnumProperty* Stop = Boss ? FindFProperty<FEnumProperty>(Boss->GetClass(), TEXT("StopAfterStoryNode")) : nullptr;
	return Required && Stop && Ship->NormalFallbackArchetype == Normal
		&& Required->GetUnderlyingProperty()->GetSignedIntPropertyValue(Required->ContainerPtrToValuePtr<void>(Boss))
			== static_cast<int64>(Tier == 1 ? EStoryNode::ReconQuestAccepted : EStoryNode::SuppressJapaneseForcesQuestAccepted)
		&& Stop->GetUnderlyingProperty()->GetSignedIntPropertyValue(Stop->ContainerPtrToValuePtr<void>(Boss))
			== static_cast<int64>(Tier == 1 ? EStoryNode::MiddleBoss1Defeated : EStoryNode::MiddleBoss3Defeated);
}
}
#endif

UShipCrewAuthoringCommandlet::UShipCrewAuthoringCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UShipCrewAuthoringCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	const bool bApply = FParse::Param(*Params, TEXT("Apply"));
	const FString MapFile = FPackageName::LongPackageNameToFilename(TEXT("/Game/Level/Lvl_CY"), FPackageName::GetMapPackageExtension());
	if (bApply)
	{
		const FString Backup = FPaths::ProjectSavedDir() / TEXT("ShipCrewAuthoring") / FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
		IFileManager::Get().MakeDirectory(*Backup, true);
		if (IFileManager::Get().Copy(*(Backup / TEXT("Lvl_CY.umap")), *MapFile) != COPY_OK) return 1;
		for (const FString& Package : {
			FString(TEXT("/Game/GameplayAbilitySystem/Enemy/Balancing/Final/BP_ES_Mid_1")),
			FString(TEXT("/Game/GameplayAbilitySystem/Enemy/Balancing/Final/BP_ES_Mid_3")),
			FString(TEXT("/Game/Blueprints/Item/Data/DA_BossChestGuaranteedLoot"))})
		{
			const FString Source = FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension());
			if (IFileManager::Get().Copy(*(Backup / FPaths::GetCleanFilename(Source)), *Source) != COPY_OK) return 1;
		}
	}
	UEnemyShipArchetypeData* Normal1 = LoadObject<UEnemyShipArchetypeData>(nullptr,
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Data/Archetype/Normal/DA_ES_Normal_1.DA_ES_Normal_1"));
	UEnemyShipArchetypeData* Normal3 = LoadObject<UEnemyShipArchetypeData>(nullptr,
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Data/Archetype/Normal/DA_ES_Normal_3.DA_ES_Normal_3"));
	UBossChestGuaranteedLootData* Loot = LoadObject<UBossChestGuaranteedLootData>(nullptr,
		TEXT("/Game/Blueprints/Item/Data/DA_BossChestGuaranteedLoot.DA_BossChestGuaranteedLoot"));
	if (!Normal1 || !Normal3 || !Loot) return 2;
	TArray<UClass*> BossClasses;
	for (int32 Tier : {1, 3})
	{
		const FString Name = FString::Printf(TEXT("BP_ES_Mid_%d"), Tier);
		UBlueprint* BP = LoadObject<UBlueprint>(nullptr,
			*(TEXT("/Game/GameplayAbilitySystem/Enemy/Balancing/Final/") + Name + TEXT(".") + Name));
		AEnemyShip* Defaults = BP && BP->GeneratedClass ? Cast<AEnemyShip>(BP->GeneratedClass->GetDefaultObject()) : nullptr;
		if (!Defaults) return 3;
		UBossEncounterComponent* Boss = Defaults->GetBossEncounterComponent();
		FClassProperty* ClassProperty = Boss ? FindFProperty<FClassProperty>(Boss->GetClass(), TEXT("BossClass")) : nullptr;
		UClass* BossClass = ClassProperty ? Cast<UClass>(ClassProperty->GetObjectPropertyValue_InContainer(Boss)) : nullptr;
		if (!BossClass) return 4;
		BossClasses.Add(BossClass);
		if (bApply)
		{
			if (!ConfigureCrewBoss(Defaults, Tier, Tier == 1 ? Normal1 : Normal3)) return 5;
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
			if (!SaveCrewAsset(BP)) return 6;
		}
		if (!CrewBossMatches(Defaults, Tier, Tier == 1 ? Normal1 : Normal3)) return 17;
	}
	if (bApply)
	{
		for (FGuaranteedBossLootEntry& Entry : Loot->Entries)
		{
			for (const FStorageItemEntry& Item : Entry.GuaranteedItems)
			{
				if (Item.ItemTag == FGameplayTag::RequestGameplayTag(TEXT("Item.Id.Clue.Clue2"))) Entry.BossClass = BossClasses[0];
				if (Item.ItemTag == FGameplayTag::RequestGameplayTag(TEXT("Item.Id.Clue.Clue4"))) Entry.BossClass = BossClasses[1];
			}
		}
		if (!SaveCrewAsset(Loot)) return 7;
	}
	for (int32 Index = 0; Index < BossClasses.Num(); ++Index)
	{
		TArray<FStorageItemEntry> Items;
		const FGameplayTag Expected = FGameplayTag::RequestGameplayTag(Index == 0
			? FName(TEXT("Item.Id.Clue.Clue2")) : FName(TEXT("Item.Id.Clue.Clue4")));
		if (!Loot->FindItemsForExactClass(BossClasses[Index], Items)
			|| Items.Num() != 1 || Items[0].ItemTag != Expected || Items[0].Count != 1) return 18;
	}
	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/Level/Lvl_CY"));
	if (!World) return 8;
	int32 ShipCount = 0, ManagerCount = 0;
	for (TActorIterator<AEnemyShip> It(World); It; ++It)
	{
		AEnemyShip* Ship = *It;
		const FString Label = Ship->GetActorLabel();
		int32 Tier = 0;
		if (Label == TEXT("BP_ES_Mid_1")) Tier = 1;
		else if (Label == TEXT("BP_ES_Mid_3")) Tier = 3;
		else if (Label == TEXT("BP_ES_Final")) Tier = 4;
		else for (int32 Candidate = 1; Candidate <= 4; ++Candidate)
			if (Label.StartsWith(FString::Printf(TEXT("BP_ES_Normal_%d_"), Candidate))) Tier = Candidate;
		if (!Tier) return 9;
		int32 ChestCount = 0;
		TInlineComponentArray<UChildActorComponent*> Components(Ship);
		for (UChildActorComponent* Component : Components)
			if (Component && Component->GetChildActorClass() && Component->GetChildActorClass()->IsChildOf(AChestSpawnPoint::StaticClass())) ++ChestCount;
		if (ChestCount != 1) return 10;
		const EProgressionZone Zone = static_cast<EProgressionZone>(Tier - 1);
		if (bApply)
		{
			Ship->ChestSpawnPointChestSettings.ProgressionZone = Zone;
			if ((Tier == 1 || Tier == 3) && !Label.Contains(TEXT("Normal"))
				&& !ConfigureCrewBoss(Ship, Tier, Tier == 1 ? Normal1 : Normal3)) return 11;
		}
		if (Ship->ChestSpawnPointChestSettings.ProgressionZone != Zone) return 12;
		if ((Tier == 1 || Tier == 3) && !Label.Contains(TEXT("Normal"))
			&& !CrewBossMatches(Ship, Tier, Tier == 1 ? Normal1 : Normal3)) return 19;
		++ShipCount;
	}
	for (TActorIterator<AGlobalLootSpawnManager> It(World); It; ++It)
	{
		FSoftObjectProperty* Property = FindFProperty<FSoftObjectProperty>(It->GetClass(), TEXT("BossGuaranteedLootData"));
		if (!Property) return 13;
		if (bApply) Property->SetPropertyValue_InContainer(*It, FSoftObjectPtr(Loot));
		if (Property->GetPropertyValue_InContainer(*It).ToSoftObjectPath() != FSoftObjectPath(Loot)) return 14;
		++ManagerCount;
	}
	if (ShipCount != 51 || ManagerCount != 1) return 15;
	if (bApply && !UEditorLoadingAndSavingUtils::SaveMap(World, TEXT("/Game/Level/Lvl_CY"))) return 16;
	UE_LOG(LogTemp, Display, TEXT("ShipCrewAuthoring complete: Apply=%d Ships=%d Managers=%d"), bApply, ShipCount, ManagerCount);
	return 0;
#else
	return 1;
#endif
}
