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

DECLARE_MULTICAST_DELEGATE_OneParam(FOnWaterBombModeChanged, bool);

USTRUCT(BlueprintType)
struct FCannonAimRotation
{
	GENERATED_BODY()

	UPROPERTY()
	float Pitch = 0.0f;

	UPROPERTY()
	float Yaw = 0.0f;
};

USTRUCT(BlueprintType)
struct FCannonResolvedFiringStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Cannon|Stats")
	float Damage = 10.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Cannon|Stats")
	float CooldownSeconds = 1.5f;

	UPROPERTY(BlueprintReadOnly, Category = "Cannon|Stats")
	float ProjectileSpeed = 3000.0f;
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

	/** Returns false for cannons mounted on ships that prohibit player cannon control. */
	UFUNCTION(BlueprintPure, Category = "Cannon|Control")
	bool AllowsPlayerControl() const;

	/** Re-evaluates interaction collision after mounting or attachment changes. */
	void RefreshPlayerInteractionAvailability();

	UInteractableComponent* GetInteractableComponent() const { return InteractableComponent; }

	/* Boarding Interaction - Ship의 Board()와 완전히 동일한 패턴 */
	void Board(APawn* PlayerPawn);

	/** Force exit from cannon control (e.g. when ship is destroyed or forced off) */
	void ForceExit();

	/** Fires the cannon. Returns true if fire was successful, false if on cooldown. */
	UFUNCTION(BlueprintCallable, Category = "Cannon")
	bool FireCannon();

	/** True when the cannon's own reload/effect state permits a normal AI shot. */
	UFUNCTION(BlueprintPure, Category = "Cannon|AI")
	bool CanFireCannon() const;

	/** Remaining per-cannon reload time. Zero means ready; negative timer states are normalized to zero. */
	UFUNCTION(BlueprintPure, Category = "Cannon|AI")
	float GetFireCooldownRemaining() const;
	void ResetAIFiringState();

	/** Checks authored pitch/yaw limits without changing the replicated aim. */
	UFUNCTION(BlueprintPure, Category = "Cannon|AI")
	bool CanAimAtWorldDirection(const FVector& WorldDirection) const;

	/** Server-only deterministic AI shot using the supplied ballistic direction. */
	UFUNCTION(BlueprintCallable, Category = "Cannon|AI")
	bool FireAICannonAtDirection(const FVector& WorldDirection);

	/** Server-only AI shot with a per-shot projectile speed, used when the authored speed cannot reach the target. */
	bool FireAICannonAtDirectionWithSpeed(const FVector& WorldDirection, float ProjectileSpeed);

	/** Normal projectile class used by this cannon; ship skills may reuse its authored mesh/effects. */
	TSubclassOf<AActor> GetCannonballClass() const { return CannonballClass; }

	/** Resolves fallback values or the owning ship's live DT/upgrade-backed attributes. */
	UFUNCTION(BlueprintPure, Category = "Cannon|Stats")
	FCannonResolvedFiringStats GetResolvedFiringStats() const;

	/** Canonical world-space projectile origin authored by this cannon. */
	UFUNCTION(BlueprintPure, Category = "Cannon|Projectile")
	FTransform GetProjectileMuzzleTransform() const;

	UFUNCTION(BlueprintPure, Category = "Cannon|Water Bomb")
	bool IsWaterBombMode() const { return bWaterBombMode; }
	FOnWaterBombModeChanged OnWaterBombModeChanged;
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

	/** Authored mounting point for the player character positioned behind the cannon. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> PlayerMountPoint;

	/** Authored exit point when the player dismounts from the cannon. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> PlayerExitPoint;

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
	void SetRiderInvulnerable(bool bEnabled);

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
	bool bRiderInvulnerabilityApplied = false;
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
