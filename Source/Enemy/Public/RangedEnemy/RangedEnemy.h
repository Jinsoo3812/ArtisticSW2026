#pragma once

#include "CoreMinimal.h"
#include "BaseEnemy.h"
#include "RangedEnemy.generated.h"

class AArrowProjectile;
class AShip;
class UAnimMontage;

/**
 * Stationary ranged-enemy MVP that can fight independently on ground or use
 * an optional moving Ship as its movement/lifecycle host. Ship navigation and
 * cannon behavior stay separate from character targeting and ranged combat.
 */
UCLASS(Blueprintable)
class ENEMY_API ARangedEnemy : public ABaseEnemy
{
	GENERATED_BODY()

public:
	ARangedEnemy();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "Ranged Enemy|Ship")
	void SetHostShip(AShip* NewHostShip);

	UFUNCTION(BlueprintCallable, Category = "Ranged Enemy|Ship")
	bool ResolveHostShip();

	UFUNCTION(BlueprintPure, Category = "Ranged Enemy|Ship")
	AShip* GetHostShip() const { return HostShip; }

	void SetCombatTarget(AActor* NewTarget);
	void ClearCombatTarget();

	UFUNCTION(BlueprintPure, Category = "Ranged Enemy|Combat")
	AActor* GetCombatTarget() const { return CombatTarget; }

	UFUNCTION(BlueprintPure, Category = "Ranged Enemy|Combat")
	bool IsValidCombatTarget(const AActor* Candidate) const;

	virtual bool CanEngageActor_Implementation(AActor* Candidate) const override;

	/** Returns the exact target-rejection reason used by the shared attack checks. */
	bool EvaluateCombatTarget(const AActor* Candidate, FString& OutReason) const;

	UFUNCTION(BlueprintPure, Category = "Ranged Enemy|Combat")
	bool HasLineOfSightTo(const AActor* Candidate) const;

	UFUNCTION(BlueprintPure, Category = "Ranged Enemy|Combat")
	bool CanAttackCurrentTarget(bool bRequireLineOfSight = true) const;

	bool CanAttackTarget(const AActor* Candidate, bool bRequireLineOfSight = true) const;

	bool TryStartRangedAttack();

	UFUNCTION(BlueprintPure, Category = "Ranged Enemy|Combat")
	float GetRemainingAttackCooldown() const;

	FVector GetRangedAttackOrigin() const;
	FVector GetRangedAimLocation(const AActor* TargetActor) const;

	TSubclassOf<AArrowProjectile> GetRangedProjectileClass() const { return ProjectileClass; }
	UAnimMontage* GetRangedAttackMontage() const { return AttackMontage; }
	FGameplayTag GetRangedFireEventTag() const { return FireEventTag; }
	float GetRangedProjectileSpeed() const { return ProjectileSpeed; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void OnRep_HostShip();

	UFUNCTION()
	void OnHostShipDestroyed(AActor* DestroyedActor);

	void BindHostShipLifecycle();
	void UnbindHostShipLifecycle();
	void RetryResolveHostShip();
	AShip* FindShipInActorHierarchy(AActor* Actor) const;
	bool EvaluateAttackTarget(const AActor* Candidate, bool bRequireLineOfSight, FString& OutReason) const;
	bool TraceLineOfSight(const AActor* Candidate, FHitResult* OutHit = nullptr) const;

protected:
	UPROPERTY(ReplicatedUsing = OnRep_HostShip, EditInstanceOnly, BlueprintReadOnly, Category = "Ranged Enemy|Ship")
	TObjectPtr<AShip> HostShip = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged Enemy|Ship")
	bool bDestroyWithHostShip = true;

	/** HostShip is an optional movement/lifecycle aid and is never required for combat. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged Enemy|Ship")
	bool bAutoResolveHostShip = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged Enemy|Ship", meta = (ClampMin = "0"))
	int32 MaxHostShipResolveAttempts = 6;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged Enemy|Ship", meta = (ClampMin = "0.05"))
	float HostShipResolveRetryInterval = 0.5f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Ranged Enemy|Combat")
	TObjectPtr<AActor> CombatTarget = nullptr;

	/** Ground movement speed. Deck-derived classes can replace this without changing combat code. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged Enemy|Movement", meta = (ClampMin = "0.0"))
	float GroundMaxWalkSpeed = 350.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged Enemy|Combat", meta = (ClampMin = "0.0"))
	float MinAttackRange = 150.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged Enemy|Combat", meta = (ClampMin = "0.0"))
	float MaxAttackRange = 2500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged Enemy|Combat", meta = (ClampMin = "0.0"))
	float AttackCooldown = 2.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged Enemy|Combat", meta = (ClampMin = "1.0"))
	float ProjectileSpeed = 2500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged Enemy|Combat")
	FVector MuzzleOffset = FVector(50.0f, 0.0f, 100.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged Enemy|Combat")
	FName MuzzleSocketName = TEXT("ArrowSocket");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged Enemy|Combat")
	float TargetAimHeightOffset = 60.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged Enemy|Combat")
	TSubclassOf<AArrowProjectile> ProjectileClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged Enemy|Animation")
	TObjectPtr<UAnimMontage> AttackMontage = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged Enemy|Animation")
	FGameplayTag FireEventTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranged Enemy|Debug")
	bool bDrawAttackLineOfSight = false;

	FTimerHandle HostShipResolveTimerHandle;
	int32 HostShipResolveAttemptCount = 0;
	double NextAttackTime = 0.0;
};
