// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputActionValue.h"
#include "Cannon.generated.h"

class UStaticMeshComponent;
class UCameraComponent;
class UInteractableComponent;
class UInputMappingContext;
class UInputAction;
class APlayerController;
class UUserWidget;

USTRUCT(BlueprintType)
struct FCannonAimRotation
{
	GENERATED_BODY()

	UPROPERTY()
	float Pitch = 0.0f;

	UPROPERTY()
	float Yaw = 0.0f;
};

UCLASS()
class WATERANDSHIP_API ACannon : public APawn
{
	GENERATED_BODY()
	
public:	
	ACannon();

protected:
	virtual void BeginPlay() override;

public:	
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void OnRep_Controller() override;
	class AShip* GetOwningShip() const;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Interact Handler bound to UInteractableComponent */
	UFUNCTION()
	void OnInteracted(AActor* Interactor);

	/** Force exit from cannon control (e.g. when ship is destroyed or forced off) */
	void ForceExit();

protected:
	// ---- Components ----
	/** Base mesh that rotates left/right (Yaw) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BaseMesh;

	/** Barrel mesh that rotates up/down (Pitch) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BarrelMesh;

	/** Aiming Camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> AimCamera;

	/** Interactable component to allow player interactions */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UInteractableComponent> InteractableComponent;

	// ---- Properties ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon|Projectile")
	TSubclassOf<AActor> CannonballClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon|Projectile")
	float FireVelocity = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon|Reload")
	float FireCooldown = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon|UI")
	TSubclassOf<UUserWidget> AimWidgetClass;

	// ---- Aiming Limits ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon|Aiming")
	float MinPitch = -15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon|Aiming")
	float MaxPitch = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon|Aiming")
	float MaxYawOffset = 60.0f;

	// ---- Inputs ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon|Input")
	TObjectPtr<UInputMappingContext> CannonInputMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon|Input")
	TObjectPtr<UInputAction> CannonLookAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon|Input")
	TObjectPtr<UInputAction> CannonFireAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon|Input")
	TObjectPtr<UInputAction> CannonExitAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon|Input")
	int32 CannonInputPriority = 10;

protected:
	// ---- Input Handlers ----
	void HandleLook(const FInputActionValue& Value);
	void HandleFire(const FInputActionValue& Value);
	void HandleExit(const FInputActionValue& Value);

	// ---- Actions ----
	void EnterAimMode(APawn* InPlayer);
	void ExitAimMode();

	// ---- Server RPCs ----
	UFUNCTION(Server, Reliable)
	void ServerFire(FVector MuzzleLocation, FRotator LaunchRotation, float Damage);

	UFUNCTION(Server, Reliable)
	void ServerUpdateAim(float NewPitch, float NewYaw);

	UFUNCTION(Server, Reliable)
	void ServerExit();

	void ResetCooldown();

	// ---- Replication Callbacks ----
	UFUNCTION()
	void OnRep_AimRotation();

	UFUNCTION()
	void OnRep_RidingPlayer(APawn* OldPlayer);

private:
	// ---- State ----
	UPROPERTY(ReplicatedUsing = OnRep_RidingPlayer)
	TObjectPtr<APawn> RidingPlayer = nullptr;

	UPROPERTY(ReplicatedUsing = OnRep_AimRotation)
	FCannonAimRotation AimRotation;

	bool bCanFire = true;
	FTimerHandle CooldownTimerHandle;

	UPROPERTY()
	TObjectPtr<UUserWidget> AimWidgetInstance = nullptr;

	// Initial local rotation of meshes to calculate offsets
	FRotator InitialBaseRotation;
	FRotator InitialBarrelRotation;

	UPROPERTY()
	TObjectPtr<APlayerController> CachedPC = nullptr;
};
