#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "DeckAI/DeckWalkTypes.h"
#include "GameplayEffectTypes.h"
#include "AbilityTask_BossVanishRelocation.generated.h"

class AShipBossEnemy;
class AEnemyShip;
class UAnimMontage;
class UAbilityTask_PlayMontageAndWait;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBossVanishRelocationDelegate);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBossVanishLocationDelegate, FVector, Location);

/** One prepared relocation. Owns visibility, montage/timers and cleanup, not attack or cooldown policy. */
UCLASS()
class ENEMY_API UAbilityTask_BossVanishRelocation : public UAbilityTask
{
	GENERATED_BODY()
public:
	static UAbilityTask_BossVanishRelocation* Relocate(UGameplayAbility* Owner,
		AShipBossEnemy* Boss, AActor* Target, UAnimMontage* PreparationMontage, float PreparationDelay,
		float HiddenDuration, float SettleTime);
	/** Snapshot variant has no player-lifetime dependency; destination is still validated by the task. */
	static UAbilityTask_BossVanishRelocation* RelocateToSnapshot(UGameplayAbility* Owner,
		AShipBossEnemy* Boss, FVector TargetLocalFloor, UAnimMontage* PreparationMontage,
		float PreparationDelay, float HiddenDuration, float SettleTime);

	UPROPERTY(BlueprintAssignable) FBossVanishRelocationDelegate OnRevealed;
	UPROPERTY(BlueprintAssignable) FBossVanishRelocationDelegate OnFailed;
	UPROPERTY(BlueprintAssignable) FBossVanishLocationDelegate OnDeparture;
	UPROPERTY(BlueprintAssignable) FBossVanishLocationDelegate OnArrival;
	bool HasStartedHiding() const { return bStartedHiding; }
	virtual void Activate() override;
protected:
	virtual void OnDestroy(bool bAbilityEnded) override;
private:
	UFUNCTION() void BeginHidden();
	UFUNCTION() void RelocateHidden();
	UFUNCTION() void Reveal();
	UFUNCTION() void Fail();
	void StopPreparationMontage();
	void ClearHiddenState();
	bool IsTargetUsable() const;
	FVector ResolveFacingPosition() const;
	UPROPERTY() TObjectPtr<AShipBossEnemy> Boss;
	TWeakObjectPtr<AActor> LockedTarget;
	TWeakObjectPtr<AEnemyShip> CapturedShip;
	UPROPERTY() TObjectPtr<UAnimMontage> PreparationMontage;
	UPROPERTY() TObjectPtr<UAbilityTask_PlayMontageAndWait> PreparationMontageTask;
	FDeckWalkLocation CapturedDestination;
	FActiveGameplayEffectHandle HiddenStateHandle;
	FTimerHandle PhaseTimer;
	float PreparationDelay = 0.f;
	float HiddenDuration = 0.35f;
	float SettleTime = 0.1f;
	bool bStartedHiding = false;
	bool bOwnsHiddenState = false;
	bool bUseSnapshotTarget = false;
	FVector SnapshotLocalFloor = FVector::ZeroVector;
};
