#pragma once

#include "CoreMinimal.h"
#include "BaseEnemy.h"
#include "DeckAI/DeckWalkTypes.h"
#include "DeckAI/DeckWaypointMovementInterface.h"
#include "BossAI/BossDeckPointSelector.h"
#include "ShipBossEnemy.generated.h"

USTRUCT()
struct FSWRoomShipBossState
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FGuid HostShipId;
	UPROPERTY(SaveGame) int32 InitialSpawnPointId = INDEX_NONE;
	// Persist surface/local coordinates; graph node indices and revisions are transient.
	UPROPERTY(SaveGame) FName PreviousSurfaceId;
	UPROPERTY(SaveGame) FVector PreviousLocalFloor = FVector::ZeroVector;
	UPROPERTY(SaveGame) FName DestinationSurfaceId;
	UPROPERTY(SaveGame) FVector DestinationLocalFloor = FVector::ZeroVector;
	UPROPERTY(SaveGame) bool bWalkingToDestination = false;
	UPROPERTY(SaveGame) FGameplayTag BossAIState;
	UPROPERTY(SaveGame) bool bStunHealthThresholdConsumed = false;
	UPROPERTY(SaveGame) int32 PendingBalanceSummons = 0;
	UPROPERTY(SaveGame) TArray<int32> ConsumedSummonThresholds;
	UPROPERTY(SaveGame) float SummonCooldownRemaining = 0.f;
	UPROPERTY(SaveGame) TArray<FGuid> SummonedEnemyIds;
};

class AEnemyShip;
class ADeckEnemy;
class UBossBasicAttackSet;
class USphereComponent;
class UDeckWalkRouteComponent;
class UDeckCombatTargetResolverComponent;

/** Server-authored boss pawn whose tactical positions live on a moving enemy ship. */
UCLASS(Blueprintable)
class ENEMY_API AShipBossEnemy : public ABaseEnemy, public IDeckWaypointMovementInterface
{
	GENERATED_BODY()

public:
	AShipBossEnemy();
	virtual void CaptureRoomDomains(TArray<FSWRoomDomainPart>& OutParts, TArray<FSWRoomCaptureIssue>& OutIssues) const override;
	virtual bool RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& OutError) override;
	virtual bool CompareRoomDomain(const FSWRoomDomainPart& Expected, const FSWRoomDomainPart& Actual,
		float TimeToleranceSeconds, TArray<FString>& OutFields) const override
	{
		return Expected.Domain == ESWRoomDomain::Enemy
			? ABaseEnemy::CompareRoomDomain(Expected, Actual, TimeToleranceSeconds, OutFields)
			: FSWRoomStructCodec::Compare<FSWRoomShipBossState>(Expected, Actual, TimeToleranceSeconds, OutFields);
	}
	virtual bool FinalizeRoomRestore(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError) override;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Balance", meta = (RowType = "/Script/Enemy.EnemyEncounterBalanceRow"))
	FDataTableRowHandle EncounterBalanceRow;
	/** Exact ranged BP class already allocated by the host's SpawnPlan. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Balance")
	TSubclassOf<ADeckEnemy> SummonedEnemyClass;
	float GetBalancedBossAttackCoefficient(float Fallback, bool bMajorAttack) const;
	float GetBalancedTelegraphDuration(float Fallback) const
	{
		return bUseEncounterBalance ? EncounterBalance.MajorAttackTelegraphSeconds : Fallback;
	}

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** Boss corpses remain animated; even legacy Blueprint ragdoll calls must not enable physics. */
	virtual void ApplyLocalDeathRagdoll() override;

	/** InitialTarget may be null when a game mode possesses the sensed Player Ship directly. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Encounter")
	bool InitializeBoss(AEnemyShip* InHostShip, int32 InitialPointId, AActor* InitialTarget);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Target")
	void SetBossCombatTarget(AActor* NewTarget);

	UFUNCTION(BlueprintPure, Category = "Boss|Target")
	AActor* GetBossCombatTarget() const;

	UFUNCTION(BlueprintPure, Category = "Boss|Ship")
	AEnemyShip* GetHostShip() const { return HostShip; }
	UDeckWalkRouteComponent* GetDeckWalkRouteComponent() const { return DeckWalkRouteComponent; }

	UFUNCTION(BlueprintPure, Category = "Boss|Spawn")
	int32 GetInitialSpawnPointId() const { return InitialSpawnPointId; }
	UFUNCTION(BlueprintPure, Category = "Boss|Walk Area")
	FDeckWalkLocation GetDestinationLocation() const { return DestinationLocation; }
	bool HasDestination() const;
	bool TrySetDestinationLocation(const FDeckWalkLocation& Location, bool bWalking);
	void ClearDestination();
	void TrackWalkingTarget(AActor* Target, const FBossDestinationSelectionSettings& Settings);
	void ReplanWalkingTarget();
	const FDeckWalkLocation& GetPreviousLocation() const { return PreviousLocation; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Point")
	void MarkDestinationReached();

	virtual AEnemyShip* GetDeckHostShip() const override { return HostShip; }
	virtual void OnDeckMoveReached() override { MarkDestinationReached(); }
	virtual void OnDeckMoveFailed() override;
	virtual bool CanMoveOnDeck() const override;

	bool ResolveDestinationTransform(FTransform& OutTransform) const;

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
	bool TrySummonDeckEnemy(ADeckEnemy*& OutEnemy);

	/** Query-only sensor enabled by DashSlash; the character capsule remains the movement body. */
	UFUNCTION(BlueprintPure, Category = "Boss|Combat")
	USphereComponent* GetDashDamageVolume() const { return DashDamageVolume; }

	UFUNCTION(BlueprintPure, Category = "Boss|Combat")
	UBossBasicAttackSet* GetBasicAttackSet() const { return BasicAttackSet; }
	bool HasBossBasicAttackStartingAbility() const;

