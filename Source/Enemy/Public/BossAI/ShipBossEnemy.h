#pragma once

#include "CoreMinimal.h"
#include "BaseEnemy.h"
#include "ShipBossEnemy.generated.h"

class AEnemyShip;
class USphereComponent;

/** Server-authored boss pawn whose tactical positions live on a moving enemy ship. */
UCLASS(Blueprintable)
class ENEMY_API AShipBossEnemy : public ABaseEnemy
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
	int32 GetDestinationPointId() const { return DestinationPointId; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Point")
	void SetDestinationPointId(int32 NewPointId) { DestinationPointId = NewPointId; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Point")
	void MarkDestinationReached();

	bool ResolvePointTransform(int32 PointId, FTransform& OutTransform) const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|State")
	bool TransitionBossAIState(FGameplayTag ExpectedState, FGameplayTag NewState);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|State")
	void SetBossHidden(bool bInHidden);

	UFUNCTION(BlueprintPure, Category = "Boss|State")
	bool IsBossHidden() const { return bBossHidden; }

	/** Query-only sensor enabled by DashSlash; the character capsule remains the movement body. */
	UFUNCTION(BlueprintPure, Category = "Boss|Combat")
	USphereComponent* GetDashDamageVolume() const { return DashDamageVolume; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void HandleDeath_Implementation() override;

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

	UPROPERTY(ReplicatedUsing = OnRep_HostShip, VisibleInstanceOnly, BlueprintReadOnly, Category = "Boss|Ship")
	TObjectPtr<AEnemyShip> HostShip = nullptr;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Boss|Point")
	int32 CurrentPointId = INDEX_NONE;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Boss|Point")
	int32 DestinationPointId = INDEX_NONE;

	UPROPERTY(ReplicatedUsing = OnRep_BossHidden, VisibleInstanceOnly, BlueprintReadOnly, Category = "Boss|State")
	bool bBossHidden = false;

	UPROPERTY(Transient)
	TObjectPtr<AActor> BossCombatTarget = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boss|Combat", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> DashDamageVolume = nullptr;

	ECollisionEnabled::Type InitialCapsuleCollision = ECollisionEnabled::QueryAndPhysics;
};
