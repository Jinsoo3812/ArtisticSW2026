#include "ShipAI/EnemyShipBundlePool.h"
#include "ShipAI/EnemyShipDiagnostics.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipArchetypeData.h"
#include "ShipAI/EnemyShipNavigationComponent.h"
#include "ShipAI/ShipSwarmSubsystem.h"
#include "ShipAI/EnemyShipWeakeningWorldSubsystem.h"
#include "DeckAI/DeckEnemySpawnerComponent.h"
#include "BossAI/BossEncounterComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/ActorChannel.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
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
const FSoftClassPath CommonClass(TEXT("/Game/Blueprints/Ship/Enemy_Ship/Blueprints/BP_EnemyShip.BP_EnemyShip_C"));

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

/** All supported bundles share their authored geometry. Assignment imports gameplay only. */
bool IsFixedBundleConfiguration(const UObject& Target, const FProperty& Property)
{
	if (Target.IsA<UPrimitiveComponent>() || Target.IsA<UDeckWalkAreaComponent>() || Target.IsA<UDeckWaypointComponent>()) return true;
	if (Target.IsA<USceneComponent>())
	{
		const FName Name = Property.GetFName();
		if (Name == TEXT("RelativeLocation") || Name == TEXT("RelativeRotation") || Name == TEXT("RelativeScale3D")
			|| Name == TEXT("AttachParent") || Name == TEXT("AttachSocketName") || Name == TEXT("Mobility")
			|| Name == TEXT("bAbsoluteLocation") || Name == TEXT("bAbsoluteRotation") || Name == TEXT("bAbsoluteScale")
			|| Name == TEXT("bVisible") || Name == TEXT("bHiddenInGame")) return true;
	}
	const FProperty* ValueProperty = &Property;
	if (const FArrayProperty* Array = CastField<FArrayProperty>(ValueProperty)) ValueProperty = Array->Inner;
	if (const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(ValueProperty))
		return Object->PropertyClass && (Object->PropertyClass->IsChildOf(USceneComponent::StaticClass())
			|| Object->PropertyClass->IsChildOf(UDeckWalkAreaComponent::StaticClass()));
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

void RemapReference(FString& Text, const FEnemyShipBundleObject& Reference, UObject& Target)
{
	if (Reference.SourcePath.IsEmpty()) return;
	// An Actor reference can carry its original derived class as well as its path.
	// Rewrite the complete reference before changing the path to the common BP Actor.
	const FString TargetReference = Target.GetClass()->GetPathName() + TEXT("'") + Target.GetPathName() + TEXT("'");
	Text.ReplaceInline(*(Reference.ClassPath.ToString() + TEXT("'") + Reference.SourcePath + TEXT("'")), *TargetReference, ESearchCase::CaseSensitive);
	Text.ReplaceInline(*(Reference.ClassPath.GetAssetName() + TEXT("'") + Reference.SourcePath + TEXT("'")), *TargetReference, ESearchCase::CaseSensitive);
	Text.ReplaceInline(*Reference.SourcePath, *Target.GetPathName(), ESearchCase::CaseSensitive);
}

void AuditProfile(AEnemyShip& Ship, const FEnemyShipBundlePlacement& Placement, bool bGameplayConfig)
{
	const auto Objects = GetObjects(Ship);
	TArray<const FEnemyShipBundleObject*> References;
	for (const auto& Saved : Placement.Objects) References.Add(&Saved);
	References.Sort([](const FEnemyShipBundleObject& A, const FEnemyShipBundleObject& B) { return A.SourcePath.Len() > B.SourcePath.Len(); });
	int32 Checked = 0, Mismatches = 0;
	for (const auto& Saved : Placement.Objects)
	{
		UObject* const* Found = Objects.Find(Saved.Key);
		if (!Found) continue;
		for (const auto& Value : Saved.Properties)
		{
			// Encounter reset intentionally changes runtime/presentation fields. Compare
			// these stable authoring fields again after initialization and activation.
			const bool bSelected = Value.Name == TEXT("SquadID") || Value.Name == TEXT("NormalFallbackArchetype")
				|| Value.Name == TEXT("DeckChestSettings") || Value.Name == TEXT("ReturnPointActor") || Value.Name == TEXT("ReturnArrivalOffset")
				|| Value.Name == TEXT("SpawnPlan") || Value.Name == TEXT("bEnableSpawning") || Value.Name == TEXT("SpawnStartDelay")
				|| Value.Name == TEXT("Surfaces") || Value.Name == TEXT("ObstacleComponentNames")
				|| Value.Name == TEXT("BossClass") || Value.Name == TEXT("BossSpawnPointId") || Value.Name == TEXT("bEncounterEnabled");
			if (bGameplayConfig && !bSelected) continue;
			FProperty* Property = (*Found)->GetClass()->FindPropertyByName(Value.Name);
			if (!Property) continue;
			FString Text = Value.Value;
			for (const auto* Reference : References)
				if (UObject* const* Target = Objects.Find(Reference->Key)) RemapReference(Text, *Reference, **Target);
			void* Expected = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
			Property->InitializeValue(Expected);
			const bool bParsed = Text.IsEmpty() || Property->ImportText_Direct(*Text, Expected, *Found, PPF_Delimited) != nullptr;
			const void* Actual = Property->ContainerPtrToValuePtr<void>(*Found);
			const bool bMatch = bParsed && Property->Identical(Expected, Actual, PPF_None);
			++Checked;
			if (!bMatch)
			{
				++Mismatches;
				FString ActualText;
				Property->ExportTextItem_Direct(ActualText, Actual, nullptr, *Found, PPF_Delimited);
				/* Diagnostics muted after pooling/crew verification.
				UE_LOG(LogTemp, Warning, TEXT("[EnemyShipPool] Event=ProfileMismatch Scope=%s Id=%s Actor=%s Object=%s Field=%s Expected=%s Actual=%s"),
					bGameplayConfig ? TEXT("EncounterConfig") : TEXT("ImportedProfile"), *Placement.StableId.ToString(), *Ship.GetName(),
					*Saved.Key, *Value.Name.ToString(), *Text.Left(512), *ActualText.Left(512)); */ {}
			}
			else if (bGameplayConfig && Saved.Key == TEXT("Actor"))
				/* Diagnostics muted after pooling/crew verification.
				UE_LOG(LogTemp, Display, TEXT("[EnemyShipPool] Event=ProfileValue Id=%s Actor=%s Field=%s Match=1 Value=%s"),
					*Placement.StableId.ToString(), *Ship.GetName(), *Value.Name.ToString(), *Text.Left(512)); */ {}
			Property->DestroyValue(Expected);
			FMemory::Free(Expected);
		}
	}
	/* Diagnostics muted after pooling/crew verification.
	UE_LOG(LogTemp, Display, TEXT("[EnemyShipPool] Event=ProfileCheck Scope=%s Id=%s Profile=%s Actor=%s Assignment=%u Checked=%d Mismatches=%d AuthoredArchetype=%s EffectiveArchetype=%s Squad=%s"),
		bGameplayConfig ? TEXT("EncounterConfig") : TEXT("ImportedProfile"), *Placement.StableId.ToString(), *Placement.ClassPath.ToString(),
		*Ship.GetName(), Placement.Assignment, Checked, Mismatches, *GetPathNameSafe(Ship.AuthoredEncounterArchetype.Get()),
		*GetPathNameSafe(Ship.EnemyShipArchetype.Get()), *Ship.SquadID.ToString()); */ {}
	if (bGameplayConfig)
	{
		UEnemyShipArchetypeData* ExpectedAuthored = Cast<UEnemyShipArchetypeData>(Placement.Archetype.ResolveObject());
		UEnemyShipArchetypeData* ExpectedEffective = ExpectedAuthored;
		const bool bFallback = Ship.NormalFallbackArchetype && Ship.GetBossEncounterComponent()
			&& !Ship.GetBossEncounterComponent()->IsCampaignGateOpen();
		if (bFallback) ExpectedEffective = Ship.NormalFallbackArchetype;
		/* Diagnostics muted after pooling/crew verification.
		UE_LOG(LogTemp, Display, TEXT("[EnemyShipPool] Event=ArchetypeCheck Id=%s Actor=%s AuthoredMatch=%d EffectiveMatch=%d Fallback=%d ExpectedAuthored=%s ExpectedEffective=%s ActualEffective=%s"),
			*Placement.StableId.ToString(), *Ship.GetName(), ExpectedAuthored == Ship.AuthoredEncounterArchetype,
			ExpectedEffective == Ship.EnemyShipArchetype, bFallback, *GetPathNameSafe(ExpectedAuthored),
			*GetPathNameSafe(ExpectedEffective), *GetPathNameSafe(Ship.EnemyShipArchetype.Get())); */ {}
	}
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
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bAllowTickOnDedicatedServer = true;
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
				CancelTransitions(TEXT("Capture"));
				if (Placements.IsEmpty()) DiscoverPlacedShips();
			});
			RestoreCompletedHandle = Room->OnRestoreCompleted.AddUObject(this, &AEnemyShipBundlePool::RestoreCompleted);
		}
	GetWorldTimerManager().SetTimer(EvaluationTimer, this, &AEnemyShipBundlePool::Evaluate, 0.5f, true);
	LevelRemovedHandle = FWorldDelegates::LevelRemovedFromWorld.AddUObject(this, &AEnemyShipBundlePool::HandleLevelRemoved);
}

