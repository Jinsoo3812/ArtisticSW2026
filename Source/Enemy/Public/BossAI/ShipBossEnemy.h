#pragma once

#include "CoreMinimal.h"
#include "BaseEnemy.h"
#include "DeckAI/DeckWaypointMovementInterface.h"
#include "ShipBossEnemy.generated.h"

class AEnemyShip;
class ADeckRangedEnemy;
class USphereComponent;

/** Server-authored boss pawn whose tactical positions live on a moving enemy ship. */
UCLASS(Blueprintable)
class ENEMY_API AShipBossEnemy : public ABaseEnemy, public IDeckWaypointMovementInterface
{
	GENERATED_BODY()

public:
	AShipBossEnemy();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Encounter")
	bool InitializeBoss(AEnemyShip* InHostShip, int32 InitialPointId, AActor* InitialTarget);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Target")
	void SetBossCombatTarget(AActor* NewTarget);

	UFUNCTION(BlueprintPure, Category = "Boss|Target")
	AActor* GetBossCombatTarget() const;

	UFUNCTION(BlueprintPure, Category = "Boss|Ship")
	AEnemyShip* GetHostShip() const { return HostShip; }

	UFUNCTION(BlueprintPure, Category = "Boss|Point")
	int32 GetCurrentPointId() const { return CurrentPointId; }

	UFUNCTION(BlueprintPure, Category = "Boss|Point")
	int32 GetPreviousPointId() const { return PreviousPointId; }

	UFUNCTION(BlueprintPure, Category = "Boss|Point")
	int32 GetDestinationPointId() const { return DestinationPointId; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Point")
	void SetDestinationPointId(int32 NewPointId) { DestinationPointId = NewPointId; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Point")
	void MarkDestinationReached();

	virtual AEnemyShip* GetDeckHostShip() const override { return HostShip; }
	virtual int32 GetCurrentDeckPointId() const override { return CurrentPointId; }
	virtual int32 GetGoalDeckPointId() const override { return DestinationPointId; }
	virtual void OnDeckPointReached() override { MarkDestinationReached(); }
	virtual void OnDeckMoveFailed() override;
	virtual bool CanMoveOnDeck() const override;

	bool ResolvePointTransform(int32 PointId, FTransform& OutTransform) const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|State")
	bool TransitionBossAIState(FGameplayTag ExpectedState, FGameplayTag NewState);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|State")
	void SetBossHidden(bool bInHidden);

	UFUNCTION(BlueprintPure, Category = "Boss|State")
	bool IsBossHidden() const { return bBossHidden; }

	/** Hides first, then freezes server-authored CharacterMovement for a safe relocation. */
	bool BeginHiddenRelocation();

	/** Teleports while hidden and reattaches to the live ship deck without revealing. */
	bool RelocateWhileHidden(const FTransform& DestinationTransform);

	/** Restores a grounded walking state before the hidden presentation is removed. */
	void FinishHiddenRelocation();

	bool IsHiddenRelocationActive() const { return bHiddenRelocationActive; }

	UFUNCTION(BlueprintPure, Category = "Boss|Summon")
	bool CanSummonDeckEnemy() const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Summon")
	bool TrySummonDeckEnemy(ADeckRangedEnemy*& OutEnemy);

	/** Query-only sensor enabled by DashSlash; the character capsule remains the movement body. */
	UFUNCTION(BlueprintPure, Category = "Boss|Combat")
	USphereComponent* GetDashDamageVolume() const { return DashDamageVolume; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void HandleDeath_Implementation() override;
	virtual bool ShouldWaitForDeathAbility() const override { return true; }

	UFUNCTION()
	void OnRep_HostShip();

	UFUNCTION()
	void OnRep_BossHidden();

	UFUNCTION()
	void HandleHostShipDestroyed(AActor* DestroyedActor);

	void BindHostShip();
	void UnbindHostShip();
	void ApplyHiddenPresentation();
	bool IsExclusiveBossAIState(FGameplayTag StateTag) const;
	void ReleaseSummonedDeckEnemies();

	UPROPERTY(ReplicatedUsing = OnRep_HostShip, VisibleInstanceOnly, BlueprintReadOnly, Category = "Boss|Ship")
	TObjectPtr<AEnemyShip> HostShip = nullptr;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Boss|Point")
	int32 CurrentPointId = INDEX_NONE;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Boss|Point")
	int32 PreviousPointId = INDEX_NONE;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Boss|Point")
	int32 DestinationPointId = INDEX_NONE;

	UPROPERTY(ReplicatedUsing = OnRep_BossHidden, VisibleInstanceOnly, BlueprintReadOnly, Category = "Boss|State")
	bool bBossHidden = false;

	UPROPERTY(Transient)
	TObjectPtr<AActor> BossCombatTarget = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boss|Combat", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> DashDamageVolume = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Summon", meta = (ClampMin = "1", ClampMax = "8"))
	int32 MaxSummonedDeckEnemies = 2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Summon", meta = (ClampMin = "0.0", Units = "s"))
	float SummonCooldown = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Summon", meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumSummonDistanceFromTarget = 300.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Summon", meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumSummonDistanceFromBoss = 200.0f;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<ADeckRangedEnemy>> SummonedDeckEnemies;

	double NextSummonAllowedTime = 0.0;

	ECollisionEnabled::Type InitialCapsuleCollision = ECollisionEnabled::QueryAndPhysics;
	bool bHiddenRelocationActive = false;
};
