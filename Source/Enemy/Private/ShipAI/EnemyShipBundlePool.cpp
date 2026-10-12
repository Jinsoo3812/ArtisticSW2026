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
#include "Engine/ActorChannel.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameplayTasksComponent.h"
#include "BaseEnemy.h"
#include "ItemSpawn/LootSpawnPoint.h"
#include "ItemSpawn/GlobalLootSpawnManager.h"
#include "Item/ItemData.h"
#include "Settings_Item.h"
#include "Storage/StorageChest.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "Room/SWRoomSnapshotSubsystem.h"
#include "Room/SWRoomProgressSubsystem.h"
#include "StoryFacadeSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "Net/DataBunch.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"
#include "Misc/ScopeExit.h"
#include "Misc/Compression.h"
#include "Misc/PackageName.h"
#include "Rendering/SWEnemyShipISMMaterialPath.h"

namespace EnemyShipBundlePool
{
const FName IdleTag(TEXT("SWEnemyShipPoolIdle"));
const FName ReleasedTag(TEXT("SWEnemyShipPoolReleased"));
const FName AssignedTag(TEXT("SWEnemyShipPoolAssigned"));
const FGuid ManagerId(0xED15927A, 0x1867412C, 0xAB81B48A, 0x315DE002);
constexpr int32 MaxRawSaveBytes = 64 * 1024 * 1024;
// This single record aggregates every logical ship; ordinary Actor records stay at 1 MiB.
constexpr int32 MaxCompressedSaveBytes = 8 * 1024 * 1024 - 4096;

FSoftObjectPath ISMMaterialPath(const FSoftObjectPath& Original)
{
	if (Original.IsNull()) return Original;
	const FSoftObjectPath Variant(SWEnemyShipISMMaterials::ObjectPath(Original.ToString()));
	return FPackageName::DoesPackageExist(Variant.GetLongPackageName()) ? Variant : Original;
}

bool ReadPoolState(const FSWRoomDomainPart& Part, FEnemyShipBundlePoolState& State)
{
	if (Part.Domain != ESWRoomDomain::EnemyShipPool) return false;
	if (Part.Version == 1) return FSWRoomStructCodec::Read(Part.Bytes, State);
	if (Part.Version != 2 || Part.Bytes.Num() <= sizeof(int32)
		|| Part.Bytes.Num() > MaxCompressedSaveBytes) return false;
	int32 RawSize = 0;
	FMemory::Memcpy(&RawSize, Part.Bytes.GetData(), sizeof(RawSize));
	if (RawSize <= 0 || RawSize > MaxRawSaveBytes) return false;
	TArray<uint8> Raw;
	Raw.SetNumUninitialized(RawSize);
	return FCompression::UncompressMemory(NAME_Zlib, Raw.GetData(), RawSize,
		Part.Bytes.GetData() + sizeof(RawSize), Part.Bytes.Num() - sizeof(RawSize))
		&& FSWRoomStructCodec::Read(Raw, State);
}

bool IsSupported(const AEnemyShip& Ship)
{
	for (const UClass* Class = Ship.GetClass(); Class; Class = Class->GetSuperClass())
		if (Class->GetPathName() == TEXT("/Game/Blueprints/Ship/Enemy_Ship/Blueprints/BP_EnemyShip.BP_EnemyShip_C")) return true;
	return false;
}

TMap<FString, UObject*> GetObjects(AEnemyShip& Ship, bool bCreateChildren = false)
{
	auto IsDefinitionComponent = [](const UActorComponent* Component)
	{
		// Runtime task/ability helpers are rebuilt by gameplay, not authored BP placement data.
		return Component && !Component->IsA<USWRoomSnapshotComponent>()
			&& (!Component->IsA<UGameplayTasksComponent>() || Component->HasAnyFlags(RF_DefaultSubObject))
			&& Component->CreationMethod != EComponentCreationMethod::Instance
			&& (Component->CreationMethod != EComponentCreationMethod::Native || Component->HasAnyFlags(RF_DefaultSubObject));
	};
	TMap<FString, UObject*> Objects;
	Objects.Add(TEXT("Actor"), &Ship);
	TInlineComponentArray<UActorComponent*> Components(&Ship);
	for (UActorComponent* Component : Components)
	{
		if (!IsDefinitionComponent(Component)) continue;
		Objects.Add(TEXT("Component:") + Component->GetName(), Component);
		if (UChildActorComponent* ChildComponent = Cast<UChildActorComponent>(Component))
		{
			if (bCreateChildren && !IsValid(ChildComponent->GetChildActor())) ChildComponent->CreateChildActor();
			if (AActor* Child = ChildComponent->GetChildActor())
			{
				Objects.Add(TEXT("Child:") + Component->GetName(), Child);
				TInlineComponentArray<UActorComponent*> ChildComponents(Child);
				for (UActorComponent* ChildPart : ChildComponents)
					if (IsDefinitionComponent(ChildPart))
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
	// Project defaults enable the registered list; this Actor uses the bounded legacy override.
	bReplicateUsingRegisteredSubObjectList = false;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	Tags.Add(TEXT("SWEnemyShipPoolManager"));
	// Definitions are introduced one ship at a time; unchanged nodes do not send data.
	SetNetUpdateFrequency(10.0f);
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
			PrepareCaptureHandle = Room->OnPrepareCapture.AddWeakLambda(this, [this]()
			{
				if (Placements.IsEmpty()) DiscoverPlacedShips();
			});
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
		Room->OnPrepareCapture.Remove(PrepareCaptureHandle);
		Room->OnRestoreCompleted.Remove(RestoreCompletedHandle);
	}
	Super::EndPlay(Reason);
}

void AEnemyShipBundlePool::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AEnemyShipBundlePool, PresentationNodes);
}

void UEnemyShipBundlePresentation::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UEnemyShipBundlePresentation, Definition);
	DOREPLIFETIME(UEnemyShipBundlePresentation, State);
}