void AEnemyShipBundlePool::EndPlay(const EEndPlayReason::Type Reason)
{
	CancelTransitions(TEXT("EndPlay"));
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
			PresentationNodes.Add(Node);
		}
		const uint32 ActivationGeneration = IsValid(Placement.ActiveShip) ? Placement.ActiveShip->GetBundleActivationGeneration() : 0;
		const bool bStateChanged = Node->State.Assignment != Placement.Assignment || Node->State.bDormant != Placement.bDormant
			|| Node->State.bDestroyed != Placement.bDestroyed || Node->State.bUnloaded != Placement.bUnloaded
			|| Node->State.bStoryHidden != Placement.bStoryHidden || Node->State.ActiveShip != Placement.ActiveShip
			|| Node->State.ActivationGeneration != ActivationGeneration;
		Node->State.Assignment = Placement.Assignment;
		Node->State.bDormant = Placement.bDormant;
		Node->State.bDestroyed = Placement.bDestroyed;
		Node->State.bUnloaded = Placement.bUnloaded;
		Node->State.bStoryHidden = Placement.bStoryHidden;
		Node->State.ActiveShip = Placement.ActiveShip;
		Node->State.ActivationGeneration = ActivationGeneration;
		if (IsValid(Placement.ActiveShip))
		{
			Node->State.ActivationTransform = Placement.ActiveShip->GetActorTransform();
			if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Placement.ActiveShip->GetRootComponent()))
			{
				Node->State.ActivationLinearVelocity = Root->GetPhysicsLinearVelocity();
				Node->State.ActivationAngularVelocity = Root->GetPhysicsAngularVelocityInDegrees();
			}
		}
		if (bStateChanged && EnemyShipDiagnostics::QueueLogLevel() > 0)
			UE_LOG(LogEnemyShipInit, Display, TEXT("[EnemyShipQueue] Side=Server Event=PublishState UTC=%s NowSec=%.6f GameSec=%.3f Id=%s Name=%s Assignment=%u Actor=%s Generation=%u Dormant=%d Destroyed=%d Unloaded=%d StoryHidden=%d"),
				*FDateTime::UtcNow().ToIso8601(), FPlatformTime::Seconds(), GetWorld()->GetTimeSeconds(), *Placement.StableId.ToString(),
				*Placement.AuthoredName.ToString(), Placement.Assignment, *GetNameSafe(Placement.ActiveShip.Get()), ActivationGeneration,
				Placement.bDormant, Placement.bDestroyed, Placement.bUnloaded, Placement.bStoryHidden);
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
			if (EnemyShipBundlePool::IsFixedBundleConfiguration(*Object, *Property) || !Property->HasAnyPropertyFlags(CPF_Edit)
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

AEnemyShip* AEnemyShipBundlePool::AllocateBundle(UClass* Class, const FEnemyShipBundlePlacement& Placement, FString& Error)
{
	FActorSpawnParameters Parameters;
	Parameters.Owner = this;
	Parameters.bDeferConstruction = true;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (ULevel* Level : GetWorld()->GetLevels())
		if (Level->GetOutermost()->GetName() == Placement.LevelPackage.ToString()) { Parameters.OverrideLevel = Level; break; }
	if (!Parameters.OverrideLevel) { Error = TEXT("Enemy ship placement level is not loaded"); return nullptr; }
	AEnemyShip* Ship = GetWorld()->SpawnActor<AEnemyShip>(Class, Placement.HomeTransform, Parameters);
	if (!Ship) { Error = TEXT("Enemy ship bundle allocation failed"); return nullptr; }
	if (const FEnemyShipBundleObject* Definition = Placement.Objects.FindByPredicate(
		[](const FEnemyShipBundleObject& Value) { return Value.Key == TEXT("Actor"); }))
		for (const FEnemyShipBundleProperty& Value : Definition->Properties)
			if (FProperty* Property = Class->FindPropertyByName(Value.Name))
			{
				if (EnemyShipBundlePool::IsFixedBundleConfiguration(*Ship, *Property)) continue;
				FString Text = Value.Value;
				EnemyShipBundlePool::RemapReference(Text, *Definition, *Ship);
				if (Text.IsEmpty()) Property->ClearValue(Property->ContainerPtrToValuePtr<void>(Ship));
				else if (!Property->ImportText_Direct(*Text, Property->ContainerPtrToValuePtr<void>(Ship), Ship, PPF_Delimited))
				{
					Error = TEXT("Enemy ship allocation property failed: ") + Value.Name.ToString();
					DestroyBundle(Ship);
					return nullptr;
				}
			}
	Ship->Tags.AddUnique(EnemyShipBundlePool::IdleTag);
	Ship->SetBundlePoolIdle(true);
	const double FinishStart = FPlatformTime::Seconds();
	Ship->FinishSpawning(Placement.HomeTransform);
	if (EnemyShipDiagnostics::InitEnabled())
	{
		const double TotalMs = (FPlatformTime::Seconds() - FinishStart) * 1000.;
		/* Diagnostics muted after pooling/crew verification.
		UE_LOG(LogEnemyShipInit, Display, TEXT("Event=FinishSpawning Actor=%s Path=Immediate TotalMs=%.3f BeginPlayMs=%.3f ConstructionAndOtherMs=%.3f"),
			*Ship->GetName(), TotalMs, Ship->GetInitialBeginPlayMs(), FMath::Max(0.0, TotalMs - Ship->GetInitialBeginPlayMs())); */ {}
	}
	Ship->SetBundlePoolIdle(true);
	SetBundleRegistration(*Ship, true, nullptr);
	if (auto* Swarm = GetWorld()->GetSubsystem<UShipSwarmSubsystem>()) Swarm->UnregisterShip(Ship);
	if (auto* Weakening = GetWorld()->GetSubsystem<UEnemyShipWeakeningWorldSubsystem>()) Weakening->UnregisterShip(Ship);
	return Ship;
}

int32 AEnemyShipBundlePool::CountActiveBundles() const
{
	int32 Count = 0;
	for (const auto& Placement : Placements) if (IsValid(Placement.ActiveShip)) ++Count;
	return Count;
}

void AEnemyShipBundlePool::MaintainBundleCapacity(bool bFillReserve)
{
	IdleBundles.RemoveAll([](const TObjectPtr<AEnemyShip>& Ship) { return !IsValid(Ship); });
	const int32 Active = CountActiveBundles();
	const int32 Target = FMath::Max(0, BundleCapacity - Active);
	while (IdleBundles.Num() > Target)
	{
		AEnemyShip* Excess = IdleBundles[0];
		IdleBundles.RemoveAt(0);
		/* UE_LOG(LogTemp, Display, TEXT("[EnemyShipPool] Event=Trim Actor=%s Active=%d Idle=%d Bundles=%d Capacity=%d"),
			*GetNameSafe(Excess), Active, IdleBundles.Num(), Active + IdleBundles.Num(), BundleCapacity); */
		DestroyBundle(Excess);
	}
	if (!bFillReserve || IdleBundles.Num() >= Target) return;
	const auto* Definition = Placements.FindByPredicate([](const FEnemyShipBundlePlacement& P)
	{ return !P.bUnloaded && !P.bDestroyed; });
	if (!Definition) return;
	UClass* Class = EnemyShipBundlePool::CommonClass.TryLoadClass<AEnemyShip>();
	if (!Class) return;
	while (IdleBundles.Num() < Target)
	{
		FString Error;
		AEnemyShip* Ship = AllocateBundle(Class, *Definition, Error);
		if (!Ship)
		{
			/* Diagnostics muted after pooling/crew verification.
			UE_LOG(LogTemp, Error, TEXT("[EnemyShipPool] Event=ReserveFailed Reason=%s"), *Error); */ {}
			break;
		}
		IdleBundles.Add(Ship);
		/* UE_LOG(LogTemp, Display, TEXT("[EnemyShipPool] Event=ReserveCreated Actor=%s Class=%s Active=%d Idle=%d Bundles=%d Capacity=%d"),
			*Ship->GetName(), *Class->GetPathName(), Active, IdleBundles.Num(), Active + IdleBundles.Num(), BundleCapacity); */
	}
}

bool AEnemyShipBundlePool::BindPlacement(FEnemyShipBundlePlacement& Placement, bool bRestoring, FString& Error)
{
	// Placement.ClassPath remains the authored profile class, never the allocation class.
	const FSoftClassPath* RestoreClass = bRestoring ? RestoreClasses.Find(Placement.StableId) : nullptr;
	UClass* Class = RestoreClass ? RestoreClass->TryLoadClass<AEnemyShip>() : EnemyShipBundlePool::CommonClass.TryLoadClass<AEnemyShip>();
	if (!Class || Placement.HomeTransform.ContainsNaN()) { Error = TEXT("Enemy ship pool definition is invalid"); return false; }
	AEnemyShip* Ship = Placement.ActiveShip;
	// FString Allocation(TEXT("Existing"));
	if (IsValid(Ship) && Ship->GetClass() != Class)
	{
		Placement.ActiveShip = nullptr;
		DestroyBundle(Ship);
		Ship = nullptr;
	}
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
				// Allocation = TEXT("Reused");
				break;
			}
		if (!Ship)
		{
			Ship = AllocateBundle(Class, Placement, Error);
			if (!Ship) return false;
			// Allocation = TEXT("Created");
		}
	}
	// const FString PreviousId = Allocation == TEXT("Reused") ? EnemyShipBundlePool::ReadId(*Ship).ToString() : TEXT("None");
	Ship->SetBundlePoolIdle(true);
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
	TArray<const FEnemyShipBundleObject*> References;
	for (const auto& Saved : Placement.Objects) References.Add(&Saved);
	// Component/child paths can contain the ship path. Remap the longest paths first.
	References.Sort([](const FEnemyShipBundleObject& A, const FEnemyShipBundleObject& B) { return A.SourcePath.Len() > B.SourcePath.Len(); });
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
		// Current profiles contain every editable field, including zero/empty values.
		// Older profiles may lack fields added later: reset those to this bundle's template.
		for (TFieldIterator<FProperty> It(Target->GetClass()); It; ++It)
		{
			FProperty* Property = *It;
			if (EnemyShipBundlePool::IsFixedBundleConfiguration(*Target, *Property) || !Property->HasAnyPropertyFlags(CPF_Edit)
				|| Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient | CPF_EditorOnly | CPF_InstancedReference)
				|| Property->GetFName() == TEXT("GuardCharacters") || Property->GetFName() == TEXT("GuardSpawners")
				|| Saved.Properties.ContainsByPredicate([&](const FEnemyShipBundleProperty& P) { return P.Name == Property->GetFName(); })) continue;
			UObject* Template = Target->GetArchetype();
			FString DefaultText;
			Property->ExportTextItem_Direct(DefaultText, Property->ContainerPtrToValuePtr<void>(Template), nullptr, Template, PPF_Delimited);
			for (const auto& Pair : Objects)
				DefaultText.ReplaceInline(*Pair.Value->GetArchetype()->GetPathName(), *Pair.Value->GetPathName(), ESearchCase::CaseSensitive);
			if (DefaultText.IsEmpty()) Property->ClearValue(Property->ContainerPtrToValuePtr<void>(Target));
			else if (!Property->ImportText_Direct(*DefaultText, Property->ContainerPtrToValuePtr<void>(Target), Target, PPF_Delimited))
			{ Error = TEXT("Enemy ship bundle default reset failed: ") + Property->GetName(); return false; }
		}
		for (const FEnemyShipBundleProperty& Value : Saved.Properties)
		{
			FProperty* Property = Target->GetClass()->FindPropertyByName(Value.Name);
			if (!Property) { Error = TEXT("Enemy ship bundle property missing: ") + Value.Name.ToString(); return false; }
			if (EnemyShipBundlePool::IsFixedBundleConfiguration(*Target, *Property)) continue;
			FString Text = Value.Value;
			for (const FEnemyShipBundleObject* Reference : References)
				if (UObject* const* Mapped = Objects.Find(Reference->Key))
					EnemyShipBundlePool::RemapReference(Text, *Reference, **Mapped);
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
	// Pool diagnostics disabled: skip the expensive parse/compare work as well as its output.
	// EnemyShipBundlePool::AuditProfile(*Ship, Placement, false);
	Ship->Tags = Placement.Tags;
	Ship->SetBundleAuthoredName(Placement.AuthoredName);
	Ship->AuthoredEncounterArchetype = Cast<UEnemyShipArchetypeData>(Placement.Archetype.TryLoad());
	Ship->RefreshBundleStoryBindings();
	Ship->GetNavigationComponent()->ResetSpawnHome(Placement.HomeTransform);
	Ship->SetActorEnableCollision(false);
	Ship->SetShipRuntimePhysicsEnabled(false);
	Ship->SetActorTransform(Placement.HomeTransform, false, nullptr, ETeleportType::TeleportPhysics);
	Placement.ActiveShip = Ship;
	Placement.bDormant = false;
	Placement.bUnloaded = false;
	if (bRestoring && Ship->EnemyShipArchetype) Ship->ConfigureEnemyShipArchetype(Ship->EnemyShipArchetype);
	if (!bRestoring)
	{
		++Placement.Assignment;
		if (auto* Crew = Ship->FindComponentByClass<UDeckEnemySpawnerComponent>()) Crew->ReconcileBundleProfile();
		Ship->PrepareFreshBundleEncounter(Placement.HomeTransform);
		EnemyShipBundlePool::RefillChests(*Ship);
	}
	SetBundleRegistration(*Ship, false, &Placement);
	if (!Ship->PrepareBundleDeck(!bRestoring, Error)) return false;
	// Crew created during preparation joins the assigned bundle's identity/registration.
	SetBundleRegistration(*Ship, false, &Placement);
	Ship->OnDestroyed.AddUniqueDynamic(this, &AEnemyShipBundlePool::HandleShipDestroyed);
	if (auto* Swarm = GetWorld()->GetSubsystem<UShipSwarmSubsystem>()) Swarm->RegisterShip(Ship);
	if (auto* Weakening = GetWorld()->GetSubsystem<UEnemyShipWeakeningWorldSubsystem>()) Weakening->RegisterShip(Ship);
	if (!bRestoring)
	{
		Ship->BeginBundleActivation();
		Ship->SetBundlePoolIdle(false);
	}
	// if (!bRestoring) EnemyShipBundlePool::AuditProfile(*Ship, Placement, true);
	bBound = true;
	MaintainBundleCapacity(false);
	/* UE_LOG(LogTemp, Display, TEXT("[EnemyShipPool] Event=Assigned Allocation=%s Id=%s PreviousId=%s Profile=%s Actor=%s BundleClass=%s Squad=%s Archetype=%s Objects=%d Assignment=%u Restoring=%d Active=%d Idle=%d Bundles=%d Capacity=%d"),
		*Allocation, *Placement.StableId.ToString(), *PreviousId, *Placement.ClassPath.ToString(), *Ship->GetName(), *Ship->GetClass()->GetPathName(),
		*Ship->SquadID.ToString(), *GetPathNameSafe(Ship->AuthoredEncounterArchetype.Get()), Objects.Num(), Placement.Assignment, bRestoring,
		CountActiveBundles(), IdleBundles.Num(), CountActiveBundles() + IdleBundles.Num(), BundleCapacity); */
	/* UE_LOG(LogTemp, Display, TEXT("[EnemyShipPoolAudit] Side=Server Event=Activated Id=%s Ship=%s Actor=%s Assignment=%u Restoring=%d Hidden=%d Idle=%d RuntimePhysics=%d SimRoot=%d ActorTick=%d Position=%s Home=%s"),
		*Placement.StableId.ToString(), *Placement.AuthoredName.ToString(), *Ship->GetName(), Placement.Assignment, bRestoring,
		Ship->IsHidden(), Ship->IsBundlePoolIdle(), Ship->IsShipRuntimePhysicsEnabled(),
		Ship->BuoyancyRoot && Ship->BuoyancyRoot->IsSimulatingPhysics(), Ship->IsActorTickEnabled(),
		*Ship->GetActorLocation().ToString(), *Placement.HomeTransform.GetLocation().ToString()); */
	ForceNetUpdate();
	return true;
}

