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
	bool IsLocked() const { return bLocked; }
	bool RequiresGuardClear() const { return bRequiresGuardClear; }
	bool HasGuardFailed() const { return bGuardFailed; }
	int32 GetAliveGuardCount() const { return AliveGuardHealthComponents.Num(); }
	bool IsPhysicsAndBuoyancyEnabled() const { return bEnablePhysicsAndBuoyancy; }

	UFUNCTION(BlueprintCallable, Category = "Storage")
	void ConfigureStorage(int32 InSlotCount, int32 InColumnCount, const TArray<FStorageItemEntry>& InItems);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Storage Chest|Physics")
	void SetPhysicsAndBuoyancyEnabled(bool bEnabled);

	void InitializeFromChestDefinition(UChestDefinition* InDefinition, int32 Seed);
	void ConfigureGuarding(bool bInRequiresGuardClear, const TArray<ABaseCharacter*>& InGuardCharacters, AShip* InOwningShip);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Storage Chest|Guarding")
	void AddGuardCharacter(ABaseCharacter* NewGuard);

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

	/** Optional reusable contents/physics definition. Spawn points set this before BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Storage Chest|Definition")
	TObjectPtr<UChestDefinition> ChestDefinition;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Storage Chest|Definition")
	int32 LootSeed = 0;

	/** Legacy placed chests float by default. Data-driven ship chests disable this. */
	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_PhysicsMode, BlueprintReadOnly, Category = "Storage Chest|Physics")
	bool bEnablePhysicsAndBuoyancy = true;

	/** Server-authoritative rigid-body mass. Buoyancy and physics are simulated only by the server. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Storage Chest|Physics", meta = (ClampMin = "1.0", Units = "kg"))
	float PhysicsMassKg = 25.0f;

	/** Client-only smoothing of server-authoritative floating chest movement. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Storage Chest|Networking", meta = (ClampMin = "0.0"))
	float ClientLocationInterpSpeed = 14.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Storage Chest|Networking", meta = (ClampMin = "0.0"))
	float ClientRotationInterpSpeed = 12.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Storage Chest|Networking", meta = (ClampMin = "0.0", Units = "s"))
	float ClientMaxExtrapolationTime = 0.1f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Storage Chest|Networking", meta = (ClampMin = "0.0", Units = "cm"))
	float ClientNetworkSnapDistance = 500.0f;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Storage Chest|Guarding")
	bool bRequiresGuardClear = false;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Storage Chest|Guarding", meta = (EditCondition = "bRequiresGuardClear"))
	TArray<TObjectPtr<ABaseCharacter>> GuardCharacters;

	/** Leave empty for an island chest. Assign for a chest mounted on an enemy ship. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Storage Chest|Guarding", meta = (EditCondition = "bRequiresGuardClear"))
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
	bool bHasBeenOpened = false;

	UFUNCTION()
	void HandleOwningShipDestroyed(AActor* DestroyedActor);

	UFUNCTION()
	void HandleStorageChanged();

	UFUNCTION()
	void OnRep_Locked();

	UFUNCTION()
	void OnRep_PhysicsMode();

	void InitializeGuardState();
	void ClearGuardBindings();
	void ApplyPhysicsMode();
	void ApplyLockPresentation();
};
