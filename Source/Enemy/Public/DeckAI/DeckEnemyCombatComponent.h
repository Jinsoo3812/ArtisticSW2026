#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DeckAI/DeckWalkTypes.h"
#include "GameplayAbilitySpecHandle.h"
#include "DeckEnemyCombatComponent.generated.h"

class AAIController;
class ADeckEnemy;
class AEnemyShip;
class UAnimMontage;

UENUM(BlueprintType)
enum class EDeckAttackOutcome : uint8
{
	Ready, Executed, BlockedLOS, OutOfRange, Cooldown, TargetInvalid, Interrupted, InvalidSetup
};

/** Server-owned combat queries/results. Movement and geometry belong to other components. */
UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
class ENEMY_API UDeckEnemyCombatComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UDeckEnemyCombatComponent();
	EDeckAttackOutcome EvaluateAttack(AActor* Target, bool bCheckCooldown = true) const;
	bool HasAttackPosition(AActor* Target) const;
	bool HasClearAttackLine(AActor* Target) const;
	bool IsCoolingDown() const;
	bool FindAttackAbility(FGameplayAbilitySpecHandle& Out) const;
	uint32 BeginAttack(AActor* Target);
	/** Owns the attack lifetime after GAS commit; target changes do not end this lifetime. */
	void CommitAttack(uint32 Attempt, FGameplayAbilitySpecHandle Ability, UAnimMontage* Montage);
	bool IsCurrentAttack(uint32 Attempt) const;
	bool HasCommittedAttack() const;
	bool IsAttackMontagePlaying() const;
	void CancelCommittedAttack();
	void RecordExecuted(uint32 Attempt);
	void RecordBlockedLOS(uint32 Attempt, AActor* Target);
	void RecordFailure(uint32 Attempt, EDeckAttackOutcome Outcome);
	void EndAttack(uint32 Attempt, bool bCancelled);
	uint32 GetAttemptId() const { return AttemptId; }
	EDeckAttackOutcome GetLastOutcome() const { return LastOutcome; }
	bool HasRecovery(AActor* Target = nullptr) const;
	bool HasStoredRecovery() const { return RecoveryTarget.IsValid(); }
	bool GetRecoveryGoal(FDeckWalkLocation& Out) const;
	void ClearRecovery();
	void ResetCombat();
	void AcquireFocus();
	void RefreshFocus();
	void ReleaseFocus();

private:
	ADeckEnemy* GetEnemy() const;
	void ReleaseAttackCommitment();
	uint32 AttemptId = 0;
	bool bAttackCommitted = false;
	bool bBlocksHitReaction = false;
	FGameplayAbilitySpecHandle CommittedAbility;
	TWeakObjectPtr<UAnimMontage> CommittedMontage;
	EDeckAttackOutcome LastOutcome = EDeckAttackOutcome::InvalidSetup;
	TWeakObjectPtr<AActor> AttemptTarget;
	TWeakObjectPtr<AActor> RecoveryTarget;
	TWeakObjectPtr<AEnemyShip> RecoveryShip;
	FDeckWalkLocation RecoveryFloor;
	double RecoveryTime = 0.0;
	int32 FocusUsers = 0;
	TWeakObjectPtr<AAIController> FocusController;
	TWeakObjectPtr<AActor> FocusTarget;
	bool bOldOrientToMovement = false;
	bool bOldDesiredRotation = false;
	bool bOldControllerYaw = false;
};