void AEnemyShipBundlePool::ReleasePlacement(FEnemyShipBundlePlacement& Placement)
{
	AEnemyShip* Ship = Placement.ActiveShip;
	if (!IsValid(Ship)) return;
	Ship->SetBundlePoolIdle(true);
	Ship->ClearCrewForIdle();
	Ship->PrepareFreshBundleEncounter(Placement.HomeTransform);
	Placement.OrbitArchetype = FSoftObjectPath(Ship->EnemyShipArchetype.Get());
	SetBundleRegistration(*Ship, true, nullptr);
	if (auto* Swarm = GetWorld()->GetSubsystem<UShipSwarmSubsystem>()) Swarm->UnregisterShip(Ship);
	if (auto* Weakening = GetWorld()->GetSubsystem<UEnemyShipWeakeningWorldSubsystem>()) Weakening->UnregisterShip(Ship);
	Placement.ActiveShip = nullptr;
	Placement.bDormant = true;
	const bool bCommon = FSoftClassPath(Ship->GetClass()) == EnemyShipBundlePool::CommonClass;
	if (bCommon) IdleBundles.AddUnique(Ship);
	/* UE_LOG(LogTemp, Display, TEXT("[EnemyShipPoolAudit] Side=Server Event=Returned Id=%s Ship=%s Actor=%s Assignment=%u Retained=%d Hidden=%d Idle=%d RuntimePhysics=%d SimRoot=%d ActorTick=%d Position=%s Home=%s"),
		*Placement.StableId.ToString(), *Placement.AuthoredName.ToString(), *GetNameSafe(Ship), Placement.Assignment, bCommon && CountActiveBundles() < FMath::Max(0, BundleCapacity),
		Ship->IsHidden(), Ship->IsBundlePoolIdle(), Ship->IsShipRuntimePhysicsEnabled(),
		Ship->BuoyancyRoot && Ship->BuoyancyRoot->IsSimulatingPhysics(), Ship->IsActorTickEnabled(),
		*Ship->GetActorLocation().ToString(), *Placement.HomeTransform.GetLocation().ToString()); */
	if (!bCommon)
	{
		/* UE_LOG(LogTemp, Display, TEXT("[EnemyShipPool] Event=RetireAuthoredBundle Id=%s Actor=%s Class=%s"),
			*Placement.StableId.ToString(), *Ship->GetName(), *Ship->GetClass()->GetPathName()); */
		DestroyBundle(Ship);
	}
	MaintainBundleCapacity(false);
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


static TAutoConsoleVariable<float> CVarEnemyShipPoolBudgetMs(
	TEXT("sw.EnemyShipPool.QueueBudgetMs"), 4.0f, TEXT("Pool preparation/client presentation budget per frame in milliseconds. Server state publication is not delayed by preparation."));

namespace
{
	const TCHAR* QueueWorkName(EEnemyShipPoolWork Kind)
	{
		switch (Kind)
		{
		case EEnemyShipPoolWork::Wake: return TEXT("Wake");
		case EEnemyShipPoolWork::Sleep: return TEXT("Sleep");
		case EEnemyShipPoolWork::Reserve: return TEXT("Reserve");
		case EEnemyShipPoolWork::Trim: return TEXT("Trim");
		default: return TEXT("Unknown");
		}
	}

	FString QueueSpatialContext(UWorld* World, const FEnemyShipBundlePlacement& Placement)
	{
		AShip* NearestShip = nullptr;
		double ShipDistanceSq = TNumericLimits<double>::Max(), PawnDistanceSq = TNumericLimits<double>::Max();
		const FVector Home = Placement.HomeTransform.GetLocation();
		for (TActorIterator<AShip> It(World); It; ++It)
			if (!It->IsEnemyShipForEffects() && It->ActorHasTag(TEXT("Player")) && !It->ActorHasTag(TEXT("Enemy")))
			{
				const double DistanceSq = FVector::DistSquared2D(Home, It->GetActorLocation());
				if (DistanceSq < ShipDistanceSq) { ShipDistanceSq = DistanceSq; NearestShip = *It; }
			}
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			if (const APlayerController* Controller = It->Get())
				if (const APawn* Pawn = Controller->GetPawn()) PawnDistanceSq = FMath::Min(PawnDistanceSq, FVector::DistSquared2D(Home, Pawn->GetActorLocation()));
		const double Distance = NearestShip ? FMath::Sqrt(ShipDistanceSq) : -1.0;
		return FString::Printf(TEXT("Home=%s ShipDistanceCm=%.1f PawnDistanceCm=%.1f PlayerShip=%s PlayerPos=%s SpeedCmS=%.1f RangeCm=%.1f InRange=%d"),
			*Home.ToCompactString(), Distance, PawnDistanceSq < TNumericLimits<double>::Max() ? FMath::Sqrt(PawnDistanceSq) : -1.0,
			*GetNameSafe(NearestShip), NearestShip ? *NearestShip->GetActorLocation().ToCompactString() : TEXT("None"),
			NearestShip ? NearestShip->GetVelocity().Size2D() : 0.0, Placement.ActivationRange,
			NearestShip && Distance <= FMath::Max(0.f, Placement.ActivationRange));
	}

	void LogQueueEvent(UWorld* World, const TCHAR* Event, const FEnemyShipPoolTransition& Job, AEnemyShip* Ship,
		int32 Pending, int32 Active, int32 Idle, const TCHAR* CurrentWork, const FString& Reason = FString())
	{
		if (EnemyShipDiagnostics::QueueLogLevel() <= 0) return;
		const double Now = FPlatformTime::Seconds();
		const bool bStarted = Job.StartedAt > 0.0;
		UE_LOG(LogEnemyShipInit, Display, TEXT("[EnemyShipQueue] Side=Server Event=%s UTC=%s NowSec=%.6f GameSec=%.3f Id=%s Name=%s Kind=%s RequestSec=%.6f Actor=%s Pending=%d CurrentWork=%s Active=%d Idle=%d BudgetMs=%.2f WaitMs=%.2f PrepareMs=%.2f TotalMs=%.2f WorkMs=%.2f MaxStepMs=%.2f SlowStage=%d Frames=%d Steps=%d Stage=%d Object=%d Property=%d DefaultProperty=%d Reason=%s %s"),
			Event, *FDateTime::UtcNow().ToIso8601(), Now, World->GetTimeSeconds(), *Job.Definition.StableId.ToString(),
			*Job.Definition.AuthoredName.ToString(), QueueWorkName(Job.Kind), Job.RequestedAt, *GetNameSafe(Ship), Pending, CurrentWork, Active, Idle,
			FMath::Clamp(CVarEnemyShipPoolBudgetMs.GetValueOnGameThread(), 0.1f, 8.0f),
			((bStarted ? Job.StartedAt : Now) - Job.RequestedAt) * 1000.0, bStarted ? (Now - Job.StartedAt) * 1000.0 : 0.0,
			(Now - Job.RequestedAt) * 1000.0, Job.WorkSeconds * 1000.0, Job.MaxStepSeconds * 1000.0, Job.SlowStage,
			Job.Frames, Job.Steps, Job.Stage, Job.Object, Job.Property, Job.DefaultProperty, *Reason, *QueueSpatialContext(World, Job.Definition));
	}
}

void AEnemyShipBundlePool::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const auto* Room = GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>();
	if (bChangingPool || (Room && Room->IsRestoringSnapshot())) return;
	const double Deadline = FPlatformTime::Seconds() + FMath::Clamp(CVarEnemyShipPoolBudgetMs.GetValueOnGameThread(), 0.1f, 8.0f) * 0.001;
	if (HasAuthority()) ProcessTransitionQueue(Deadline);
	if (bPresentationPending && (HasAuthority() || FPlatformTime::Seconds() < Deadline)) ProcessPresentation(Deadline);
}

