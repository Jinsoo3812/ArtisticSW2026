// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Storage/StorageComponent.h"
#include "StorageChest.generated.h"

class UInteractableComponent;
class USceneComponent;
class UStaticMeshComponent;
class USWBuoyancyComponent;
class UBaseHealthComponent;
class UChestDefinition;
class ABaseCharacter;
class AShip;
class UItemData;
struct FProgressionComputedDrop;

UCLASS()
class CLASSFEATURE_API AStorageChest : public AActor
{
	GENERATED_BODY()

public:
	AStorageChest();

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnRep_ReplicatedMovement() override;

	UStorageComponent* GetStorageComponent() const { return StorageComponent; }
	UInteractableComponent* GetInteractableComponent() const { return InteractableComponent; }
	UStaticMeshComponent* GetChestMesh() const { return ChestMesh; }
	USWBuoyancyComponent* GetSWBuoyancyComponent() const { return SWBuoyancyComponent; }
	FText GetStorageName() const { return StorageName; }
	UFUNCTION(BlueprintPure, Category = "Storage Chest|Lock")
	bool IsLocked() const { return bLocked; }
	bool RequiresGuardClear() const { return bRequiresGuardClear; }
	bool HasGuardFailed() const { return bGuardFailed; }
	UFUNCTION(BlueprintPure, Category = "Storage Chest|Guarding")
	int32 GetAliveGuardCount() const { return AliveGuardHealthComponents.Num(); }
	UFUNCTION(BlueprintPure, Category = "Storage Chest|Guarding")
	bool HasBossGuard() const { return BossGuardCharacter != nullptr; }
	UFUNCTION(BlueprintPure, Category = "Storage Chest|Guarding")
	bool IsBossGuardAlive() const;
	UFUNCTION(BlueprintPure, Category = "Storage Chest|Guarding")
	bool IsBossEncounterReserved() const { return bBossEncounterReserved; }
	bool HasBeenOpened() const { return bHasBeenOpened; }
	bool IsPhysicsAndBuoyancyEnabled() const { return bEnablePhysicsAndBuoyancy; }
	bool IsDistanceOptimizationEnabled() const { return bEnableDistanceOptimization; }
	bool IsDistanceOptimizationDormant() const { return bDistanceOptimizationDormant; }

	UFUNCTION(BlueprintCallable, Category = "Storage")
	void ConfigureStorage(int32 InSlotCount, int32 InColumnCount, const TArray<FStorageItemEntry>& InItems);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Storage Chest|Physics")
	void SetPhysicsAndBuoyancyEnabled(bool bEnabled);

	/** Opt in individual floating chests, including deferred-spawned chests. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Storage Chest|Optimization")
	void SetDistanceOptimizationEnabled(bool bEnabled);

	void InitializeFromChestDefinition(UChestDefinition* InDefinition, int32 Seed, float ExpectedValueRatio = 1.f);
	/** Prevents a chest BP's legacy default definition from injecting snapshot loot. */
	void ClearLegacyChestDefinition();
	void ReplaceProgressionLoot(const TArray<FProgressionComputedDrop>& Drops, const UItemData* Definitions, int32 Seed);

	/** Appends independent rare drops after progression replacement without changing its rolls. */
	void AppendFixedLoot(const TArray<FStorageItemEntry>& ExtraItems);
	void ConfigureGuarding(bool bInRequiresGuardClear, const TArray<ABaseCharacter*>& InGuardCharacters, AShip* InOwningShip);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Storage Chest|Guarding")
	void AddGuardCharacter(ABaseCharacter* NewGuard);
	void RemoveGuardCharacter(ABaseCharacter* Guard);
	void AddBossGuardCharacter(ABaseCharacter* Boss);
	void SetBossEncounterReserved(bool bReserved);
	void EnsureGuaranteedLoot(const TArray<FStorageItemEntry>& GuaranteedItems);

	UFUNCTION()
	void HandleTrackedHealthDeath(UBaseHealthComponent* HealthComponent);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Storage Chest|Lock")
	void SetLocked(bool bInLocked);

	bool HasAuthorityOrIsTesting() const { return HasAuthority() || (GetWorld() == nullptr); }

	UFUNCTION()
	void HandleInteracted(AActor* Interactor);

	UFUNCTION()
	void HandleEmptyDestroyTimeout();

protected:
	/**
	 * Compatibility child for existing Blueprint assets that were authored when
	 * SceneRoot was the native root. ChestMesh remains the physics/replication root.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ChestMesh;

	/** Server-authoritative custom buoyancy. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USWBuoyancyComponent> SWBuoyancyComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UInteractableComponent> InteractableComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStorageComponent> StorageComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	FText StorageName = FText::FromString(TEXT("Storage Chest"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	FText ActionText = FText::FromString(TEXT("Open"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	FText LockedActionText = FText::FromString(TEXT("Locked"));

	/** Runtime definition supplied by a chest spawn point or a sinking enemy ship. */
	UPROPERTY(Transient)
	TObjectPtr<UChestDefinition> ChestDefinition;

