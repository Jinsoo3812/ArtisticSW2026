#include "ShipAI/EnemyShipBundlePool.h"

#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipArchetypeData.h"
#include "ShipAI/EnemyShipNavigationComponent.h"
#include "ShipAI/ShipSwarmSubsystem.h"
#include "ShipAI/EnemyShipWeakeningWorldSubsystem.h"
#include "Components/ChildActorComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "BaseEnemy.h"
#include "ItemSpawn/LootSpawnPoint.h"
#include "ItemSpawn/GlobalLootSpawnManager.h"
#include "Item/ItemData.h"
#include "Settings_Item.h"
#include "Storage/StorageChest.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "Room/SWRoomSnapshotSubsystem.h"
#include "Room/SWRoomProgressSubsystem.h"
#include "StoryFacadeSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"
#include "Misc/ScopeExit.h"

namespace EnemyShipBundlePool
{
const FName IdleTag(TEXT("SWEnemyShipPoolIdle"));
const FName ReleasedTag(TEXT("SWEnemyShipPoolReleased"));
const FName AssignedTag(TEXT("SWEnemyShipPoolAssigned"));
const FGuid ManagerId(0xED15927A, 0x1867412C, 0xAB81B48A, 0x315DE002);

bool IsSupported(const AEnemyShip& Ship)
{
	for (const UClass* Class = Ship.GetClass(); Class; Class = Class->GetSuperClass())
		if (Class->GetPathName() == TEXT("/Game/Blueprints/Ship/Enemy_Ship/Blueprints/BP_EnemyShip.BP_EnemyShip_C")) return true;
	return false;
}

TMap<FString, UObject*> GetObjects(AEnemyShip& Ship, bool bCreateChildren = false)
{
	TMap<FString, UObject*> Objects;
	Objects.Add(TEXT("Actor"), &Ship);
	TInlineComponentArray<UActorComponent*> Components(&Ship);
	for (UActorComponent* Component : Components)
	{
		if (Component->IsA<USWRoomSnapshotComponent>()) continue;
		Objects.Add(TEXT("Component:") + Component->GetName(), Component);
		if (UChildActorComponent* ChildComponent = Cast<UChildActorComponent>(Component))
		{
			if (bCreateChildren && !IsValid(ChildComponent->GetChildActor())) ChildComponent->CreateChildActor();
			if (AActor* Child = ChildComponent->GetChildActor())
			{
				Objects.Add(TEXT("Child:") + Component->GetName(), Child);
				TInlineComponentArray<UActorComponent*> ChildComponents(Child);
				for (UActorComponent* ChildPart : ChildComponents)
					if (!ChildPart->IsA<USWRoomSnapshotComponent>())
						Objects.Add(TEXT("ChildComponent:") + Component->GetName() + TEXT(":") + ChildPart->GetName(), ChildPart);
				if (AChestSpawnPoint* Point = Cast<AChestSpawnPoint>(Child))
					if (AStorageChest* Chest = Cast<AStorageChest>(Point->GetSpawnedActor()))
					{
						Objects.Add(TEXT("SpawnedChest:") + Component->GetName(), Chest);
						if (Chest->GetChestMesh()) Objects.Add(TEXT("SpawnedChestMesh:") + Component->GetName(), Chest->GetChestMesh());
					}
			}
		}
	}
	return Objects;
}

FGuid ReadId(AActor& Actor)
{
	if (auto* Snapshot = Actor.FindComponentByClass<USWRoomSnapshotComponent>())
		if (Snapshot->StableId.IsValid()) return Snapshot->StableId;
	const FString Prefix(TEXT("SWRoomStableId="));
	for (FName Tag : Actor.Tags)
	{
		FGuid Id;
		const FString Value = Tag.ToString();
		if (Value.StartsWith(Prefix) && FGuid::Parse(Value.RightChop(Prefix.Len()), Id)) return Id;
	}
	return FGuid();
}

USWRoomSnapshotComponent* EnsureIdentity(AActor& Actor, const FGuid& Id,
	ESWRoomPersistenceClass Persistence, const FGuid& Creator, uint64 Sequence)
{
	auto* Component = Actor.FindComponentByClass<USWRoomSnapshotComponent>();
	if (!Component)
	{
		Component = NewObject<USWRoomSnapshotComponent>(&Actor, TEXT("BundleRoomSnapshot"));
		Actor.AddInstanceComponent(Component);
		Component->RegisterComponent();
	}
	const FGuid Previous = Component->StableId;
	Component->PersistenceClass = Persistence;
	Component->SetLevelInstanceId(Id);
	Component->SetRuntimeOrigin(Creator, Sequence);
	if (auto* Room = Actor.GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>())
		Room->UpdateRegisteredActorId(&Actor, Previous, Id);
	return Component;
}

void RefillChests(AEnemyShip& Ship)
{
	AGlobalLootSpawnManager* Loot = nullptr;
	for (TActorIterator<AGlobalLootSpawnManager> It(Ship.GetWorld()); It; ++It) { Loot = *It; break; }
	TInlineComponentArray<UChildActorComponent*> Children(&Ship);
	for (UChildActorComponent* Child : Children)
		if (AChestSpawnPoint* Point = Cast<AChestSpawnPoint>(Child->GetChildActor()))
			if (Point->GetSpawnMode() == EChestSpawnMode::Guarded && !IsValid(Point->GetSpawnedActor()))
			{
				const int32 Seed = static_cast<int32>(HashCombine(GetTypeHash(ReadId(Ship)), GetTypeHash(Ship.GetWorld()->GetTimeSeconds())));
				if (AStorageChest* Chest = Point->SpawnConfiguredChest(nullptr, Seed))
				{
					if (Loot) Loot->RefillPooledGuardedChest(Point, Seed);
				}
			}
}
}