void AEnemyShipBundlePool::EnqueueTransition(const FEnemyShipBundlePlacement& Placement, EEnemyShipPoolWork Kind)
{
	if (Transition && Transition->Definition.StableId == Placement.StableId) return;
	if (TransitionRequests.ContainsByPredicate([&](const FEnemyShipPoolTransition& Request) { return Request.Definition.StableId == Placement.StableId; })) return;
	FEnemyShipPoolTransition& Request = TransitionRequests.AddDefaulted_GetRef();
	Request.Definition = Placement;
	Request.Kind = Kind;
	Request.RequestedAt = FPlatformTime::Seconds();
	LogQueueEvent(GetWorld(), TEXT("Enqueue"), Request, nullptr, TransitionRequests.Num(), CountActiveBundles(), IdleBundles.Num(),
		Transition ? QueueWorkName(Transition->Kind) : TEXT("None"));
}

void AEnemyShipBundlePool::QueueCapacityMaintenance()
{
	// Reserve work is serialized with assignments; do not manufacture an entire reserve in one frame.
	if (Transition || !TransitionRequests.IsEmpty()) return;
	const int32 Target = FMath::Max(0, BundleCapacity - CountActiveBundles());
	if (IdleBundles.Num() > Target && !Placements.IsEmpty()) EnqueueTransition(Placements[0], EEnemyShipPoolWork::Trim);
	else if (IdleBundles.Num() < Target)
		if (const auto* Definition = Placements.FindByPredicate([](const FEnemyShipBundlePlacement& P) { return !P.bUnloaded && !P.bDestroyed; }))
			EnqueueTransition(*Definition, EEnemyShipPoolWork::Reserve);
}

void AEnemyShipBundlePool::FinishTransition(const TCHAR* Result, const FString& Reason)
{
	if (!Transition) return;
	LogQueueEvent(GetWorld(), Result, *Transition, TransitionShip, TransitionRequests.Num(), CountActiveBundles(), IdleBundles.Num(),
		QueueWorkName(Transition->Kind), Reason);
	if (FCString::Strcmp(Result, TEXT("Failed")) == 0)
		/* Diagnostics muted after pooling/crew verification.
		UE_LOG(LogEnemyShipInit, Error, TEXT("Event=PoolPreparationFailed Ship=%s Stage=%d Reason=%s"),
			*Transition->Definition.AuthoredName.ToString(), Transition->Stage, *Reason); */ {}
	Transition.Reset();
	TransitionShip = nullptr;
}

void AEnemyShipBundlePool::CancelTransitions(const TCHAR* Reason)
{
	if (EnemyShipDiagnostics::QueueLogLevel() > 0)
		for (const FEnemyShipPoolTransition& Request : TransitionRequests)
			LogQueueEvent(GetWorld(), TEXT("Dropped"), Request, nullptr, TransitionRequests.Num(), CountActiveBundles(), IdleBundles.Num(),
				Transition ? QueueWorkName(Transition->Kind) : TEXT("None"), Reason);
	TransitionRequests.Reset();
	if (!Transition) return;
	if (IsValid(TransitionShip) && FCString::Strcmp(Reason, TEXT("EndPlay")) != 0)
	{
		if (Transition->Kind == EEnemyShipPoolWork::Sleep)
		{
			// Commit the remaining administrative cleanup before snapshot capture; never save a half-returned bundle.
			if (auto* Placement = Placements.FindByPredicate([&](const FEnemyShipBundlePlacement& P) { return P.StableId == Transition->Definition.StableId; }))
				ReleasePlacement(*Placement);
		}
		else
		{
			if (auto* Walk = TransitionShip->GetDeckWalkAreaComponent()) Walk->CancelQueuedRebuild();
			DestroyBundle(TransitionShip);
		}
	}
	FinishTransition(TEXT("Cancelled"), Reason);
}

