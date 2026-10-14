#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "Storage/StorageChestPoolTypes.h"
#include "StorageChestPool.generated.h"

class AStorageChest;
class AChestSpawnPoint;
class UInstancedStaticMeshComponent;
class UActorChannel;
class FOutBunch;
struct FReplicationFlags;

USTRUCT()
struct FStorageChestClassPool
{
	GENERATED_BODY()
	UPROPERTY() FSoftClassPath ClassPath;
	UPROPERTY() TArray<TObjectPtr<AStorageChest>> IdleChests;
};

UCLASS()
class CLASSFEATURE_API UStorageChestPoolPresentation : public UObject
{
	GENERATED_BODY()
public:
	virtual bool IsSupportedForNetworking() const override { return true; }
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UPROPERTY(Replicated) FGuid StableId;
	UPROPERTY(Replicated) FTransform Transform;
	UPROPERTY(Replicated) TArray<FStorageChestPoolMesh> Meshes;
	UPROPERTY(Replicated) bool bDormant = false;
	UPROPERTY(Replicated) TObjectPtr<AStorageChest> ActiveChest;
};

/** Server-owned random chest Actor pool; clients render dormant logical placements with ISMs. */
UCLASS()
class CLASSFEATURE_API AStorageChestPool : public AActor
{
	GENERATED_BODY()
public:
	AStorageChestPool();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch, FReplicationFlags* RepFlags) override;
	/** Same capacity policy as EnemyShip: active demand may exceed the baseline. */
	UPROPERTY(EditAnywhere, Category="Storage Chest|Optimization", meta=(ClampMin="0")) int32 PoolCapacityPerClass = 5;
	void ForgetPoint(AChestSpawnPoint* Point);
	static bool CaptureChest(AStorageChest& Chest, FStorageChestPoolPlacement& Placement, FString& Error);
	static bool RestoreChestConfiguration(AStorageChest& Chest, const FStorageChestPoolPlacement& Placement, FString& Error);
	static bool IsEligible(const AChestSpawnPoint& Point, const AStorageChest* Chest);
	void Evaluate();
	bool MaterializeForLootRebalance();
private:
	friend class FStorageChestPoolLifecycleTest;
	friend class FStorageChestPoolClassBudgetTest;
	FStorageChestClassPool& GetClassPool(const FSoftClassPath& ClassPath);
	void MaintainCapacity(FStorageChestClassPool& Pool, int32 ActiveCount);
	void Discover();
	bool Wake(AChestSpawnPoint& Point, FString& Error);
	bool Sleep(AChestSpawnPoint& Point, FString& Error);
	AStorageChest* Allocate(AChestSpawnPoint& Point, const FStorageChestPoolPlacement& Placement);
	void Park(AStorageChest& Chest);
	void Publish();
	void RenderPresentation();
	bool IsNear(const FVector& Location, float Range) const;
	void PrepareRestore(const FSWRoomWorldSnapshot& Snapshot, FString& Error);
	void RestoreCompleted();
	UPROPERTY(Transient) TArray<FStorageChestClassPool> ClassPools;
	UPROPERTY(Replicated) TArray<TObjectPtr<UStorageChestPoolPresentation>> Presentations;
	UPROPERTY(Transient) TArray<TObjectPtr<UInstancedStaticMeshComponent>> ProxyInstanceComponents;
	TArray<TWeakObjectPtr<AChestSpawnPoint>> Points;
	TArray<TWeakObjectPtr<AChestSpawnPoint>> PendingPoints;
	TMap<FGuid, double> SettledSince;
	uint32 PresentationHash = 0;
	FTimerHandle EvaluationTimer;
	FDelegateHandle PrepareRestoreHandle;
	FDelegateHandle RestoreCompletedHandle;
	bool bPreparingRestore = false;
};

UCLASS()
class CLASSFEATURE_API UStorageChestPoolSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual void OnWorldBeginPlay(UWorld& World) override;
	AStorageChestPool* GetPool() const { return Pool; }
private:
	UPROPERTY(Transient) TObjectPtr<AStorageChestPool> Pool;
};

