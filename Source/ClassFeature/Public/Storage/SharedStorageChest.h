#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Storage/StorageChest.h"
#include "SharedStorageChest.generated.h"

/** Persistent, fixed, publicly shared storage. Place directly or derive a Blueprint. */
UCLASS()
class CLASSFEATURE_API ASharedStorageChest : public AStorageChest
{
	GENERATED_BODY()
public:
	ASharedStorageChest();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void PostActorCreated() override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
	bool CanPlayerAccess(const APawn* Pawn) const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shared Storage")
	bool ExpandStorage(int32 NewSlotsPerTab);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shared Storage", meta = (ClampMin = "25", ClampMax = "10000"))
	int32 InitialSlotsPerTab = 25;

	/** Generated for editor placement/duplication. Keep stable once a chest has saved data. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shared Storage|Save")
	FGuid PersistentChestId;

	/** Separate profiles/world saves by changing this on placed chests. PIE is isolated automatically. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shared Storage|Save")
	FString SaveNamespace = TEXT("SharedStorage");

private:
	void MarkStorageDirty();
	void PumpSave();
	bool SaveSynchronously();
	FString SaveSlot;
	FTimerHandle SaveTimer;
	TFuture<bool> PendingSave;
	bool bDirty = false;
	bool bPersistenceReady = false;
};
