#pragma once

#include "CoreMinimal.h"
#include "GAS/Ability/Boss/BossGameplayAbility.h"
#include "GA_BossDashSlash.generated.h"

class UAbilityTask_PlayMontageAndWait;
class UAnimMontage;
class UPrimitiveComponent;
class UStaticMeshComponent;

/**
 * Authoring contract for the server-driven DashSlash montage phases.
 * Gameplay timing is derived from these sections and never depends on AnimNotifies.
 */
USTRUCT(BlueprintType)
struct ENEMY_API FDashSlashMontageConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Montage")
	TObjectPtr<UAnimMontage> Montage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Montage")
	FName WindupEnterSectionName = TEXT("Windup");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Montage")
	FName WindupHoldSectionName = TEXT("WindupHold");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Montage")
	FName AttackSectionName = TEXT("DashSlash");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Montage")
	FName TravelHoldSectionName = TEXT("DashHold");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Montage")
	FName RecoverySectionName = TEXT("Recover");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Montage", meta = (ClampMin = "0.01"))
	float PlayRate = 1.0f;

	/** Time spent in the looping WindupHold section, excluding WindupEnter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Montage", meta = (ClampMin = "0.0", Units = "s"))
	float WindupHoldDuration = 0.5f;

	/** Fails safe if the authored Recover section never completes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Montage", meta = (ClampMin = "0.1", Units = "s"))
	float RecoveryTimeout = 1.5f;
};

enum class EDashSlashPhase : uint8
{
	Inactive,
	WindupEntering,
	WindupHolding,
	DashAttacking,
	WaitingForCompletion,
	Recovering
};

UCLASS()
class ENEMY_API UGA_BossDashSlash : public UBossGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_BossDashSlash();
	virtual void PostLoad() override;
	virtual bool ShouldSurviveBehaviorTreeAbort() const override { return true; }
	virtual bool OwnsPreselectedDestinationAfterCommit() const override { return true; }

	FName GetWindupSectionName() const { return MontageConfig.WindupEnterSectionName; }
	FName GetWindupHoldSectionName() const { return MontageConfig.WindupHoldSectionName; }
	FName GetDashSlashSectionName() const { return MontageConfig.AttackSectionName; }
	FName GetDashHoldSectionName() const { return MontageConfig.TravelHoldSectionName; }
	FName GetRecoverySectionName() const { return MontageConfig.RecoverySectionName; }
	float GetWindupHoldDuration() const { return MontageConfig.WindupHoldDuration; }
	float GetDashAcceptanceRadius() const { return DashAcceptanceRadius; }
	const FDashSlashMontageConfig& GetMontageConfig() const { return MontageConfig; }

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
	void BeginWindupHold();
	void ReleaseWindupAndBeginDash();
	void BeginDash();
	void MarkSlashFinished();

	UFUNCTION()
	void HandleMontageCompleted();

	UFUNCTION()
	void HandleMontageBlendOut();

	UFUNCTION()
	void HandleMontageInterrupted();

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
	void TryStartRecovery();
	void StartRecovery();
	void ConfigureMontageSections();
	bool TransitionMontagePhase(
		EDashSlashPhase ExpectedPhase,
		EDashSlashPhase NextPhase,
		FName DestinationSection);
	bool ValidateMontageConfig(FString& OutError) const;
	bool HasMontageSection(FName SectionName) const;
	float GetSectionDurationSeconds(FName SectionName) const;
	void ActivateDashCollision();
	void DeactivateDashCollision();
	bool CapturePreselectedDestination();
	void FinishDash(bool bWasCancelled);
	void ClearDashState();
	void ClearRuntimeTimers();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash|Montage")
	FDashSlashMontageConfig MontageConfig;

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
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask = nullptr;

	// Serialized compatibility for BPGA_SlashDash assets authored before MontageConfig.
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use MontageConfig.Montage."))
	TObjectPtr<UAnimMontage> DashMontage_DEPRECATED = nullptr;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use MontageConfig.WindupEnterSectionName."))
	FName WindupSectionName_DEPRECATED = TEXT("Windup");

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use MontageConfig.AttackSectionName."))
	FName DashSlashSectionName_DEPRECATED = TEXT("DashSlash");

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use MontageConfig.TravelHoldSectionName."))
	FName DashHoldSectionName_DEPRECATED = TEXT("DashHold");

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use MontageConfig.RecoverySectionName."))
	FName RecoverySectionName_DEPRECATED = TEXT("Recover");

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use MontageConfig.WindupHoldDuration."))
	float WindupDuration_DEPRECATED = 0.5f;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use MontageConfig.RecoveryTimeout."))
	float RecoveryTimeout_DEPRECATED = 1.5f;

	FActiveGameplayEffectHandle DashStateHandle;
	FTimerHandle WindupLeadInTimerHandle;
	FTimerHandle WindupHoldTimerHandle;
	FTimerHandle SlashCompletionTimerHandle;
	FTimerHandle DashTimerHandle;
	FTimerHandle RecoveryTimeoutTimerHandle;
	FVector DashStartLocal = FVector::ZeroVector;
	FVector DashEndLocal = FVector::ZeroVector;
	FVector PreviousWorldLocation = FVector::ZeroVector;
	TWeakObjectPtr<UStaticMeshComponent> CapturedDeckMesh;
	int32 CapturedDestinationPointId = INDEX_NONE;
	double DashStartServerTime = 0.0;
	float EffectiveDashAcceptanceRadius = 0.0f;
	float DashTickInterval = 1.0f / 60.0f;
	TSet<TWeakObjectPtr<AActor>> HitActorsThisDash;
	TEnumAsByte<ECollisionResponse> CachedPawnCollisionResponse = ECR_Block;
	EDashSlashPhase Phase = EDashSlashPhase::Inactive;
	bool bDashStarted = false;
	bool bSlashFinished = false;
	bool bDestinationReached = false;
	bool bCollisionOverrideActive = false;
	bool bFinishing = false;
};
