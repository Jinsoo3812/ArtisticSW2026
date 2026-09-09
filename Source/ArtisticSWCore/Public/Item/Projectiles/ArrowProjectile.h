#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameplayEffectTypes.h"
#include "Item/BaseProjectile.h"
#include "ArrowProjectile.generated.h"

class UPrimitiveComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UAbilitySystemComponent;
class UGameplayEffect;

/** Minimal transient data required to render an arrow impact on remote clients. */
USTRUCT(BlueprintType)
struct FArrowImpactPresentationData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Arrow|Impact")
	FVector_NetQuantize10 ImpactLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Arrow|Impact")
	FVector_NetQuantizeNormal ImpactNormal = FVector::UpVector;

	UPROPERTY(BlueprintReadOnly, Category = "Arrow|Impact")
	FVector_NetQuantizeNormal IncomingDirection = FVector::ForwardVector;

	/** Set only for a stable replicated moving component, such as a ship query hull. */
	UPROPERTY(BlueprintReadOnly, Category = "Arrow|Impact")
	TObjectPtr<USceneComponent> AttachComponent;

	UPROPERTY(BlueprintReadOnly, Category = "Arrow|Impact")
	FName BoneName = NAME_None;
};

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

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "Arrow")
	void LaunchArrow(const FVector& LaunchVelocity);

	UFUNCTION(BlueprintCallable, Category = "Arrow")
	void IgnoreActorForMovement(AActor* ActorToIgnore);

	/** Copies this projectile's authored mesh, materials, and relative transform to a presentation component. */
	bool ApplyVisualTo(UStaticMeshComponent* TargetMesh) const;

	UFUNCTION(BlueprintPure, Category = "Arrow|Visual")
	UStaticMesh* GetArrowVisualMesh() const;

	UFUNCTION(BlueprintPure, Category = "Arrow|Visual")
	FTransform GetArrowVisualRelativeTransform() const;

	UFUNCTION(BlueprintPure, Category = "Arrow|Collision")
	FVector GetCollisionHalfExtent() const { return CollisionHalfExtent; }

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
	 * Team filtering is enabled by default so actors on the same team cannot damage each other.
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

	UFUNCTION(NetMulticast, Unreliable, BlueprintCallable, Category = "Arrow",
		meta = (DeprecatedFunction, DeprecationMessage = "Use the compact impact presentation pipeline."))
	void Multicast_PlayImpactFX(const FHitResult& Hit);

	UFUNCTION(NetMulticast, Unreliable, Category = "Arrow")
	void Multicast_PlayImpactPresentation(const FArrowImpactPresentationData& ImpactData);

protected:
	UFUNCTION()
	void OnArrowHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	virtual bool ShouldIgnoreHitActor(const AActor* OtherActor) const;
	virtual bool CanApplyDamageToActor(const AActor* OtherActor) const;
	void ApplyCollisionShape();
	void ApplyArrowCollisionProfile();
	FArrowImpactPresentationData BuildImpactPresentationData(
		UPrimitiveComponent* OtherComp,
		const FHitResult& Hit) const;
	void BuildStatusEffectSpecs();
	void ApplyDamageToActor(AActor* TargetActor, const FHitResult& HitResult);

	UFUNCTION(BlueprintImplementableEvent, Category = "Arrow")
	void K2_OnImpactFX(const FHitResult& Hit);

protected:
	/** Edit this instead of scaling BoxComp so the arrow mesh keeps its authored size. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Collision",
		meta = (ClampMin = "0.1", UIMin = "0.1"))
	FVector CollisionHalfExtent = FVector(8.0f, 2.0f, 2.0f);

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

	/** Client-only stuck-arrow lifetime. The presentation actor never replicates. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Impact", meta = (ClampMin = "0.1", Units = "s"))
	float StuckArrowLifeSpan = 8.0f;

	/** Visual penetration measured forward from the collision box's leading face. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Impact", meta = (ClampMin = "0.0", Units = "cm"))
	float ImpactEmbedDepth = 2.0f;

	bool bImpactHandled = false;

	/**
	 * When enabled, arrows reject targets that share Team.Player or Team.Enemy
	 * with their source. Enabled by default to prevent friendly fire.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Debug")
	bool bEnableTeamDamageFiltering = true;
};
