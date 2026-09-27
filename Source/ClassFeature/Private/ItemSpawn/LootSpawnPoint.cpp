#include "ItemSpawn/LootSpawnPoint.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "BaseCharacter.h"
#include "AbilitySystemComponent.h"
#include "Item/BaseItem.h"
#include "ItemSpawn/ChestSpawnData.h"
#include "Balance/FixedChestDropData.h"
#include "Ship.h"
#include "Storage/StorageChest.h"
#include "StoryConditionalSpawner.h"
#include "ItemSpawn/GlobalLootSpawnManager.h"
#include "EngineUtils.h"

ALootSpawnPointBase::ALootSpawnPointBase()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;
}

void ALootSpawnPointBase::ResetSpawnPoint(bool bDestroySpawnedActor)
{
	if (bDestroySpawnedActor && IsValid(SpawnedActor))
	{
		SpawnedActor->Destroy();
	}

	SpawnedActor = nullptr;
	bActivated = false;
}

bool ALootSpawnPointBase::CanBeActivated() const
{
	return bEnabled && !bActivated && PointWeight > 0.f;
}

void ALootSpawnPointBase::MarkActivated(AActor* InSpawnedActor)
{
	SpawnedActor = InSpawnedActor;
	bActivated = true;
}

ABaseItem* ALooseLootSpawnPoint::SpawnLooseLoot(const FZoneLootItemRow& LootRow, TSubclassOf<ABaseItem> FallbackItemClass)
{
	if (!HasAuthority() || !CanBeActivated() || !LootRow.ItemTag.IsValid())
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	TSubclassOf<ABaseItem> ItemClass = LootRow.ItemClassOverride;
	if (!ItemClass)
	{
		ItemClass = ItemClassOverride;
	}
	if (!ItemClass)
	{
		ItemClass = FallbackItemClass;
	}
	if (!ItemClass)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	ABaseItem* SpawnedItem = World->SpawnActorDeferred<ABaseItem>(
		ItemClass,
		GetActorTransform(),
		this,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn
	);

	if (!IsValid(SpawnedItem))
	{
		return nullptr;
	}

	SpawnedItem->ItemTag = LootRow.ItemTag;
	SpawnedItem->FinishSpawning(GetActorTransform());
	AlignItemBottomToGround(SpawnedItem);
	MarkActivated(SpawnedItem);

	return SpawnedItem;
}

void ALooseLootSpawnPoint::AlignItemBottomToGround(ABaseItem* Item) const
{
	if (!bAlignItemBottomToGround || !IsValid(Item))
	{
		return;
	}

	UStaticMeshComponent* ItemMesh = Item->FindComponentByClass<UStaticMeshComponent>();
	if (!IsValid(ItemMesh) || !ItemMesh->GetStaticMesh())
	{
		return;
	}

	const FVector SpawnPointLocation = GetActorLocation();
	const FVector TraceStart = SpawnPointLocation + FVector::UpVector * GroundTraceUpDistance;
	const FVector TraceEnd = SpawnPointLocation - FVector::UpVector * GroundTraceDownDistance;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(LooseLootSpawnGroundTrace), false);
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(Item);

	FHitResult GroundHit;
	const bool bFoundGround = GetWorld()->LineTraceSingleByChannel(
		GroundHit,
		TraceStart,
		TraceEnd,
		ECC_Pawn,
		QueryParams
	);

	const float GroundZ = bFoundGround ? GroundHit.ImpactPoint.Z : SpawnPointLocation.Z;

	ItemMesh->UpdateBounds();
	const float MeshBottomZ = ItemMesh->Bounds.GetBox().Min.Z;
	const float VerticalOffset = GroundZ + GroundClearance - MeshBottomZ;

	Item->SetActorLocation(
		Item->GetActorLocation() + FVector::UpVector * VerticalOffset,
		false,
		nullptr,
		ETeleportType::TeleportPhysics
	);
}
void AChestSpawnPoint::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		for (AStoryConditionalSpawner* Spawner : GuardSpawners)
		{
			if (IsValid(Spawner))
			{
				Spawner->OnActorSpawned.AddUniqueDynamic(this, &AChestSpawnPoint::HandleGuardActorSpawned);
				if (AActor* AlreadySpawned = Spawner->GetSpawnedActor())
				{
					HandleGuardActorSpawned(AlreadySpawned);
				}
			}
		}

		// The global manager owns initial spawning and progression reward allocation.
		// Child actors can BeginPlay before their owning ship applies authoring settings.
	}
}

