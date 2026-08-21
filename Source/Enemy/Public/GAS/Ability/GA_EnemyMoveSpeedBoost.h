#pragma once

#include "CoreMinimal.h"
#include "BaseGameplayAbility.h"
#include "GameplayEffect.h"

#include "GA_EnemyMoveSpeedBoost.generated.h"

/** Duration effect that additively modifies UEnemyAttributeSet::MoveSpeedBonus. */
UCLASS()
class ENEMY_API UEnemyMoveSpeedBoostEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UEnemyMoveSpeedBoostEffect();
};

UCLASS()
class ENEMY_API UEnemyMoveSpeedBoostCooldownEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UEnemyMoveSpeedBoostCooldownEffect();
};

/**
 * Server-only Enemy self buff. It applies a duration GE and ends immediately,
 * allowing the shared BT ability task to complete synchronously and continue.
 */
UCLASS()
class ENEMY_API UGA_EnemyMoveSpeedBoost : public UBaseGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_EnemyMoveSpeedBoost();

	virtual const FGameplayTagContainer* GetCooldownTags() const override;
	virtual void ApplyCooldown(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) const override;

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	float GetMoveSpeedBonus() const { return MoveSpeedBonus; }
	float GetBuffDuration() const { return BuffDuration; }

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Buff", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MoveSpeedBonus = 150.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Buff", meta = (ClampMin = "0.01", Units = "s"))
	float BuffDuration = 5.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Buff", meta = (ClampMin = "0.0", Units = "s"))
	float CooldownDuration = 8.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Buff")
	TSubclassOf<UGameplayEffect> MoveSpeedBoostEffectClass;

private:
	FGameplayTagContainer NativeCooldownTags;
};
