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
class UGameplayAbility;
class AWaterBombCannonball;

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
	virtual void UnPossessed() override;
	virtual void OnRep_Controller() override;
	class AShip* GetOwningShip() const;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/* Boarding Interaction - Ship의 Board()와 완전히 동일한 패턴 */
	void Board(APawn* PlayerPawn);

	/** Force exit from cannon control (e.g. when ship is destroyed or forced off) */
	void ForceExit();

	/** Fires the cannon. Returns true if fire was successful, false if on cooldown. */
	UFUNCTION(BlueprintCallable, Category = "Cannon")
	bool FireCannon();

	/** Normal projectile class used by this cannon; ship skills may reuse its authored mesh/effects. */
	TSubclassOf<AActor> GetCannonballClass() const { return CannonballClass; }

	UFUNCTION(BlueprintPure, Category = "Cannon|Water Bomb")
	bool IsWaterBombMode() const { return bWaterBombMode; }
	APawn* GetRidingPlayer() const { return RidingPlayer; }

	bool ActivateWaterBombModeFromAbility(
		UGameplayAbility* Ability,
		TSubclassOf<AWaterBombCannonball> ProjectileClass,
		float EffectDurationSeconds,
		float AttackSpeedMultiplier);
	void DeactivateWaterBombModeFromAbility(UGameplayAbility* Ability);

	/** Allows AI to set aim rotation directly on the server. */
	void SetAIAimRotation(float NewPitch, float NewYaw);

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

	/** 비어 있으면 일반 포탄으로 fallback하지 않고 발사를 거부합니다. */
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

	/** Assign the Water Bomb IA mapped to key 4 in the cannon IMC. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon|Input")
	TObjectPtr<UInputAction> CannonWaterBombToggleAction;

	/** Direct Water Bomb hotkey. Defaults to 7 and can be changed per Cannon Blueprint. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cannon|Input|Skills")
	FKey WaterBombSkillHotkey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon|Input")
	int32 CannonInputPriority = 10;

protected:
	// ---- Input Handlers ----
	void HandleLook(const FInputActionValue& Value);
	void HandleFire(const FInputActionValue& Value);
	void HandleExit(const FInputActionValue& Value);
	void HandleWaterBombToggle(const FInputActionValue& Value);
	void ToggleWaterBombAbility();

	// ---- Actions ----
	void ExitAimMode();

	// ---- Server RPCs ----
	UFUNCTION(Server, Reliable)
	void ServerFire();

	UFUNCTION(Server, Reliable)
	void ServerToggleWaterBombAbility();

	void SpawnCannonball(FVector MuzzleLocation, FRotator LaunchRotation, float Damage, float Speed);

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

	UFUNCTION()
	void OnRep_WaterBombMode();

	void SetWaterBombModeAuthoritative(bool bEnabled);
	void ToggleWaterBombAbilityAuthoritative();
	void CancelWaterBombAbilityAuthoritative();
	class UAbilitySystemComponent* GetRidingPlayerAbilitySystem() const;
	bool IsOwningShipCannonDisabled() const;

private:
	// ---- Passenger Reference (Ship의 RidingPlayer와 동일 패턴) ----
	UPROPERTY(ReplicatedUsing = OnRep_RidingPlayer)
	TObjectPtr<APawn> RidingPlayer = nullptr;

	UPROPERTY(ReplicatedUsing = OnRep_AimRotation)
	FCannonAimRotation AimRotation;

	UPROPERTY(ReplicatedUsing = OnRep_WaterBombMode)
	bool bWaterBombMode = false;

	TWeakObjectPtr<UGameplayAbility> ActiveWaterBombAbility;
	TSubclassOf<AWaterBombCannonball> ActiveWaterBombProjectileClass;
	float ActiveWaterBombEffectDurationSeconds = 5.0f;
	float ActiveWaterBombAttackSpeedMultiplier = 0.5f;

	bool bCanFire = true;
	/** AI가 매 Tick 발사를 재시도해도 물폭탄 봉쇄 로그는 효과당 한 번만 출력합니다. */
	bool bLoggedWaterBombFireBlock = false;
	FTimerHandle CooldownTimerHandle;

	UPROPERTY()
	TObjectPtr<UUserWidget> AimWidgetInstance = nullptr;

	// Initial local rotation of meshes to calculate offsets
	FRotator InitialBaseRotation;
	FRotator InitialBarrelRotation;

	UPROPERTY()
	TObjectPtr<APlayerController> CachedPlayerController = nullptr;
};