AEnemyShipBundlePool::AEnemyShipBundlePool()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	Tags.Add(TEXT("SWEnemyShipPoolManager"));
	SetNetUpdateFrequency(2.0f);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	auto* Snapshot = CreateDefaultSubobject<USWRoomSnapshotComponent>(TEXT("RoomSnapshot"));
	Snapshot->SetRuntimeId(EnemyShipBundlePool::ManagerId);
	Snapshot->PersistenceClass = ESWRoomPersistenceClass::ManualOnly;
}

void UEnemyShipBundlePoolSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	if (World.GetNetMode() == NM_Client || !World.IsGameWorld()) return;
	Pool = World.SpawnActor<AEnemyShipBundlePool>();
}

void AEnemyShipBundlePool::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
		if (auto* Room = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>())
		{
			PrepareRestoreHandle = Room->OnPrepareRestore.AddUObject(this, &AEnemyShipBundlePool::PrepareRestore);
			RestoreCompletedHandle = Room->OnRestoreCompleted.AddUObject(this, &AEnemyShipBundlePool::RestoreCompleted);
		}
	GetWorldTimerManager().SetTimer(EvaluationTimer, this, &AEnemyShipBundlePool::Evaluate, 0.5f, true);
	LevelRemovedHandle = FWorldDelegates::LevelRemovedFromWorld.AddUObject(this, &AEnemyShipBundlePool::HandleLevelRemoved);
}

void AEnemyShipBundlePool::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(EvaluationTimer);
	FWorldDelegates::LevelRemovedFromWorld.Remove(LevelRemovedHandle);
	if (auto* Room = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>())
	{
		Room->OnPrepareRestore.Remove(PrepareRestoreHandle);
		Room->OnRestoreCompleted.Remove(RestoreCompletedHandle);
	}
	Super::EndPlay(Reason);
}

void AEnemyShipBundlePool::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AEnemyShipBundlePool, Placements);
}

bool AEnemyShipBundlePool::CapturePlacement(AEnemyShip& Ship, FEnemyShipBundlePlacement& Placement)
{
	Placement.StableId = EnemyShipBundlePool::ReadId(Ship);
	if (!Placement.StableId.IsValid() || !Ship.GetNavigationComponent()) return false;
	Placement.ClassPath = FSoftClassPath(Ship.GetClass());
	Placement.AuthoredName = Ship.GetFName();
	Placement.SquadId = Ship.SquadID;
	Placement.Tags = Ship.Tags;
	Placement.LevelPackage = FSoftObjectPath(Ship.GetLevel()->GetOutermost()->GetName());
	Placement.bStoryHidden = Ship.IsStoryGateDormant();
	Placement.ActivationRange = Ship.GetDistanceOptimizationRange();
	Placement.Archetype = FSoftObjectPath(Ship.AuthoredEncounterArchetype ? Ship.AuthoredEncounterArchetype.Get() : Ship.EnemyShipArchetype.Get());
	Placement.OrbitArchetype = FSoftObjectPath(Ship.EnemyShipArchetype.Get());
	if (!Ship.GetNavigationComponent()->GetSpawnHomeTransform(Placement.HomeTransform)) return false;
	Placement.HomeTransform.SetScale3D(Ship.GetActorScale3D());
	const auto Objects = EnemyShipBundlePool::GetObjects(Ship);
	TArray<FString> Keys;
	Objects.GetKeys(Keys);
	Keys.Sort();
	for (const FString& Key : Keys)
	{
		UObject* Object = Objects.FindChecked(Key);
		FEnemyShipBundleObject& Saved = Placement.Objects.AddDefaulted_GetRef();
		Saved.Key = Key;
		Saved.SourcePath = Object->GetPathName();
		Saved.ClassPath = FSoftClassPath(Object->GetClass());
		if (AActor* Actor = Cast<AActor>(Object))
		{
			Saved.bLevelPlaced = Actor->HasAnyFlags(RF_WasLoaded);
			Saved.StableId = EnemyShipBundlePool::ReadId(*Actor);
			if (!Saved.StableId.IsValid()) Saved.StableId = FGuid::NewGuid();
			if (auto* Snapshot = Actor->FindComponentByClass<USWRoomSnapshotComponent>())
			{
				Saved.bSnapshot = true;
				Saved.Persistence = Snapshot->PersistenceClass;
			}
			else Saved.bSnapshot = Key == TEXT("Actor") || Key.StartsWith(TEXT("Child:"));
		}
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_Edit)
				|| Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient | CPF_EditorOnly | CPF_InstancedReference)
				|| Property->GetFName() == TEXT("GuardCharacters") || Property->GetFName() == TEXT("GuardSpawners")) continue;
			FEnemyShipBundleProperty& Value = Saved.Properties.AddDefaulted_GetRef();
			Value.Name = Property->GetFName();
			Property->ExportText_InContainer(0, Value.Value, Object, nullptr, Object, PPF_None);
		}
		if (UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Object))
			if (Mesh->GetStaticMesh() && Mesh->IsVisible() && !Mesh->bHiddenInGame)
			{
				FEnemyShipBundleMesh& Visual = Placement.Meshes.AddDefaulted_GetRef();
				Visual.Mesh = FSoftObjectPath(Mesh->GetStaticMesh());
				Visual.RelativeTransform = Mesh->GetComponentTransform().GetRelativeTransform(Ship.GetActorTransform());
				Visual.bCastShadow = Mesh->CastShadow;
				for (int32 Index = 0; Index < Mesh->GetNumMaterials(); ++Index)
				{
					UMaterialInterface* Material = Mesh->GetMaterial(Index);
					if (auto* Dynamic = Cast<UMaterialInstanceDynamic>(Material)) Material = Dynamic->Parent;
					Visual.Materials.Add(FSoftObjectPath(Material));
				}
			}
	}
	Placement.ActiveShip = &Ship;
	Ship.OnDestroyed.AddUniqueDynamic(this, &AEnemyShipBundlePool::HandleShipDestroyed);
	SetBundleRegistration(Ship, false, &Placement);
	return true;
}

