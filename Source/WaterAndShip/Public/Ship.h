// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputActionValue.h"
#include "Ship.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInteractableComponent;
class UInputMappingContext;
class UInputAction;

UCLASS()
class WATERANDSHIP_API AShip : public APawn
{
	GENERATED_BODY()

public:
	// Sets default values for this pawn's properties
	AShip();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	virtual void UnPossessed() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/* Boarding Interaction */
	void Board(APawn* PlayerPawn);

	/* Components */

	/** Root physics mesh for the ship */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* BuoyancyRoot;

	/** Camera boom for orbiting camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USpringArmComponent* CameraBoom;

	/** Follow camera attached to the boom */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UCameraComponent* FollowCamera;

	/** Interactable component to allow player interactions */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UInteractableComponent* InteractableComponent;

	/** World location of the fixed observation camera (set XYZ in editor) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Fixed Camera")
	FVector FixedCameraLocation = FVector(0.0f, 0.0f, 1000.0f);

	/** World rotation of the fixed observation camera (set in editor) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Fixed Camera")
	FRotator FixedCameraRotation = FRotator(-45.0f, 0.0f, 0.0f);

	// ---- Movement Parameters ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Movement")
	float ForwardForce = 500000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Movement")
	float TurnTorque = 50000000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Movement")
	float LateralDragCoefficient = 200.f;

	// ---- Input Config ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Input")
	UInputMappingContext* ShipInputMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Input")
	int32 ShipInputPriority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Input")
	UInputAction* ShipMoveAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Input")
	UInputAction* ShipTurnAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Input")
	UInputAction* ShipLookAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Input")
	UInputAction* ShipToggleCameraAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Input")
	UInputAction* ShipDisembarkAction;

protected:
	// ---- Input Handlers ----
	void ShipMove(const FInputActionValue& Value);
	void ShipTurn(const FInputActionValue& Value);
	void ShipLook(const FInputActionValue& Value);
	void ToggleFixedCamera();
	void OnDisembarkAction(const FInputActionValue& Value);

	UFUNCTION(Server, Reliable)
	void ServerDisembark();

	void Disembark();

	// ---- Camera State ----
	bool bUsingFixedCamera = false;
	FTransform SavedBoomRelativeTransform;
	FRotator SavedControlRotation;
	float SavedTargetArmLength = 800.0f;
	FVector SavedFollowCameraRelativeLocation = FVector::ZeroVector;
	FRotator SavedFollowCameraRelativeRotation = FRotator::ZeroRotator;

	// ---- Passenger Reference ----
	UPROPERTY(ReplicatedUsing = OnRep_RidingPlayer)
	APawn* RidingPlayer = nullptr;

	UFUNCTION()
	void OnRep_RidingPlayer(APawn* OldRidingPlayer);

	UPROPERTY()
	APlayerController* CachedPlayerController = nullptr;

	UFUNCTION()
	void OnShipHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

public:
	virtual void OnRep_Controller() override;

	/** Returns CameraBoom subobject */
	FORCEINLINE USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** Returns FollowCamera subobject */
	FORCEINLINE UCameraComponent* GetFollowCamera() const { return FollowCamera; }

	/** Resets camera to follow mode (called when disembarking) */
	void ResetToFollowCamera();
};
