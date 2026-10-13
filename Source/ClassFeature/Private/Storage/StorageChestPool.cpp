#include "Storage/StorageChestPool.h"

#include "Storage/StorageChest.h"
#include "Storage/SharedStorageChest.h"
#include "ItemSpawn/LootSpawnPoint.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/ActorChannel.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/DataBunch.h"
#include "Net/UnrealNetwork.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "Room/SWRoomSnapshotSubsystem.h"
#include "Rendering/SWEnemyShipISMMaterialPath.h"
#include "Misc/PackageName.h"
#include "Ship.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

namespace StorageChestPool
{
const FName IdleTag(TEXT("SWStorageChestPoolIdle"));
const FName ReleasedTag(TEXT("SWStorageChestPoolReleased"));
const FName ActorKey(TEXT("Actor"));

FSoftObjectPath ISMMaterialPath(const FSoftObjectPath& Original)
{
	if (Original.IsNull()) return Original;
	const FSoftObjectPath Variant(SWEnemyShipISMMaterials::ObjectPath(Original.ToString()));
	return FPackageName::DoesPackageExist(Variant.GetLongPackageName()) ? Variant : Original;
}

TMap<FName, UObject*> Objects(AStorageChest& Chest)
{
	TMap<FName, UObject*> Result;
	Result.Add(ActorKey, &Chest);
	TInlineComponentArray<UActorComponent*> Components(&Chest);
	for (UActorComponent* Component : Components)
		if (Component && !Component->IsA<USWRoomSnapshotComponent>() && Component->CreationMethod != EComponentCreationMethod::Instance)
			Result.Add(Component->GetFName(), Component);
	return Result;
}

bool IsConfiguration(const FProperty& Property)
{
	const FProperty* ValueProperty = &Property;
	if (const auto* Array = CastField<FArrayProperty>(ValueProperty)) ValueProperty = Array->Inner;
	if (const auto* Object = CastField<FObjectPropertyBase>(ValueProperty))
		if (Object->PropertyClass->IsChildOf(AActor::StaticClass()) || Object->PropertyClass->IsChildOf(UActorComponent::StaticClass())) return false;
	return Property.HasAnyPropertyFlags(CPF_Edit)
		&& !Property.HasAnyPropertyFlags(CPF_EditConst | CPF_Transient | CPF_EditorOnly | CPF_InstancedReference | CPF_ContainsInstancedReference)
		&& Property.GetFName() != TEXT("RelativeLocation") && Property.GetFName() != TEXT("RelativeRotation")
		&& Property.GetFName() != TEXT("RelativeScale3D");
}

void Release(AStorageChest& Chest)
{
	if (auto* Room = Chest.GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>()) Room->ReleaseActorForPooling(&Chest);
}

void ApplyMeshes(AStorageChest& Chest, const TArray<FStorageChestPoolMesh>& Meshes)
{
	const auto Objects = StorageChestPool::Objects(Chest);
	for (const auto& Visual : Meshes)
		if (auto* Mesh = Cast<UStaticMeshComponent>(Objects.FindRef(Visual.Component)))
		{
			Mesh->SetStaticMesh(Cast<UStaticMesh>(Visual.Mesh.TryLoad()));
			if (Mesh != Chest.GetRootComponent()) Mesh->SetRelativeTransform(Visual.RelativeTransform);
			Mesh->EmptyOverrideMaterials();
			for (int32 Index = 0; Index < Visual.Materials.Num(); ++Index)
				Mesh->SetMaterial(Index, Cast<UMaterialInterface>(Visual.Materials[Index].TryLoad()));
		}
}

bool ApplyConfiguration(AStorageChest& Chest, const FStorageChestPoolPlacement& Placement)
{
	const auto Objects = StorageChestPool::Objects(Chest);
	for (const auto& Value : Placement.Properties)
	{
		UObject* Object = Objects.FindRef(Value.Object);
		FProperty* Property = Object ? Object->GetClass()->FindPropertyByName(Value.Name) : nullptr;
		if (!Property || !IsConfiguration(*Property)) return false;
		void* Address = Property->ContainerPtrToValuePtr<void>(Object);
		if (Value.Value.IsEmpty()) { Property->ClearValue(Address); continue; }
		if (!Property->ImportText_Direct(*Value.Value, Address, Object, PPF_Delimited)) return false;
	}
	ApplyMeshes(Chest, Placement.Meshes);
	return true;
}
}

