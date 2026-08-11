#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShipBoardingPoint.generated.h"

class AShip;
class UInteractableComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * A reusable sea-boarding point authored as a Child Actor Component inside a
 * ship Blueprint. Add or duplicate as many instances as the ship needs.
 */
UCLASS(BlueprintType, Blueprintable)
class WATERANDSHIP_API AShipBoardingPoint : public AActor
{
	GENERATED_BODY()

public:
	AShipBoardingPoint();

	virtual void OnConstruction(const FTransform& Transform) override;

	UFUNCTION(BlueprintPure, Category = "Ship|Boarding")
	AShip* GetOwningShip() const;

	UFUNCTION(BlueprintPure, Category = "Ship|Boarding")
	UInteractableComponent* GetBoardingInteractable() const { return BoardingInteractable; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Boarding|Visual")
	TObjectPtr<UStaticMesh> PointMeshAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Boarding|Visual")
	FTransform MeshRelativeTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Boarding|Interaction")
	FTransform InteractionRelativeTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Boarding|Interaction", meta = (ClampMin = "1.0", Units = "cm"))
	float InteractionSphereRadius = 100.0f;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void HandleInteracted(AActor* Interactor);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Boarding")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Boarding")
	TObjectPtr<UStaticMeshComponent> PointMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Boarding")
	TObjectPtr<UInteractableComponent> BoardingInteractable;
};