void AEnemyShipBundlePool::ProcessTransitionQueue(double Deadline)
{
	if (!Transition && !TransitionRequests.IsEmpty())
	{
		int32 SelectedIndex = 0;
		const TCHAR* SelectionPolicy = TEXT("FIFO");
		if (TransitionRequests[0].Kind == EEnemyShipPoolWork::Wake)
		{
			TArray<FVector> PlayerLocations;
			for (TActorIterator<AShip> It(GetWorld()); It; ++It)
				if (!It->IsEnemyShipForEffects() && It->ActorHasTag(TEXT("Player")) && !It->ActorHasTag(TEXT("Enemy"))) PlayerLocations.Add(It->GetActorLocation());
			if (!PlayerLocations.IsEmpty())
			{
				SelectionPolicy = TEXT("NearestPlayerShip");
				double NearestDistanceSq = TNumericLimits<double>::Max();
				for (int32 Index = 0; Index < TransitionRequests.Num(); ++Index)
				{
					const FEnemyShipPoolTransition& Request = TransitionRequests[Index];
					if (Request.Kind != EEnemyShipPoolWork::Wake) continue;
					double DistanceSq = TNumericLimits<double>::Max();
					for (const FVector& Position : PlayerLocations) DistanceSq = FMath::Min(DistanceSq, FVector::DistSquared2D(Position, Request.Definition.HomeTransform.GetLocation()));
					if (DistanceSq < NearestDistanceSq || (DistanceSq == NearestDistanceSq && Request.RequestedAt < TransitionRequests[SelectedIndex].RequestedAt))
					{
						NearestDistanceSq = DistanceSq;
						SelectedIndex = Index;
					}
				}
			}
		}
		// Only exchange Wake slots; cleanup and reserve work keep their existing order.
		TransitionRequests.Swap(0, SelectedIndex);
		Transition = MakeUnique<FEnemyShipPoolTransition>(MoveTemp(TransitionRequests[0]));
		TransitionRequests.RemoveAt(0, 1, EAllowShrinking::No);
		Transition->StartedAt = FPlatformTime::Seconds();
		LogQueueEvent(GetWorld(), TEXT("Start"), *Transition, nullptr, TransitionRequests.Num(), CountActiveBundles(), IdleBundles.Num(), QueueWorkName(Transition->Kind),
			FString::Printf(TEXT("Policy=%s SelectedIndex=%d"), SelectionPolicy, SelectedIndex));
	}
	if (!Transition) return;
	const int32 QueueDiagnosticLevel = EnemyShipDiagnostics::QueueLogLevel();
	if (QueueDiagnosticLevel > 0 && ++Transition->Frames % 30 == 0)
		LogQueueEvent(GetWorld(), TEXT("Progress"), *Transition, TransitionShip, TransitionRequests.Num(), CountActiveBundles(), IdleBundles.Num(), QueueWorkName(Transition->Kind));
		if (Transition->Kind == EEnemyShipPoolWork::Wake)
		{
			bool bInRange = false;
			for (TActorIterator<AShip> It(GetWorld()); It; ++It)
				if (!It->IsEnemyShipForEffects() && It->ActorHasTag(TEXT("Player")) && !It->ActorHasTag(TEXT("Enemy"))
					&& FVector::DistSquared2D(It->GetActorLocation(), Transition->Definition.HomeTransform.GetLocation()) <= FMath::Square(FMath::Max(0.f, Transition->Definition.ActivationRange))) { bInRange = true; break; }
			if (!bInRange)
			{
				if (IsValid(TransitionShip)) DestroyBundle(TransitionShip);
				FinishTransition(TEXT("Cancelled"), TEXT("OutOfRange")); return;
			}
		}
	while (Transition && FPlatformTime::Seconds() < Deadline)
	{
		auto* Placement = Placements.FindByPredicate([&](const FEnemyShipBundlePlacement& P) { return P.StableId == Transition->Definition.StableId; });
		const bool bWake = Transition->Kind == EEnemyShipPoolWork::Wake;
		if (!Placement || Placement->bDestroyed || Placement->bUnloaded || (bWake && !Placement->bDormant))
		{
			if (IsValid(TransitionShip) && bWake) DestroyBundle(TransitionShip);
			FinishTransition(TEXT("Cancelled"), TEXT("PlacementChanged"));
			break;
		}

		FString Error;
		const int32 PreviousStage = Transition->Stage;
		const double StepStartedAt = QueueDiagnosticLevel > 0 ? FPlatformTime::Seconds() : 0.0;
		const bool bDone = StepTransition(*Transition, Error);
		if (QueueDiagnosticLevel > 0)
		{
			const double StepSeconds = FPlatformTime::Seconds() - StepStartedAt;
			Transition->WorkSeconds += StepSeconds;
			++Transition->Steps;
			if (StepSeconds > Transition->MaxStepSeconds) { Transition->MaxStepSeconds = StepSeconds; Transition->SlowStage = PreviousStage; }
			if (QueueDiagnosticLevel > 1 && PreviousStage != Transition->Stage)
				LogQueueEvent(GetWorld(), TEXT("Stage"), *Transition, TransitionShip, TransitionRequests.Num(), CountActiveBundles(), IdleBundles.Num(),
					QueueWorkName(Transition->Kind), FString::Printf(TEXT("PreviousStage=%d StepMs=%.3f"), PreviousStage, StepSeconds * 1000.0));
		}
		if (!Error.IsEmpty())
		{
			if (bWake) LastWakeErrors.Add(Transition->Definition.StableId, Error);
			if (IsValid(TransitionShip)) DestroyBundle(TransitionShip);
			FinishTransition(TEXT("Failed"), Error); break;
		}
		if (bDone) { FinishTransition(TEXT("Complete")); break; }
	}
}