AStorageChestPool::AStorageChestPool()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	bReplicateUsingRegisteredSubObjectList = false;
	SetReplicateMovement(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Tags.Add(TEXT("RoomDerived"));
}

void UStorageChestPoolSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	if (World.IsGameWorld() && World.GetNetMode() != NM_Client) Pool = World.SpawnActor<AStorageChestPool>();
}

void AStorageChestPool::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(EvaluationTimer, this, &AStorageChestPool::Evaluate, 0.5f, true);
		if (auto* Room = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>())
		{
			PrepareRestoreHandle = Room->OnPrepareRestore.AddUObject(this, &AStorageChestPool::PrepareRestore);
			RestoreCompletedHandle = Room->OnRestoreCompleted.AddUObject(this, &AStorageChestPool::RestoreCompleted);
		}
	}
}

void AStorageChestPool::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(EvaluationTimer);
	if (auto* Room = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>())
	{
		Room->OnPrepareRestore.Remove(PrepareRestoreHandle);
		Room->OnRestoreCompleted.Remove(RestoreCompletedHandle);
	}
	Super::EndPlay(Reason);
}

void AStorageChestPool::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AStorageChestPool, Presentations);
}

void UStorageChestPoolPresentation::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UStorageChestPoolPresentation, StableId);
	DOREPLIFETIME(UStorageChestPoolPresentation, Transform);
	DOREPLIFETIME(UStorageChestPoolPresentation, Meshes);
	DOREPLIFETIME(UStorageChestPoolPresentation, bDormant);
	DOREPLIFETIME(UStorageChestPoolPresentation, ActiveChest);
}

bool AStorageChestPool::ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch, FReplicationFlags* RepFlags)
{
	bool bWrote = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);
	bool bIntroduced = false;
	for (UStorageChestPoolPresentation* Node : Presentations)
	{
		if (!Node) continue;
		const bool bNew = !Channel->ReplicationMap.Contains(Node);
		if (bNew && bIntroduced) continue;
		if (Bunch->GetNumBytes() >= 16 * 1024) break;
		bWrote |= Channel->ReplicateSubobject(Node, *Bunch, *RepFlags);
		bIntroduced |= bNew;
	}
	return bWrote;
}

bool AStorageChestPool::IsEligible(const AChestSpawnPoint& Point, const AStorageChest* Chest)
{
	return Point.IsActivated() && Point.GetSpawnMode() == EChestSpawnMode::Random
		&& Point.GetEnvironment() != EChestEnvironment::ShipDeck && !Point.GetOwningShip()
		&& !Point.GetAttachParentActor() && !Point.HasAuthoredGuards()
		&& (!Chest || (!Chest->IsA<ASharedStorageChest>() && !Chest->IsLocked() && !Chest->RequiresGuardClear()
			&& !Chest->HasBossGuard() && !Chest->IsBossChest() && !Chest->IsBossEncounterReserved()
			&& Chest->GetAliveGuardCount() == 0 && !Chest->GetOwningShip() && !Chest->GetAttachParentActor()));
}