void AChestSpawnPoint::ApplyAuthoringSettings(
	const FChestSpawnPointChestSettings& ChestSettings,
	const FChestSpawnPointLootSettings& LootSettings)
{
	bIsBossChest = ChestSettings.bIsBossChest;
	RequiredBossTag = ChestSettings.RequiredBossTag;
	GuaranteedBossQuestItemTag = ChestSettings.GuaranteedBossQuestItemTag;
	GuaranteedBossQuestItemCount = FMath::Max(1, ChestSettings.GuaranteedBossQuestItemCount);
	Environment = ChestSettings.Environment;
	bEnableDistanceOptimization = ChestSettings.bEnableDistanceOptimization;
	SpawnMode = ChestSettings.SpawnMode;
	ProgressionZone = ChestSettings.ProgressionZone;
	ProgressionKind = ChestSettings.ProgressionKind;
	ChestClassOverride = ChestSettings.ChestClassOverride;
	RandomGroup = nullptr;
	ChestDefinition = nullptr; // Retired authoring input; the manager supplies progression loot.
	GuardCharacters = ChestSettings.GuardCharacters;
	GuardSpawners = ChestSettings.GuardSpawners;
	OwningShip = ChestSettings.OwningShip;

	bEnabled = LootSettings.bEnabled;
	PointWeight = FMath::Max(0.f, LootSettings.PointWeight);
	bAlignChestBottomToGround = LootSettings.bAlignChestBottomToGround;
	GroundClearance = FMath::Max(0.f, LootSettings.GroundClearance);
	GroundTraceUpDistance = FMath::Max(0.f, LootSettings.GroundTraceUpDistance);
	GroundTraceDownDistance = FMath::Max(0.f, LootSettings.GroundTraceDownDistance);
}

void AChestSpawnPoint::HandleGuardActorSpawned(AActor* InSpawnedActor)
{
	if (!HasAuthority() || !IsValid(InSpawnedActor))
	{
		return;
	}

	ABaseCharacter* GuardChar = Cast<ABaseCharacter>(InSpawnedActor);
	if (!GuardChar)
	{
		return;
	}

	RegisterGuardCharacter(GuardChar);

	if (!OwningShip && IsValid(ActiveChestInstance))
	{

		if (bIsBossChest && !bBossQuestItemInjected && GuaranteedBossQuestItemTag.IsValid() && HasMatchingBossGuard())
		{
			if (UStorageComponent* StorageComp = ActiveChestInstance->GetStorageComponent())
			{
				StorageComp->AddItem(GuaranteedBossQuestItemTag, FMath::Max(1, GuaranteedBossQuestItemCount));
				bBossQuestItemInjected = true;
				UE_LOG(LogTemp, Log, TEXT("AChestSpawnPoint: Dynamically added quest item [%s] to chest [%s] on boss spawn."),
					*GuaranteedBossQuestItemTag.ToString(), *ActiveChestInstance->GetName());
			}
		}
	}
}

bool AChestSpawnPoint::HasMatchingBossGuard() const
{
	if (!bIsBossChest || !RequiredBossTag.IsValid())
	{
		return false;
	}

	for (ABaseCharacter* GuardChar : GuardCharacters)
	{
		if (!IsValid(GuardChar))
		{
			continue;
		}

		// 1. ASC 태그 검사
		if (UAbilitySystemComponent* ASC = GuardChar->GetAbilitySystemComponent())
		{
			if (ASC->HasMatchingGameplayTag(RequiredBossTag))
			{
				return true;
			}
		}

		// 2. Actor의 태그 검사
		if (GuardChar->ActorHasTag(RequiredBossTag.GetTagName()) || GuardChar->ActorHasTag(FName(*RequiredBossTag.ToString())))
		{
			return true;
		}
	}

	return false;
}

