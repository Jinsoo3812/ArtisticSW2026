#pragma once

#include "CoreMinimal.h"
#include "GAS/Ability/Boss/BossGameplayAbility.h"
#include "GA_BossDashSlash.generated.h"

class UAbilityTask_PlayMontageAndWait;
class UAbilityTask_WaitDelay;
class UAbilityTask_WaitGameplayEvent;
class UAnimMontage;
class UPrimitiveComponent;

UCLASS()
class ENEMY_API UGA_BossDashSlash : public UBossGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_BossDashSlash();
	FName GetWindupSectionName() const { return WindupSectionName; }
	FName GetDashSlashSectionName() const { return DashSlashSectionName; }
	FName GetDashHoldSectionName() const { return DashHoldSectionName; }
	FName GetRecoverySectionName() const { return RecoverySectionName; }
	float GetDashAcceptanceRadius() const { return DashAcceptanceRadius; }

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
	void BeginDash();

	UFUNCTION()
	void HandleDashStartEvent(FGameplayEventData Payload);

	UFUNCTION()
	void HandleSlashFinishedEvent(FGameplayEventData Payload);

	UFUNCTION()
	void HandleMontageCompleted();

	UFUNCTION()
	void HandleMontageBlendOut();

	UFUNCTION()
	void HandleMontageInterrupted();

	UFUNCTION()
	void HandleRecoveryTimeout();

	UFUNCTION()
	void HandleDashOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	void TickDash();
	void ApplySweptDashHits(const FVector& SegmentStart, const FVector& SegmentEnd);
	void TryApplyDashDamage(AActor* Target, const FHitResult& HitResult);
	void HandleDestinationReached();
	void StartRecovery();
	void ConfigureMontageSections();
	bool HasMontageSection(FName SectionName) const;
	void ActivateDashCollision();
	void DeactivateDashCollision();
	bool ValidatePreselectedDestination() const;
	void FinishDash(bool bWasCancelled);
	void ClearDashState();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash")
	TObjectPtr<UAnimMontage> DashMontage = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash|Montage")
	FName WindupSectionName = TEXT("Windup");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash|Montage")
	FName DashSlashSectionName = TEXT("DashSlash");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash|Montage")
	FName DashHoldSectionName = TEXT("DashHold");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash|Montage")
	FName RecoverySectionName = TEXT("Recover");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash", meta = (ClampMin = "0.0", Units = "s"))
	float WindupDuration = 0.5f;

	/** Fails safe if the authored Recover section never completes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash|Montage", meta = (ClampMin = "0.1", Units = "s"))
	float RecoveryTimeout = 1.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash", meta = (ClampMin = "0.05", Units = "s"))
	float DashDuration = 0.45f;

	/** Stops inside this deck-local planar range instead of snapping to one exact point. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash", meta = (ClampMin = "0.0", Units = "cm"))
	float DashAcceptanceRadius = 75.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash", meta = (ClampMin = "1.0", Units = "cm"))
	float DashHitRadius = 120.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash", meta = (ClampMin = "0.0"))
	float Damage = 20.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitDelay> WindupTask = nullptr;

	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitDelay> RecoveryTimeoutTask = nullptr;

	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitGameplayEvent> DashStartEventTask = nullptr;

	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitGameplayEvent> SlashFinishedEventTask = nullptr;

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask = nullptr;

	FActiveGameplayEffectHandle DashStateHandle;
	FTimerHandle DashTimerHandle;
	FVector DashStartLocal = FVector::ZeroVector;
	FVector DashEndLocal = FVector::ZeroVector;
	FVector PreviousWorldLocation = FVector::ZeroVector;
	float EffectiveDashAcceptanceRadius = 0.0f;
	float DashElapsed = 0.0f;
	float DashTickInterval = 1.0f / 60.0f;
	TSet<TWeakObjectPtr<AActor>> HitActorsThisDash;
	TEnumAsByte<ECollisionResponse> CachedPawnCollisionResponse = ECR_Block;
	bool bDashStarted = false;
	bool bSlashFinished = false;
	bool bDestinationReached = false;
	bool bRecoveryStarted = false;
	bool bCollisionOverrideActive = false;
	bool bFinishing = false;
};
