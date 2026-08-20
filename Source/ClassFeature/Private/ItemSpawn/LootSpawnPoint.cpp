#include "ItemSpawn/LootSpawnPoint.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "BaseCharacter.h"
#include "Item/BaseItem.h"
#include "ItemSpawn/ChestSpawnData.h"
#include "Ship.h"
#include "Storage/StorageChest.h"

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

AStorageChest* AChestSpawnPoint::SpawnChest(const TArray<FChestInitialLootRow>& LootRows, TSubclassOf<AStorageChest> FallbackChestClass, int32 Seed)
{
	if (!HasAuthority() || !CanBeActivated() || IsDataDrivenChestPoint())
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	TSubclassOf<AStorageChest> ChestClass = ChestClassOverride ? ChestClassOverride : FallbackChestClass;
	if (!ChestClass)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	AStorageChest* SpawnedChest = World->SpawnActor<AStorageChest>(
		ChestClass,
		GetActorTransform(),
		SpawnParameters
	);

	if (!IsValid(SpawnedChest))
	{
		return nullptr;
	}

	AlignChestBottomToGround(SpawnedChest);
	SpawnedChest->ConfigureStorage(SlotCount, ColumnCount, BuildInitialItems(LootRows, Seed));
	MarkActivated(SpawnedChest);

	return SpawnedChest;
}

AStorageChest* AChestSpawnPoint::SpawnConfiguredChest(UChestDefinition* Definition, int32 Seed)
{
	if (!HasAuthority() || !CanSpawnDataDrivenChest() || !IsValid(Definition) || !Definition->ChestClass)
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	AStorageChest* SpawnedChest = World->SpawnActorDeferred<AStorageChest>(
		Definition->ChestClass,
		GetActorTransform(),
		this,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!IsValid(SpawnedChest))
	{
		return nullptr;
	}

	SpawnedChest->InitializeFromChestDefinition(Definition, Seed);
	const bool bUseBuoyancy = bEnablePhysicsAndBuoyancy || (Environment == EChestEnvironment::Water);
	SpawnedChest->SetPhysicsAndBuoyancyEnabled(bUseBuoyancy);

	AShip* EffectiveOwningShip = OwningShip ? OwningShip.Get() : Cast<AShip>(GetAttachParentActor());

	TArray<ABaseCharacter*> Guards;
	Guards.Reserve(GuardCharacters.Num());
	for (ABaseCharacter* Guard : GuardCharacters)
	{
		Guards.Add(Guard);
	}
	SpawnedChest->ConfigureGuarding(SpawnMode == EChestSpawnMode::Guarded, Guards, EffectiveOwningShip);
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
	return SpawnedChest;
}

void AChestSpawnPoint::SetEnvironment(EChestEnvironment InEnvironment)
{
	Environment = InEnvironment;
	if (Environment == EChestEnvironment::Water)
	{
		bEnablePhysicsAndBuoyancy = true;
		bAlignChestBottomToGround = false;
	}
	else
	{
		bEnablePhysicsAndBuoyancy = false;
		bAlignChestBottomToGround = true;
	}
}

void AChestSpawnPoint::ConfigureRandomSpawn(URandomChestGroup* InRandomGroup, float InPointWeight)
{
	SpawnMode = EChestSpawnMode::Random;
	RandomGroup = InRandomGroup;
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

	// 2. 일반 지상/육지인 경우: ECC_Pawn 채널로 지형 검사 (지형과 갑판 모두 Pawn Block 반응)
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

TArray<FStorageItemEntry> AChestSpawnPoint::BuildInitialItems(const TArray<FChestInitialLootRow>& LootRows, int32 Seed) const
{
	return UChestDefinition::RollItemsFromRows(LootRows, InitialItemRollCount, Seed);
}