void AEnemyShipBundlePool::DiscoverPlacedShips()
{
	for (TActorIterator<AEnemyShip> It(GetWorld()); It; ++It)
	{
		AEnemyShip* Ship = *It;
		if (!IsValid(Ship) || !Ship->HasAnyFlags(RF_WasLoaded) || !Ship->HasActorBegunPlay() || Ship->IsBundlePoolIdle()
			|| Ship->ActorHasTag(EnemyShipBundlePool::AssignedTag) || !Ship->IsDistanceOptimizationEnabled()
			|| !EnemyShipBundlePool::IsSupported(*Ship)) continue;
		const FGuid Id = EnemyShipBundlePool::ReadId(*Ship);
		if (auto* Existing = Placements.FindByPredicate([&](const FEnemyShipBundlePlacement& P) { return P.StableId == Id; }))
		{
			if (Existing->bUnloaded)
			{
				Existing->bUnloaded = false;
				Existing->ActiveShip = Ship;
				Existing->bDormant = false;
				if (Existing->bDestroyed) DestroyBundle(Ship);
				else SetBundleRegistration(*Ship, false, Existing);
			}
			continue;
		}
		FEnemyShipBundlePlacement Placement;
		if (CapturePlacement(*Ship, Placement)) Placements.Add(MoveTemp(Placement));
	}
}

void AEnemyShipBundlePool::SetBundleRegistration(AEnemyShip& Ship, bool bIdle, const FEnemyShipBundlePlacement* Placement)
{
	TArray<AActor*> Actors;
	Ship.CollectBundleActors(Actors);
	auto* Room = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>();
	for (AActor* Actor : Actors)
	{
		if (bIdle)
		{
			Actor->Tags.AddUnique(EnemyShipBundlePool::IdleTag);
			if (Room) Room->ReleaseActorForPooling(Actor);
		}
		else
		{
			Actor->Tags.Remove(EnemyShipBundlePool::IdleTag);
			Actor->Tags.Remove(EnemyShipBundlePool::ReleasedTag);
			Actor->Tags.AddUnique(EnemyShipBundlePool::AssignedTag);
			Actor->Tags.Remove(TEXT("SWEnemyShipPoolLevelOrigin"));
			if (auto* Snapshot = Actor->FindComponentByClass<USWRoomSnapshotComponent>())
				if (Room && Snapshot->StableId.IsValid()) Room->UpdateRegisteredActorId(Actor, Snapshot->StableId, Snapshot->StableId);
		}
	}
	if (!bIdle && Placement)
	{
		const auto Objects = EnemyShipBundlePool::GetObjects(Ship);
		for (const FEnemyShipBundleObject& Saved : Placement->Objects)
			if (Saved.bSnapshot)
				if (UObject* const* Found = Objects.Find(Saved.Key))
					if (AActor* Actor = Cast<AActor>(*Found))
					{
						if (Saved.bLevelPlaced) Actor->Tags.AddUnique(TEXT("SWEnemyShipPoolLevelOrigin"));
						if (Room && !Room->IsRestoringSnapshot()) Room->ClearPooledActorTombstone(Saved.StableId);
						EnemyShipBundlePool::EnsureIdentity(*Actor, Saved.StableId, Saved.Persistence,
							Saved.Key == TEXT("Actor") ? EnemyShipBundlePool::ManagerId : Placement->StableId,
							static_cast<uint64>(Placement->Assignment) + 1);
					}
		if (auto* Weakening = GetWorld()->GetSubsystem<UEnemyShipWeakeningWorldSubsystem>())
			for (AActor* Actor : Actors)
				if (ABaseEnemy* Enemy = Cast<ABaseEnemy>(Actor)) Weakening->RegisterMember(&Ship, Enemy);
	}
}