bool AEnemyShipBundlePool::ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch, FReplicationFlags* RepFlags)
{
	bool bWrote = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);
	bool bIntroducedNode = false;
	for (UEnemyShipBundlePresentation* Node : PresentationNodes)
	{
		if (!Node) continue;
		const bool bNewNode = !Channel->ReplicationMap.Contains(Node);
		if (bNewNode && bIntroducedNode) continue;
		// Bound the aggregate bunch too, including initial replication and retransmission.
		if (Bunch->GetNumBytes() >= 16 * 1024) break;
		bWrote |= Channel->ReplicateSubobject(Node, *Bunch, *RepFlags);
		bIntroducedNode |= bNewNode;
	}
	return bWrote;
}

void AEnemyShipBundlePool::UpdatePresentation()
{
	for (const FEnemyShipBundlePlacement& Placement : Placements)
	{
		UEnemyShipBundlePresentation* Node = nullptr;
		for (UEnemyShipBundlePresentation* Candidate : PresentationNodes)
			if (Candidate && Candidate->Definition.StableId == Placement.StableId) { Node = Candidate; break; }
		if (!Node)
		{
			Node = NewObject<UEnemyShipBundlePresentation>(this);
			Node->Definition.StableId = Placement.StableId;
			Node->Definition.AuthoredName = Placement.AuthoredName;
			Node->Definition.HomeTransform = Placement.HomeTransform;
			Node->Definition.Meshes = Placement.Meshes;
			for (const FEnemyShipBundleObject& Saved : Placement.Objects)
			{
				FEnemyShipBundleVisualObject Visual;
				Visual.Key = Saved.Key;
				for (const FEnemyShipBundleProperty& Value : Saved.Properties)
					if (Value.Name == TEXT("StaticMesh") || Value.Name == TEXT("OverrideMaterials")
						|| Value.Name == TEXT("RelativeLocation") || Value.Name == TEXT("RelativeRotation")
						|| Value.Name == TEXT("RelativeScale3D") || Value.Name == TEXT("bVisible"))
						Visual.Properties.Add(Value);
				if (!Visual.Properties.IsEmpty()) Node->Definition.Objects.Add(MoveTemp(Visual));
			}
			PresentationNodes.Add(Node);
		}
		Node->State.Assignment = Placement.Assignment;
		Node->State.bDormant = Placement.bDormant;
		Node->State.bDestroyed = Placement.bDestroyed;
		Node->State.bUnloaded = Placement.bUnloaded;
		Node->State.bStoryHidden = Placement.bStoryHidden;
		Node->State.ActiveShip = Placement.ActiveShip;
	}
	for (int32 Index = PresentationNodes.Num() - 1; Index >= 0; --Index)
		if (!PresentationNodes[Index] || !Placements.ContainsByPredicate([&](const FEnemyShipBundlePlacement& Placement)
			{ return Placement.StableId == PresentationNodes[Index]->Definition.StableId; }))
			PresentationNodes.RemoveAt(Index);
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
			// ExportText_InContainer with a null delta omits zero/default values (including False).
			Property->ExportTextItem_Direct(Value.Value, Property->ContainerPtrToValuePtr<void>(Object),
				nullptr, Object, PPF_Delimited);
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
					Visual.Materials.Add(EnemyShipBundlePool::ISMMaterialPath(FSoftObjectPath(Material)));
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
						Property->ImportText_Direct(*Text, Property->ContainerPtrToValuePtr<void>(Ship), Ship, PPF_Delimited);
					}
			Ship->Tags.AddUnique(EnemyShipBundlePool::IdleTag);
			Ship->SetBundlePoolIdle(true);
			Ship->FinishSpawning(Placement.HomeTransform);
			Ship->SetBundlePoolIdle(true);
		}
	}
	Placement.ActiveShip = Ship;
	bool bBound = false;
	ON_SCOPE_EXIT
	{
		if (!bBound && !bRestoring)
		{
			Placement.ActiveShip = nullptr;
			Placement.bDormant = true;
			DestroyBundle(Ship);
		}
	};
	const auto Objects = EnemyShipBundlePool::GetObjects(*Ship, true);
	for (const FEnemyShipBundleObject& Saved : Placement.Objects)
	{
		UObject* const* Found = Objects.Find(Saved.Key);
		if (!Found)
		{
			if (Saved.Key.StartsWith(TEXT("SpawnedChest"))) continue;
			// Old definitions captured dynamically created GameplayTasks components as required.
			if (UClass* SavedClass = Saved.ClassPath.TryLoadClass<UActorComponent>())
				if (SavedClass->IsChildOf(UGameplayTasksComponent::StaticClass())) continue;
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
			// The old exporter omitted values identical to a null delta. Restore that zero value,
			// rather than attempting to parse an empty bool/number/name/array token.
			if (Text.IsEmpty())
			{
				Property->ClearValue(Property->ContainerPtrToValuePtr<void>(Target));
				continue;
			}
			if (!Property->ImportText_Direct(*Text, Property->ContainerPtrToValuePtr<void>(Target), Target, PPF_Delimited))
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
	bBound = true;
	UE_LOG(LogTemp, Display, TEXT("[EnemyShipPoolAudit] Side=Server Event=Activated Id=%s Ship=%s Actor=%s Assignment=%u Restoring=%d Hidden=%d Idle=%d RuntimePhysics=%d SimRoot=%d ActorTick=%d Position=%s Home=%s"),
		*Placement.StableId.ToString(), *Placement.AuthoredName.ToString(), *Ship->GetName(), Placement.Assignment, bRestoring,
		Ship->IsHidden(), Ship->IsBundlePoolIdle(), Ship->IsShipRuntimePhysicsEnabled(),
		Ship->BuoyancyRoot && Ship->BuoyancyRoot->IsSimulatingPhysics(), Ship->IsActorTickEnabled(),
		*Ship->GetActorLocation().ToString(), *Placement.HomeTransform.GetLocation().ToString());
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
	UE_LOG(LogTemp, Display, TEXT("[EnemyShipPoolAudit] Side=Server Event=Returned Id=%s Ship=%s Actor=%s Assignment=%u Retained=%d Hidden=%d Idle=%d RuntimePhysics=%d SimRoot=%d ActorTick=%d Position=%s Home=%s"),
		*Placement.StableId.ToString(), *Placement.AuthoredName.ToString(), *GetNameSafe(Ship), Placement.Assignment, IsValid(Ship) && IdleBundles.Contains(Ship),
		Ship->IsHidden(), Ship->IsBundlePoolIdle(), Ship->IsShipRuntimePhysicsEnabled(),
		Ship->BuoyancyRoot && Ship->BuoyancyRoot->IsSimulatingPhysics(), Ship->IsActorTickEnabled(),
		*Ship->GetActorLocation().ToString(), *Placement.HomeTransform.GetLocation().ToString());
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
	if (!HasAuthority()) { OnRep_Placements(); AuditClientPresentation(); return; }
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
				// A deterministic definition error must not spawn/destroy a bundle every 0.5 seconds.
				// Retry on the next approach, after the player has left the activation range.
				if (LastWakeErrors.Contains(Placement.StableId)) continue;
				FString Error;
				if (!BindPlacement(Placement, false, Error))
				{
					if (LastWakeErrors.FindRef(Placement.StableId) != Error)
						UE_LOG(LogTemp, Error, TEXT("[EnemyShipPool] Event=WakeFailed Ship=%s Reason=%s"), *Placement.AuthoredName.ToString(), *Error);
					LastWakeErrors.Add(Placement.StableId, Error);
				}
				else LastWakeErrors.Remove(Placement.StableId);
			}
			else LastWakeErrors.Remove(Placement.StableId);
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
	LogPoolCountsIfChanged();
}

