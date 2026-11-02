#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BossAI/BossGunAimLine.h"
#include "BossGunCombatComponent.generated.h"

class AGunnerBossEnemy;
class UGunnerBossCombatData;

/** Server socket/trace queries, movement lock and presentation lifetime. GA owns attack timing/damage. */
UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
class ENEMY_API UBossGunCombatComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UBossGunCombatComponent();
	bool CanStart(AActor* Target) const;
	bool IsLiveTarget(AActor* Target) const;
	bool IsRuntimeValid(AActor* Target) const;
	bool GetOrigin(FVector& Out) const;
	FVector GetAimPoint(AActor* Target) const;
	bool HasClearLine(AActor* Target) const;
	bool HasClearLineFrom(AActor* Target, const FVector& Start) const;
	bool Trace(const FVector& Start, const FVector& Direction, FHitResult& OutHit, FVector& OutEnd) const;
	uint32 BeginAttack(AActor* Target);
	bool UpdateAim(uint32 AttackId, AActor* Target, EBossGunAimPhase Phase, float StartTime, float EndTime, float PlannedShotTime);
	bool LockAim(uint32 AttackId, AActor* Target);
	bool BuildShot(uint32 AttackId, FHitResult& OutHit, FVector& OutStart, FVector& OutEnd) const;
	void ShowShot(uint32 AttackId, const FVector& Start, const FVector& End, const FHitResult& Hit);
	void SetHiddenPhase(uint32 AttackId, EBossGunAimPhase Phase, float StartTime, float EndTime);
	void FinishAttack(uint32 AttackId, float PostAttackGap, bool bCancelled = true);
	void ResetCombat();
	bool IsAttackLocked() const { return bAttackLocked; }
	bool OwnsAttack(uint32 AttackId) const { return AttackId != 0 && ActiveAttackId == AttackId; }
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	UPROPERTY(ReplicatedUsing = OnRep_AttackLocked) bool bAttackLocked = false;
	UFUNCTION() void OnRep_AttackLocked();
	UPROPERTY(Transient) TObjectPtr<ABossGunAimLine> AimLine;
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastShotFeedback(FVector_NetQuantize Start, FVector_NetQuantize End);
private:
	AGunnerBossEnemy* Boss() const;
	const UGunnerBossCombatData* Data() const;
	void ApplyMovementLock();
	FVector LockedWorldDirection() const;
	uint32 NextAttackId = 0;
	uint32 ActiveAttackId = 0;
	double NextAttackTime = 0.;
	FVector LocalLockedDirection = FVector::ForwardVector;
	TWeakObjectPtr<USceneComponent> LockedFrame;
	bool bAimLocked = false;
	bool bLocalLockApplied = false;
	bool bOldControllerYaw = false;
	bool bOldOrientToMovement = false;
	bool bOldDesiredRotation = false;
	float OldBaseSpeed = 0.f;
	uint8 OldPoseTickPolicy = 0;
	uint8 OldRootMotionMode = 0;
};