bool AEnemyShipBundlePool::StepTransition(FEnemyShipPoolTransition& Job, FString& Error)
{
	auto& Definition = Job.Definition;
	auto* Placement = Placements.FindByPredicate([&](const FEnemyShipBundlePlacement& P) { return P.StableId == Definition.StableId; });
	if (!Placement) { Error = TEXT("PlacementMissing"); return true; }
	if (Job.Kind == EEnemyShipPoolWork::Trim)
	{
		if (IdleBundles.IsEmpty()) return true;
		AEnemyShip* Excess = IdleBundles.Pop(EAllowShrinking::No);
		DestroyBundle(Excess); return true;
	}
	if (Job.Kind == EEnemyShipPoolWork::Sleep)
	{
		if (Job.Stage == 0)
		{
			TransitionShip = Placement->ActiveShip;
			if (!IsValid(TransitionShip) || !TransitionShip->IsDistanceOptimizationDormant()) return true;
			TransitionShip->SetBundlePoolIdle(true); ++Job.Stage; return false;
		}
		AEnemyShip* Ship = TransitionShip;
		if (!IsValid(Ship)) return true;
		switch (Job.Stage)
		{
		case 1: if (Ship->StepClearCrewForIdle()) ++Job.Stage; return false;
		case 2: Ship->PrepareFreshBundleEncounter(Definition.HomeTransform); ++Job.Stage; return false;
		case 3:
			Placement->OrbitArchetype = FSoftObjectPath(Ship->EnemyShipArchetype.Get());
			SetBundleRegistration(*Ship, true, nullptr);
			if (auto* Swarm = GetWorld()->GetSubsystem<UShipSwarmSubsystem>()) Swarm->UnregisterShip(Ship);
			if (auto* Weakening = GetWorld()->GetSubsystem<UEnemyShipWeakeningWorldSubsystem>()) Weakening->UnregisterShip(Ship);
			++Job.Stage; return false;
		default:
			Placement->ActiveShip = nullptr; Placement->bDormant = true;
			if (FSoftClassPath(Ship->GetClass()) == EnemyShipBundlePool::CommonClass) IdleBundles.AddUnique(Ship);
			else DestroyBundle(Ship);
			ForceNetUpdate(); OnRep_Placements(); return true;
		}
	}
	AEnemyShip* Ship = TransitionShip;
	if (Job.Stage > 0 && !IsValid(Ship)) { Error = TEXT("ActorLost"); return true; }
	switch (Job.Stage)
	{
	case 0:
	{
		UClass* Class = EnemyShipBundlePool::CommonClass.TryLoadClass<AEnemyShip>();
		if (!Class) { Error = TEXT("AllocationClassMissing"); return true; }
		if (Job.Kind == EEnemyShipPoolWork::Wake)
			for (int32 Index = IdleBundles.Num() - 1; Index >= 0; --Index)
				if (IsValid(IdleBundles[Index]) && IdleBundles[Index]->GetClass() == Class && IdleBundles[Index]->GetLevel()->GetOutermost()->GetName() == Definition.LevelPackage.ToString())
				{ TransitionShip = IdleBundles[Index]; IdleBundles.RemoveAtSwap(Index); Job.Stage = 3; return false; }
		FActorSpawnParameters Parameters;
		Parameters.Owner = this; Parameters.bDeferConstruction = true;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		for (ULevel* Level : GetWorld()->GetLevels())
			if (Level->GetOutermost()->GetName() == Definition.LevelPackage.ToString()) { Parameters.OverrideLevel = Level; break; }
		if (!Parameters.OverrideLevel) { Error = TEXT("LevelMissing"); return true; }
		const double AllocationStart = FPlatformTime::Seconds();
		TransitionShip = GetWorld()->SpawnActor<AEnemyShip>(Class, Definition.HomeTransform, Parameters);
		if (EnemyShipDiagnostics::InitEnabled())
			/* Diagnostics muted after pooling/crew verification.
			UE_LOG(LogEnemyShipInit, Display, TEXT("Event=AllocateShell Actor=%s Class=%s Kind=%d DurationMs=%.3f Active=%d Idle=%d Capacity=%d"),
				*GetNameSafe(TransitionShip), *GetNameSafe(Class), static_cast<int32>(Job.Kind), (FPlatformTime::Seconds() - AllocationStart) * 1000., CountActiveBundles(), IdleBundles.Num(), BundleCapacity); */ {}
		if (!TransitionShip) { Error = TEXT("AllocationFailed"); return true; }
		TransitionShip->SetBundlePoolIdle(true);
		Job.Stage = 1; return false;
	}
	case 1:
	{
		const auto* ActorDefinition = Definition.Objects.FindByPredicate([](const FEnemyShipBundleObject& Saved) { return Saved.Key == TEXT("Actor"); });
		if (!ActorDefinition || Job.Property >= ActorDefinition->Properties.Num()) { Job.Property = 0; ++Job.Stage; return false; }
		const auto& Value = ActorDefinition->Properties[Job.Property++];
		if (FProperty* Property = Ship->GetClass()->FindPropertyByName(Value.Name); Property && !EnemyShipBundlePool::IsFixedBundleConfiguration(*Ship, *Property))
		{
			FString Text = Value.Value; EnemyShipBundlePool::RemapReference(Text, *ActorDefinition, *Ship);
			if (Text.IsEmpty()) Property->ClearValue(Property->ContainerPtrToValuePtr<void>(Ship));
			else if (!Property->ImportText_Direct(*Text, Property->ContainerPtrToValuePtr<void>(Ship), Ship, PPF_Delimited)) Error = TEXT("AllocationProperty:") + Value.Name.ToString();
		}
		Ship->SetActorHiddenInGame(true); Ship->SetActorEnableCollision(false); Ship->SetActorTickEnabled(false);
		return false;
	}
	case 2:
		Ship->Tags.AddUnique(EnemyShipBundlePool::IdleTag);
		Ship->SetBundlePoolIdle(true);
		{
			const double Start = FPlatformTime::Seconds();
			Ship->FinishSpawning(Definition.HomeTransform);
			if (EnemyShipDiagnostics::InitEnabled())
			{
				const double TotalMs = (FPlatformTime::Seconds() - Start) * 1000.;
				/* Diagnostics muted after pooling/crew verification.
				UE_LOG(LogEnemyShipInit, Display, TEXT("Event=FinishSpawning Actor=%s Kind=%d TotalMs=%.3f BeginPlayMs=%.3f ConstructionAndOtherMs=%.3f"),
					*Ship->GetName(), static_cast<int32>(Job.Kind), TotalMs, Ship->GetInitialBeginPlayMs(), FMath::Max(0.0, TotalMs - Ship->GetInitialBeginPlayMs())); */ {}
			}
		}
		Ship->SetBundlePoolIdle(true);
		SetBundleRegistration(*Ship, true, nullptr);
		if (auto* Swarm = GetWorld()->GetSubsystem<UShipSwarmSubsystem>()) Swarm->UnregisterShip(Ship);
		if (auto* Weakening = GetWorld()->GetSubsystem<UEnemyShipWeakeningWorldSubsystem>()) Weakening->UnregisterShip(Ship);
		if (Job.Kind == EEnemyShipPoolWork::Reserve) { IdleBundles.AddUnique(Ship); return true; }
		++Job.Stage; return false;
	case 3:
	{
		Ship->SetBundlePoolIdle(true);
		const auto Objects = EnemyShipBundlePool::GetObjects(*Ship, true);
		for (const auto& Pair : Objects) { Job.Objects.Add(Pair.Key, Pair.Value); Job.ObjectKeys.Add(Pair.Key); }
		for (int32 Index = 0; Index < Definition.Objects.Num(); ++Index) Job.References.Add(Index);
		Job.References.Sort([&](int32 A, int32 B) { return Definition.Objects[A].SourcePath.Len() > Definition.Objects[B].SourcePath.Len(); });
		Ship->RefreshBundlePresentation();
		++Job.Stage; return false;
	}
	case 4:
	{
		if (Job.Object >= Definition.Objects.Num()) { ++Job.Stage; return false; }
		const auto& Saved = Definition.Objects[Job.Object];
		UObject* Target = Job.Objects.FindRef(Saved.Key).Get();
		if (!Target)
		{
			if (!Saved.Key.StartsWith(TEXT("SpawnedChest")))
				if (UClass* Class = Saved.ClassPath.TryLoadClass<UActorComponent>(); !Class || !Class->IsChildOf(UGameplayTasksComponent::StaticClass())) Error = TEXT("ComponentMissing:") + Saved.Key;
			++Job.Object; return false;
		}
		if (Target->IsA<UPrimitiveComponent>() || Target->IsA<UDeckWalkAreaComponent>() || Target->IsA<UDeckWaypointComponent>())
		{
			++Job.Object; Job.bObjectStarted = false; return false;
		}
		if (!Job.bObjectStarted)
		{
			Job.DefaultProperties.Reset();
			for (TFieldIterator<FProperty> It(Target->GetClass()); It; ++It)
				if (!EnemyShipBundlePool::IsFixedBundleConfiguration(*Target, **It) && It->HasAnyPropertyFlags(CPF_Edit) && !It->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient | CPF_EditorOnly | CPF_InstancedReference)
					&& It->GetFName() != TEXT("GuardCharacters") && It->GetFName() != TEXT("GuardSpawners")
					&& !Saved.Properties.ContainsByPredicate([&](const FEnemyShipBundleProperty& Value) { return Value.Name == It->GetFName(); })) Job.DefaultProperties.Add(*It);
			Job.bObjectStarted = true; Job.DefaultProperty = 0; Job.Property = 0; return false;
		}
		if (Job.DefaultProperty < Job.DefaultProperties.Num())
		{
			FProperty* Property = Job.DefaultProperties[Job.DefaultProperty++];
			UObject* Template = Target->GetArchetype(); FString Text;
			Property->ExportTextItem_Direct(Text, Property->ContainerPtrToValuePtr<void>(Template), nullptr, Template, PPF_Delimited);
			for (const auto& Pair : Job.Objects)
				if (UObject* Object = Pair.Value.Get()) Text.ReplaceInline(*Object->GetArchetype()->GetPathName(), *Object->GetPathName(), ESearchCase::CaseSensitive);
			// Raw editable imports must not expose a child Actor between queue slices.
			ON_SCOPE_EXIT
			{
				if (AActor* Actor = Cast<AActor>(Target)) { Actor->SetActorHiddenInGame(true); Actor->SetActorEnableCollision(false); Actor->SetActorTickEnabled(false); }
				if (UActorComponent* Component = Cast<UActorComponent>(Target)) Component->SetComponentTickEnabled(false);
				Ship->SetShipRuntimePhysicsEnabled(false);
			};
			if (Text.IsEmpty()) Property->ClearValue(Property->ContainerPtrToValuePtr<void>(Target));
			else if (!Property->ImportText_Direct(*Text, Property->ContainerPtrToValuePtr<void>(Target), Target, PPF_Delimited)) Error = TEXT("DefaultReset:") + Property->GetName();
			return false;
		}
		if (Job.Property < Saved.Properties.Num())
		{
			const auto& Value = Saved.Properties[Job.Property++];
			FProperty* Property = Target->GetClass()->FindPropertyByName(Value.Name);
			if (!Property) { Error = TEXT("PropertyMissing:") + Value.Name.ToString(); return false; }
			if (EnemyShipBundlePool::IsFixedBundleConfiguration(*Target, *Property)) return false;
			FString Text = Value.Value;
			for (int32 Index : Job.References)
				if (UObject* Mapped = Job.Objects.FindRef(Definition.Objects[Index].Key).Get()) EnemyShipBundlePool::RemapReference(Text, Definition.Objects[Index], *Mapped);
			// Raw editable imports must not expose a child Actor between queue slices.
			ON_SCOPE_EXIT
			{
				if (AActor* Actor = Cast<AActor>(Target)) { Actor->SetActorHiddenInGame(true); Actor->SetActorEnableCollision(false); Actor->SetActorTickEnabled(false); }
				if (UActorComponent* Component = Cast<UActorComponent>(Target)) Component->SetComponentTickEnabled(false);
				Ship->SetShipRuntimePhysicsEnabled(false);
			};
			if (Text.IsEmpty()) Property->ClearValue(Property->ContainerPtrToValuePtr<void>(Target));
			else if (!Property->ImportText_Direct(*Text, Property->ContainerPtrToValuePtr<void>(Target), Target, PPF_Delimited)) Error = TEXT("PropertyImport:") + Value.Name.ToString();
			return false;
		}
		++Job.Object; Job.bObjectStarted = false; return false;
	}
	case 5:
		Ship->Tags = Definition.Tags; Ship->SetBundleAuthoredName(Definition.AuthoredName);
		Ship->AuthoredEncounterArchetype = Cast<UEnemyShipArchetypeData>(Definition.Archetype.TryLoad());
		Ship->RefreshBundleStoryBindings(); Ship->GetNavigationComponent()->ResetSpawnHome(Definition.HomeTransform);
		Ship->SetActorEnableCollision(false); Job.Stage = 7; return false;
	case 7:
		Ship->SetShipRuntimePhysicsEnabled(false);
		Ship->SetActorTransform(Definition.HomeTransform, false, nullptr, ETeleportType::TeleportPhysics);
		++Job.Stage; return false;
	case 8:
		if (auto* Crew = Ship->GetDeckEnemySpawnerComponent()) Crew->ReconcileBundleProfile();
		++Definition.Assignment; ++Job.Stage; return false;
	case 9: Ship->PrepareFreshBundleEncounter(Definition.HomeTransform); Job.Mesh = 0; ++Job.Stage; return false;
	case 10:
	{
		// Each chest can construct a replicated actor; limit each step to one spawn point.
		TInlineComponentArray<UChildActorComponent*> ChildComponents(Ship);
		if (Job.Mesh >= ChildComponents.Num()) { ++Job.Stage; return false; }
		UChildActorComponent* Child = ChildComponents[Job.Mesh++];
		if (AChestSpawnPoint* Point = Cast<AChestSpawnPoint>(Child->GetChildActor()))
			if (Point->GetSpawnMode() == EChestSpawnMode::Guarded && !IsValid(Point->GetSpawnedActor()))
			{
				const int32 Seed = static_cast<int32>(HashCombine(GetTypeHash(EnemyShipBundlePool::ReadId(*Ship)), GetTypeHash(GetWorld()->GetTimeSeconds())));
				if (Point->SpawnConfiguredChest(nullptr, Seed))
					for (TActorIterator<AGlobalLootSpawnManager> It(GetWorld()); It; ++It) { It->RefillPooledGuardedChest(Point, Seed); break; }
				Ship->RefreshBundlePresentation();
			}
		return false;
	}
	case 11: SetBundleRegistration(*Ship, false, &Definition); Job.Stage = 12; return false;
	case 12:
		// Geometry and the local-space graph were prepared once when this Actor was constructed.
		if (!Ship->GetDeckWalkAreaComponent() || !Ship->GetDeckWalkAreaComponent()->IsReady())
		{ Error = TEXT("InitialDeckNotReady"); return false; }
		Ship->GetDeckWalkAreaComponent()->ResetRuntimeClaims();
		Job.Stage = 14; return false;
	case 14:
		if (auto* Crew = Ship->GetDeckEnemySpawnerComponent()) Crew->BeginQueuedPoolInitialization();
		++Job.Stage; return false;
	case 15:
		if (auto* Crew = Ship->GetDeckEnemySpawnerComponent())
		{
			const bool bComplete = Crew->StepQueuedPoolInitialization();
			Ship->RefreshBundlePresentation();
			if (!bComplete) return false;
			if (!Crew->AuditBundleCrew(true, Error)) return false;
		}
		++Job.Stage; return false;
	case 16: SetBundleRegistration(*Ship, false, &Definition); ++Job.Stage; return false;
	default:
		Placement->Assignment = Definition.Assignment;
		Placement->ActiveShip = Ship; Placement->bDormant = false; Placement->bUnloaded = false;
		Ship->OnDestroyed.AddUniqueDynamic(this, &AEnemyShipBundlePool::HandleShipDestroyed);
		if (auto* Swarm = GetWorld()->GetSubsystem<UShipSwarmSubsystem>()) Swarm->RegisterShip(Ship);
		if (auto* Weakening = GetWorld()->GetSubsystem<UEnemyShipWeakeningWorldSubsystem>()) Weakening->RegisterShip(Ship);
		Ship->BeginBundleActivation(); Ship->SetBundlePoolIdle(false);
		LastWakeErrors.Remove(Placement->StableId);
		ForceNetUpdate(); OnRep_Placements(); return true;
	}
}