void AEnemyShipBundlePool::LogPoolCountsIfChanged()
{
	if (!HasAuthority() || Placements.IsEmpty()) return;
	TMap<FGuid, uint8> States;
	TSet<TWeakObjectPtr<AEnemyShip>> Idle;
	int32 Active = 0, Dormant = 0, VisibleISM = 0, Destroyed = 0, Unloaded = 0;
	for (AEnemyShip* Ship : IdleBundles) if (IsValid(Ship)) Idle.Add(Ship);
	for (const auto& Placement : Placements)
	{
		const bool bActive = IsValid(Placement.ActiveShip) && !Placement.ActiveShip->IsBundlePoolIdle();
		const uint8 State = (bActive ? 1 : 0) | (Placement.bDormant ? 2 : 0)
			| (Placement.bDestroyed ? 4 : 0) | (Placement.bUnloaded ? 8 : 0) | (Placement.bStoryHidden ? 16 : 0);
		States.Add(Placement.StableId, State);
		if (Placement.bDestroyed) ++Destroyed;
		else if (Placement.bUnloaded) ++Unloaded;
		else if (bActive) ++Active;
		else if (Placement.bDormant)
		{
			++Dormant;
			if (!Placement.bStoryHidden && !Placement.Meshes.IsEmpty()) ++VisibleISM;
		}
	}
	bool bChanged = States.Num() != LastLoggedPlacementStates.Num() || Idle.Num() != LastLoggedIdleBundles.Num();
	for (const auto& Pair : States)
	{
		const uint8* Previous = LastLoggedPlacementStates.Find(Pair.Key);
		bChanged |= !Previous || *Previous != Pair.Value;
	}
	for (const auto& Ship : Idle) bChanged |= !LastLoggedIdleBundles.Contains(Ship);
	if (bLoggedPoolCounts && !bChanged) return;
	UE_LOG(LogTemp, Display, TEXT("[EnemyShipPool] Event=%s Total=%d IdlePool=%d Capacity=%d Active=%d Dormant=%d ISMShips=%d Destroyed=%d Unloaded=%d"),
		bLoggedPoolCounts ? TEXT("Changed") : TEXT("Initial"), Placements.Num(), Idle.Num(), IdleCapacity,
		Active, Dormant, VisibleISM, Destroyed, Unloaded);
	bLoggedPoolCounts = true;
	LastLoggedPlacementStates = MoveTemp(States);
	LastLoggedIdleBundles = MoveTemp(Idle);
}

