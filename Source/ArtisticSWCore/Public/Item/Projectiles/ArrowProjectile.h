#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameplayEffectTypes.h"
#include "Item/BaseProjectile.h"
#include "ArrowProjectile.generated.h"

class UPrimitiveComponent;
class UStaticMesh;
class UAbilitySystemComponent;
class UGameplayEffect;

USTRUCT(BlueprintType)
struct FArrowDamageEffect
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Damage")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Damage", meta = (ClampMin = "0.0"))
	float BaseDamage = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Damage")
	bool bScaleWithCharge = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Damage")
	bool bCanCrit = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Damage", meta = (ClampMin = "1"))
	int32 EffectLevel = 1;
};

USTRUCT(BlueprintType)
struct FArrowStatusEffect
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Status")
	TSubclassOf<UGameplayEffect> StatusEffectClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Status", meta = (ClampMin = "1"))
	int32 EffectLevel = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Status")
	FGameplayTag RefreshGrantedTag;
};

USTRUCT(BlueprintType)
struct FArrowDamageData
{
	GENERATED_BODY()

	/** Common direct-damage GE. The firing ability snapshots Strength into its spec. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Damage")
	TSubclassOf<UGameplayEffect> DirectDamageEffectClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Damage", meta = (ClampMin = "0.0"))
	float AttackCoefficient = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Damage", meta = (ClampMin = "1"))
	int32 DirectDamageEffectLevel = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Damage", meta = (DeprecatedProperty, DeprecationMessage = "Use DirectDamageEffectClass and AttackCoefficient."))
	TArray<FArrowDamageEffect> DamageEffects;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Damage")
	FGameplayTag ElementType;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Damage")
	FGameplayTagContainer DamageTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Damage", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CritChance = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Damage", meta = (ClampMin = "1.0"))
	float CritMultiplier = 1.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Damage", meta = (ClampMin = "0"))
	int32 PierceCount = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Status")
	TArray<FArrowStatusEffect> StatusEffects;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Damage", meta = (DeprecatedProperty, DeprecationMessage = "Use StatusEffects instead."))
	TArray<TSubclassOf<UGameplayEffect>> StatusEffectClasses;
};

UCLASS()
class ARTISTICSWCORE_API AArrowProjectile : public ABaseProjectile
{
	GENERATED_BODY()

	friend class FStrengthProjectilePayloadTest;

public:
	AArrowProjectile();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "Arrow")
	void LaunchArrow(const FVector& LaunchVelocity);

	UFUNCTION(BlueprintCallable, Category = "Arrow")
	void IgnoreActorForMovement(AActor* ActorToIgnore);

	UFUNCTION(BlueprintCallable, Category = "Arrow")
	void SetArrowMesh(UStaticMesh* InMesh);

	UFUNCTION(BlueprintCallable, Category = "Arrow")
	void InitializeStrengthDamage(
		UAbilitySystemComponent* InSourceASC,
		AActor* InInstigatorActor,
		const FGameplayEffectSpecHandle& InDirectDamageSpec);

	UFUNCTION(BlueprintCallable, Category = "Arrow", meta = (DeprecatedFunction, DeprecationMessage = "Use InitializeStrengthDamage with a launch-time Strength damage spec."))
	void InitializeDamage(UAbilitySystemComponent* InSourceASC, AActor* InInstigatorActor, float InChargeDamageMultiplier);

	UFUNCTION(BlueprintPure, Category = "Arrow|Damage")
	TSubclassOf<UGameplayEffect> GetDirectDamageEffectClass() const;

	UFUNCTION(BlueprintPure, Category = "Arrow|Damage")
	float GetAttackCoefficient() const { return DamageData.AttackCoefficient; }

	UFUNCTION(BlueprintPure, Category = "Arrow|Damage")
	int32 GetDirectDamageEffectLevel() const { return FMath::Max(1, DamageData.DirectDamageEffectLevel); }

	/**
	 * Returns whether this arrow may apply its embedded DamageData to TargetActor.
	 * Team filtering is disabled by default so normal gameplay does not distinguish factions.
	 */
	UFUNCTION(BlueprintPure, Category = "Arrow|Damage")
	bool IsValidDamageTarget(const AActor* TargetActor) const;

	/** Runtime/debug override for testing friendly-fire behavior. */
	UFUNCTION(BlueprintCallable, Category = "Arrow|Debug")
	void SetTeamDamageFilteringEnabled(bool bEnabled) { bEnableTeamDamageFiltering = bEnabled; }

	UFUNCTION(BlueprintPure, Category = "Arrow|Debug")
	bool IsTeamDamageFilteringEnabled() const { return bEnableTeamDamageFiltering; }

	UFUNCTION(BlueprintCallable, Category = "Arrow", meta = (DeprecatedFunction, DeprecationMessage = "Use InitializeStrengthDamage."))
	void SetDamageEffectSpecHandle(const FGameplayEffectSpecHandle& InDamageEffectSpecHandle);

	UFUNCTION(BlueprintCallable, Category = "Arrow", meta = (DeprecatedFunction, DeprecationMessage = "Strength MVP uses one direct damage spec."))
	void SetAdditionalDamageEffectSpecHandles(const TArray<FGameplayEffectSpecHandle>& InAdditionalDamageEffectSpecHandles);

	UFUNCTION(NetMulticast, Unreliable, BlueprintCallable, Category = "Arrow")
	void Multicast_PlayImpactFX(const FHitResult& Hit);

protected:
	UFUNCTION()
	void OnArrowHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	virtual bool ShouldIgnoreHitActor(const AActor* OtherActor) const;
	virtual bool CanApplyDamageToActor(const AActor* OtherActor) const;
	void BuildStatusEffectSpecs();
	void ApplyDamageToActor(AActor* TargetActor);

	UFUNCTION(BlueprintImplementableEvent, Category = "Arrow")
	void K2_OnImpactFX(const FHitResult& Hit);

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Damage")
	FArrowDamageData DamageData;

	UPROPERTY(BlueprintReadWrite, meta = (ExposeOnSpawn = "true"), Category = "GAS")
	TArray<FGameplayEffectSpecHandle> DamageEffectSpecHandles;

	UPROPERTY(BlueprintReadWrite, meta = (ExposeOnSpawn = "true"), Category = "GAS")
	TArray<FGameplayEffectSpecHandle> StatusEffectSpecHandles;

	UPROPERTY(Transient)
	TArray<FGameplayTag> StatusEffectRefreshGrantedTags;

	UPROPERTY(Transient)
	TObjectPtr<UAbilitySystemComponent> SourceASC;

	UPROPERTY(Transient)
	TObjectPtr<AActor> InstigatorActor;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> MovementIgnoredActors;

	/** Guarantees one direct/status application per target even for piercing arrows. */
	TSet<TWeakObjectPtr<AActor>> AppliedActors;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Movement", meta = (ClampMin = "0.0"))
	float FlightGravityScale = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow")
	bool bDestroyOnImpact = true;

	/**
	 * Debug option. When enabled, arrows reject targets that share Team.Player
	 * or Team.Enemy with their source. Disabled by default for faction-agnostic gameplay.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Debug")
	bool bEnableTeamDamageFiltering = false;
};