bool AEnemyShipBundlePool::BindPlacement(FEnemyShipBundlePlacement& Placement, bool bRestoring, FString& Error)
{
	UClass* Class = Placement.ClassPath.TryLoadClass<AEnemyShip>();
	if (!Class || Placement.HomeTransform.ContainsNaN()) { Error = TEXT("Enemy ship pool definition is invalid"); return false; }
	AEnemyShip* Ship = Placement.ActiveShip;
	if (!IsValid(Ship))
	{
		for (TActorIterator<AEnemyShip> It(GetWorld()); It; ++It)
			if (!It->IsBundlePoolIdle() && It->GetClass() == Class && EnemyShipBundlePool::ReadId(**It) == Placement.StableId)
			{ Ship = *It; break; }
		for (int32 Index = IdleBundles.Num() - 1; Index >= 0; --Index)
			if (!Ship && IsValid(IdleBundles[Index]) && IdleBundles[Index]->GetClass() == Class
				&& IdleBundles[Index]->GetLevel()->GetOutermost()->GetName() == Placement.LevelPackage.ToString())
			{
				Ship = IdleBundles[Index];
				IdleBundles.RemoveAtSwap(Index);
				break;
			}
		if (!Ship)
		{
			FActorSpawnParameters Parameters;
			Parameters.Owner = this;
			Parameters.bDeferConstruction = true;
			Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			for (ULevel* Level : GetWorld()->GetLevels())
				if (Level->GetOutermost()->GetName() == Placement.LevelPackage.ToString()) { Parameters.OverrideLevel = Level; break; }
			Ship = GetWorld()->SpawnActor<AEnemyShip>(Class, Placement.HomeTransform, Parameters);
			if (!Ship) { Error = TEXT("Enemy ship bundle allocation failed"); return false; }
			if (const FEnemyShipBundleObject* ActorDefinition = Placement.Objects.FindByPredicate(
				[](const FEnemyShipBundleObject& Value) { return Value.Key == TEXT("Actor"); }))
				for (const FEnemyShipBundleProperty& Value : ActorDefinition->Properties)
					if (FProperty* Property = Class->FindPropertyByName(Value.Name))
					{
						const FString Text = Value.Value.Replace(*ActorDefinition->SourcePath, *Ship->GetPathName());
						Property->ImportText_Direct(*Text, Property->ContainerPtrToValuePtr<void>(Ship), Ship, PPF_None);
					}
			Ship->Tags.AddUnique(EnemyShipBundlePool::IdleTag);
			Ship->SetBundlePoolIdle(true);
			Ship->FinishSpawning(Placement.HomeTransform);
			Ship->SetBundlePoolIdle(true);
		}
	}
	Placement.ActiveShip = Ship;
	const auto Objects = EnemyShipBundlePool::GetObjects(*Ship, true);
	for (const FEnemyShipBundleObject& Saved : Placement.Objects)
	{
		UObject* const* Found = Objects.Find(Saved.Key);
		if (!Found)
		{
			if (Saved.Key.StartsWith(TEXT("SpawnedChest"))) continue;
			Error = TEXT("Enemy ship bundle component missing: ") + Saved.Key;
			return false;
		}
		UObject* Target = *Found;
		for (const FEnemyShipBundleProperty& Value : Saved.Properties)
		{
			FProperty* Property = Target->GetClass()->FindPropertyByName(Value.Name);
			if (!Property) { Error = TEXT("Enemy ship bundle property missing: ") + Value.Name.ToString(); return false; }
			FString Text = Value.Value;
			for (const FEnemyShipBundleObject& Reference : Placement.Objects)
				if (UObject* const* Mapped = Objects.Find(Reference.Key))
					Text.ReplaceInline(*Reference.SourcePath, *(*Mapped)->GetPathName(), ESearchCase::CaseSensitive);
			if (!Property->ImportText_Direct(*Text, Property->ContainerPtrToValuePtr<void>(Target), Target, PPF_None))
			{ Error = TEXT("Enemy ship bundle property restore failed: ") + Value.Name.ToString(); return false; }
		}
	}
	Ship->Tags = Placement.Tags;
	Ship->SetBundleAuthoredName(Placement.AuthoredName);
	Ship->AuthoredEncounterArchetype = Cast<UEnemyShipArchetypeData>(Placement.Archetype.TryLoad());
	Ship->RefreshBundleStoryBindings();
	Ship->GetNavigationComponent()->ResetSpawnHome(Placement.HomeTransform);
	Ship->SetActorTransform(Placement.HomeTransform, false, nullptr, ETeleportType::TeleportPhysics);
	Ship->SetActorEnableCollision(false);
	for (const auto& Pair : Objects)
		if (UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Pair.Value)) Mesh->ReregisterComponent();
	Ship->SetShipRuntimePhysicsEnabled(false);
	Placement.ActiveShip = Ship;
	Placement.bDormant = false;
	Placement.bUnloaded = false;
	if (bRestoring && Ship->EnemyShipArchetype) Ship->ConfigureEnemyShipArchetype(Ship->EnemyShipArchetype);
	if (!bRestoring)
	{
		++Placement.Assignment;
		Ship->PrepareFreshBundleEncounter(Placement.HomeTransform);
		EnemyShipBundlePool::RefillChests(*Ship);
	}
	SetBundleRegistration(*Ship, false, &Placement);
	Ship->OnDestroyed.AddUniqueDynamic(this, &AEnemyShipBundlePool::HandleShipDestroyed);
	if (auto* Swarm = GetWorld()->GetSubsystem<UShipSwarmSubsystem>()) Swarm->RegisterShip(Ship);
	if (auto* Weakening = GetWorld()->GetSubsystem<UEnemyShipWeakeningWorldSubsystem>()) Weakening->RegisterShip(Ship);
	if (!bRestoring) Ship->SetBundlePoolIdle(false);
	ForceNetUpdate();
	return true;
}

