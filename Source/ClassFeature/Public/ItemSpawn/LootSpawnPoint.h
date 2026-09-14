#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameFramework/Actor.h"
#include "ChestSpawnData.h"
#include "LootSpawnTypes.h"
#include "Storage/StorageComponent.h"
#include "LootSpawnPoint.generated.h"

class ABaseItem;
class ABaseCharacter;
class AShip;
class AStorageChest;
class AStoryConditionalSpawner;
class UFixedChestDropData;

/** Authoring values shown under the Chest section when a chest spawn point is embedded in another actor. */
USTRUCT(BlueprintType)
struct CLASSFEATURE_API FChestSpawnPointChestSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Boss")
	bool bIsBossChest = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Boss", meta = (EditCondition = "bIsBossChest"))
	FGameplayTag RequiredBossTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Boss", meta = (EditCondition = "bIsBossChest"))
	FGameplayTag GuaranteedBossQuestItemTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Boss", meta = (EditCondition = "bIsBossChest", ClampMin = "1", UIMin = "1"))
	int32 GuaranteedBossQuestItemCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Placement")
	EChestEnvironment Environment = EChestEnvironment::Land;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Data Driven")
	EChestSpawnMode SpawnMode = EChestSpawnMode::Guarded;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Progression")
	EProgressionZone ProgressionZone = EProgressionZone::Mid1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Progression")
	EProgressionChestKind ProgressionKind = EProgressionChestKind::ShipGuarded;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Spawn")
	TSubclassOf<AStorageChest> ChestClassOverride;

	/** Legacy serialized field; random activation now comes from Progression Zone Plans. */
	UPROPERTY()
	TObjectPtr<URandomChestGroup> RandomGroup = nullptr;

	/** Legacy serialized value; progression chest spawning does not read it. */
	UPROPERTY()
	TObjectPtr<UChestDefinition> ChestDefinition = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Guard",
		meta = (EditCondition = "SpawnMode == EChestSpawnMode::Guarded"))
	TArray<TObjectPtr<ABaseCharacter>> GuardCharacters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Guard",
		meta = (EditCondition = "SpawnMode == EChestSpawnMode::Guarded"))
	TArray<TObjectPtr<AStoryConditionalSpawner>> GuardSpawners;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Guard",
		meta = (EditCondition = "SpawnMode == EChestSpawnMode::Guarded"))
	TObjectPtr<AShip> OwningShip = nullptr;
};

/** Authoring values shown under the Loot section when a chest spawn point is embedded in another actor. */
USTRUCT(BlueprintType)
struct CLASSFEATURE_API FChestSpawnPointLootSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot|Spawn")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot|Spawn", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PointWeight = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot|Placement")
	bool bAlignChestBottomToGround = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot|Placement", meta = (EditCondition = "bAlignChestBottomToGround", ClampMin = "0.0", UIMin = "0.0"))
	float GroundClearance = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot|Placement", meta = (EditCondition = "bAlignChestBottomToGround", ClampMin = "0.0", UIMin = "0.0"))
	float GroundTraceUpDistance = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot|Placement", meta = (EditCondition = "bAlignChestBottomToGround", ClampMin = "0.0", UIMin = "0.0"))
	float GroundTraceDownDistance = 1000.f;

	/** Used only to conditionally display the random-point weight in embedded authoring panels. */
	UPROPERTY(Transient)
	EChestSpawnMode SpawnMode = EChestSpawnMode::Guarded;
};

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
	AActor* GetSpawnedActor() const { return SpawnedActor; }

