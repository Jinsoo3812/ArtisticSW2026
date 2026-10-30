#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Components/ChildActorComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ItemSpawn/LootSpawnPoint.h"
#include "Storage/StorageChest.h"
#include "ShipAI/EnemyShip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLvlCYShipPresentationAudit,
 "ArtisticSW.Integration.LvlCY.ShipPresentationAudit",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLvlCYShipPresentationAudit::RunTest(const FString& Parameters)
{
 UPackage* Package = LoadPackage(nullptr, TEXT("/Game/Level/Lvl_CY"), LOAD_None);
 UWorld* World = Package ? UWorld::FindWorldInPackage(Package) : nullptr;
 if (!TestNotNull(TEXT("Saved level loads read-only"), World)) return false;
 FString Report;
 int32 Ships = 0, Points = 0, Invisible = 0;
 AEnemyShip* ProbeTemplate = nullptr;
 auto Bool = [](const UObject* Object, const TCHAR* Name) {
  return FindFProperty<FBoolProperty>(Object->GetClass(), Name)->GetPropertyValue_InContainer(Object);
 };
 for (ULevel* Level : World->GetLevels()) for (AActor* Actor : Level->Actors)
 {
  AShip* Ship = Cast<AShip>(Actor);
  if (!Ship) continue;
  Report += FString::Printf(TEXT("Ship=%s Class=%s Enemy=%d LookYawInvert=%d LookPitchInvert=%d\n"),
   *Ship->GetActorLabel(), *Ship->GetClass()->GetPathName(), Ship->IsA<AEnemyShip>(),
   Bool(Ship, TEXT("bInvertShipLookYaw")), Bool(Ship, TEXT("bInvertShipLookPitch")));
  for (const TCHAR* Name : {TEXT("ShipInputMappingContext"), TEXT("ShipLookAction")})
   Report += FString::Printf(TEXT("  %s=%s\n"), Name, *GetPathNameSafe(FindFProperty<FObjectPropertyBase>(Ship->GetClass(), Name)->GetObjectPropertyValue_InContainer(Ship)));
  AEnemyShip* Enemy = Cast<AEnemyShip>(Ship);
  if (!Enemy) continue;
  if (!ProbeTemplate || !Enemy->ChestSpawnPointChestSettings.ChestClassOverride) ProbeTemplate = Enemy;
  ++Ships;
  TInlineComponentArray<UChildActorComponent*> Children(Ship);
  for (UChildActorComponent* Child : Children)
  {
   if (!Child->GetChildActorClass() || !Child->GetChildActorClass()->IsChildOf(AChestSpawnPoint::StaticClass())) continue;
   ++Points;
   UClass* ChestClass = Enemy->GetDeckChestClass().Get();
   if (!ChestClass) ChestClass = AStorageChest::StaticClass(); // Manager calls SpawnConfiguredChest(nullptr, Seed).
   AStorageChest* Chest = ChestClass->GetDefaultObject<AStorageChest>();
   TInlineComponentArray<UStaticMeshComponent*> Meshes(Chest);
   int32 VisibleMeshes = 0;
   Report += FString::Printf(TEXT("  Point=%s PointClass=%s Transform=%s ChestClassOverride=%s EffectiveClass=%s Mode=%d Env=%d\n"),
    *Child->GetName(), *Child->GetChildActorClass()->GetPathName(), *Child->GetRelativeTransform().ToString(),
    *GetPathNameSafe(Enemy->ChestSpawnPointChestSettings.ChestClassOverride.Get()), *ChestClass->GetPathName(),
    static_cast<int32>(Enemy->ChestSpawnPointChestSettings.SpawnMode), static_cast<int32>(Enemy->ChestSpawnPointChestSettings.Environment));
   for (UStaticMeshComponent* Mesh : Meshes)
   {
    if (Mesh->GetStaticMesh() && Mesh->IsVisible() && !Mesh->bHiddenInGame) ++VisibleMeshes;
    Report += FString::Printf(TEXT("    Mesh=%s Asset=%s Visible=%d Hidden=%d Scale=%s\n"),
     *Mesh->GetName(), *GetPathNameSafe(Mesh->GetStaticMesh()), Mesh->IsVisible(), Mesh->bHiddenInGame, *Mesh->GetRelativeScale3D().ToString());
   }
   if (!VisibleMeshes) ++Invisible;
  }
 }
 Report += FString::Printf(TEXT("TOTAL EnemyShips=%d ChestPoints=%d PointsWithoutVisibleNativeMesh=%d\n"), Ships, Points, Invisible);
 TestEqual(TEXT("Every enemy ship spawns a visible chest class"), Invisible, 0);
 // Spawn a copy in a temporary game world to exercise serialized BP overrides,
 // PostInitializeComponents and the real child-point spawn path without saving the map.
 UWorld* ProbeWorld = UWorld::CreateWorld(EWorldType::Game, false);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(ProbeWorld);
 ProbeWorld->InitializeActorsForPlay(FURL());
 if (ProbeTemplate)
 {
  FActorSpawnParameters Spawn;
  Spawn.Template = ProbeTemplate;
  Spawn.bDeferConstruction = true;
  Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
  AEnemyShip* Probe = ProbeWorld->SpawnActor<AEnemyShip>(ProbeTemplate->GetClass(), FTransform::Identity, Spawn);
  if (TestNotNull(TEXT("Authored enemy ship spawns in temporary world"), Probe))
  {
   // This presentation fixture does not initialize an AI blackboard/session.
   Probe->AutoPossessAI = EAutoPossessAI::Disabled;
   Probe->FinishSpawning(FTransform::Identity);
   TestFalse(TEXT("Runtime enemy yaw matches player ship"), Bool(Probe, TEXT("bInvertShipLookYaw")));
   TestTrue(TEXT("Runtime enemy pitch matches player ship"), Bool(Probe, TEXT("bInvertShipLookPitch")));
   TInlineComponentArray<UChildActorComponent*> Children(Probe);
   int32 SpawnedChests = 0;
   for (UChildActorComponent* Child : Children)
    if (AChestSpawnPoint* Point = Cast<AChestSpawnPoint>(Child->GetChildActor()))
    {
     AStorageChest* Chest = Point->SpawnConfiguredChest(nullptr, 123);
     if (!TestNotNull(TEXT("Actual child point spawns chest without legacy definition"), Chest)) continue;
     UStaticMeshComponent* Mesh = Chest->FindComponentByClass<UStaticMeshComponent>();
     TestTrue(TEXT("Spawned chest has a visible mesh"), Mesh && Mesh->GetStaticMesh() && Mesh->IsVisible() && !Mesh->bHiddenInGame);
     ++SpawnedChests;
    }
   TestTrue(TEXT("Runtime child spawn path exercised"), SpawnedChests > 0);
  }
 }
 ProbeWorld->DestroyWorld(false);
 GEngine->DestroyWorldContext(ProbeWorld);
 const FString Filename = FPaths::ProjectSavedDir() / TEXT("Diagnostics/LvlCYShipPresentationAudit.txt");
 IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
 TestTrue(TEXT("Audit report written"), FFileHelper::SaveStringToFile(Report, *Filename));
 AddInfo(Report);
 return TestTrue(TEXT("Enemy ships and chest points inspected"), Ships > 0 && Points > 0);
}
#endif
