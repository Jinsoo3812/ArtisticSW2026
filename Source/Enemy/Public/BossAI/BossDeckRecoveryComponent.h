#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DeckAI/DeckWalkTypes.h"
#include "BossDeckRecoveryComponent.generated.h"

class AEnemyShip;
class AShipBossEnemy;
class UBrainComponent;
class UDeckWalkAreaComponent;

/** Server-only, event-driven post-teleport support monitoring and bounded hidden recovery. */
UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
class ENEMY_API UBossDeckRecoveryComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UBossDeckRecoveryComponent();
	void NotifyHidden();
	/** Inspects actual support once immediately after a teleport attempt. */
	void NotifyRelocated(const FDeckWalkLocation& IntendedFloor);
	/** Starts a fresh consecutive-failure sequence only after visibility is restored. */
	void NotifyRevealed();
	void ResetRecovery(bool bResumeLogic = false);
	bool IsRecovering() const { return bRecovering; }

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Deck Recovery", meta = (ClampMin = "0.02", Units = "s"))
	float SupportCheckInterval = 0.1f;
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Deck Recovery", meta = (ClampMin = "2", ClampMax = "10"))
	int32 ConsecutiveUnsupportedChecks = 3;
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Deck Recovery", meta = (ClampMin = "2.0", Units = "cm"))
	float MaximumFeetGap = 15.f;
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Deck Recovery", meta = (ClampMin = "0.01", Units = "s"))
	float HiddenLeadTime = 0.15f;
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Deck Recovery", meta = (ClampMin = "0.01", Units = "s"))
	float HiddenSettleTime = 0.1f;
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Deck Recovery", meta = (ClampMin = "1", ClampMax = "10"))
	int32 MaximumRecoveryAttempts = 3;
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Deck Recovery", meta = (ClampMin = "0.5", Units = "s"))
	float MaximumRecoverySeconds = 2.f;

private:
	AShipBossEnemy* GetLiveBoss() const;
	UDeckWalkAreaComponent* GetArea() const;
	void CheckSupport();
	void ScheduleSupportCheck();
	void BeginRecovery();
	void HideForRecovery();
	void RelocateForRecovery();
	void RevealAfterRecovery();
	void FinishRecovery(bool bSupported);
	bool HasRecoveryTimedOut() const;
	void ReleaseRecoveryResources(bool bResumeLogic);
	void CancelBossCombatAbilities(AShipBossEnemy& Boss);

	TWeakObjectPtr<AEnemyShip> ObservedShip;
	TWeakObjectPtr<UBrainComponent> PausedBrain;
	FDeckWalkLocation IntendedLocation;
	FDeckWalkLocation LastSupportedLocation;
	FDeckWalkLocation RecoveryLocation;
	TSet<int32> FailedNodes;
	FTimerHandle SupportTimer;
	FTimerHandle RecoveryTimer;
	FTimerHandle RecoveryDeadlineTimer;
	double MonitoringDeadline = 0.;
	double RecoveryDeadline = 0.;
	int32 UnsupportedChecks = 0;
	int32 RecoveryAttempts = 0;
	int32 RecoveryRevision = INDEX_NONE;
	bool bAwaitingReveal = false;
	bool bRecovering = false;
	bool bOwnsBusyTag = false;
};