void AEnemyShipBundlePool::AuditClientPresentation()
{
	if (HasAuthority()) return;
	TMap<FName, TArray<AEnemyShip*>> ActorsByName;
	for (TActorIterator<AEnemyShip> It(GetWorld()); It; ++It)
		ActorsByName.FindOrAdd(It->GetBundleAuthoredName()).Add(*It);
	const double Now = GetWorld()->GetTimeSeconds();
	for (const auto& Placement : Placements)
	{
		FEnemyShipPoolAuditSample& Previous = ClientAuditSamples.FindOrAdd(Placement.StableId);
		AEnemyShip* Ship = Placement.ActiveShip;
		const TArray<AEnemyShip*>* Matching = ActorsByName.Find(Placement.AuthoredName);
		if (!IsValid(Ship) && Matching && !Matching->IsEmpty()) Ship = (*Matching)[0];
		const bool bReassigned = Previous.PreviousActor.IsValid()
			&& Previous.PreviousActor->GetBundleAuthoredName() != Placement.AuthoredName;
		if (!IsValid(Ship) && !bReassigned) Ship = Previous.PreviousActor.Get();
		const bool bPhaseChanged = !Previous.bInitialized || Previous.Assignment != Placement.Assignment
			|| Previous.bDormant != Placement.bDormant;
		if (bPhaseChanged)
		{
			Previous.PhaseStartTime = Now;
			Previous.bDeferredSample = false;
			Previous.bMotionReported = false;
		}
		int32 ISMCount = 0, ISMTransformErrors = 0;
		if (const auto* Instances = AuditInstances.Find(Placement.StableId))
			for (const auto& Instance : *Instances)
			{
				FTransform Actual;
				if (Instance.Component.IsValid() && Instance.Component->GetInstanceTransform(Instance.Index, Actual, true))
				{
					++ISMCount;
					if (!Actual.Equals(Instance.ExpectedWorldTransform, 0.1f)) ++ISMTransformErrors;
				}
			}
		int32 VisibleMeshes = 0, SimBodies = 0, AwakeBodies = 0, BuoyancyActive = 0, BuoyancyTick = 0, ChildVisible = 0;
		FString VisibleExample, PhysicsExample;
		if (IsValid(Ship))
		{
			TArray<AActor*> Bundle;
			Ship->CollectBundleActors(Bundle);
			for (AActor* Actor : Bundle)
			{
				if (!IsValid(Actor)) continue;
				TInlineComponentArray<UActorComponent*> Components(Actor);
				for (UActorComponent* Component : Components)
				{
					if (Component->GetClass()->GetName().Contains(TEXT("Buoyancy")))
					{
						BuoyancyActive += Component->IsActive() ? 1 : 0;
						BuoyancyTick += Component->IsComponentTickEnabled() ? 1 : 0;
					}
					if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
					{
						if (!Actor->IsHidden() && Primitive->IsVisible() && !Primitive->bHiddenInGame)
						{
							++VisibleMeshes;
							if (Actor != Ship) ++ChildVisible;
							if (VisibleExample.IsEmpty()) VisibleExample = Primitive->GetPathName();
						}
						if (Primitive->IsSimulatingPhysics())
						{
							++SimBodies;
							AwakeBodies += Primitive->IsAnyRigidBodyAwake() ? 1 : 0;
							if (PhysicsExample.IsEmpty()) PhysicsExample = Primitive->GetPathName();
						}
					}
				}
			}
		}
		const bool bSameActor = IsValid(Ship) && Previous.PreviousActor.Get() == Ship;
		const float MoveCM = bSameActor ? FVector::Distance(Ship->GetActorLocation(), Previous.PreviousTransform.GetLocation()) : 0.f;
		const float RotateDeg = bSameActor ? FMath::RadiansToDegrees(Ship->GetActorQuat().AngularDistance(Previous.PreviousTransform.GetRotation())) : 0.f;
		const bool bUnexpectedMotion = !bPhaseChanged && Placement.bDormant && (MoveCM > 1.f || RotateDeg > 0.1f);
		uint32 Signature = GetTypeHash(Ship);
		for (int32 Value : {ISMCount, ISMTransformErrors, VisibleMeshes, SimBodies, AwakeBodies, BuoyancyActive, BuoyancyTick, ChildVisible,
			IsValid(Ship) ? static_cast<int32>(Ship->IsHidden()) : -1,
			IsValid(Ship) ? static_cast<int32>(Ship->IsBundlePoolIdle()) : -1,
			IsValid(Ship) ? static_cast<int32>(Ship->IsShipRuntimePhysicsEnabled()) : -1,
			IsValid(Ship) ? static_cast<int32>(Ship->IsActorTickEnabled()) : -1})
			Signature = HashCombine(Signature, GetTypeHash(Value));
		const bool bSettled = !Previous.bDeferredSample && Now - Previous.PhaseStartTime >= 2.0;
		if (bPhaseChanged || (Placement.bDormant && Signature != Previous.Signature) || bSettled || (bUnexpectedMotion && !Previous.bMotionReported))
		{
			UE_LOG(LogTemp, Display, TEXT("[EnemyShipPoolAudit] Side=Client Event=%s Id=%s Ship=%s Assignment=%u Dormant=%d Actor=%s ActorMatches=%d PreviousReassigned=%d Hidden=%d Idle=%d RuntimePhysics=%d ActorTick=%d Collision=%d VisiblePrimitives=%d ChildVisible=%d SimBodies=%d AwakeBodies=%d BuoyancyActive=%d BuoyancyTick=%d ISMInstances=%d ISMTransformErrors=%d DuplicateVisible=%d MoveCM=%.2f RotateDeg=%.3f Position=%s VisibleExample=%s PhysicsExample=%s"),
				bPhaseChanged ? TEXT("Transition") : bSettled ? TEXT("Settled2s") : bUnexpectedMotion && !Previous.bMotionReported ? TEXT("DormantMotion") : TEXT("StateChanged"),
				*Placement.StableId.ToString(), *Placement.AuthoredName.ToString(), Placement.Assignment, Placement.bDormant, *GetNameSafe(Ship),
				Matching ? Matching->Num() : 0, bReassigned, IsValid(Ship) ? Ship->IsHidden() : -1,
				IsValid(Ship) ? Ship->IsBundlePoolIdle() : -1, IsValid(Ship) ? Ship->IsShipRuntimePhysicsEnabled() : -1,
				IsValid(Ship) ? Ship->IsActorTickEnabled() : -1, IsValid(Ship) ? Ship->GetActorEnableCollision() : -1,
				VisibleMeshes, ChildVisible, SimBodies, AwakeBodies, BuoyancyActive, BuoyancyTick, ISMCount, ISMTransformErrors,
				ISMCount > 0 && VisibleMeshes > 0, MoveCM, RotateDeg, IsValid(Ship) ? *Ship->GetActorLocation().ToString() : TEXT("None"),
				*VisibleExample, *PhysicsExample);
		}
		Previous.bInitialized = true;
		Previous.Signature = Signature;
		Previous.Assignment = Placement.Assignment;
		Previous.bDormant = Placement.bDormant;
		Previous.bDeferredSample |= bSettled;
		Previous.bMotionReported |= bUnexpectedMotion;
		Previous.PreviousActor = Ship;
		if (IsValid(Ship)) Previous.PreviousTransform = Ship->GetActorTransform();
	}
}

