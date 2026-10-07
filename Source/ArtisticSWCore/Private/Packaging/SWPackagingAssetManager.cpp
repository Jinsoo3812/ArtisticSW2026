#include "Packaging/SWPackagingAssetManager.h"
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetData.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/SoftObjectPath.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWPackaging, Log, All);

void USWPackagingAssetManager::ModifyCook(TConstArrayView<const ITargetPlatform*> TargetPlatforms,
	TArray<FName>& PackagesToCook, TArray<FName>& PackagesToNeverCook)
{
	Super::ModifyCook(TargetPlatforms, PackagesToCook, PackagesToNeverCook);
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	const TCHAR* ExactRoots[] = {
		TEXT("/Game/Level/ConnectionLobby"),
		TEXT("/Game/Level/Lvl_CY"),
		TEXT("/Game/Blueprints/02_UI/UI_Lobby/WBP_ConnectionLobby"),
		TEXT("/Game/Blueprints/02_UI/UI_Loading/WBP_LoadingScreen"),
		TEXT("/Game/Blueprints/02_UI/UI_WorkTable/WBP_WorkspaceScreen"),
		TEXT("/Game/Input/Actions/IA_RoomMenu"),
		TEXT("/Game/UI/Room/WBP_RoomMenu"),
		TEXT("/Game/Blueprints/02_UI/UI_WorkTable/UI_Crafting/WBP_CraftingPanel"),
		TEXT("/Game/Blueprints/02_UI/UI_WorkTable/UI_ShipUpgrade/M_ShipPreviewOverlay"),
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle"),
		TEXT("/Game/Anim_Logic/Anim_Assets/Bow/Bow_Anims/Standing_Idle_01_Anim"),
		TEXT("/Game/Sword_Anims/Animations/HandsomeSwordV2/Manny_UE5/RootMotion/Idle/Anim_SwordV2_Idle"),
		TEXT("/Game/Anim_Logic/PSD/PSD_Run_Tnasition"),
		TEXT("/Game/Blueprints/Item/Data/ShipUpgrade/DA_ShipUpgradeTree"),
		TEXT("/Game/Blueprints/Water/MPC_Water_Custom"),
		TEXT("/Game/StylizedWeather/Material/MPC_Sky"),
		TEXT("/Game/Blueprints/Water/Culling/DA_SW_ShipCabinWaterCull"),
		TEXT("/Game/Resources_Assets/Splash_Effects/Effects/NS_Stream_Splash_01"),
		TEXT("/Game/GameplayAbilitySystem/Enemy/BP_MeleeEnemy"),
		TEXT("/Game/GameplayAbilitySystem/Enemy/BP_RangedEnemy"),
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Blueprints/BP_ES_Torpedo"),
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Blueprints/BP_ES_TimeStopField"),
		TEXT("/Game/Blueprints/Ship/Enemy_Ship/Blueprints/BP_ES_Obstacle"),
		TEXT("/Game/Developer/Testing/Input/IMC_DevTest"),
		TEXT("/Game/Developer/Testing/Input/Actions/IA_DevTest_KillSelf"),
		TEXT("/Game/Developer/Testing/Input/Actions/IA_DevTest_KillBoth"),
		TEXT("/Game/Developer/Testing/Input/Actions/IA_DevTest_SinkPlayerShip"),
		TEXT("/Game/Developer/Testing/Input/Actions/IA_DevTest_EnterFinalEncounter"),
		TEXT("/Game/Developer/Testing/Input/Actions/IA_DevTest_ModifierCtrl"),
		TEXT("/Game/Developer/Testing/Input/Actions/IA_DevTest_ModifierAlt"),
		TEXT("/Engine/EngineResources/WhiteSquareTexture"),
		TEXT("/Engine/BasicShapes/Plane"),
		TEXT("/Engine/BasicShapes/Sphere"),
		TEXT("/Engine/BasicShapes/Cylinder")
	};
	TSet<FName> RequestedPackages;
	for (const TCHAR* Root : ExactRoots) RequestedPackages.Add(FName(Root));
	if (GConfig)
	{
		auto AddConfigRoot = [&RequestedPackages](const TCHAR* Section, const TCHAR* Key)
		{
			FString Value;
			if (!GConfig->GetString(Section, Key, Value, GGameIni)) return;
			Value.TrimStartAndEndInline();
			if (Value.IsEmpty() || Value.Equals(TEXT("None"), ESearchCase::IgnoreCase)) return;
			const FString Package = FSoftObjectPath(Value).GetLongPackageName();
			if (!FPackageName::IsValidLongPackageName(Package))
			{
				UE_LOG(LogSWPackaging, Error, TEXT("Invalid Cook config package: %s.%s = %s"), Section, Key, *Value);
				return;
			}
			RequestedPackages.Add(FName(*Package));
		};
		const TCHAR* ItemKeys[] = { TEXT("ItemFeatureDataTable"), TEXT("ItemAssetRegistry"), TEXT("ItemRecipeDataTable"),
			TEXT("CraftingRecipeDataTable"), TEXT("ProgressionBalanceData"), TEXT("FixedChestDropData") };
		for (const TCHAR* Key : ItemKeys) AddConfigRoot(TEXT("/Script/ArtisticSWCore.Settings_Item"), Key);
		AddConfigRoot(TEXT("/Script/Enemy.EnemyShipWeakeningSettings"), TEXT("WeakeningData"));
		AddConfigRoot(TEXT("/Script/Story.StorySettings"), TEXT("DefaultStoryDefinition"));
	}
	else
	{
		UE_LOG(LogSWPackaging, Error, TEXT("Cook config roots could not be read: GConfig is null"));
	}
	const TArray<FString> Folders = {
		TEXT("/Game/StylizedWeather/Material/Sky"), TEXT("/Game/StylizedWeather/Curve/Color"),
		TEXT("/Game/StylizedWeather/Curve/Float"), TEXT("/Game/StylizedWeather/Texture"), TEXT("/Game/GameplayCues")
	};
	Registry.ScanPathsSynchronous(Folders, false, true);
	const FTopLevelAssetPath RedirectorClass(TEXT("/Script/CoreUObject"), TEXT("ObjectRedirector"));
	for (const FString& Folder : Folders)
	{
		TArray<FAssetData> Assets;
		Registry.GetAssetsByPath(FName(*Folder), Assets, true, true);
		int32 ValidPackages = 0;
		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetClassPath == RedirectorClass) continue;
			RequestedPackages.Add(Asset.PackageName);
			++ValidPackages;
		}
		if (ValidPackages == 0) UE_LOG(LogSWPackaging, Error, TEXT("Cook folder has no assets: %s"), *Folder);
	}
	TArray<FName> SortedPackages = RequestedPackages.Array();
	SortedPackages.Sort([](const FName& Left, const FName& Right) { return Left.ToString() < Right.ToString(); });
	int32 Added = 0, Missing = 0, Conflicts = 0;
	for (FName Package : SortedPackages)
	{
		if (!FPackageName::DoesPackageExist(Package.ToString()))
		{
			UE_LOG(LogSWPackaging, Error, TEXT("Cook package does not exist: %s"), *Package.ToString());
			++Missing;
			continue;
		}
		if (PackagesToNeverCook.Contains(Package))
		{
			UE_LOG(LogSWPackaging, Error, TEXT("Cook package conflicts with NeverCook: %s"), *Package.ToString());
			++Conflicts;
			continue;
		}
		if (!PackagesToCook.Contains(Package)) ++Added;
		PackagesToCook.AddUnique(Package);
	}
	UE_LOG(LogSWPackaging, Display, TEXT("Cook roots Requested=%d Added=%d Missing=%d Conflicts=%d"),
		SortedPackages.Num(), Added, Missing, Conflicts);
}
#endif
