#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "Room/SWRoomStateAdapter.h"
#include "EnemyShipBundlePool.generated.h"

class AEnemyShip;
class UInstancedStaticMeshComponent;
class ULevel;

USTRUCT()
struct FEnemyShipBundleProperty
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FName Name;
	UPROPERTY(SaveGame) FString Value;
};

USTRUCT()
struct FEnemyShipBundleObject
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FString Key;
	UPROPERTY(SaveGame) FString SourcePath;
	UPROPERTY(SaveGame) FSoftClassPath ClassPath;
	UPROPERTY(SaveGame) FGuid StableId;
	UPROPERTY(SaveGame) bool bSnapshot = false;
	UPROPERTY(SaveGame) bool bLevelPlaced = false;
	UPROPERTY(SaveGame) ESWRoomPersistenceClass Persistence = ESWRoomPersistenceClass::ManualOnly;
	UPROPERTY(SaveGame) TArray<FEnemyShipBundleProperty> Properties;
};

USTRUCT()
struct FEnemyShipBundleMesh
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FSoftObjectPath Mesh;
	UPROPERTY(SaveGame) TArray<FSoftObjectPath> Materials;
	UPROPERTY(SaveGame) FTransform RelativeTransform;
	UPROPERTY(SaveGame) bool bCastShadow = true;
};

USTRUCT()
struct FEnemyShipBundlePlacement
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FGuid StableId;
	UPROPERTY(SaveGame) FSoftClassPath ClassPath;
	UPROPERTY(SaveGame) FName AuthoredName;
	UPROPERTY(SaveGame) FName SquadId;
	UPROPERTY(SaveGame) FSoftObjectPath Archetype;
	UPROPERTY(SaveGame) FSoftObjectPath OrbitArchetype;
	UPROPERTY(SaveGame) FTransform HomeTransform;
	UPROPERTY(SaveGame) FSoftObjectPath LevelPackage;
	UPROPERTY(SaveGame) TArray<FName> Tags;
	UPROPERTY(SaveGame) TArray<FEnemyShipBundleObject> Objects;
	UPROPERTY(SaveGame) TArray<FEnemyShipBundleMesh> Meshes;
	UPROPERTY(SaveGame) float ActivationRange = 100000.0f;
	UPROPERTY(SaveGame) uint32 Assignment = 0;
	UPROPERTY(SaveGame) bool bDormant = false;
	UPROPERTY(SaveGame) bool bDestroyed = false;
	UPROPERTY(SaveGame) bool bUnloaded = false;
	UPROPERTY(SaveGame) bool bStoryHidden = false;
	UPROPERTY() TObjectPtr<AEnemyShip> ActiveShip;
};

USTRUCT()
struct FEnemyShipBundlePoolState
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) TArray<FEnemyShipBundlePlacement> Placements;
};

UCLASS()
class ENEMY_API AEnemyShipBundlePool : public AActor, public ISWRoomStateAdapter
{
	GENERATED_BODY()
public:
	AEnemyShipBundlePool();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void CaptureRoomDomains(TArray<FSWRoomDomainPart>& Parts, TArray<FSWRoomCaptureIssue>& Issues) const override;
	virtual bool RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& Error) override;
	virtual bool FinalizeRoomRestore(const TMap<FGuid, AActor*>& Actors, FString& Error) override;
	virtual bool CompareRoomDomain(const FSWRoomDomainPart& Expected, const FSWRoomDomainPart& Actual,
		float Tolerance, TArray<FString>& Fields) const override;
	bool ApplyLogicalOrbitDistance(AEnemyShip* Ship) const;
	UPROPERTY(EditAnywhere, Category="Ship|Optimization", meta=(ClampMin="0")) int32 IdleCapacity = 8;

private:
	void Evaluate();
	void DiscoverPlacedShips();
	bool CapturePlacement(AEnemyShip& Ship, FEnemyShipBundlePlacement& Placement);
	bool BindPlacement(FEnemyShipBundlePlacement& Placement, bool bRestoring, FString& Error);
	void ReleasePlacement(FEnemyShipBundlePlacement& Placement);
	void DestroyBundle(AEnemyShip* Ship);
	void SetBundleRegistration(AEnemyShip& Ship, bool bIdle, const FEnemyShipBundlePlacement* Placement);
	void PrepareRestore(const FSWRoomWorldSnapshot& Snapshot, FString& Error);
	void RestoreCompleted();
	void HandleLevelRemoved(ULevel* Level, UWorld* World);
	UFUNCTION() void OnRep_Placements();
	UFUNCTION() void HandleShipDestroyed(AActor* Ship);
	UPROPERTY(ReplicatedUsing=OnRep_Placements) TArray<FEnemyShipBundlePlacement> Placements;
	UPROPERTY(Transient) TArray<TObjectPtr<AEnemyShip>> IdleBundles;
	UPROPERTY(Transient) TArray<TObjectPtr<UInstancedStaticMeshComponent>> ProxyInstanceComponents;
	TMap<FGuid, uint32> ClientAssignments;
	uint32 LastPresentationHash = 0;
	FTimerHandle EvaluationTimer;
	FDelegateHandle PrepareRestoreHandle;
	FDelegateHandle RestoreCompletedHandle;
	FDelegateHandle LevelRemovedHandle;
	bool bChangingPool = false;
};

UCLASS()
class ENEMY_API UEnemyShipBundlePoolSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual void OnWorldBeginPlay(UWorld& World) override;
	AEnemyShipBundlePool* GetPool() const { return Pool.Get(); }
private:
	UPROPERTY(Transient) TObjectPtr<AEnemyShipBundlePool> Pool;
};