AStorageChest* AChestSpawnPoint::SpawnConfiguredChest(UChestDefinition* Definition, int32 Seed)
{
	if (!HasAuthority() || !CanSpawnDataDrivenChest())
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	TSubclassOf<AStorageChest> SpawnClass = ChestClassOverride;
	if (!SpawnClass && IsValid(Definition)) SpawnClass = Definition->ChestClass;
	if (!SpawnClass) SpawnClass = AStorageChest::StaticClass();
	AStorageChest* SpawnedChest = World->SpawnActorDeferred<AStorageChest>(
		SpawnClass,
		GetActorTransform(),
		this,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!IsValid(SpawnedChest))
	{
		return nullptr;
	}

	ActiveChestInstance = SpawnedChest;
	if (Definition) SpawnedChest->InitializeFromChestDefinition(Definition, Seed);
	else SpawnedChest->ClearLegacyChestDefinition();
	SpawnedChest->SetPhysicsAndBuoyancyEnabled(Environment == EChestEnvironment::Water);
	SpawnedChest->SetDistanceOptimizationEnabled(
		Environment == EChestEnvironment::Water && bEnableDistanceOptimization);

	AShip* EffectiveOwningShip = OwningShip ? OwningShip.Get() : Cast<AShip>(GetAttachParentActor());

	TArray<ABaseCharacter*> Guards;
	Guards.Reserve(GuardCharacters.Num());
	for (ABaseCharacter* Guard : GuardCharacters)
	{
		Guards.Add(Guard);
	}
	SpawnedChest->ConfigureGuarding(SpawnMode == EChestSpawnMode::Guarded, Guards, EffectiveOwningShip);

	// 보스 상자이고 요구되는 보스 가드가 확인되면 확정 퀘스트 아이템 추가
	if (!EffectiveOwningShip && bIsBossChest && GuaranteedBossQuestItemTag.IsValid() && HasMatchingBossGuard())
	{
		if (UStorageComponent* StorageComp = SpawnedChest->GetStorageComponent())
		{
			StorageComp->AddItem(GuaranteedBossQuestItemTag, FMath::Max(1, GuaranteedBossQuestItemCount));
			bBossQuestItemInjected = true;
			UE_LOG(LogTemp, Log, TEXT("AChestSpawnPoint::SpawnConfiguredChest - Added guaranteed quest item [%s] to chest [%s] guarded by boss."),
				*GuaranteedBossQuestItemTag.ToString(), *SpawnedChest->GetName());
		}
	}

	SpawnedChest->SetBossEncounterReserved(bBossEncounterReserved);
	if (ABaseCharacter* Boss = BossGuard.Get()) SpawnedChest->AddBossGuardCharacter(Boss);
	SpawnedChest->FinishSpawning(GetActorTransform());

	if (SpawnMode == EChestSpawnMode::Guarded && IsValid(EffectiveOwningShip))
	{
		SpawnedChest->AttachToActor(EffectiveOwningShip, FAttachmentTransformRules::KeepWorldTransform);
	}

	if (Environment != EChestEnvironment::Water)
	{
		AlignChestBottomToGround(SpawnedChest);
	}

	MarkActivated(SpawnedChest);
	OnChestSpawned.Broadcast(SpawnedChest);
	return SpawnedChest;
}

void AChestSpawnPoint::SetEnvironment(EChestEnvironment InEnvironment)
{
	Environment = InEnvironment;
	if (Environment == EChestEnvironment::Water)
	{
		bAlignChestBottomToGround = false;
	}
	else
	{
		bAlignChestBottomToGround = true;
	}
}

void AChestSpawnPoint::ConfigureRandomSpawn(EProgressionZone InZone, EProgressionChestKind InKind, float InPointWeight)
{
	SpawnMode = EChestSpawnMode::Random;
	ProgressionZone = InZone;
	ProgressionKind = InKind;
	RandomGroup = nullptr;
	ChestDefinition = nullptr;
	GuardCharacters.Reset();
	OwningShip = nullptr;
	PointWeight = FMath::Max(0.f, InPointWeight);
}

void AChestSpawnPoint::ConfigureGuardedSpawn(
	UChestDefinition* InChestDefinition,
	const TArray<ABaseCharacter*>& InGuardCharacters,
	AShip* InOwningShip)
{
	SpawnMode = EChestSpawnMode::Guarded;
	RandomGroup = nullptr;
	ChestDefinition = InChestDefinition;
	GuardCharacters.Reset();
	for (ABaseCharacter* Guard : InGuardCharacters)
	{
		GuardCharacters.Add(Guard);
	}
	OwningShip = InOwningShip;
	if (OwningShip)
	{
		SetEnvironment(EChestEnvironment::ShipDeck);
	}
}

