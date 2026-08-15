#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BossEncounterComponent.generated.h"

class AEnemyShip;
class AShipBossEnemy;
class AStorageChest;
class UBaseHealthComponent;

UENUM(BlueprintType)
enum class EBossEncounterState : uint8
{
	Waiting,
	Spawning,
	Active,
	Defeated,
	Failed
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnBossEncounterStateChangedSignature,
	EBossEncounterState, OldState,
	EBossEncounterState, NewState);

/** Server-authoritative bridge from a locked EnemyItemBox interaction to one boss spawn. */
UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
class ENEMY_API UBossEncounterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBossEncounterComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Boss|Encounter")
	EBossEncounterState GetEncounterState() const { return EncounterState; }

	UFUNCTION(BlueprintPure, Category = "Boss|Encounter")
	AShipBossEnemy* GetSpawnedBoss() const { return SpawnedBoss; }

	UFUNCTION(BlueprintPure, Category = "Boss|Encounter")
	bool IsEncounterEnabled() const { return bEncounterEnabled; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Encounter")
	void ConfigureEncounter(
		AStorageChest* InEnemyItemBox,
		TSubclassOf<AShipBossEnemy> InBossClass,
		int32 InBossSpawnPointId = -1);

	UPROPERTY(BlueprintAssignable, Category = "Boss|Encounter")
	FOnBossEncounterStateChangedSignature OnEncounterStateChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void HandleItemBoxInteracted(AActor* Interactor);

	UFUNCTION()
	void HandleBossDeathStarted(UBaseHealthComponent* HealthComponent);

	UFUNCTION()
	void HandleHostShipDestroyed(AActor* DestroyedActor);

	UFUNCTION()
	void OnRep_EncounterState(EBossEncounterState OldState);

	bool SpawnBossFor(AActor* Interactor);
	bool ResolveSpawnPoint(AEnemyShip& HostShip, int32& OutPointId, FTransform& OutTransform) const;
	void BindItemBox();
	void UnbindItemBox();
	void SetEncounterState(EBossEncounterState NewState);

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Boss|Encounter")
	TObjectPtr<AStorageChest> EnemyItemBox = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Encounter")
	bool bEncounterEnabled = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Encounter")
	TSubclassOf<AShipBossEnemy> BossClass;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Boss|Encounter")
	int32 BossSpawnPointId = INDEX_NONE;

	UPROPERTY(ReplicatedUsing = OnRep_EncounterState, VisibleInstanceOnly, BlueprintReadOnly, Category = "Boss|Encounter")
	EBossEncounterState EncounterState = EBossEncounterState::Waiting;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Boss|Encounter")
	TObjectPtr<AShipBossEnemy> SpawnedBoss = nullptr;
};