bool AStorageChestPool::CaptureChest(AStorageChest& Chest, FStorageChestPoolPlacement& Placement, FString& Error)
{
	auto* Snapshot = Chest.FindComponentByClass<USWRoomSnapshotComponent>();
	if (!Snapshot || !Snapshot->StableId.IsValid()) { Error = TEXT("Chest identity unavailable"); return false; }
	Placement.StableId = Snapshot->StableId;
	Placement.CreatorId = Snapshot->GetCreatorId();
	Placement.CreatorSequence = Snapshot->GetCreatorSequence();
	Placement.Persistence = Snapshot->PersistenceClass;
	Placement.ClassPath = FSoftClassPath(Chest.GetClass());
	Placement.Transform = Chest.GetActorTransform();
	Placement.Tags = Chest.Tags;
	Placement.ActivationRange = Chest.GetDistanceOptimizationRange();
	Placement.Properties.Reset();
	Placement.Meshes.Reset();
	const auto Objects = StorageChestPool::Objects(Chest);
	TArray<FName> Keys;
	Objects.GetKeys(Keys);
	Keys.Sort(FNameLexicalLess());
	for (const FName Key : Keys)
	{
		UObject* Object = Objects.FindChecked(Key);
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			if (Object->IsA<UStaticMeshComponent>()) continue;
			if (!StorageChestPool::IsConfiguration(**It)) continue;
			FStorageChestPoolProperty& Value = Placement.Properties.AddDefaulted_GetRef();
			Value.Object = Key;
			Value.Name = It->GetFName();
			It->ExportTextItem_Direct(Value.Value, It->ContainerPtrToValuePtr<void>(Object), nullptr, Object, PPF_Delimited);
		}
		if (auto* Mesh = Cast<UStaticMeshComponent>(Object); Mesh && Mesh->GetStaticMesh() && Mesh->IsVisible() && !Mesh->bHiddenInGame)
		{
			auto& Visual = Placement.Meshes.AddDefaulted_GetRef();
			Visual.Component = Key;
			Visual.Mesh = FSoftObjectPath(Mesh->GetStaticMesh());
			Visual.RelativeTransform = Mesh->GetComponentTransform().GetRelativeTransform(Placement.Transform);
			Visual.bCastShadow = Mesh->CastShadow;
			for (int32 Index = 0; Index < Mesh->GetNumMaterials(); ++Index)
			{
				UMaterialInterface* Material = Mesh->GetMaterial(Index);
				if (auto* Dynamic = Cast<UMaterialInstanceDynamic>(Material)) Material = Dynamic->Parent;
				Visual.Materials.Add(FSoftObjectPath(Material));
			}
		}
	}
	TArray<FSWRoomDomainPart> Parts;
	TArray<FSWRoomCaptureIssue> Issues;
	Chest.CaptureRoomDomains(Parts, Issues);
	if (!Issues.IsEmpty() || Parts.Num() != 1) { Error = TEXT("Chest state capture failed"); return false; }
	Placement.ChestState = Parts[0];
	return true;
}

bool AStorageChestPool::RestoreChestConfiguration(AStorageChest& Chest, const FStorageChestPoolPlacement& Placement, FString& Error)
{
	if (Chest.GetClass()->GetPathName() != Placement.ClassPath.ToString() || !StorageChestPool::ApplyConfiguration(Chest, Placement))
	{
		Error = TEXT("Chest saved class or placement configuration is incompatible");
		return false;
	}
	return true;
}

void AStorageChestPool::Discover()
{
	Points.RemoveAll([](const auto& Point) { return !Point.IsValid(); });
	IdleChests.RemoveAll([](const auto& Chest) { return !IsValid(Chest); });
	for (TActorIterator<AChestSpawnPoint> It(GetWorld()); It; ++It)
	{
		AChestSpawnPoint* Point = *It;
		AStorageChest* Chest = Cast<AStorageChest>(Point->GetSpawnedActor());
		if (!IsEligible(*Point, Chest)) continue;
		auto& Placement = Point->GetPooledChest();
		if (!Placement.StableId.IsValid() && IsValid(Chest) && Chest->HasActorBegunPlay())
		{
			FString Error;
			if (!CaptureChest(*Chest, Placement, Error)) continue;
			// This pool replaces the old physics-only distance sleep.
			Chest->SetDistanceOptimizationEnabled(false);
			Chest->SetPoolAssignment(Placement.StableId, Placement.Transform);
		}
		if (Placement.StableId.IsValid()) Points.AddUnique(Point);
	}
}