void AEnemyShipBundlePool::Evaluate()
{
	if (!HasAuthority()) { OnRep_Placements(); /* AuditClientPresentation(); */ return; }
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
				EnqueueTransition(Placement, EEnemyShipPoolWork::Wake);
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
			if (!bOutstanding) EnqueueTransition(Placement, EEnemyShipPoolWork::Sleep);
		}
	}
	QueueCapacityMaintenance();
	OnRep_Placements();
	// LogPoolCountsIfChanged();
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
	/* Diagnostics muted after pooling/crew verification.
	UE_LOG(LogTemp, Display, TEXT("[EnemyShipPool] Event=%s Total=%d IdlePool=%d Capacity=%d Bundles=%d Active=%d Dormant=%d ISMShips=%d Destroyed=%d Unloaded=%d"),
		bLoggedPoolCounts ? TEXT("Changed") : TEXT("Initial"), Placements.Num(), Idle.Num(), BundleCapacity, CountActiveBundles() + Idle.Num(),
		Active, Dormant, VisibleISM, Destroyed, Unloaded); */ {}
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
			/* Diagnostics muted after pooling/crew verification.
			UE_LOG(LogTemp, Display, TEXT("[EnemyShipPoolAudit] Side=Client Event=%s Id=%s Ship=%s Assignment=%u Dormant=%d Actor=%s ActorMatches=%d PreviousReassigned=%d Hidden=%d Idle=%d RuntimePhysics=%d ActorTick=%d Collision=%d VisiblePrimitives=%d ChildVisible=%d SimBodies=%d AwakeBodies=%d BuoyancyActive=%d BuoyancyTick=%d ISMInstances=%d ISMTransformErrors=%d DuplicateVisible=%d MoveCM=%.2f RotateDeg=%.3f Position=%s VisibleExample=%s PhysicsExample=%s"),
				bPhaseChanged ? TEXT("Transition") : bSettled ? TEXT("Settled2s") : bUnexpectedMotion && !Previous.bMotionReported ? TEXT("DormantMotion") : TEXT("StateChanged"),
				*Placement.StableId.ToString(), *Placement.AuthoredName.ToString(), Placement.Assignment, Placement.bDormant, *GetNameSafe(Ship),
				Matching ? Matching->Num() : 0, bReassigned, IsValid(Ship) ? Ship->IsHidden() : -1,
				IsValid(Ship) ? Ship->IsBundlePoolIdle() : -1, IsValid(Ship) ? Ship->IsShipRuntimePhysicsEnabled() : -1,
				IsValid(Ship) ? Ship->IsActorTickEnabled() : -1, IsValid(Ship) ? Ship->GetActorEnableCollision() : -1,
				VisibleMeshes, ChildVisible, SimBodies, AwakeBodies, BuoyancyActive, BuoyancyTick, ISMCount, ISMTransformErrors,
				ISMCount > 0 && VisibleMeshes > 0, MoveCM, RotateDeg, IsValid(Ship) ? *Ship->GetActorLocation().ToString() : TEXT("None"),
				*VisibleExample, *PhysicsExample); */ {}
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
	bPresentationPending = true;
}

void AEnemyShipBundlePool::ProcessPresentation(double Deadline)
{
	if (HasAuthority()) { UpdatePresentation(); ForceNetUpdate(); }
	else
	{
		// Subobject data and Actor references may resolve after the pointer-array notification.
		// The existing evaluation timer retries presentation until both are available.
		Placements.RemoveAll([&](const FEnemyShipBundlePlacement& P) { return !PresentationNodes.ContainsByPredicate([&](const UEnemyShipBundlePresentation* Node) { return Node && Node->Definition.StableId == P.StableId; }); });
		TArray<FGuid> RemovedIds;
		for (const auto& Pair : PlacementPresentationHashes)
			if (!Placements.ContainsByPredicate([&](const FEnemyShipBundlePlacement& P) { return P.StableId == Pair.Key; })) RemovedIds.Add(Pair.Key);
		for (const FGuid& Id : RemovedIds)
		{
			FEnemyShipBundlePlacement Removed; Removed.StableId = Id;
			UpdatePlacementInstances(Removed, false);
			PlacementPresentationHashes.Remove(Id); ClientVisualKeys.Remove(Id); ClientActors.Remove(Id); ClientAssignments.Remove(Id);
			ClientActivationWaitReasons.Remove(Id);
		}
		for (const UEnemyShipBundlePresentation* Node : PresentationNodes)
		{
			if (!Node || !Node->Definition.StableId.IsValid()) continue;
			auto* Existing = Placements.FindByPredicate([&](const FEnemyShipBundlePlacement& P) { return P.StableId == Node->Definition.StableId; });
			if (!Existing)
			{
				Existing = &Placements.AddDefaulted_GetRef();
				Existing->StableId = Node->Definition.StableId;
				Existing->AuthoredName = Node->Definition.AuthoredName;
				Existing->HomeTransform = Node->Definition.HomeTransform;
				Existing->Meshes = Node->Definition.Meshes;
				for (const auto& Visual : Node->Definition.Objects)
				{
					auto& Saved = Existing->Objects.AddDefaulted_GetRef();
					Saved.Key = Visual.Key; Saved.Properties = Visual.Properties;
				}
			}
			auto& Placement = *Existing;
			Placement.Assignment = Node->State.Assignment;
			Placement.bDormant = Node->State.bDormant; Placement.bDestroyed = Node->State.bDestroyed;
			Placement.bUnloaded = Node->State.bUnloaded; Placement.bStoryHidden = Node->State.bStoryHidden;
			Placement.ActiveShip = Node->State.ActiveShip;
			Placement.ActivationGeneration = Node->State.ActivationGeneration;
			Placement.ActivationTransform = Node->State.ActivationTransform;
			Placement.ActivationLinearVelocity = Node->State.ActivationLinearVelocity;
			Placement.ActivationAngularVelocity = Node->State.ActivationAngularVelocity;
		}
	}
	if (GetNetMode() == NM_DedicatedServer) { bPresentationPending = false; return; }
	bPresentationPending = false;
	for (const auto& Placement : Placements)
	{
		if (FPlatformTime::Seconds() >= Deadline) { bPresentationPending = true; return; }
		bool bActivate = false;
		uint32 PresentationHash = GetTypeHash(Placement.StableId);
		if (!HasAuthority() && !Placement.bDormant && !Placement.bDestroyed && !Placement.bUnloaded)
		{
			FString Reason;
			if (!IsValid(Placement.ActiveShip)) Reason = TEXT("ActorReference");
			else
			{
				if (!Placement.ActiveShip->IsBundlePresentationReady()) Placement.ActiveShip->RefreshBundlePresentation();
				if (Placement.ActiveShip->IsBundlePoolIdle()) Reason = TEXT("ActorIdle");
				else if (Placement.ActivationGeneration == 0 || Placement.ActivationGeneration != Placement.ActiveShip->GetBundleActivationGeneration()) Reason = TEXT("GenerationMismatch");
				else if (!Placement.ActivationTransform.IsValid() || Placement.ActivationLinearVelocity.ContainsNaN() || Placement.ActivationAngularVelocity.ContainsNaN()) Reason = TEXT("InvalidPose");
				else if (!Placement.ActiveShip->IsBundlePresentationReady()) Reason = TEXT("PresentationNotReady");
			}
			if (!Reason.IsEmpty() && ClientActivationWaitReasons.FindRef(Placement.StableId) != Reason)
			{
				ClientActivationWaitReasons.Add(Placement.StableId, Reason);
				if (EnemyShipDiagnostics::QueueLogLevel() > 0)
					UE_LOG(LogEnemyShipInit, Display, TEXT("[EnemyShipQueue] Side=Client Event=ClientWait UTC=%s NowSec=%.6f GameSec=%.3f Id=%s Name=%s Assignment=%u Actor=%s Generation=%u ActorGeneration=%u Reason=%s"),
						*FDateTime::UtcNow().ToIso8601(), FPlatformTime::Seconds(), GetWorld()->GetTimeSeconds(), *Placement.StableId.ToString(),
						*Placement.AuthoredName.ToString(), Placement.Assignment, *GetNameSafe(Placement.ActiveShip.Get()), Placement.ActivationGeneration,
						IsValid(Placement.ActiveShip) ? Placement.ActiveShip->GetBundleActivationGeneration() : 0u, *Reason);
			}
		}
		PresentationHash = HashCombine(PresentationHash, GetTypeHash(Placement.StableId));
		PresentationHash = HashCombine(PresentationHash, GetTypeHash(Placement.ActiveShip.Get()));
		PresentationHash = HashCombine(PresentationHash, GetTypeHash(Placement.Assignment));
		PresentationHash = HashCombine(PresentationHash, GetTypeHash(Placement.ActivationGeneration));
		PresentationHash = HashCombine(PresentationHash, GetTypeHash(Placement.bDormant));
		PresentationHash = HashCombine(PresentationHash, GetTypeHash(Placement.bDestroyed));
		PresentationHash = HashCombine(PresentationHash, GetTypeHash(Placement.bUnloaded));
		PresentationHash = HashCombine(PresentationHash, GetTypeHash(Placement.bStoryHidden));
		PresentationHash = HashCombine(PresentationHash,
			GetTypeHash(IsValid(Placement.ActiveShip) && Placement.ActiveShip->IsBundlePoolIdle()));
		if (!HasAuthority() && IsValid(Placement.ActiveShip) && !Placement.bDormant && !Placement.bDestroyed && !Placement.bUnloaded
			&& !Placement.ActiveShip->IsBundlePoolIdle() && Placement.ActivationGeneration != 0
			&& Placement.ActivationGeneration == Placement.ActiveShip->GetBundleActivationGeneration()
			&& !Placement.ActiveShip->IsBundlePresentationReady())
		{
			if (Placement.ActiveShip->PrepareBundlePosition(Placement.ActivationGeneration, Placement.ActivationTransform))
			{
				bActivate = true;
				ClientAssignments.Add(Placement.StableId, Placement.Assignment);
				ClientActors.Add(Placement.StableId, Placement.ActiveShip.Get());
				ClientActivationWaitReasons.Remove(Placement.StableId);
			}
		}

		PresentationHash = HashCombine(PresentationHash,
			GetTypeHash(IsValid(Placement.ActiveShip)
				&& (HasAuthority() || Placement.ActivationGeneration == Placement.ActiveShip->GetBundleActivationGeneration())
				&& (Placement.ActiveShip->IsBundlePresentationReady() || Placement.ActiveShip->IsBundlePositionPrepared())));
		const bool bShowISM = !(Placement.bDestroyed || Placement.bUnloaded || Placement.bStoryHidden || (!Placement.bDormant && IsValid(Placement.ActiveShip)
			&& !Placement.ActiveShip->IsBundlePoolIdle()
			&& (HasAuthority() || Placement.ActivationGeneration == Placement.ActiveShip->GetBundleActivationGeneration())
			&& (Placement.ActiveShip->IsBundlePresentationReady() || Placement.ActiveShip->IsBundlePositionPrepared())));
		if (!PlacementPresentationHashes.Contains(Placement.StableId) || PlacementPresentationHashes.FindRef(Placement.StableId) != PresentationHash)
		{
			UpdatePlacementInstances(Placement, bShowISM);
			PlacementPresentationHashes.Add(Placement.StableId, PresentationHash);
			if (EnemyShipDiagnostics::QueueLogLevel() > 0 && !HasAuthority())
				UE_LOG(LogEnemyShipInit, Display, TEXT("[EnemyShipQueue] Side=Client Event=Presentation UTC=%s NowSec=%.6f GameSec=%.3f Id=%s Name=%s Assignment=%u Actor=%s Generation=%u ISM=%d Dormant=%d Destroyed=%d Unloaded=%d StoryHidden=%d"),
					*FDateTime::UtcNow().ToIso8601(), FPlatformTime::Seconds(), GetWorld()->GetTimeSeconds(), *Placement.StableId.ToString(),
					*Placement.AuthoredName.ToString(), Placement.Assignment, *GetNameSafe(Placement.ActiveShip.Get()), Placement.ActivationGeneration,
					bShowISM, Placement.bDormant, Placement.bDestroyed, Placement.bUnloaded, Placement.bStoryHidden);
		}
		if (bActivate)
		{
			Placement.ActiveShip->ActivatePreparedBundle(Placement.ActivationGeneration, Placement.ActivationLinearVelocity, Placement.ActivationAngularVelocity);
			if (EnemyShipDiagnostics::QueueLogLevel() > 0)
				UE_LOG(LogEnemyShipInit, Display, TEXT("[EnemyShipQueue] Side=Client Event=ClientActivate UTC=%s NowSec=%.6f GameSec=%.3f Id=%s Name=%s Assignment=%u Actor=%s Generation=%u Ready=%d Idle=%d Position=%s"),
					*FDateTime::UtcNow().ToIso8601(), FPlatformTime::Seconds(), GetWorld()->GetTimeSeconds(), *Placement.StableId.ToString(),
					*Placement.AuthoredName.ToString(), Placement.Assignment, *GetNameSafe(Placement.ActiveShip.Get()), Placement.ActivationGeneration,
					Placement.ActiveShip->IsBundlePresentationReady(), Placement.ActiveShip->IsBundlePoolIdle(), *Placement.ActiveShip->GetActorLocation().ToCompactString());
		}
	}
}

