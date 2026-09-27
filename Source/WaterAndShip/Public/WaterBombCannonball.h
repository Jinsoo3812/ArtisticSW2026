#pragma once

#include "CoreMinimal.h"
#include "Cannonball.h"
#include "GameplayTagContainer.h"
#include "WaterBombCannonball.generated.h"

class ABaseCharacter;
class AShip;
class UAbilitySystemComponent;
class UGameplayEffect;
class UNiagaraSystem;

/**
 * 적함에 명중하면 대포를 봉쇄하고, 실제로 그 배를 Movement Base/부착 부모로
 * 사용 중인 적 캐릭터에게 공격속도 GE를 적용하는 물폭탄입니다.
 */
UCLASS(HideCategories=("Cannonball|Effects"))
class WATERANDSHIP_API AWaterBombCannonball : public ACannonball
{
	GENERATED_BODY()

public:
	AWaterBombCannonball();

	UFUNCTION(BlueprintPure, Category = "Water Bomb")
	float GetEffectDurationSeconds() const { return EffectDurationSeconds; }

	UFUNCTION(BlueprintPure, Category = "Water Bomb")
	float GetAttackSpeedMultiplier() const { return AttackSpeedMultiplier; }

	void ConfigureFromAbility(float InEffectDurationSeconds, float InAttackSpeedMultiplier);

protected:
	virtual void HandleShipHit(AShip* HitShip) override;
	virtual UNiagaraSystem* GetProjectileEffect() const override;
	virtual float GetProjectileEffectScale() const override;
	virtual float GetProjectileEffectLifetimeScale() const override;
	virtual float GetProjectileEffectPlaybackSpeed() const override;
	virtual UNiagaraSystem* GetWaterImpactEffect() const override;
	virtual float GetWaterImpactEffectScale() const override;
	virtual float GetWaterImpactEffectLifetimeScale() const override;
	virtual float GetWaterImpactEffectPlaybackSpeed() const override;

	/** 대포 봉쇄와 승선 적 감속에 공통으로 사용되는 GE 지속시간입니다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Water Bomb|Effect", meta = (ClampMin = "0.1", Units = "s"))
	float EffectDurationSeconds = 5.0f;

	/** 1.0은 정상, 0.5는 공격 모션/주기가 50% 속도로 진행됩니다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Water Bomb|Effect", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float AttackSpeedMultiplier = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Water Bomb|Effect")
	TSubclassOf<UGameplayEffect> AttackSpeedEffectClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Water Bomb|Effect")
	TSubclassOf<UGameplayEffect> CannonDisableEffectClass;

	/** Niagara used only when the water-bomb projectile bursts on an enemy ship. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Water Bomb|Effects")
	TObjectPtr<UNiagaraSystem> WaterBombExplosionEffect = nullptr;

	/** Size multiplier applied through the shared Niagara tuning system. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Water Bomb|Effects", meta = (ClampMin = "0.01"))
	float WaterBombExplosionEffectScale = 1.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Water Bomb|Effects", meta = (ClampMin = "0.01"))
	float WaterBombExplosionEffectLifetimeScale = 1.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Water Bomb|Effects", meta = (ClampMin = "0.01"))
	float WaterBombExplosionEffectPlaybackSpeed = 1.0f;

	/** Niagara effect that follows only the water-bomb projectile while in flight. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Water Bomb|Effects")
	TObjectPtr<UNiagaraSystem> WaterBombProjectileEffect = nullptr;

	/** Size multiplier applied through the shared Niagara tuning system. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Water Bomb|Effects", meta = (ClampMin = "0.01"))
	float WaterBombProjectileEffectScale = 1.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Water Bomb|Effects", meta = (ClampMin = "0.01"))
	float WaterBombProjectileEffectLifetimeScale = 1.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Water Bomb|Effects", meta = (ClampMin = "0.01"))
	float WaterBombProjectileEffectPlaybackSpeed = 1.0f;

	/** Explicit water-entry effect for water bombs; does not silently inherit the normal cannonball asset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Water Bomb|Effects")
	TObjectPtr<UNiagaraSystem> WaterBombWaterImpactEffect = nullptr;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Water Bomb|Effects", meta = (ClampMin = "0.01"))
	float WaterBombWaterImpactEffectScale = 1.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Water Bomb|Effects", meta = (ClampMin = "0.01"))
	float WaterBombWaterImpactEffectLifetimeScale = 1.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Water Bomb|Effects", meta = (ClampMin = "0.01"))
	float WaterBombWaterImpactEffectPlaybackSpeed = 1.0f;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FWaterBombProjectileIntegrationTest;
#endif

	bool IsCharacterOnShip(const ABaseCharacter* Character, const AShip* Ship) const;
	bool ApplyTimedEffect(
		UAbilitySystemComponent* TargetASC,
		TSubclassOf<UGameplayEffect> EffectClass,
		const FGameplayTag& GrantedTag,
		TOptional<float> SetByCallerAttackSpeedMultiplier = TOptional<float>()) const;
};
