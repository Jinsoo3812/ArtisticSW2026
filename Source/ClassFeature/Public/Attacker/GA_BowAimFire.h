#pragma once

#include "CoreMinimal.h"
#include "BaseGameplayAbility.h"
#include "GA_BowAimFire.generated.h"

class ABowItem;
class AArrowProjectile;
class UBowComponent;
class UAbilityTask_PlayMontageAndWait;
class UAnimMontage;

/**
 * Bow ability driven by right-click aim, left-click draw, and left-click release fire.
 */
UCLASS()
class CLASSFEATURE_API UGA_BowAimFire : public UBaseGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_BowAimFire();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

protected:
	UFUNCTION()
	void OnLeftClickPressed(FGameplayEventData Payload);

	UFUNCTION()
	void OnLeftClickReleased(FGameplayEventData Payload);

	UFUNCTION()
	void OnRightClickReleased(float TimeHeld);

	UFUNCTION()
	void OnReleaseFireEvent(FGameplayEventData Payload);

	UFUNCTION()
	void OnReleaseMontageCompleted();

	UFUNCTION()
	void OnReleaseMontageInterrupted();

	void UpdateDrawAlpha();
	void PlayDrawMontage();
	void StopDrawMontage(float BlendOutTime);
	void BeginRelease();
	void FireArrow();
	void FinishShot();
	void ResetBowState();
	bool CacheBowFromAvatar();
	void AddBowStateTags();
	void RemoveBowStateTags();
	void SetBowDrawTagState(bool bDrawing, bool bFullyDrawn, bool bReleasing);

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Charge", meta = (ClampMin = "0.01"))
	float MaxChargeTime = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Charge", meta = (ClampMin = "0.0"))
	float DrawAlphaStartDelay = 0.6f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Charge", meta = (ClampMin = "0.01"))
	float FullDrawTime = 1.03f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Charge", meta = (ClampMin = "0.005"))
	float ChargeTickRate = 0.02f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Charge", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FullDrawAlphaToRelease = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Animation")
	TObjectPtr<UAnimMontage> DrawMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Animation", meta = (ClampMin = "0.01"))
	float DrawMontagePlayRate = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Animation", meta = (ClampMin = "0.0"))
	float DrawMontageBlendOutTime = 0.1f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Animation")
	TObjectPtr<UAnimMontage> ReleaseMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Animation", meta = (ClampMin = "0.01"))
	float ReleaseMontagePlayRate = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Animation")
	bool bRequireReleaseNotifyToFire = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Damage", meta = (ClampMin = "0.0"))
	float MinChargeDamageMultiplier = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Damage", meta = (ClampMin = "0.0"))
	float MaxChargeDamageMultiplier = 1.0f;

	UPROPERTY()
	TObjectPtr<ABowItem> CachedBow;

	UPROPERTY()
	TObjectPtr<UBowComponent> CachedBowComponent;

	FTimerHandle ChargeTimerHandle;
	float DrawStartTime = 0.0f;
	bool bIsDrawing = false;
	bool bIsFullyDrawn = false;
	bool bIsReleaseInProgress = false;
	bool bHasFiredCurrentShot = false;
};