void AEnemyShipBundlePool::ReleasePlacement(FEnemyShipBundlePlacement& Placement)
{
	AEnemyShip* Ship = Placement.ActiveShip;
	if (!IsValid(Ship)) return;
	Ship->SetBundlePoolIdle(true);
	Ship->PrepareFreshBundleEncounter(Placement.HomeTransform);
	Placement.OrbitArchetype = FSoftObjectPath(Ship->EnemyShipArchetype.Get());
	SetBundleRegistration(*Ship, true, nullptr);
	if (auto* Swarm = GetWorld()->GetSubsystem<UShipSwarmSubsystem>()) Swarm->UnregisterShip(Ship);
	if (auto* Weakening = GetWorld()->GetSubsystem<UEnemyShipWeakeningWorldSubsystem>()) Weakening->UnregisterShip(Ship);
	Placement.ActiveShip = nullptr;
	Placement.bDormant = true;
	IdleBundles.AddUnique(Ship);
	while (IdleBundles.Num() > FMath::Max(0, IdleCapacity))
	{
		AEnemyShip* Excess = IdleBundles[0];
		IdleBundles.RemoveAt(0);
		DestroyBundle(Excess);
	}
	ForceNetUpdate();
}

void AEnemyShipBundlePool::DestroyBundle(AEnemyShip* Ship)
{
	if (!IsValid(Ship)) return;
	TArray<AActor*> Actors;
	Ship->CollectBundleActors(Actors);
	for (AActor* Actor : Actors)
	{
		Actor->Tags.AddUnique(EnemyShipBundlePool::ReleasedTag);
		if (auto* Room = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>()) Room->ReleaseActorForPooling(Actor);
	}
	Ship->OnDestroyed.RemoveDynamic(this, &AEnemyShipBundlePool::HandleShipDestroyed);
	for (int32 Index = Actors.Num() - 1; Index >= 0; --Index)
		if (IsValid(Actors[Index]) && Actors[Index] != Ship) Actors[Index]->Destroy();
	Ship->Destroy();
}

void AEnemyShipBundlePool::HandleShipDestroyed(AActor* Ship)
{
	if (bChangingPool || Ship->ActorHasTag(EnemyShipBundlePool::IdleTag) || Ship->ActorHasTag(EnemyShipBundlePool::ReleasedTag)) return;
	for (FEnemyShipBundlePlacement& Placement : Placements)
		if (Placement.ActiveShip == Ship)
		{
			Placement.ActiveShip = nullptr;
			Placement.bDestroyed = true;
		}
	ForceNetUpdate();
}