void AEnemyShipBundlePool::OnRep_Placements()
{
	if (HasAuthority()) UpdatePresentation();
	else
	{
		// Subobject data and Actor references may resolve after the pointer-array notification.
		// The existing evaluation timer retries presentation until both are available.
		Placements.Reset();
		for (const UEnemyShipBundlePresentation* Node : PresentationNodes)
		{
			if (!Node || !Node->Definition.StableId.IsValid()) continue;
			FEnemyShipBundlePlacement& Placement = Placements.AddDefaulted_GetRef();
			Placement.StableId = Node->Definition.StableId;
			Placement.AuthoredName = Node->Definition.AuthoredName;
			Placement.HomeTransform = Node->Definition.HomeTransform;
			Placement.Meshes = Node->Definition.Meshes;
			Placement.Assignment = Node->State.Assignment;
			Placement.bDormant = Node->State.bDormant;
			Placement.bDestroyed = Node->State.bDestroyed;
			Placement.bUnloaded = Node->State.bUnloaded;
			Placement.bStoryHidden = Node->State.bStoryHidden;
			Placement.ActiveShip = Node->State.ActiveShip;
			for (const FEnemyShipBundleVisualObject& Visual : Node->Definition.Objects)
			{
				FEnemyShipBundleObject& Saved = Placement.Objects.AddDefaulted_GetRef();
				Saved.Key = Visual.Key;
				Saved.Properties = Visual.Properties;
			}
		}
	}
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
	AuditInstances.Reset();
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
				UMaterialInterface* Material = Cast<UMaterialInterface>(EnemyShipBundlePool::ISMMaterialPath(Path).TryLoad());
				Materials.Add(Material);
				Key += TEXT("|") + GetPathNameSafe(Material);
			}
			UInstancedStaticMeshComponent*& Batch = Batches.FindOrAdd(Key);
			if (!Batch)
			{
				int32 MissingMaterials = 0, MissingISMUsage = 0;
				for (int32 Index = 0; Index < Materials.Num(); ++Index)
				{
					UMaterialInterface* Material = Materials[Index];
					MissingMaterials += Material == nullptr ? 1 : 0;
					UMaterial* Base = Material ? Material->GetMaterial() : nullptr;
					MissingISMUsage += Base && !Base->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes) ? 1 : 0;
				}
				UE_LOG(LogTemp, Display, TEXT("[EnemyShipPoolMaterial] Event=BatchCreated Mesh=%s MeshSlots=%d CapturedSlots=%d MissingMaterials=%d MissingISMUsage=%d FirstMaterial=%s"),
					*Mesh->GetPathName(), Mesh->GetStaticMaterials().Num(), Materials.Num(), MissingMaterials, MissingISMUsage,
					Materials.IsEmpty() ? TEXT("None") : *GetPathNameSafe(Materials[0]));
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
			FEnemyShipPoolAuditInstance& Instance = AuditInstances.FindOrAdd(Placement.StableId).AddDefaulted_GetRef();
			Instance.Component = Batch;
			Instance.ExpectedWorldTransform = Visual.RelativeTransform * Placement.HomeTransform;
			Instance.Index = Batch->AddInstance(Instance.ExpectedWorldTransform, true);
		}
	}
	if (!HasAuthority() && !PresentationNodes.IsEmpty() && Placements.Num() == PresentationNodes.Num())
	{
		int32 Instances = 0, Active = 0, ISMShips = 0;
		for (const UInstancedStaticMeshComponent* Component : ProxyInstanceComponents)
			if (Component) Instances += Component->GetInstanceCount();
		for (const auto& Placement : Placements)
			if (IsValid(Placement.ActiveShip) && !Placement.ActiveShip->IsBundlePoolIdle()) ++Active;
			else if (!Placement.bDestroyed && !Placement.bUnloaded && !Placement.bStoryHidden && !Placement.Meshes.IsEmpty()) ++ISMShips;
		UE_LOG(LogTemp, Display, TEXT("[EnemyShipPoolClient] Event=Presentation Total=%d Active=%d ISMShips=%d MeshInstances=%d"),
			Placements.Num(), Active, ISMShips, Instances);
	}
}