bool AStorageChestPool::IsNear(const FVector& Location, float Range) const
{
	const double RangeSquared = FMath::Square(FMath::Max(0.f, Range));
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		if (const APlayerController* Controller = It->Get())
			if (const APawn* Pawn = Controller->GetPawn(); IsValid(Pawn) && FVector::DistSquared2D(Location, Pawn->GetActorLocation()) <= RangeSquared) return true;
	for (TActorIterator<AShip> It(GetWorld()); It; ++It)
		if (!It->IsEnemyShipForEffects() && It->ActorHasTag(TEXT("Player")) && !It->ActorHasTag(TEXT("Enemy"))
			&& FVector::DistSquared2D(Location, It->GetActorLocation()) <= RangeSquared) return true;
	return false;
}

bool AStorageChestPool::MaterializeForLootRebalance()
{
	if (!HasAuthority()) return false;
	Discover();
	PendingPoints.Reset();
	for (const auto& Weak : Points)
		if (AChestSpawnPoint* Point = Weak.Get(); Point && Point->GetPooledChest().bDormant)
		{
			FString Error;
			if (!Wake(*Point, Error))
			{
				UE_LOG(LogTemp, Error, TEXT("[StorageChestPool] Loot rebalance could not restore %s: %s"), *Point->GetName(), *Error);
				return false;
			}
		}
	Publish();
	return true;
}

void AStorageChestPool::Evaluate()
{
	if (!HasAuthority() || bPreparingRestore || GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>()->IsRestoringSnapshot()) return;
	Discover();
	int32 ActiveCount = 0;
	for (const auto& Weak : Points)
	{
		AChestSpawnPoint* Point = Weak.Get();
		if (!Point) continue;
		auto& Placement = Point->GetPooledChest();
		AStorageChest* Chest = Cast<AStorageChest>(Point->GetSpawnedActor());
		if (!Placement.StableId.IsValid()) continue;
		if (!IsEligible(*Point, Chest)) { ForgetPoint(Point); break; }
		if (!Placement.bDormant && !IsValid(Chest)) { Point->GetPooledChest() = FStorageChestPoolPlacement(); continue; }
		if (IsValid(Chest)) ++ActiveCount;
		const FVector Location = IsValid(Chest) ? Chest->GetActorLocation() : Placement.Transform.GetLocation();
		const bool bNear = IsNear(Location, Placement.ActivationRange + (Placement.bDormant ? 0.f : 10000.f));
		if (Placement.bDormant && bNear)
		{
			if (!PendingPoints.Contains(Point)) PendingPoints.Insert(Point, 0);
		}
		else if (!Placement.bDormant && !bNear && Chest->CanReturnToPool())
		{
			double& Since = SettledSince.FindOrAdd(Placement.StableId, GetWorld()->GetTimeSeconds());
			if (GetWorld()->GetTimeSeconds() - Since >= 2.0) PendingPoints.AddUnique(Point);
		}
		else SettledSince.Remove(Placement.StableId);
	}
	const int32 IdleTarget = FMath::Max(0, PoolCapacity - ActiveCount);
	while (IdleChests.Num() > IdleTarget)
	{
		AStorageChest* Chest = IdleChests.Pop();
		Chest->Tags.AddUnique(StorageChestPool::ReleasedTag);
		Chest->Destroy();
	}
	// Allocate one reserve per evaluation, avoiding an initial burst of BP construction.
	if (IdleChests.Num() < IdleTarget)
	{
		AChestSpawnPoint* ReservePoint = nullptr;
		int32 FewestReserves = MAX_int32;
		for (const auto& Weak : Points)
			if (AChestSpawnPoint* Point = Weak.Get(); Point && Point->GetPooledChest().StableId.IsValid())
			{
				int32 Count = 0;
				for (AStorageChest* Chest : IdleChests)
					if (Chest->GetClass()->GetPathName() == Point->GetPooledChest().ClassPath.ToString() && Chest->GetLevel() == Point->GetLevel()) ++Count;
				if (Count < FewestReserves) { FewestReserves = Count; ReservePoint = Point; }
			}
		if (ReservePoint)
			if (AStorageChest* Chest = Allocate(*ReservePoint, ReservePoint->GetPooledChest())) IdleChests.Add(Chest);
	}
	Publish();
}