void AEnemyShipBundlePool::Evaluate()
{
	if (!HasAuthority()) { OnRep_Placements(); return; }
	const auto* Room = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>();
	if (bChangingPool || (Room && Room->IsRestoringSnapshot())) return;
	DiscoverPlacedShips();
	TArray<FVector> Players;
	for (TActorIterator<AShip> It(GetWorld()); It; ++It)
		if (!It->IsEnemyShipForEffects() && It->ActorHasTag(TEXT("Player")) && !It->ActorHasTag(TEXT("Enemy"))) Players.Add(It->GetActorLocation());
	for (FEnemyShipBundlePlacement& Placement : Placements)
	{
		if (Placement.bDestroyed || Placement.bUnloaded) continue;
		if (IsValid(Placement.ActiveShip))
		{
			Placement.bStoryHidden = Placement.ActiveShip->IsStoryGateDormant();
			Placement.OrbitArchetype = FSoftObjectPath(Placement.ActiveShip->EnemyShipArchetype.Get());
		}
		else if (Placement.SquadId == TEXT("Final"))
		{
			const auto* Story = GetGameInstance()->GetSubsystem<UStoryFacadeSubsystem>();
			const auto* Progress = GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>();
			const bool bDevelopment = Progress && Progress->IsDevelopmentFinalEncounterWorld(GetWorld());
			Placement.bStoryHidden = !Story || ((!bDevelopment && !Story->IsStoryNodeReached(EStoryNode::UldolmokBattleQuestAccepted))
				|| Story->IsStoryNodeReached(EStoryNode::FinalBossDefeated));
		}
		if (Placement.bDormant)
		{
			const bool bInRange = Players.ContainsByPredicate([&](const FVector& Position)
			{ return FVector::DistSquared2D(Position, Placement.HomeTransform.GetLocation()) <= FMath::Square(FMath::Max(0.f, Placement.ActivationRange)); });
			if (bInRange)
			{
				FString Error;
				if (!BindPlacement(Placement, false, Error)) UE_LOG(LogTemp, Error, TEXT("Enemy ship pool wake failed: %s"), *Error);
			}
		}
		else if (IsValid(Placement.ActiveShip) && Placement.ActiveShip->IsDistanceOptimizationDormant())
		{
			TArray<AActor*> Bundle;
			Placement.ActiveShip->CollectBundleActors(Bundle);
			bool bOutstanding = false;
			for (TActorIterator<AActor> It(GetWorld()); It; ++It)
				if (It->GetOwner() == Placement.ActiveShip && !Bundle.Contains(*It)) { bOutstanding = true; break; }
			if (!bOutstanding) ReleasePlacement(Placement);
		}
	}
	OnRep_Placements();
}

void AEnemyShipBundlePool::OnRep_Placements()
{
	if (GetNetMode() == NM_DedicatedServer) return;
	uint32 PresentationHash = GetTypeHash(Placements.Num());
	for (const auto& Placement : Placements)
	{
		PresentationHash = HashCombine(PresentationHash, GetTypeHash(Placement.StableId));
		PresentationHash = HashCombine(PresentationHash, GetTypeHash(Placement.ActiveShip.Get()));
		PresentationHash = HashCombine(PresentationHash, GetTypeHash(Placement.Assignment));
		PresentationHash = HashCombine(PresentationHash, GetTypeHash(Placement.bDormant));
		PresentationHash = HashCombine(PresentationHash, GetTypeHash(Placement.bDestroyed));
		PresentationHash = HashCombine(PresentationHash, GetTypeHash(Placement.bUnloaded));
		PresentationHash = HashCombine(PresentationHash, GetTypeHash(Placement.bStoryHidden));
		PresentationHash = HashCombine(PresentationHash,
			GetTypeHash(IsValid(Placement.ActiveShip) && Placement.ActiveShip->IsBundlePoolIdle()));
		if (!HasAuthority() && IsValid(Placement.ActiveShip)
			&& ClientAssignments.FindRef(Placement.StableId) != Placement.Assignment)
		{
			const auto Objects = EnemyShipBundlePool::GetObjects(*Placement.ActiveShip);
			for (const FEnemyShipBundleObject& Saved : Placement.Objects)
				if (UObject* const* Target = Objects.Find(Saved.Key))
					for (const FEnemyShipBundleProperty& Value : Saved.Properties)
						if (Value.Name == TEXT("StaticMesh") || Value.Name == TEXT("OverrideMaterials")
							|| Value.Name == TEXT("RelativeLocation") || Value.Name == TEXT("RelativeRotation")
							|| Value.Name == TEXT("RelativeScale3D") || Value.Name == TEXT("bVisible"))
							if (FProperty* Property = (*Target)->GetClass()->FindPropertyByName(Value.Name))
								Property->ImportText_Direct(*Value.Value, Property->ContainerPtrToValuePtr<void>(*Target), *Target, PPF_None);
			ClientAssignments.Add(Placement.StableId, Placement.Assignment);
			TInlineComponentArray<USceneComponent*> Scenes(Placement.ActiveShip.Get());
			for (USceneComponent* Scene : Scenes) Scene->UpdateComponentToWorld();
			for (const auto& Pair : Objects)
				if (UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Pair.Value)) Mesh->MarkRenderStateDirty();
		}
	}
	if (LastPresentationHash == PresentationHash) return;
	LastPresentationHash = PresentationHash;
	for (UInstancedStaticMeshComponent* Component : ProxyInstanceComponents) if (Component) Component->ClearInstances();
	TMap<FString, UInstancedStaticMeshComponent*> Batches;
	for (UInstancedStaticMeshComponent* Component : ProxyInstanceComponents)
		if (Component)
		{
			FString Key = Component->GetStaticMesh()->GetPathName() + (Component->CastShadow ? TEXT("|1") : TEXT("|0"));
			for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index) Key += TEXT("|") + GetPathNameSafe(Component->GetMaterial(Index));
			Batches.Add(Key, Component);
		}
	for (const FEnemyShipBundlePlacement& Placement : Placements)
	{
		if (Placement.bDestroyed || Placement.bUnloaded || Placement.bStoryHidden || (!Placement.bDormant && IsValid(Placement.ActiveShip)
			&& !Placement.ActiveShip->IsBundlePoolIdle())) continue;
		for (const FEnemyShipBundleMesh& Visual : Placement.Meshes)
		{
			UStaticMesh* Mesh = Cast<UStaticMesh>(Visual.Mesh.TryLoad());
			if (!Mesh) continue;
			FString Key = Mesh->GetPathName() + (Visual.bCastShadow ? TEXT("|1") : TEXT("|0"));
			TArray<UMaterialInterface*> Materials;
			for (const FSoftObjectPath& Path : Visual.Materials)
			{
				UMaterialInterface* Material = Cast<UMaterialInterface>(Path.TryLoad());
				Materials.Add(Material);
				Key += TEXT("|") + GetPathNameSafe(Material);
			}
			UInstancedStaticMeshComponent*& Batch = Batches.FindOrAdd(Key);
			if (!Batch)
			{
				Batch = NewObject<UInstancedStaticMeshComponent>(this);
				Batch->SetupAttachment(RootComponent);
				Batch->SetMobility(EComponentMobility::Movable);
				Batch->SetStaticMesh(Mesh);
				Batch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Batch->SetCanEverAffectNavigation(false);
				Batch->SetCastShadow(Visual.bCastShadow);
				for (int32 Index = 0; Index < Materials.Num(); ++Index) Batch->SetMaterial(Index, Materials[Index]);
				Batch->RegisterComponent();
				ProxyInstanceComponents.Add(Batch);
			}
			Batch->AddInstance(Visual.RelativeTransform * Placement.HomeTransform, true);
		}
	}
}