void AEnemyShipBundlePool::CaptureRoomDomains(TArray<FSWRoomDomainPart>& Parts, TArray<FSWRoomCaptureIssue>& Issues) const
{
	// Discovery runs before the snapshot audit, never while its actor map is iterated.
	FEnemyShipBundlePoolState State;
	State.Placements = Placements;
	FSWRoomDomainPart& Part = Parts.AddDefaulted_GetRef();
	Part.Domain = ESWRoomDomain::EnemyShipPool;
	Part.Version = 2;
	TArray<uint8> Raw;
	bool bWritten = FSWRoomStructCodec::Write(State, Raw) && Raw.Num() > 0 && Raw.Num() <= EnemyShipBundlePool::MaxRawSaveBytes;
	if (bWritten)
	{
		int32 CompressedSize = FCompression::CompressMemoryBound(NAME_Zlib, Raw.Num());
		Part.Bytes.SetNumUninitialized(sizeof(int32) + CompressedSize);
		const int32 RawSize = Raw.Num();
		FMemory::Memcpy(Part.Bytes.GetData(), &RawSize, sizeof(RawSize));
		bWritten = FCompression::CompressMemory(NAME_Zlib, Part.Bytes.GetData() + sizeof(RawSize), CompressedSize,
			Raw.GetData(), RawSize);
		if (bWritten) Part.Bytes.SetNum(sizeof(RawSize) + CompressedSize);
		bWritten &= Part.Bytes.Num() <= EnemyShipBundlePool::MaxCompressedSaveBytes;
	}
	if (!bWritten)
	{
		UE_LOG(LogTemp, Error, TEXT("[EnemyShipPool] Event=SaveEncodingFailed RawBytes=%d EncodedBytes=%d Limit=%d"),
			Raw.Num(), Part.Bytes.Num(), EnemyShipBundlePool::MaxCompressedSaveBytes);
		Part.Bytes.Reset();
		FSWRoomCaptureIssue& Issue = Issues.AddDefaulted_GetRef();
		Issue.StableId = EnemyShipBundlePool::ManagerId;
		Issue.OwnerPath = GetPathName();
		Issue.ClassPath = FSoftClassPath(GetClass());
		Issue.Domain = TEXT("EnemyShipPool");
		Issue.FieldKey = TEXT("Placements");
		Issue.Reason = TEXT("Enemy ship pool definitions could not be serialized");
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("[EnemyShipPoolSave] Event=Encoded Ships=%d RawBytes=%d CompressedBytes=%d"),
			Placements.Num(), Raw.Num(), Part.Bytes.Num());
	}
}

