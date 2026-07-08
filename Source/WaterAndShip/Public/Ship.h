// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputActionValue.h"
#include "AbilitySystemInterface.h"
#include "Engine/DataTable.h"
#include "Ship.generated.h"

class UStaticMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class UInteractableComponent;
class UInputMappingContext;
class UInputAction;
class APlayerController;
class UPrimitiveComponent;
class UAbilitySystemComponent;
class UBaseAttributeSet;
class UShipAttributeSet;

USTRUCT(BlueprintType)
struct FShipStatRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float MaxHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float ShipSpeedMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float CannonDamage = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float CannonFireCooldown = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float CannonballSpeed = 3000.f;
};

USTRUCT(BlueprintType)
struct FShipReplicatedState
{
	GENERATED_BODY()

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	FRotator Rotation = FRotator::ZeroRotator;
};

UCLASS()
class WATERANDSHIP_API AShip : public APawn, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	// Sets default values for this pawn's properties
	AShip();

	// IAbilitySystemInterface 구현
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

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

	/** Sea boarding interactable component for climbing up from the water */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UInteractableComponent* SeaBoardingInteractable;

	/** Location point where the player will be teleported when boarding from the sea */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* SeaBoardingDestination;

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

	UFUNCTION()
	void HandleSeaBoarding(AActor* Interactor);

	// Physics forces apply functions
	void ApplyForwardForce(float MoveValue);
	void ApplyTurnTorque(float TurnValue);

	// Server RPCs for movement (Unreliable because of high frequency axis updates)
	UFUNCTION(Server, Unreliable)
	void ServerMove(float MoveValue);

	UFUNCTION(Server, Unreliable)
	void ServerTurn(float TurnValue);

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

	// ---- Custom Replication State & Interp Configuration ----
	UPROPERTY(Replicated)
	FShipReplicatedState ReplicatedState;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Replication")
	float LocationInterpSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Replication")
	float RotationInterpSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Replication")
	float TeleportThreshold = 500.0f;

public:
	virtual void OnRep_Controller() override;

	/** Returns CameraBoom subobject */
	FORCEINLINE USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** Returns FollowCamera subobject */
	FORCEINLINE UCameraComponent* GetFollowCamera() const { return FollowCamera; }

	/** Resets camera to follow mode (called when disembarking) */
	void ResetToFollowCamera();

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Stats")
	TObjectPtr<UDataTable> ShipStatTable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Stats")
	FName ShipStatRowName;

protected:
	void InitializeDefaultAttributes();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UShipAttributeSet> AttributeSet;
};