AStorageChest* AStorageChestPool::Allocate(AChestSpawnPoint& Point, const FStorageChestPoolPlacement& Placement)
{
	UClass* Class = Placement.ClassPath.TryLoadClass<AStorageChest>();
	if (!Class) return nullptr;
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.OverrideLevel = Point.GetLevel();
	Params.bDeferConstruction = true;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStorageChest* Chest = GetWorld()->SpawnActor<AStorageChest>(Class, Placement.Transform, Params);
	if (!Chest) return nullptr;
	Chest->Tags.AddUnique(StorageChestPool::IdleTag);
	Chest->ClearLegacyChestDefinition();
	if (auto* Snapshot = Chest->FindComponentByClass<USWRoomSnapshotComponent>()) Snapshot->PersistenceClass = ESWRoomPersistenceClass::Transient;
	Chest->SetPoolAssignment(FGuid(), Placement.Transform);
	Chest->FinishSpawning(Placement.Transform);
	Park(*Chest);
	return Chest;
}

void AStorageChestPool::Park(AStorageChest& Chest)
{
	StorageChestPool::Release(Chest);
	Chest.SetDistanceOptimizationEnabled(false);
	Chest.SetPhysicsAndBuoyancyEnabled(false);
	Chest.ResetForNewEncounter();
	Chest.SetOwner(this);
	Chest.Tags.AddUnique(StorageChestPool::IdleTag);
	if (auto* Snapshot = Chest.FindComponentByClass<USWRoomSnapshotComponent>()) Snapshot->PersistenceClass = ESWRoomPersistenceClass::Transient;
	Chest.SetPoolAssignment(FGuid(), Chest.GetActorTransform());
}

bool AStorageChestPool::Sleep(AChestSpawnPoint& Point, FString& Error)
{
	AStorageChest* Chest = Cast<AStorageChest>(Point.GetSpawnedActor());
	if (!IsValid(Chest) || !IsEligible(Point, Chest) || !Chest->CanReturnToPool()) return true;
	if (!CaptureChest(*Chest, Point.GetPooledChest(), Error)) return false;
	Point.GetPooledChest().bDormant = true;
	Point.SetPooledChestActor(nullptr);
	Park(*Chest);
	IdleChests.AddUnique(Chest);
	return true;
}

bool AStorageChestPool::Wake(AChestSpawnPoint& Point, FString& Error)
{
	auto& Placement = Point.GetPooledChest();
	AStorageChest* Chest = nullptr;
	for (int32 Index = 0; Index < IdleChests.Num(); ++Index)
		if (IdleChests[Index]->GetClass()->GetPathName() == Placement.ClassPath.ToString() && IdleChests[Index]->GetLevel() == Point.GetLevel())
		{ Chest = IdleChests[Index]; IdleChests.RemoveAt(Index); break; }
	if (!Chest) Chest = Allocate(Point, Placement);
	if (!Chest) { Error = TEXT("Chest allocation failed"); return false; }
	Chest->ResetForNewEncounter();
	const bool bConfigured = StorageChestPool::ApplyConfiguration(*Chest, Placement);
	if (!bConfigured || !Chest->RestoreRoomDomain(Placement.ChestState, Error)
		|| !Chest->FinalizeRoomRestore(TMap<FGuid, AActor*>(), Error))
	{
		if (Error.IsEmpty()) Error = TEXT("Chest placement configuration failed");
		Park(*Chest);
		IdleChests.Add(Chest);
		return false;
	}
	Chest->SetDistanceOptimizationEnabled(false);
	Chest->SetActorTransform(Placement.Transform, false, nullptr, ETeleportType::TeleportPhysics);
	Chest->Tags = Placement.Tags;
	Chest->Tags.Remove(StorageChestPool::IdleTag);
	Chest->Tags.Remove(StorageChestPool::ReleasedTag);
	Chest->SetOwner(&Point);
	auto* Identity = Chest->FindComponentByClass<USWRoomSnapshotComponent>();
	const FGuid PreviousId = Identity->StableId;
	Identity->SetRuntimeId(Placement.StableId);
	Identity->SetRuntimeOrigin(Placement.CreatorId, Placement.CreatorSequence);
	Identity->PersistenceClass = Placement.Persistence;
	if (auto* Room = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>())
	{
		Room->ClearPooledActorTombstone(Placement.StableId);
		Room->UpdateRegisteredActorId(Chest, PreviousId, Placement.StableId);
	}
	Chest->SetPoolAssignment(Placement.StableId, Placement.Transform);
	Point.SetPooledChestActor(Chest);
	Placement.bDormant = false;
	SettledSince.Remove(Placement.StableId);
	return true;
}