void AEnemyShipBundlePool::CaptureRoomDomains(TArray<FSWRoomDomainPart>& Parts, TArray<FSWRoomCaptureIssue>& Issues) const
{
	FEnemyShipBundlePoolState State;
	State.Placements = Placements;
	FSWRoomDomainPart& Part = Parts.AddDefaulted_GetRef();
	Part.Domain = ESWRoomDomain::EnemyShipPool;
	Part.Version = 1;
	if (!FSWRoomStructCodec::Write(State, Part.Bytes))
	{
		FSWRoomCaptureIssue& Issue = Issues.AddDefaulted_GetRef();
		Issue.Domain = TEXT("EnemyShipPool");
		Issue.FieldKey = TEXT("Placements");
		Issue.Reason = TEXT("Enemy ship pool definitions could not be serialized");
	}
}

bool AEnemyShipBundlePool::RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& Error)
{
	FEnemyShipBundlePoolState State;
	if (Part.Domain != ESWRoomDomain::EnemyShipPool || Part.Version != 1 || !FSWRoomStructCodec::Read(Part.Bytes, State))
	{ Error = TEXT("Invalid enemy ship pool state"); return false; }
	TMap<FGuid, AEnemyShip*> Assigned;
	for (const auto& Placement : Placements) if (IsValid(Placement.ActiveShip)) Assigned.Add(Placement.StableId, Placement.ActiveShip);
	Placements = MoveTemp(State.Placements);
	for (auto& Placement : Placements) Placement.ActiveShip = Assigned.FindRef(Placement.StableId);
	return true;
}

bool AEnemyShipBundlePool::CompareRoomDomain(const FSWRoomDomainPart& Expected, const FSWRoomDomainPart& Actual,
	float Tolerance, TArray<FString>& Fields) const
{
	return FSWRoomStructCodec::Compare<FEnemyShipBundlePoolState>(Expected, Actual, Tolerance, Fields);
}

