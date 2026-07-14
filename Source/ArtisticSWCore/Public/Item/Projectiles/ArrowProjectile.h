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
struct FArrowDamageData
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Damage")
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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Damage")
	TArray<TSubclassOf<UGameplayEffect>> StatusEffectClasses;
};

UCLASS()
class ARTISTICSWCORE_API AArrowProjectile : public ABaseProjectile
{
	GENERATED_BODY()

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
	void InitializeDamage(UAbilitySystemComponent* InSourceASC, AActor* InInstigatorActor, float InChargeDamageMultiplier);

	UFUNCTION(BlueprintCallable, Category = "Arrow", meta = (DeprecatedFunction, DeprecationMessage = "Use DamageData.DamageEffects on the arrow class instead."))
	void SetDamageEffectSpecHandle(const FGameplayEffectSpecHandle& InDamageEffectSpecHandle);

	UFUNCTION(BlueprintCallable, Category = "Arrow", meta = (DeprecatedFunction, DeprecationMessage = "Use DamageData.DamageEffects on the arrow class instead."))
	void SetAdditionalDamageEffectSpecHandles(const TArray<FGameplayEffectSpecHandle>& InAdditionalDamageEffectSpecHandles);

	UFUNCTION(NetMulticast, Unreliable, BlueprintCallable, Category = "Arrow")
	void Multicast_PlayImpactFX(const FHitResult& Hit);

protected:
	UFUNCTION()
	void OnArrowHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	bool ShouldIgnoreHitActor(const AActor* OtherActor) const;
	void BuildDamageEffectSpecs();
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
	TObjectPtr<UAbilitySystemComponent> SourceASC;

	UPROPERTY(Transient)
	TObjectPtr<AActor> InstigatorActor;

	UPROPERTY(Transient)
	float ChargeDamageMultiplier = 1.0f;

	UPROPERTY(Transient)
	bool bHasRolledCritical = false;

	UPROPERTY(Transient)
	bool bCriticalHit = false;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> MovementIgnoredActors;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Movement", meta = (ClampMin = "0.0"))
	float FlightGravityScale = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow")
	bool bDestroyOnImpact = true;
};
