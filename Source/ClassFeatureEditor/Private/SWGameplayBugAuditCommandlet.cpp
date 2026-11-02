#include "SWGameplayBugAuditCommandlet.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Engine/Blueprint.h"
#include "Materials/MaterialInstance.h"
#include "ItemData.h"
#include "Settings_Item.h"
#include "Development/SWDevelopmentSettings.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ChildActorComponent.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "ShipAI/EnemyShip.h"
#include "Ship.h"
#include "LandscapeProxy.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "Room/SWRoomSaveStore.h"
#include "Room/SWRoomRuntimePaths.h"
#include "SWNetworkWeatherActor.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"

int32 USWGameplayBugAuditCommandlet::Main(const FString& Params)
{
	UE_LOG(LogTemp, Display, TEXT("BugAudit Root=%s SavedRoom=%d Legacy=%d"),
		*FSWRoomRuntimePaths::GetRoot(), FSWRoomSaveStore::HasValidCurrentRoom(), FSWRoomSaveStore::HasLegacyRoomFile());
	if (UWidgetBlueprint* Lobby = LoadObject<UWidgetBlueprint>(nullptr, TEXT("/Game/Blueprints/02_UI/UI_Lobby/WBP_ConnectionLobby.WBP_ConnectionLobby")))
	{
		UE_LOG(LogTemp, Display, TEXT("BugAudit Lobby Parent=%s"), *GetNameSafe(Lobby->ParentClass));
		Lobby->WidgetTree->ForEachWidget([](UWidget* Widget)
		{
			if (Widget->GetName().Contains(TEXT("Continue")) || Widget->GetClass()->GetName().Contains(TEXT("Button")))
				UE_LOG(LogTemp, Display, TEXT("BugAudit LobbyWidget=%s Class=%s Visibility=%d Parent=%s"), *Widget->GetName(), *Widget->GetClass()->GetName(), static_cast<int32>(Widget->GetVisibility()), *GetNameSafe(Widget->GetParent()));
		});
	}
	const USWDevelopmentSettings* Starting = GetDefault<USWDevelopmentSettings>();
	UItemData* Items = GetDefault<USettings_Item>()->ItemAssetRegistry.LoadSynchronous();
	UE_LOG(LogTemp, Display, TEXT("BugAudit Starting Enabled=%d Entries=%d Registry=%s"), Starting->bGiveStartingItems, Starting->StartingItems.Num(), *GetPathNameSafe(Items));
	if (Items) for (const auto& Pair : Items->ItemDefinitions)
	{
		const FString Tag = Pair.Key.ToString();
		if (!Tag.Contains(TEXT("Plank")) && !Tag.Contains(TEXT("Plate")) && !Tag.Contains(TEXT("SwordA1")) && !Tag.Contains(TEXT("ShortBow1"))) continue;
		const FItemDefinition& D = Pair.Value;
		UE_LOG(LogTemp, Display, TEXT("BugAudit Item=%s Mesh=%s MeshLoaded=%d Spawn=%s Kind=%d WeaponRow=%s"),
			*Tag, *D.ItemMesh.ToString(), D.ItemMesh.LoadSynchronous() != nullptr,
			*D.SpawnClassByCrafting.ToString(), static_cast<int32>(D.ProgressionKind), *D.WeaponBalanceRow.RowName.ToString());
	}
	FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	Module.Get().SearchAllAssets(true);
	TArray<FAssetData> Assets;
	Module.Get().GetAssetsByPath(TEXT("/Game/Ship_Mesh/Costum_JoseonShip_fin"), Assets, true);
	for (const FAssetData& Asset : Assets)
	{
		const FString Name = Asset.AssetName.ToString();
		if (Name.StartsWith(TEXT("MI_")))
		{
			if (UMaterialInstance* M = Cast<UMaterialInstance>(Asset.GetAsset()))
				UE_LOG(LogTemp, Display, TEXT("BugAudit Material=%s Blend=%d Override=%d OverrideBlend=%d Parent=%s"),
					*M->GetPathName(), static_cast<int32>(M->GetBlendMode()), M->BasePropertyOverrides.bOverride_BlendMode,
					static_cast<int32>(M->BasePropertyOverrides.BlendMode), *GetPathNameSafe(M->Parent));
		}
		else if (Asset.AssetClassPath == UStaticMesh::StaticClass()->GetClassPathName() && (Name.StartsWith(TEXT("SM_Ship_Visual")) || Name.Contains(TEXT("Flag"))))
		{
			UStaticMesh* Mesh = CastChecked<UStaticMesh>(Asset.GetAsset());
			UE_LOG(LogTemp, Display, TEXT("BugAudit Mesh=%s Nanite=%d"), *Mesh->GetPathName(), Mesh->NaniteSettings.bEnabled);
			for (const FStaticMaterial& Slot : Mesh->GetStaticMaterials())
				UE_LOG(LogTemp, Display, TEXT("BugAudit Slot=%s Material=%s Blend=%d"), *Slot.MaterialSlotName.ToString(),
					*GetPathNameSafe(Slot.MaterialInterface), Slot.MaterialInterface ? static_cast<int32>(Slot.MaterialInterface->GetBlendMode()) : -1);
		}
	}
	if (Params.Contains(TEXT("Map")))
	{
		UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/Level/Lvl_CY"));
		if (!World) return 1;
		for (TActorIterator<AActor> It(World); It; ++It)
			if (It->GetName().Contains(TEXT("Weather")))
			{
				UE_LOG(LogTemp, Display, TEXT("BugAudit WeatherActor=%s Class=%s Native=%d"), *It->GetName(), *It->GetClass()->GetPathName(), It->IsA<ASWNetworkWeatherActor>());
				for (const FName Name : {FName(TEXT("RiseTime(DebugParam)")), FName(TEXT("DownTime(DebugParam)")), FName(TEXT("1 Hour Seconds"))})
					if (FProperty* P = It->GetClass()->FindPropertyByName(Name))
					{
						FString Value;
						P->ExportText_InContainer(0, Value, *It, nullptr, *It, PPF_None);
						UE_LOG(LogTemp, Display, TEXT("BugAudit AuthoredWeather %s=%s"), *Name.ToString(), *Value);
					}
			}
		for (TActorIterator<ASWNetworkWeatherActor> It(World); It; ++It)
			for (const FName Name : {FName(TEXT("RiseTime(DebugParam)")), FName(TEXT("DownTime(DebugParam)")), FName(TEXT("1 Hour Seconds"))})
				if (FProperty* P = It->GetClass()->FindPropertyByName(Name))
				{
					FString Value;
					P->ExportText_InContainer(0, Value, *It, nullptr, *It, PPF_None);
					UE_LOG(LogTemp, Display, TEXT("BugAudit Weather %s=%s"), *Name.ToString(), *Value);
				}
		for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
			for (ULandscapeHeightfieldCollisionComponent* C : It->CollisionComponents)
				if (C) UE_LOG(LogTemp, Display, TEXT("BugAudit Landscape=%s Component=%s Profile=%s Object=%d Enabled=%d PhysicsBody=%d ShipBlock=%d"),
					*It->GetName(), *C->GetName(), *C->GetCollisionProfileName().ToString(), static_cast<int32>(C->GetCollisionObjectType()),
					static_cast<int32>(C->GetCollisionEnabled()), C->IsPhysicsCollisionEnabled(), C->GetCollisionResponseToChannel(GetDefault<AShip>()->BuoyancyRoot->GetCollisionObjectType()) == ECR_Block);
		for (TActorIterator<AEnemyShip> It(World); It; ++It)
		{
			UE_LOG(LogTemp, Display, TEXT("BugAudit Ship=%s AngularDamping=%.3f"), *It->GetName(), It->BuoyancyRoot->GetAngularDamping());
			TInlineComponentArray<UChildActorComponent*> Children(*It);
			for (UChildActorComponent* Child : Children)
				UE_LOG(LogTemp, Display, TEXT("BugAudit Child=%s Class=%s Actor=%s Location=%s"), *Child->GetName(),
					*GetNameSafe(Child->GetChildActorClass()), *GetNameSafe(Child->GetChildActor()), *Child->GetComponentLocation().ToString());
		}
	}
	return 0;
}