void AEnemyShipBundlePool::PrepareRestore(const FSWRoomWorldSnapshot& Snapshot, FString& Error)
{
	DiscoverPlacedShips();
	const FSWRoomActorRecord* Manager = Snapshot.Actors.FindByPredicate([](const FSWRoomActorRecord& Record)
	{ return Record.StableId == EnemyShipBundlePool::ManagerId; });
	if (!Manager)
	{
		for (auto& Placement : Placements)
			if (Snapshot.DestroyedLevelActorIds.Contains(Placement.StableId))
			{
				if (IsValid(Placement.ActiveShip)) ReleasePlacement(Placement);
				Placement.bDestroyed = true;
			}
			else if (Snapshot.Actors.ContainsByPredicate([&](const FSWRoomActorRecord& Record) { return Record.StableId == Placement.StableId; }))
				if (!BindPlacement(Placement, true, Error)) return;
		return;
	}
	FSWRoomDomainPayload Payload;
	if (!FSWRoomStructCodec::Read(Manager->AdapterBytes, Payload)) { Error = TEXT("Invalid enemy ship pool restore payload"); return; }
	const FSWRoomDomainPart* Part = Payload.Parts.FindByPredicate([](const FSWRoomDomainPart& Value) { return Value.Domain == ESWRoomDomain::EnemyShipPool; });
	if (!Part) { Error = TEXT("Enemy ship pool restore domain missing"); return; }
	FEnemyShipBundlePoolState State;
	if (!FSWRoomStructCodec::Read(Part->Bytes, State)) { Error = TEXT("Enemy ship pool restore definitions invalid"); return; }
	TSet<FGuid> Ids;
	for (const auto& Saved : State.Placements)
		if (!Saved.StableId.IsValid() || Ids.Contains(Saved.StableId) || Saved.HomeTransform.ContainsNaN() || !FMath::IsFinite(Saved.ActivationRange))
		{ Error = TEXT("Enemy ship pool restore contains invalid identity or transform"); return; }
		else Ids.Add(Saved.StableId);
	TGuardValue<bool> Changing(bChangingPool, true);
	for (auto& Current : Placements)
	{
		const auto* Saved = State.Placements.FindByPredicate([&](const FEnemyShipBundlePlacement& Value) { return Value.StableId == Current.StableId; });
		if (IsValid(Current.ActiveShip) && (!Saved || Saved->bDormant || Saved->bDestroyed)) ReleasePlacement(Current);
	}
	TMap<FGuid, AEnemyShip*> Assigned;
	for (const auto& Current : Placements) if (IsValid(Current.ActiveShip)) Assigned.Add(Current.StableId, Current.ActiveShip);
	Placements = MoveTemp(State.Placements);
	for (auto& Placement : Placements)
	{
		Placement.ActiveShip = Assigned.FindRef(Placement.StableId);
		if (!Placement.bDormant && !Placement.bDestroyed && !Placement.bUnloaded && !BindPlacement(Placement, true, Error)) return;
	}
}

bool AEnemyShipBundlePool::FinalizeRoomRestore(const TMap<FGuid, AActor*>& Actors, FString& Error)
{
	for (auto& Placement : Placements)
	{
		Placement.ActiveShip = Placement.bDormant || Placement.bDestroyed || Placement.bUnloaded ? nullptr : Cast<AEnemyShip>(Actors.FindRef(Placement.StableId));
		if (!Placement.bDormant && !Placement.bDestroyed && !Placement.bUnloaded && !IsValid(Placement.ActiveShip))
		{ Error = TEXT("Restored active enemy ship bundle is missing"); return false; }
	}
	return true;
}

void AEnemyShipBundlePool::RestoreCompleted()
{
	for (auto& Placement : Placements)
		if (IsValid(Placement.ActiveShip) && Placement.ActiveShip->IsBundlePoolIdle()) Placement.ActiveShip->SetBundlePoolIdle(false);
	ForceNetUpdate();
	OnRep_Placements();
}

bool AEnemyShipBundlePool::ApplyLogicalOrbitDistance(AEnemyShip* Ship) const
{
	if (!Ship) return false;
	TArray<const FEnemyShipBundlePlacement*> Members;
	for (const auto& Placement : Placements) if (!Placement.bDestroyed && Placement.SquadId == Ship->SquadID) Members.Add(&Placement);
	if (Members.IsEmpty()) return false;
	Members.Sort([](const FEnemyShipBundlePlacement& A, const FEnemyShipBundlePlacement& B) { return A.AuthoredName.LexicalLess(B.AuthoredName); });
	float Ideal = 0.0f, Spacing = 0.0f;
	int32 Index = INDEX_NONE;
	for (int32 Member = 0; Member < Members.Num(); ++Member)
	{
		const auto& Definition = *Members[Member];
		const auto* Archetype = IsValid(Definition.ActiveShip) ? Definition.ActiveShip->EnemyShipArchetype.Get()
			: Cast<UEnemyShipArchetypeData>(Definition.OrbitArchetype.TryLoad());
		if (!Archetype) return false;
		Ideal += Archetype->NavigationProfile.IdealDistance;
		Spacing += FMath::Max(0.f, Archetype->OrbitDistanceSpacing);
		if (Definition.ActiveShip == Ship) Index = Member;
	}
	if (Index == INDEX_NONE) return false;
	Ship->SetSquadAssignedIdealDistance(FMath::Max(1.f, Ideal / Members.Num() + (Index - (Members.Num() - 1) * 0.5f) * Spacing / Members.Num()));
	return true;
}

void AEnemyShipBundlePool::HandleLevelRemoved(ULevel* Level, UWorld* World)
{
	if (!HasAuthority() || World != GetWorld() || !Level) return;
	const FString Package = Level->GetOutermost()->GetName();
	for (auto& Placement : Placements)
		if (Placement.LevelPackage.ToString() == Package)
		{
			Placement.ActiveShip = nullptr;
			Placement.bUnloaded = true;
		}
	IdleBundles.RemoveAll([&](const TObjectPtr<AEnemyShip>& Ship) { return !IsValid(Ship) || Ship->GetLevel() == Level; });
	ForceNetUpdate();
	OnRep_Placements();
}
