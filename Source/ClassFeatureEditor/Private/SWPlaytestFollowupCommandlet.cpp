#include "SWPlaytestFollowupCommandlet.h"
#include "ItemData.h"
#include "Settings_Item.h"
#include "BaseGameplayTags.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "FileHelpers.h"
#include "EngineUtils.h"
#include "ShipAI/EnemyShip.h"
#include "Components/ChildActorComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ItemSpawn/LootSpawnPoint.h"
#include "Storage/StorageChest.h"
#include "StaticMeshOperations.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshCompiler.h"
#include "StaticMeshAttributes.h"

namespace
{
	bool SaveBackedUp(UObject* Object)
	{
		UPackage* Package = Object->GetOutermost();
		const FString Extension = Object->IsA<UWorld>() ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), Extension);
		const FString Backup = FPaths::ProjectSavedDir() / TEXT("PlaytestFollowupBackup") / (FPaths::MakeValidFileName(Package->GetName()) + Extension);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
		if (!IFileManager::Get().FileExists(*Backup) && IFileManager::Get().Copy(*Backup, *Filename) != COPY_OK) return false;
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Package, Object, *Filename, Args);
	}
}

int32 USWPlaytestFollowupCommandlet::Main(const FString& Params)
{
	UItemData* Items = GetDefault<USettings_Item>()->ItemAssetRegistry.LoadSynchronous();
	if (!Items) return 1;
	const TArray<FGameplayTag> Tags = {Item_Id_Material_ShipMaterials_WoodenPlank, Item_Id_Material_ShipMaterials_GoodWoodenPlank,
		Item_Id_Material_ShipMaterials_IronPlate, Item_Id_Material_ShipMaterials_GoodIronPlate};
	for (int32 Index = 0; Index < Tags.Num(); ++Index)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Index < 2
			? TEXT("/Game/Blueprints/Item/Mesh/SM_Repair_WoodenPlank.SM_Repair_WoodenPlank")
			: TEXT("/Game/Blueprints/Item/Mesh/SM_Repair_IronPlate.SM_Repair_IronPlate"));
		FItemDefinition* Definition = Items->ItemDefinitions.Find(Tags[Index]);
		if (!Mesh || !Definition) return 2;
		if (Params.Contains(TEXT("ApplyItems")) && (Index == 0 || Index == 2) && !Mesh->GetBoundingBox().GetCenter().IsNearlyZero(0.1f))
		{
			// Existing repair copies still contain the imported object's world-space
			// vertex offset. Normalize these dedicated handheld assets, not the scan.
			const FBox Bounds = Mesh->GetBoundingBox();
			const double Scale = 45. / Bounds.GetSize().GetMax();
			Mesh->Modify();
			for (int32 LOD = 0; LOD < Mesh->GetNumSourceModels(); ++LOD)
				if (FMeshDescription* Description = Mesh->GetMeshDescription(LOD))
				{
					FStaticMeshOperations::ApplyTransform(*Description, FTransform(FQuat::Identity, -Bounds.GetCenter() * Scale, FVector(Scale)), true);
					Mesh->CommitMeshDescription(LOD);
				}
			if (UBodySetup* Body = Mesh->GetBodySetup())
			{
				Body->RemoveSimpleCollision();
				FKBoxElem Box;
				const FVector Size = Bounds.GetSize() * Scale;
				Box.X = Size.X; Box.Y = Size.Y; Box.Z = FMath::Max(1., Size.Z);
				Body->AggGeom.BoxElems.Add(Box);
				Body->CollisionTraceFlag = CTF_UseSimpleAndComplex;
				Body->InvalidatePhysicsData();
			}
			Mesh->Build(false);
			FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
			if (!SaveBackedUp(Mesh)) return 7;
		}
		if (Params.Contains(TEXT("ApplyItems"))) { Items->Modify(); Definition->ItemMesh = Mesh; }
		UE_LOG(LogTemp, Display, TEXT("Followup Item=%s Mesh=%s Expected=%s Bounds=%s"), *Tags[Index].ToString(),
			*Definition->ItemMesh.ToString(), *Mesh->GetPathName(), *Mesh->GetBoundingBox().ToString());
	}
	if (Params.Contains(TEXT("ApplyItems")) && !SaveBackedUp(Items)) return 3;
	UDataTable* Features = GetDefault<USettings_Item>()->ItemFeatureDataTable.LoadSynchronous();
	const FItemFeatureData* WoodFeature = Features ? Features->FindRow<FItemFeatureData>(Tags[0].GetTagName(), TEXT("RepairHand")) : nullptr;
	if (!WoodFeature) return 8;
	const FName HandSocket = WoodFeature->AttachmentSocketName;
	for (const FGameplayTag& Tag : Tags)
	{
		FItemFeatureData* Feature = Features->FindRow<FItemFeatureData>(Tag.GetTagName(), TEXT("RepairHand"), false);
		if (Params.Contains(TEXT("ApplyItems")))
		{
			Features->Modify();
			if (Feature) Feature->AttachmentSocketName = HandSocket;
			else
			{
				FItemFeatureData Row;
				Row.AttachmentSocketName = HandSocket;
				Row.ItemName = FText::FromString(Tag == Tags[2] ? TEXT("철판") : TEXT("고급 철판"));
				Features->AddRow(Tag.GetTagName(), Row);
			}
		}
		UE_LOG(LogTemp, Display, TEXT("Followup Hand Item=%s Socket=%s"), *Tag.ToString(), *HandSocket.ToString());
	}
	if (Params.Contains(TEXT("ApplyItems")) && !SaveBackedUp(Features)) return 9;
	UMaterial* Material = LoadObject<UMaterial>(nullptr, TEXT("/InterchangeAssets/gltf/M_Default.M_Default"));
	if (!Material) return 4;
	UE_LOG(LogTemp, Display, TEXT("Followup NaniteBefore=%d Package=%s"), Material->GetUsageByFlag(MATUSAGE_Nanite), *Material->GetOutermost()->GetName());
	if (Params.Contains(TEXT("ApplyNanite")))
	{
		Material->Modify();
		bool bNeedsRecompile = false;
		Material->SetMaterialUsage(bNeedsRecompile, MATUSAGE_Nanite);
		Material->PostEditChange();
		if (!Material->GetUsageByFlag(MATUSAGE_Nanite) || !SaveBackedUp(Material)) return 5;
	}
	UE_LOG(LogTemp, Display, TEXT("Followup NaniteAfter=%d"), Material->GetUsageByFlag(MATUSAGE_Nanite));
	if (!Params.Contains(TEXT("ChestProbe"))) return 0;
	UWorld* Map = UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/Level/Lvl_CY"));
	if (!Map) return 6;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	for (TActorIterator<AEnemyShip> It(Map); It; ++It)
	{
		FActorSpawnParameters Spawn;
		Spawn.Template = *It;
		Spawn.bDeferConstruction = true;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AEnemyShip* Ship = World->SpawnActor<AEnemyShip>(It->GetClass(), It->GetActorTransform(), Spawn);
		Ship->AutoPossessAI = EAutoPossessAI::Disabled;
		Ship->FinishSpawning(It->GetActorTransform());
		Ship->SetActorEnableCollision(true);
		TInlineComponentArray<UChildActorComponent*> Children(Ship);
		for (UChildActorComponent* Child : Children)
			if (AChestSpawnPoint* Point = Cast<AChestSpawnPoint>(Child->GetChildActor()))
			{
				AStorageChest* Chest = Point->SpawnConfiguredChest(nullptr, 123);
				UE_LOG(LogTemp, Display, TEXT("Followup Chest Ship=%s Point=%s Enabled=%d Env=%d Class=%s Chest=%s Hidden=%d PointPos=%s ChestPos=%s"),
					*It->GetActorLabel(), *Point->GetName(), Point->CanSpawnDataDrivenChest(), static_cast<int32>(Point->GetEnvironment()),
					*GetNameSafe(Ship->GetDeckChestClass().Get()), *GetNameSafe(Chest), Chest ? Chest->IsHidden() : -1,
					*Point->GetActorLocation().ToString(), Chest ? *Chest->GetActorLocation().ToString() : TEXT("None"));
				TInlineComponentArray<UStaticMeshComponent*> Meshes(Ship);
				for (UStaticMeshComponent* Deck : Meshes)
					if (Deck->GetCollisionProfileName() == TEXT("ShipDeck"))
					{
						Deck->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
						FCollisionQueryParams Query;
						Query.bTraceComplex = false;
						FHitResult Hit;
						const FVector P = Point->GetActorLocation();
						const bool bHit = Deck->LineTraceComponent(Hit, P + FVector(0,0,10000), P - FVector(0,0,10000), Query);
						UE_LOG(LogTemp, Display, TEXT("Followup Deck Ship=%s Mesh=%s DeckHit=%d DeckZ=%.3f ChestBottom=%.3f"),
							*It->GetActorLabel(), *GetNameSafe(Deck->GetStaticMesh()), bHit, Hit.ImpactPoint.Z,
							Chest ? Chest->GetChestMesh()->Bounds.GetBox().Min.Z : 0.);
						UE_LOG(LogTemp, Display, TEXT("Followup DeckBounds=%s Extent=%s"), *Deck->Bounds.Origin.ToString(), *Deck->Bounds.BoxExtent.ToString());
						if (It->GetActorLabel() == TEXT("BP_ES_Normal_1_3"))
						{
							UE_LOG(LogTemp, Display, TEXT("Followup DeckPhysics Registered=%d State=%d Body=%d TraceFlag=%d"), Deck->IsRegistered(), Deck->IsPhysicsStateCreated(), Deck->GetBodyInstance()->IsValidBodyInstance(), static_cast<int32>(Deck->GetBodySetup()->CollisionTraceFlag.GetValue()));
							if (FMeshDescription* Description = Deck->GetStaticMesh()->GetMeshDescription(0))
							{
								const auto Positions = FStaticMeshAttributes(*Description).GetVertexPositions();
								for (int32 Y = -400; Y <= 400; Y += 200)
								{
									const FVector Probe = P + Ship->GetActorRightVector() * Y;
									const FVector Start = Deck->GetComponentTransform().InverseTransformPosition(Probe + FVector(0,0,10000));
									const FVector End = Deck->GetComponentTransform().InverseTransformPosition(Probe - FVector(0,0,10000));
									double Highest = -1.e10;
									for (FTriangleID Triangle : Description->Triangles().GetElementIDs())
									{
										const auto Vertices = Description->GetTriangleVertices(Triangle);
										FVector Intersection, Normal;
										if (FMath::SegmentTriangleIntersection(Start, End, FVector(Positions[Vertices[0]]), FVector(Positions[Vertices[1]]), FVector(Positions[Vertices[2]]), Intersection, Normal))
											Highest = FMath::Max(Highest, Deck->GetComponentTransform().TransformPosition(Intersection).Z);
									}
									UE_LOG(LogTemp, Display, TEXT("Followup Geometry Mesh=%s SideOffset=%d HighestZ=%.3f"), *Deck->GetName(), Y, Highest);
								}
							}
						}
					}
			}
		Ship->Destroy();
	}
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return 0;
}