	UPROPERTY(Transient)
	int32 LootSeed = 0;

	/** Set from the spawn context: enabled for ocean/sunk chests and disabled on land/decks. */
	UPROPERTY(ReplicatedUsing = OnRep_PhysicsMode, VisibleInstanceOnly, BlueprintReadOnly, Category = "Storage Chest|Physics")
	bool bEnablePhysicsAndBuoyancy = false;

	/** Server-authoritative rigid-body mass. Buoyancy and physics are simulated only by the server. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Storage Chest|Physics", meta = (ClampMin = "1.0", Units = "kg"))
	float PhysicsMassKg = 25.0f;

	/** Only independent floating chests can sleep. Deck/attached chests are always excluded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storage Chest|Optimization")
	bool bEnableDistanceOptimization = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storage Chest|Optimization",
		meta = (EditCondition = "bEnableDistanceOptimization", ClampMin = "0.0", Units = "cm"))
	float DistanceOptimizationRange = 100000.0f;

	UPROPERTY(ReplicatedUsing = OnRep_DistanceOptimizationDormant, VisibleInstanceOnly, BlueprintReadOnly,
		Category = "Storage Chest|Optimization")
	bool bDistanceOptimizationDormant = false;

	/** Client-only smoothing of server-authoritative floating chest movement. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Storage Chest|Networking", meta = (ClampMin = "0.0"))
	float ClientLocationInterpSpeed = 14.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Storage Chest|Networking", meta = (ClampMin = "0.0"))
	float ClientRotationInterpSpeed = 12.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Storage Chest|Networking", meta = (ClampMin = "0.0", Units = "s"))
	float ClientMaxExtrapolationTime = 0.1f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Storage Chest|Networking", meta = (ClampMin = "0.0", Units = "cm"))
	float ClientNetworkSnapDistance = 500.0f;

	UPROPERTY(Transient)
	bool bRequiresGuardClear = false;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ABaseCharacter>> GuardCharacters;
	UPROPERTY(Transient)
	TObjectPtr<ABaseCharacter> BossGuardCharacter;
	UPROPERTY(Transient)
	bool bBossEncounterReserved = false;
	UPROPERTY(Transient)
	TObjectPtr<UBaseHealthComponent> BossGuardHealth;

	/** Runtime owner for a guarded deck chest; null for island and ocean chests. */
	UPROPERTY(Transient)
	TObjectPtr<AShip> OwningShip;

	UPROPERTY(ReplicatedUsing = OnRep_Locked, VisibleInstanceOnly, BlueprintReadOnly, Category = "Storage Chest|Lock")
	bool bLocked = false;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Storage Chest|Lock")
	bool bGuardFailed = false;

	UPROPERTY(Transient)
	TSet<TObjectPtr<UBaseHealthComponent>> AliveGuardHealthComponents;

	UPROPERTY(Transient)
	TObjectPtr<UBaseHealthComponent> OwningShipHealthComponent;

	bool bDefinitionInitialized = false;
	bool bHasClientMovementTarget = false;
	FVector ClientMovementTargetLocation = FVector::ZeroVector;
	FQuat ClientMovementTargetRotation = FQuat::Identity;
	FVector ClientMovementTargetVelocity = FVector::ZeroVector;
	float ClientMovementTargetReceiveTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storage Chest|Lifecycle")
	bool bDestroyWhenEmpty = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storage Chest|Lifecycle", meta = (ClampMin = "0.0", Units = "s"))
	float EmptyDestroyDelay = 1.0f;

	FTimerHandle EmptyDestroyTimerHandle;
	FTimerHandle DistanceOptimizationTimerHandle;
	float DistanceOptimizationStableTime = 0.0f;
	bool bHasBeenOpened = false;

	UFUNCTION()
	void HandleOwningShipDestroyed(AActor* DestroyedActor);

	UFUNCTION()
	void HandleStorageChanged();

	UFUNCTION()
	void OnRep_Locked();

	UFUNCTION()
	void OnRep_PhysicsMode();

	UFUNCTION()
	void OnRep_DistanceOptimizationDormant();

	void InitializeGuardState();
	void RecalculateGuardLock();
	void ClearGuardBindings();
	void ApplyPhysicsMode();
	void RefreshDistanceOptimizationTimer();
	void EvaluateDistanceOptimization();
	void SetDistanceOptimizationDormant(bool bDormant);
	void ApplyLockPresentation();
};