void AChestSpawnPoint::AlignChestBottomToGround(AStorageChest* Chest) const
{
	if (!bAlignChestBottomToGround || !IsValid(Chest) || Environment == EChestEnvironment::Water)
	{
		return;
	}

	UStaticMeshComponent* ChestMesh = Chest->GetChestMesh();
	if (!IsValid(ChestMesh) || !ChestMesh->GetStaticMesh())
	{
		return;
	}

	const FVector SpawnPointLocation = GetActorLocation();
	const FVector TraceStart = SpawnPointLocation + FVector::UpVector * GroundTraceUpDistance;
	const FVector TraceEnd = SpawnPointLocation - FVector::UpVector * GroundTraceDownDistance;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ChestSpawnGroundTrace), false);
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(Chest);

	FHitResult GroundHit;
	bool bFoundGround = false;

	// 1. 배에 배치된 경우: 배의 ShipDeckMesh(CollisionProfile == ShipDeck)를 찾아 직접 Component 라인트레이스 수행
	AShip* TargetShip = OwningShip ? OwningShip.Get() : Cast<AShip>(GetAttachParentActor());
	if (TargetShip)
	{
		TArray<UStaticMeshComponent*> StaticMeshes;
		TargetShip->GetComponents<UStaticMeshComponent>(StaticMeshes);
		for (UStaticMeshComponent* MeshComp : StaticMeshes)
		{
			if (MeshComp && MeshComp->GetCollisionProfileName() == TEXT("ShipDeck"))
			{
				bFoundGround = MeshComp->LineTraceComponent(
					GroundHit,
					TraceStart,
					TraceEnd,
					QueryParams
				);
				break;
			}
		}
	}

	// 2. 일반 지상/육지인 경우: ECC_WorldStatic(Landscape/Mesh) -> ECC_Visibility -> ECC_Pawn 순으로 지형 검사
	if (!bFoundGround)
	{
		bFoundGround = GetWorld()->LineTraceSingleByChannel(
			GroundHit,
			TraceStart,
			TraceEnd,
			ECC_WorldStatic,
			QueryParams
		);
	}
	if (!bFoundGround)
	{
		bFoundGround = GetWorld()->LineTraceSingleByChannel(
			GroundHit,
			TraceStart,
			TraceEnd,
			ECC_Visibility,
			QueryParams
		);
	}
	if (!bFoundGround)
	{
		bFoundGround = GetWorld()->LineTraceSingleByChannel(
			GroundHit,
			TraceStart,
			TraceEnd,
			ECC_Pawn,
			QueryParams
		);
	}

	const float GroundZ = bFoundGround ? GroundHit.ImpactPoint.Z : SpawnPointLocation.Z;

	ChestMesh->UpdateBounds();
	const float MeshBottomZ = ChestMesh->Bounds.GetBox().Min.Z;
	const float VerticalOffset = GroundZ + GroundClearance - MeshBottomZ;

	Chest->SetActorLocation(
		Chest->GetActorLocation() + FVector::UpVector * VerticalOffset,
		false,
		nullptr,
		ETeleportType::TeleportPhysics
	);
}

void AChestSpawnPoint::RegisterGuardCharacter(ABaseCharacter* GuardCharacter)
{
	if (!HasAuthority() || !IsValid(GuardCharacter)) return;
	GuardCharacters.AddUnique(GuardCharacter);
	if (IsValid(ActiveChestInstance)) ActiveChestInstance->AddGuardCharacter(GuardCharacter);
}

void AChestSpawnPoint::UnregisterGuardCharacter(ABaseCharacter* GuardCharacter)
{
	if (!HasAuthority() || !GuardCharacter) return;
	GuardCharacters.Remove(GuardCharacter);
	if (IsValid(ActiveChestInstance)) ActiveChestInstance->RemoveGuardCharacter(GuardCharacter);
}

void AChestSpawnPoint::SetBossEncounterReserved(bool bReserved)
{
	if (!HasAuthority()) return;
	bBossEncounterReserved = bReserved;
	if (IsValid(ActiveChestInstance)) ActiveChestInstance->SetBossEncounterReserved(bReserved);
}

void AChestSpawnPoint::RegisterBossGuard(ABaseCharacter* Boss)
{
	if (!HasAuthority() || !IsValid(Boss)) return;
	BossGuard = Boss;
	if (IsValid(ActiveChestInstance)) ActiveChestInstance->AddBossGuardCharacter(Boss);
	int32 ManagerCount = 0;
	AGlobalLootSpawnManager* Manager = nullptr;
	for (TActorIterator<AGlobalLootSpawnManager> It(GetWorld()); It; ++It)
	{
		Manager = *It;
		++ManagerCount;
	}
	if (ManagerCount == 1) Manager->EnsureBossGuaranteedLoot(this);
	else UE_LOG(LogTemp, Error, TEXT("Boss loot requires exactly one manager; found %d"), ManagerCount);
}

void AChestSpawnPoint::ApplyFixedChanceDrops(const UFixedChestDropData* DropData, int32 Seed)
{
	AStorageChest* Chest = Cast<AStorageChest>(GetSpawnedActor());
	if (!HasAuthority() || !IsValid(Chest) || !DropData) return;
	FRandomStream Stream(Seed);
	TArray<FStorageItemEntry> AddedItems;
	for (const FFixedChestDropEntry& Entry : DropData->Drops)
	{
		const float Chance = Entry.GetChance(ProgressionZone);
		if (!Entry.ItemTag.IsValid() || Entry.Quantity < 1 || Chance <= 0.f) continue;
		const float Roll = Stream.FRand();
		const bool bDropped = Roll < Chance;
		UE_LOG(LogTemp, Log, TEXT("Fixed chest roll: point=%s zone=%d item=%s chance=%.4f roll=%.4f dropped=%d"),
			*GetNameSafe(this), static_cast<int32>(ProgressionZone), *Entry.ItemTag.ToString(), Chance, Roll, bDropped ? 1 : 0);
		if (bDropped)
		{
			FStorageItemEntry& Item = AddedItems.AddDefaulted_GetRef();
			Item.ItemTag = Entry.ItemTag;
			Item.Count = Entry.Quantity;
		}
	}
	Chest->AppendFixedLoot(AddedItems);
}