protected:
	void MarkActivated(AActor* InSpawnedActor);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Spawn")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Spawn", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PointWeight = 1.f;

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
	UFUNCTION(BlueprintPure, Category = "Loot|Spawn")
	FName GetZoneId() const { return ZoneId; }

	UFUNCTION(BlueprintCallable, Category = "Loot|Spawn")
	ABaseItem* SpawnLooseLoot(const FZoneLootItemRow& LootRow, TSubclassOf<ABaseItem> FallbackItemClass);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Spawn")
	FName ZoneId = NAME_None;

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
	/** Copies the selected Chest and Loot authoring values from an owning actor. */
	UFUNCTION(BlueprintCallable, Category = "Chest|Authoring")
	void ApplyAuthoringSettings(
		const FChestSpawnPointChestSettings& ChestSettings,
		const FChestSpawnPointLootSettings& LootSettings);

	UFUNCTION(BlueprintCallable, Category = "Chest|Spawn")
	AStorageChest* SpawnConfiguredChest(UChestDefinition* Definition, int32 Seed);

	/** Rolls independent rare items after the manager has applied progression loot. */
	void ApplyFixedChanceDrops(const UFixedChestDropData* DropData, int32 Seed);

	/** Registers a ship crew member even when the crew spawned after the chest. */
	void RegisterGuardCharacter(ABaseCharacter* GuardCharacter);

	UFUNCTION(BlueprintPure, Category = "Chest|Spawn")
	bool CanSpawnDataDrivenChest() const
	{
		return bEnabled && !bActivated
			&& (SpawnMode == EChestSpawnMode::Guarded || PointWeight > 0.f);
	}

	UFUNCTION(BlueprintPure, Category = "Chest|Spawn")
	EChestSpawnMode GetSpawnMode() const { return SpawnMode; }

	UFUNCTION(BlueprintPure, Category = "Chest|Progression")
	EProgressionZone GetProgressionZone() const { return ProgressionZone; }

	UFUNCTION(BlueprintPure, Category = "Chest|Progression")
	EProgressionChestKind GetProgressionKind() const { return ProgressionKind; }

	UFUNCTION(BlueprintPure, Category = "Chest|Spawn")
	UChestDefinition* GetGuardedChestDefinition() const { return ChestDefinition; }

	UFUNCTION(BlueprintPure, Category = "Chest|Placement")
	EChestEnvironment GetEnvironment() const { return Environment; }

	UFUNCTION(BlueprintCallable, Category = "Chest|Placement")
	void SetEnvironment(EChestEnvironment InEnvironment);

	UFUNCTION(BlueprintCallable, Category = "Chest|Spawn")
	void ConfigureRandomSpawn(EProgressionZone InZone, EProgressionChestKind InKind, float InPointWeight = 1.f);

	UFUNCTION(BlueprintCallable, Category = "Chest|Spawn")
	void ConfigureGuardedSpawn(
		UChestDefinition* InChestDefinition,
		const TArray<ABaseCharacter*>& InGuardCharacters,
		AShip* InOwningShip = nullptr);

	UFUNCTION()
	void HandleGuardActorSpawned(AActor* InSpawnedActor);

	UFUNCTION(BlueprintPure, Category = "Chest|Boss")
	bool HasMatchingBossGuard() const;

	/** 보스 상자 여부 (체크 시 특정 보스가 가드로 있을 때 확정 퀘스트 아이템 지급) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Boss")
	bool bIsBossChest = false;

	/** 요구되는 보스 적의 태그 (예: Enemy.Type.Boss.Mid1, Enemy.Type.Boss.Mid2, Enemy.Type.Boss.Mid3) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Boss", meta = (EditCondition = "bIsBossChest"))
	FGameplayTag RequiredBossTag;

	/** 가드 목록에 해당 보스가 존재할 때 반드시 100% 추가 드랍할 퀘스트 아이템 태그 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Boss", meta = (EditCondition = "bIsBossChest"))
	FGameplayTag GuaranteedBossQuestItemTag;

	/** 확정 퀘스트 아이템 드랍 개수 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Boss", meta = (EditCondition = "bIsBossChest", ClampMin = "1", UIMin = "1"))
	int32 GuaranteedBossQuestItemCount = 1;

	/** 조건부로 런타임에 보스를 소환하는 스토리 스포너 목록 (보스가 소환되면 상자의 가드로 동적 추가되고 잠김) */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Chest|Guard",
		meta = (EditCondition = "SpawnMode == EChestSpawnMode::Guarded"))
	TArray<TObjectPtr<AStoryConditionalSpawner>> GuardSpawners;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(Transient)
	TObjectPtr<AStorageChest> ActiveChestInstance = nullptr;

	UPROPERTY(Transient)
	bool bBossQuestItemInjected = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Placement")
	EChestEnvironment Environment = EChestEnvironment::Land;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Data Driven")
	EChestSpawnMode SpawnMode = EChestSpawnMode::Guarded;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Progression")
	EProgressionZone ProgressionZone = EProgressionZone::Mid1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Progression")
	EProgressionChestKind ProgressionKind = EProgressionChestKind::ShipGuarded;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Spawn")
	TSubclassOf<AStorageChest> ChestClassOverride;

	/** Legacy serialized field; no longer shown or read. */
	UPROPERTY()
	TObjectPtr<URandomChestGroup> RandomGroup = nullptr;

	/** Legacy serialized value; new manager-owned chests do not read it. */
	UPROPERTY()
	TObjectPtr<UChestDefinition> ChestDefinition = nullptr;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Chest|Guard",
		meta = (EditCondition = "SpawnMode == EChestSpawnMode::Guarded"))
	TArray<TObjectPtr<ABaseCharacter>> GuardCharacters;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Chest|Guard",
		meta = (EditCondition = "SpawnMode == EChestSpawnMode::Guarded"))
	TObjectPtr<AShip> OwningShip = nullptr;

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
};