void AStorageChestPool::ForgetPoint(AChestSpawnPoint* Point)
{
	if (!Point) return;
	Points.Remove(Point);
	PendingPoints.Remove(Point);
	SettledSince.Remove(Point->GetPooledChest().StableId);
	Point->GetPooledChest() = FStorageChestPoolPlacement();
	Publish();
}

void AStorageChestPool::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// One Actor transition per frame; discovery and capacity maintenance remain at 2 Hz.
	if (HasAuthority() && !bPreparingRestore && !GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>()->IsRestoringSnapshot() && !PendingPoints.IsEmpty())
	{
		AChestSpawnPoint* Point = PendingPoints[0].Get();
		PendingPoints.RemoveAt(0);
		if (Point && Point->GetPooledChest().StableId.IsValid() && IsEligible(*Point, Cast<AStorageChest>(Point->GetSpawnedActor())))
		{
			auto& Placement = Point->GetPooledChest();
			AStorageChest* Chest = Cast<AStorageChest>(Point->GetSpawnedActor());
			const FVector Location = Chest ? Chest->GetActorLocation() : Placement.Transform.GetLocation();
			const bool bNear = IsNear(Location, Placement.ActivationRange + (Placement.bDormant ? 0.f : 10000.f));
			FString Error;
			if (Placement.bDormant && bNear) Wake(*Point, Error);
			else if (!Placement.bDormant && !bNear) Sleep(*Point, Error);
			if (!Error.IsEmpty()) UE_LOG(LogTemp, Warning, TEXT("[StorageChestPool] Point=%s Error=%s"), *Point->GetName(), *Error);
			Publish();
		}
	}
	if (GetNetMode() != NM_DedicatedServer) RenderPresentation();
}

void AStorageChestPool::Publish()
{
	TSet<FGuid> Live;
	for (const auto& Weak : Points)
		if (AChestSpawnPoint* Point = Weak.Get())
		{
			const auto& Placement = Point->GetPooledChest();
			if (!Placement.StableId.IsValid()) continue;
			Live.Add(Placement.StableId);
			UStorageChestPoolPresentation* Node = nullptr;
			for (UStorageChestPoolPresentation* Candidate : Presentations) if (Candidate && Candidate->StableId == Placement.StableId) { Node = Candidate; break; }
			if (!Node)
			{
				Node = NewObject<UStorageChestPoolPresentation>(this);
				Node->StableId = Placement.StableId;
				Presentations.Add(Node);
			}
			Node->Meshes = Placement.Meshes;
			Node->Transform = Placement.Transform;
			Node->bDormant = Placement.bDormant;
			Node->ActiveChest = Cast<AStorageChest>(Point->GetSpawnedActor());
		}
	Presentations.RemoveAll([&](const auto& Node) { return !Node || !Live.Contains(Node->StableId); });
	ForceNetUpdate();
}

