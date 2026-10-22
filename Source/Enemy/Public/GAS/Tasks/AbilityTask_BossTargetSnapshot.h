#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "GAS/Tasks/BossTargetSnapshotTypes.h"
#include "AbilityTask_BossTargetSnapshot.generated.h"

class AShipBossEnemy;

/** Tracks one player's deck pose and permanently freezes it on death/destruction. Server only. */
UCLASS()
class ENEMY_API UAbilityTask_BossTargetSnapshot : public UAbilityTask
{
	GENERATED_BODY()
public:
	static UAbilityTask_BossTargetSnapshot* Track(UGameplayAbility* Owner, AShipBossEnemy* Boss, AActor* Target);
	virtual void Activate() override;
	const FBossTargetSnapshot& GetSnapshot();
	AActor* GetTrackedActor() const { return Target.Get(); }
	bool IsFrozen() const { return bFrozen; }
protected:
	virtual void OnDestroy(bool bAbilityEnded) override;
private:
	void Refresh();
	void SamplePose();
	void Freeze();
	void OnDeadTagChanged(FGameplayTag Tag, int32 NewCount);
	UFUNCTION() void OnTargetEndPlay(AActor* Actor, EEndPlayReason::Type Reason);
	TWeakObjectPtr<AShipBossEnemy> Boss;
	TWeakObjectPtr<AActor> Target;
	TWeakObjectPtr<UAbilitySystemComponent> TargetASC;
	UPROPERTY() FBossTargetSnapshot Snapshot;
	FDelegateHandle DeathDelegate;
	FTimerHandle RefreshTimer;
	bool bFrozen = false;
};