protected:
	friend class FEnemyBalanceSummonThresholdTest;
	UFUNCTION()
	void HandleBalanceHealthChanged(UBaseHealthComponent* Health, float OldHealth, float NewHealth, AActor* InstigatorActor);
	bool SummonOneDeckEnemy(ADeckEnemy*& OutEnemy);
	UPROPERTY(Transient) FEnemyEncounterBalanceRow EncounterBalance;
	bool bUseEncounterBalance = false;
	int32 PendingBalanceSummons = 0;
	TSet<int32> ConsumedSummonThresholds;
	friend class FBossStatusTriggersTest;
	void HandleConfirmedDamage(float Damage, const FGameplayEffectContextHandle& Context, bool bPeriodic);
	UFUNCTION()
	void HandleStunHealthChanged(UBaseHealthComponent* Health, float OldHealth, float NewHealth, AActor* InstigatorActor);
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Status")
	TSubclassOf<UGameplayEffect> HeadHitStunEffect;
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Status")
	TSubclassOf<UGameplayEffect> HealthThresholdStunEffect;
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Status", meta = (ClampMin = "0", ClampMax = "1"))
	float StunHealthThreshold = 0.5f;
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Status")
	TArray<FName> StunHeadBones = { TEXT("head") };
	bool bStunHealthThresholdConsumed = false;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void HandleDeath_Implementation() override;
	virtual void HandleDeathFinishedPresentation() override;
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
	void ApplyDeathMovementState();
	void AnchorDeathToDeck(const FTransform& DeathWorldTransform);

	UPROPERTY(ReplicatedUsing = OnRep_HostShip, VisibleInstanceOnly, BlueprintReadOnly, Category = "Boss|Ship")
	TObjectPtr<AEnemyShip> HostShip = nullptr;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Boss|Spawn")
	int32 InitialSpawnPointId = INDEX_NONE;
	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Boss|Walk Area")
	FDeckWalkLocation DestinationLocation;
	FDeckWalkLocation PreviousLocation;

	UPROPERTY(ReplicatedUsing = OnRep_BossHidden, VisibleInstanceOnly, BlueprintReadOnly, Category = "Boss|State")
	bool bBossHidden = false;

	UPROPERTY(Transient)
	TObjectPtr<AActor> BossCombatTarget = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boss|Combat", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> DashDamageVolume = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boss|Walk Area")
	TObjectPtr<UDeckWalkRouteComponent> DeckWalkRouteComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boss|Tracking")
	TObjectPtr<UDeckCombatTargetResolverComponent> DeckTargetResolver;

	/** Visual/cadence variations for the one currently equipped weapon. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Combat")
	TObjectPtr<UBossBasicAttackSet> BasicAttackSet = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Summon", meta = (ClampMin = "1", ClampMax = "8"))
	int32 MaxSummonedDeckEnemies = 2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Summon", meta = (ClampMin = "0.0", Units = "s"))
	float SummonCooldown = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Summon", meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumSummonDistanceFromTarget = 300.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Summon", meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumSummonDistanceFromBoss = 200.0f;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<ADeckEnemy>> SummonedDeckEnemies;

	double NextSummonAllowedTime = 0.0;

	ECollisionEnabled::Type InitialCapsuleCollision = ECollisionEnabled::QueryAndPhysics;
	bool bHiddenRelocationActive = false;
	FSWRoomShipBossState PendingRoomState;
	bool bHasPendingRoomState = false;
	TWeakObjectPtr<AActor> WalkingTarget;
	FBossDestinationSelectionSettings WalkingSettings;
	FVector PlannedWalkingCenter = FVector::ZeroVector;
	FName PlannedWalkingSurface;
	double NextWalkingReplanTime = 0.;
};
