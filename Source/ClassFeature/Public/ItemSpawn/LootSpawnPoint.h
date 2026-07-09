#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LootSpawnTypes.h"
#include "Storage/StorageComponent.h"
#include "LootSpawnPoint.generated.h"

class ABaseItem;
class AStorageChest;

UCLASS(Abstract)
class CLASSFEATURE_API ALootSpawnPointBase : public AActor
{
	GENERATED_BODY()

public:
	ALootSpawnPointBase();

	UFUNCTION(BlueprintCallable, Category = "Loot|Spawn")
	virtual void ResetSpawnPoint(bool bDestroySpawnedActor);

	UFUNCTION(BlueprintPure, Category = "Loot|Spawn")
	bool CanBeActivated() const;

	UFUNCTION(BlueprintPure, Category = "Loot|Spawn")
	bool IsActivated() const { return bActivated; }

	UFUNCTION(BlueprintPure, Category = "Loot|Spawn")
	float GetPointWeight() const { return PointWeight; }

	UFUNCTION(BlueprintPure, Category = "Loot|Spawn")
	FName GetZoneId() const { return ZoneId; }

protected:
	void MarkActivated(AActor* InSpawnedActor);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Spawn")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Spawn", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PointWeight = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Spawn")
	FName ZoneId = NAME_None;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Loot|Spawn")
	bool bActivated = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Loot|Spawn")
	TObjectPtr<AActor> SpawnedActor = nullptr;
};

UCLASS()
class CLASSFEATURE_API ALooseLootSpawnPoint : public ALootSpawnPointBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Loot|Spawn")
	ABaseItem* SpawnLooseLoot(const FZoneLootItemRow& LootRow, TSubclassOf<ABaseItem> FallbackItemClass);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Spawn")
	TSubclassOf<ABaseItem> ItemClassOverride = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Placement")
	bool bAlignItemBottomToGround = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Placement", meta = (EditCondition = "bAlignItemBottomToGround", ClampMin = "0.0", UIMin = "0.0"))
	float GroundClearance = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Placement", meta = (EditCondition = "bAlignItemBottomToGround", ClampMin = "0.0", UIMin = "0.0"))
	float GroundTraceUpDistance = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Placement", meta = (EditCondition = "bAlignItemBottomToGround", ClampMin = "0.0", UIMin = "0.0"))
	float GroundTraceDownDistance = 1000.f;

private:
	void AlignItemBottomToGround(ABaseItem* Item) const;
};

UCLASS()
class CLASSFEATURE_API AChestSpawnPoint : public ALootSpawnPointBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Loot|Spawn")
	AStorageChest* SpawnChest(const TArray<FChestInitialLootRow>& LootRows, TSubclassOf<AStorageChest> FallbackChestClass, int32 Seed);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Spawn")
	TSubclassOf<AStorageChest> ChestClassOverride = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Storage", meta = (ClampMin = "1", UIMin = "1"))
	int32 SlotCount = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Storage", meta = (ClampMin = "1", UIMin = "1"))
	int32 ColumnCount = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Storage", meta = (ClampMin = "0", UIMin = "0"))
	int32 InitialItemRollCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Placement")
	bool bAlignChestBottomToGround = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Placement", meta = (EditCondition = "bAlignChestBottomToGround", ClampMin = "0.0", UIMin = "0.0"))
	float GroundClearance = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Placement", meta = (EditCondition = "bAlignChestBottomToGround", ClampMin = "0.0", UIMin = "0.0"))
	float GroundTraceUpDistance = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Placement", meta = (EditCondition = "bAlignChestBottomToGround", ClampMin = "0.0", UIMin = "0.0"))
	float GroundTraceDownDistance = 1000.f;

private:
	void AlignChestBottomToGround(AStorageChest* Chest) const;
	TArray<FStorageItemEntry> BuildInitialItems(const TArray<FChestInitialLootRow>& LootRows, int32 Seed) const;
};