bool AEnemyShipBundlePool::RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& Error)
{
	FEnemyShipBundlePoolState State;
	if (!EnemyShipBundlePool::ReadPoolState(Part, State))
	{ Error = TEXT("Invalid enemy ship pool state"); return false; }
	// An early legacy checkpoint stored no definitions yet; keep the level definitions
	// prepared by PrepareRestore instead of erasing every logical ship.
	if (State.Placements.IsEmpty() && !Placements.IsEmpty()) return true;
	TMap<FGuid, AEnemyShip*> Assigned;
	for (const auto& Placement : Placements) if (IsValid(Placement.ActiveShip)) Assigned.Add(Placement.StableId, Placement.ActiveShip);
	Placements = MoveTemp(State.Placements);
	for (auto& Placement : Placements) Placement.ActiveShip = Assigned.FindRef(Placement.StableId);
	return true;
}

bool AEnemyShipBundlePool::CompareRoomDomain(const FSWRoomDomainPart& Expected, const FSWRoomDomainPart& Actual,
	float Tolerance, TArray<FString>& Fields) const
{
	FEnemyShipBundlePoolState Before, After;
	if (!EnemyShipBundlePool::ReadPoolState(Expected, Before) || !EnemyShipBundlePool::ReadPoolState(Actual, After))
	{ Fields.Add(TEXT("Enemy ship pool payload is invalid")); return false; }
	FSWRoomStructCodec::CompareSaveGameStruct(FEnemyShipBundlePoolState::StaticStruct(), &Before, &After, Tolerance, Fields);
	return Fields.IsEmpty();
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
	if (!EnemyShipBundlePool::ReadPoolState(*Part, State)) { Error = TEXT("Enemy ship pool restore definitions invalid"); return; }
	if (State.Placements.IsEmpty() && !Placements.IsEmpty())
	{
		for (auto& Placement : Placements)
			if (Snapshot.DestroyedLevelActorIds.Contains(Placement.StableId))
			{
				if (IsValid(Placement.ActiveShip)) ReleasePlacement(Placement);
				Placement.bDestroyed = true;
			}
			else if (Snapshot.Actors.ContainsByPredicate([&](const FSWRoomActorRecord& Record) { return Record.StableId == Placement.StableId; }))
			{
				if (!BindPlacement(Placement, true, Error)) return;
			}
			else if (IsValid(Placement.ActiveShip)) ReleasePlacement(Placement);
		return;
	}
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
