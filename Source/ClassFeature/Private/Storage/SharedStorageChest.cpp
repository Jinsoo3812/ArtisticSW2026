#include "Storage/SharedStorageChest.h"
#include "Storage/SharedStorageSaveGame.h"
#include "Async/Async.h"
#include "Components/StaticMeshComponent.h"
#include "InteractableComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/SecureHash.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "BasePlayerController.h"
#include "BasePlayer.h"

ASharedStorageChest::ASharedStorageChest()
{
	bEnablePhysicsAndBuoyancy = false;
	bDestroyWhenEmpty = false;
	StorageName = NSLOCTEXT("SharedStorage", "Name", "Shared Storage");
	SetReplicateMovement(false);
}

void ASharedStorageChest::PostActorCreated()
{
	Super::PostActorCreated();
	if (!IsTemplate()) PersistentChestId = FGuid::NewGuid();
}

void ASharedStorageChest::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);
	if (DuplicateMode == EDuplicateMode::Normal && !IsTemplate()) PersistentChestId = FGuid::NewGuid();
}

bool ASharedStorageChest::CanPlayerAccess(const APawn* Pawn) const
{
	if (!IsValid(Pawn) || !InteractableComponent || IsLocked()) return false;
	const ABasePlayer* BasePlayer = Cast<ABasePlayer>(Pawn);
	const float Reach = BasePlayer ? BasePlayer->GetInteractionReach() : Pawn->GetSimpleCollisionRadius();
	return FVector::DistSquared(Pawn->GetActorLocation(), InteractableComponent->GetComponentLocation())
		<= FMath::Square(InteractableComponent->GetScaledSphereRadius() + Reach);
}

void ASharedStorageChest::BeginPlay()
{
	// These are invariants of the fixed storage type, including for derived Blueprints.
	bEnablePhysicsAndBuoyancy = false;
	bDestroyWhenEmpty = false;
	bRequiresGuardClear = false;
	ChestDefinition = nullptr;
	Super::BeginPlay();
	ChestMesh->SetSimulatePhysics(false);
	ChestMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	if (!HasAuthority()) return;
	StorageComponent->ConfigureTabbedStorage(FMath::Clamp(InitialSlotsPerTab, 25, 10000));
	if (!PersistentChestId.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Shared storage %s has no persistent ID. Replace this editor instance before using it."), *GetPathName());
		SetLocked(true);
		return;
	}
	for (TActorIterator<ASharedStorageChest> It(GetWorld()); It; ++It)
	{
		if (*It != this && IsValid(*It) && !It->IsActorBeingDestroyed() && It->PersistentChestId == PersistentChestId && It->SaveNamespace == SaveNamespace)
		{
			UE_LOG(LogTemp, Error, TEXT("Duplicate shared storage ID: %s"), *GetPathName());
			SetLocked(true);
			return;
		}
	}
	const FString Key = SaveNamespace + TEXT("_") + PersistentChestId.ToString();
	SaveSlot = TEXT("SharedChest_") + FMD5::HashAnsiString(*Key);
	if (GetWorld()->WorldType == EWorldType::PIE) SaveSlot = TEXT("PIE_") + SaveSlot;
	if (UGameplayStatics::DoesSaveGameExist(SaveSlot, 0))
	{
		USharedStorageSaveGame* Save = Cast<USharedStorageSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlot, 0));
		if (!Save || Save->Version != 1 || Save->SlotsPerTab < 1 || Save->SlotsPerTab > 10000
			|| !StorageComponent->ConfigureTabbedStorage(InitialSlotsPerTab, Save->Slots, Save->SlotsPerTab))
		{
			UE_LOG(LogTemp, Error, TEXT("Unable to restore %s. Storage locked; existing save will not be overwritten."), *SaveSlot);
			SetLocked(true);
			return;
		}
	}
	bPersistenceReady = true;
	StorageComponent->OnStorageChanged.AddUObject(this, &ASharedStorageChest::MarkStorageDirty);
	GetWorldTimerManager().SetTimer(SaveTimer, this, &ASharedStorageChest::PumpSave, 1.0f, true);
}

bool ASharedStorageChest::ExpandStorage(int32 NewSlotsPerTab)
{
	return HasAuthority() && bPersistenceReady && NewSlotsPerTab >= StorageComponent->GetSlotsPerTab()
		&& StorageComponent->ConfigureTabbedStorage(NewSlotsPerTab);
}

void ASharedStorageChest::MarkStorageDirty()
{
	bDirty = true;
	ForceNetUpdate();
}

void ASharedStorageChest::PumpSave()
{
	if (PendingSave.IsValid())
	{
		if (!PendingSave.IsReady()) return;
		if (!PendingSave.Get())
		{
			bDirty = true;
			UE_LOG(LogTemp, Error, TEXT("Shared storage save failed; retrying: %s"), *SaveSlot);
		}
		PendingSave = TFuture<bool>();
	}
	if (!bDirty || !bPersistenceReady) return;
	USharedStorageSaveGame* Save = NewObject<USharedStorageSaveGame>();
	Save->SlotsPerTab = StorageComponent->GetSlotsPerTab();
	Save->Slots = StorageComponent->GetPersistentSlots();
	TArray<uint8> Bytes;
	if (!UGameplayStatics::SaveGameToMemory(Save, Bytes)) return;
	bDirty = false;
	// Only immutable bytes cross threads. One writer per chest, with a join at EndPlay.
	PendingSave = Async(EAsyncExecution::ThreadPool, [Bytes = MoveTemp(Bytes), Slot = SaveSlot]()
	{
		return UGameplayStatics::SaveDataToSlot(Bytes, Slot, 0);
	});
}

bool ASharedStorageChest::SaveSynchronously()
{
	USharedStorageSaveGame* Save = NewObject<USharedStorageSaveGame>();
	Save->SlotsPerTab = StorageComponent->GetSlotsPerTab();
	Save->Slots = StorageComponent->GetPersistentSlots();
	return UGameplayStatics::SaveGameToSlot(Save, SaveSlot, 0);
}

void ASharedStorageChest::EndPlay(const EEndPlayReason::Type Reason)
{
	StorageComponent->ReturnAllReservedCursors();
	GetWorldTimerManager().ClearTimer(SaveTimer);
	StorageComponent->OnStorageChanged.RemoveAll(this);
	if (PendingSave.IsValid())
	{
		if (!PendingSave.Get()) bDirty = true;
		PendingSave = TFuture<bool>();
	}
	if (HasAuthority() && bPersistenceReady && bDirty && !SaveSynchronously())
		UE_LOG(LogTemp, Error, TEXT("Failed to flush shared storage: %s"), *SaveSlot);
	if (HasAuthority())
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			if (ABasePlayerController* PC = Cast<ABasePlayerController>(It->Get())) PC->CloseStorageFromServer(this);
	Super::EndPlay(Reason);
}