void AEnemyShipBundlePool::UpdatePlacementInstances(const FEnemyShipBundlePlacement& Placement, bool bShowISM)
{
	if (auto* Existing = AuditInstances.Find(Placement.StableId))
	{
		// Update tracked indices for both engine removal policies, including the global swap CVar.
		while (!Existing->IsEmpty())
		{
			const auto Removed = Existing->Pop(EAllowShrinking::No);
			if (auto* Batch = Removed.Component.Get())
			{
				const int32 Last = Batch->GetInstanceCount() - 1;
				const bool bSwap = Batch->SupportsRemoveSwap();
				if (Batch->RemoveInstance(Removed.Index))
					for (auto& Pair : AuditInstances)
						for (auto& Instance : Pair.Value)
							if (Instance.Component.Get() == Batch)
							{
								if (bSwap) { if (Instance.Index == Last) Instance.Index = Removed.Index; }
								else if (Instance.Index > Removed.Index) --Instance.Index;
							}
			}
		}
	}
	AuditInstances.Remove(Placement.StableId);
	if (!bShowISM) return;
	for (const auto& Visual : Placement.Meshes)
	{
		UStaticMesh* Mesh = Cast<UStaticMesh>(Visual.Mesh.TryLoad());
		if (!Mesh) continue;
		TArray<UMaterialInterface*> Materials;
		for (const auto& Path : Visual.Materials) Materials.Add(Cast<UMaterialInterface>(EnemyShipBundlePool::ISMMaterialPath(Path).TryLoad()));
		UInstancedStaticMeshComponent* Batch = nullptr;
		for (UInstancedStaticMeshComponent* Candidate : ProxyInstanceComponents)
		{
			if (!Candidate || Candidate->GetStaticMesh() != Mesh || Candidate->CastShadow != Visual.bCastShadow || Candidate->GetNumMaterials() != Materials.Num()) continue;
			bool bMatch = true;
			for (int32 Index = 0; Index < Materials.Num(); ++Index) if (Candidate->GetMaterial(Index) != Materials[Index]) { bMatch = false; break; }
			if (bMatch) { Batch = Candidate; break; }
		}
		if (!Batch)
		{
			Batch = NewObject<UInstancedStaticMeshComponent>(this);
			Batch->SetupAttachment(RootComponent); Batch->SetMobility(EComponentMobility::Movable);
			Batch->SetStaticMesh(Mesh); Batch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Batch->SetCanEverAffectNavigation(false); Batch->SetCastShadow(Visual.bCastShadow); Batch->SetRemoveSwap();
			for (int32 Index = 0; Index < Materials.Num(); ++Index) Batch->SetMaterial(Index, Materials[Index]);
			Batch->RegisterComponent(); ProxyInstanceComponents.Add(Batch);
		}
		auto& Instance = AuditInstances.FindOrAdd(Placement.StableId).AddDefaulted_GetRef();
		Instance.Component = Batch; Instance.ExpectedWorldTransform = Visual.RelativeTransform * Placement.HomeTransform;
		Instance.Index = Batch->AddInstance(Instance.ExpectedWorldTransform, true);
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
		/* Diagnostics muted after pooling/crew verification.
		UE_LOG(LogTemp, Error, TEXT("[EnemyShipPool] Event=SaveEncodingFailed RawBytes=%d EncodedBytes=%d Limit=%d"),
			Raw.Num(), Part.Bytes.Num(), EnemyShipBundlePool::MaxCompressedSaveBytes); */ {}
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
		/* UE_LOG(LogTemp, Display, TEXT("[EnemyShipPoolSave] Event=Encoded Ships=%d RawBytes=%d CompressedBytes=%d"),
			Placements.Num(), Raw.Num(), Part.Bytes.Num()); */
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
	CancelTransitions(TEXT("Restore"));
	DiscoverPlacedShips();
	RestoreClasses.Reset();
	for (const FSWRoomActorRecord& Record : Snapshot.Actors) RestoreClasses.Add(Record.StableId, Record.ClassPath);
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
	RestoreClasses.Reset();
	for (auto& Placement : Placements)
		if (IsValid(Placement.ActiveShip))
		{
			Placement.ActiveShip->BeginBundleActivation();
			if (Placement.ActiveShip->IsBundlePoolIdle()) Placement.ActiveShip->SetBundlePoolIdle(false);
			// EnemyShipBundlePool::AuditProfile(*Placement.ActiveShip, Placement, true);
			/* if (auto* Crew = Placement.ActiveShip->GetDeckEnemySpawnerComponent())
			{
				FString Error;
				Crew->AuditBundleCrew(false, Error);
			} */
		}
	MaintainBundleCapacity(true);
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
	CancelTransitions(TEXT("LevelRemoved"));
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
