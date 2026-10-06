#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "DeckAI/DeckWalkTypes.h"
#include "GAS/Tasks/BossSlashDashTypes.h"
#include "GAS/SWGameplayEffectContext.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AbilityTask_BossSlashDashExecution.generated.h"

class AShipBossEnemy;
class UAbilityTask_PlayMontageAndWait;
class UPrimitiveComponent;
class UPathCombatPresentationDataAsset;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBossSlashDashExecutionDelegate);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBossSlashDashHitDelegate, AActor*, Target, const FHitResult&, Hit);

/** One server-authoritative dash. Owns movement/presentation/cleanup, never GA commit or cooldown. */
UCLASS()
class ENEMY_API UAbilityTask_BossSlashDashExecution : public UAbilityTask
{
	GENERATED_BODY()
public:
	static UAbilityTask_BossSlashDashExecution* Execute(UGameplayAbility* Owner, AShipBossEnemy* Boss,
		const FDashSlashMontageConfig& Montage, float Duration, float MinimumDistance, float HitRadius,
		UPathCombatPresentationDataAsset* Presentation, FGameplayTag ChargeCue, float TotalWaitOverride = -1.f,
		bool bSkipWindup = false, bool bSkipRecovery = false);
	UPROPERTY(BlueprintAssignable) FBossSlashDashExecutionDelegate OnCompleted;
	UPROPERTY(BlueprintAssignable) FBossSlashDashExecutionDelegate OnFailed;
	UPROPERTY(BlueprintAssignable) FBossSlashDashHitDelegate OnHit;
	static bool ValidateConfig(const FDashSlashMontageConfig& Montage, float TotalWaitOverride, FString& OutError,
		bool bSkipWindup = false);
	virtual void Activate() override;
protected:
	virtual void OnDestroy(bool bAbilityEnded) override;
	AShipBossEnemy* GetBossAvatar() const { return BossAvatar.Get(); }
	UAbilitySystemComponent* GetAbilitySystemComponentFromActorInfo() const;
	void StartChargingCue();
	void StopChargingCue();
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
	bool ValidateCommittedPath(FString& OutError) const;
	FActiveGameplayEffectHandle ApplyPathPresentationEffect(
		TSubclassOf<UGameplayEffect> EffectClass) const;
	void StartPathTelegraph();
	void StopPathTelegraph();
	void StartExecutedPathPresentation();
	bool LockMovementToCommittedStart();
	void RestoreMovementAfterAbility();
	bool ResolveCommittedPathWorld(
		FVector& OutStart,
		FVector& OutEnd,
		FVector& OutSurfaceNormal) const;
	void FinishDash(bool bWasCancelled);
	void ClearDashState();
	void ClearRuntimeTimers();

	UPROPERTY()
	FDashSlashMontageConfig MontageConfig;

	UPROPERTY()
	float DashDuration = 0.45f;

	/** Authoritative safety check in addition to the destination selector filter. */
	UPROPERTY()
	float MinimumDashDistance = 500.0f;

	UPROPERTY()
	float DashHitRadius = 120.0f;


	/** Reusable path presentation policy. GameplayEffect classes own cue tags and lifetime. */
	UPROPERTY()
	TObjectPtr<UPathCombatPresentationDataAsset> PathPresentation = nullptr;


	UPROPERTY() TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;
	TWeakObjectPtr<AShipBossEnemy> BossAvatar;
	FGameplayTag ChargingGameplayCueTag;
	FTimerHandle WatchdogTimer;
	float TotalWaitOverride = -1.f;
	bool bChargingCueActive = false;
	FActiveGameplayEffectHandle DashStateHandle;
	FActiveGameplayEffectHandle TelegraphEffectHandle;
	FTimerHandle WindupLeadInTimerHandle;
	FTimerHandle WindupHoldTimerHandle;
	FTimerHandle SlashCompletionTimerHandle;
	FTimerHandle DashTimerHandle;
	FTimerHandle RecoveryTimeoutTimerHandle;
	UPROPERTY(Transient)
	FSWPathCuePayload CommittedPath;

	FVector PreviousWorldLocation = FVector::ZeroVector;
	TWeakObjectPtr<UStaticMeshComponent> CapturedDeckMesh;
	FDeckWalkLocation CapturedDestinationLocation;
	FDeckWalkLocation CapturedStartLocation;
	double DashStartServerTime = 0.0;
	float DashTickInterval = 1.0f / 60.0f;
	TEnumAsByte<EMovementMode> CachedMovementMode = MOVE_Walking;
	uint8 CachedCustomMovementMode = 0;
	float CachedMaxWalkSpeed = 0.0f;
	int32 NextPathInstanceId = 0;
	TEnumAsByte<ECollisionResponse> CachedPawnCollisionResponse = ECR_Block;
	EDashSlashPhase Phase = EDashSlashPhase::Inactive;
	bool bDashStarted = false;
	bool bSlashFinished = false;
	bool bDestinationReached = false;
	bool bCollisionOverrideActive = false;
	bool bMovementLocked = false;
	bool bFinishing = false;
	bool bSkipWindup = false;
	bool bSkipRecovery = false;
};
