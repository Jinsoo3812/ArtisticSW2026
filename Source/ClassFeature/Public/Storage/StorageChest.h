// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Storage/StorageComponent.h"
#include "StorageChest.generated.h"

class UInteractableComponent;
class UStaticMeshComponent;
class UBuoyancyComponent;

UCLASS()
class CLASSFEATURE_API AStorageChest : public AActor
{
	GENERATED_BODY()

public:
	AStorageChest();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UStorageComponent* GetStorageComponent() const { return StorageComponent; }
	UInteractableComponent* GetInteractableComponent() const { return InteractableComponent; }
	UStaticMeshComponent* GetChestMesh() const { return ChestMesh; }
	UBuoyancyComponent* GetBuoyancyComponent() const { return BuoyancyComponent; }
	FText GetStorageName() const { return StorageName; }

	UFUNCTION(BlueprintCallable, Category = "Storage")
	void ConfigureStorage(int32 InSlotCount, int32 InColumnCount, const TArray<FStorageItemEntry>& InItems);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ChestMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBuoyancyComponent> BuoyancyComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UInteractableComponent> InteractableComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStorageComponent> StorageComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	FText StorageName = FText::FromString(TEXT("Storage Chest"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	FText ActionText = FText::FromString(TEXT("Open"));

	/** Server-authoritative rigid-body mass. Buoyancy and physics are simulated only by the server. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Storage Chest|Physics", meta = (ClampMin = "1.0", Units = "kg"))
	float PhysicsMassKg = 25.0f;

	UFUNCTION()
	void HandleInteracted(AActor* Interactor);
};