void AStorageChestPool::RenderPresentation()
{
	uint32 Hash = 1;
	for (const UStorageChestPoolPresentation* Node : Presentations)
	{
		if (!Node) continue;
		const bool bReady = IsValid(Node->ActiveChest) && Node->ActiveChest->HasActorBegunPlay()
			&& Node->ActiveChest->GetPoolAssignment().StableId == Node->StableId && !Node->ActiveChest->IsPoolIdle();
		if (bReady) Node->ActiveChest->SetActorHiddenInGame(Node->bDormant);
		Hash = HashCombine(Hash, GetTypeHash(Node->StableId));
		Hash = HashCombine(Hash, GetTypeHash(Node->bDormant || !bReady));
		Hash = HashCombine(Hash, GetTypeHash(Node->ActiveChest.Get()));
		Hash = HashCombine(Hash, GetTypeHash(Node->Meshes.Num()));
		Hash = HashCombine(Hash, GetTypeHash(Node->Transform.GetLocation()));
		Hash = HashCombine(Hash, GetTypeHash(Node->Transform.GetScale3D()));
		Hash = HashCombine(Hash, GetTypeHash(Node->Transform.Rotator().Yaw));
		for (const auto& Mesh : Node->Meshes)
		{
			Hash = HashCombine(Hash, GetTypeHash(Mesh.Mesh));
			for (const auto& Material : Mesh.Materials) Hash = HashCombine(Hash, GetTypeHash(Material));
		}
	}
	if (Hash == PresentationHash) return;
	PresentationHash = Hash;
	for (UInstancedStaticMeshComponent* Component : ProxyInstanceComponents) if (Component) Component->ClearInstances();
	TMap<FString, UInstancedStaticMeshComponent*> Groups;
	int32 GroupIndex = 0;
	for (const UStorageChestPoolPresentation* Node : Presentations)
	{
		if (!Node) continue;
		const bool bReady = IsValid(Node->ActiveChest) && Node->ActiveChest->HasActorBegunPlay()
			&& Node->ActiveChest->GetPoolAssignment().StableId == Node->StableId && !Node->ActiveChest->IsPoolIdle();
		// Keep the proxy until the new Actor identity has arrived on this client.
		if (!Node->bDormant && bReady)
		{
			StorageChestPool::ApplyMeshes(*Node->ActiveChest, Node->Meshes);
			continue;
		}
		for (const auto& Mesh : Node->Meshes)
		{
			FString Key = Mesh.Mesh.ToString() + FString::FromInt(Mesh.bCastShadow);
			for (const auto& Material : Mesh.Materials) Key += TEXT("|") + Material.ToString();
			UInstancedStaticMeshComponent* Component = Groups.FindRef(Key);
			if (!Component)
			{
				if (ProxyInstanceComponents.IsValidIndex(GroupIndex)) Component = ProxyInstanceComponents[GroupIndex];
				else
				{
					Component = NewObject<UInstancedStaticMeshComponent>(this);
					Component->SetupAttachment(RootComponent);
					Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					Component->SetCanEverAffectNavigation(false);
					Component->RegisterComponent();
					ProxyInstanceComponents.Add(Component);
				}
				++GroupIndex;
				Component->SetStaticMesh(Cast<UStaticMesh>(Mesh.Mesh.TryLoad()));
				Component->EmptyOverrideMaterials();
				Component->SetCastShadow(Mesh.bCastShadow);
				for (int32 Index = 0; Index < Mesh.Materials.Num(); ++Index)
					Component->SetMaterial(Index, Cast<UMaterialInterface>(StorageChestPool::ISMMaterialPath(Mesh.Materials[Index]).TryLoad()));
				Groups.Add(Key, Component);
			}
			Component->AddInstance(Mesh.RelativeTransform * Node->Transform, true);
		}
	}
}

void AStorageChestPool::PrepareRestore(const FSWRoomWorldSnapshot& Snapshot, FString& Error)
{
	// The point's Loot domain restores the virtual placement; active chests use their normal Actor records.
	bPreparingRestore = true;
	PendingPoints.Reset();
	SettledSince.Reset();
	Points.Reset();
	Presentations.Reset();
	for (AStorageChest* Chest : IdleChests) if (IsValid(Chest)) { StorageChestPool::Release(*Chest); Chest->Destroy(); }
	IdleChests.Reset();
}

void AStorageChestPool::RestoreCompleted()
{
	bPreparingRestore = false;
	Discover();
	for (const auto& Weak : Points)
		if (auto* Point = Weak.Get())
			if (auto* Chest = Cast<AStorageChest>(Point->GetSpawnedActor()))
			{
				Chest->SetDistanceOptimizationEnabled(false);
				Chest->SetPoolAssignment(Point->GetPooledChest().StableId, Chest->GetActorTransform());
			}
	Publish();
}

