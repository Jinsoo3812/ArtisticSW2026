#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/DataTable.h"
#include "StoryFacadeSubsystem.h"
#include "Room/SWRoomSnapshotTypes.h"
#include "BossEncounterComponent.generated.h"

USTRUCT()
struct FSWRoomBossEncounterState
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) bool bEncounterEnabled = false;
	UPROPERTY(SaveGame) uint8 EncounterState = 0;
	UPROPERTY(SaveGame) int32 BossSpawnPointId = INDEX_NONE;
	UPROPERTY(SaveGame) FSoftClassPath BossClass;
	UPROPERTY(SaveGame) FGuid BossId;
	UPROPERTY(SaveGame) FGuid ItemBoxId;
};

class AEnemyShip;
class AShip;
class AShipBossEnemy;
class AStorageChest;
class UBaseHealthComponent;
class AChestSpawnPoint;

UENUM(BlueprintType)
enum class EBossEncounterState : uint8
{
	Waiting,
	Spawning,
	Active,
	Defeated,
	Failed
};

UENUM(BlueprintType)
enum class EBossEncounterTrigger : uint8
{
	ItemBoxInteraction,
	PlayerShipSight
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnBossEncounterStateChangedSignature,
	EBossEncounterState, OldState,
	EBossEncounterState, NewState);

/** Server-authoritative, idempotent bridge from an authored trigger to one boss spawn. */
UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
class ENEMY_API UBossEncounterComponent : public UActorComponent
{
	GENERATED_BODY()

#if WITH_EDITOR
	friend class FDeckSpawnAnchorValidator;
#endif

public:
	UBossEncounterComponent();
	void CaptureRoomState(FSWRoomBossEncounterState& OutState, TArray<FSWRoomCaptureIssue>& OutIssues) const;
	bool RestoreRoomState(const FSWRoomBossEncounterState& State, FString& OutError);
	bool FinalizeRoomState(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Boss|Encounter")
	EBossEncounterState GetEncounterState() const { return EncounterState; }

	UFUNCTION(BlueprintPure, Category = "Boss|Encounter")
	AShipBossEnemy* GetSpawnedBoss() const { return SpawnedBoss; }

	UFUNCTION(BlueprintPure, Category = "Boss|Encounter")
	bool IsEncounterEnabled() const { return bEncounterEnabled; }

	bool IsCampaignGateOpen() const;

	UFUNCTION(BlueprintPure, Category = "Boss|Encounter")
	EBossEncounterTrigger GetEncounterTrigger() const { return EncounterTrigger; }

	UFUNCTION(BlueprintPure, Category = "Boss|Encounter")
	AStorageChest* GetEnemyItemBox() const { return EnemyItemBox; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Encounter")
	void ConfigureEncounter(
		AStorageChest* InEnemyItemBox,
		TSubclassOf<AShipBossEnemy> InBossClass,
		int32 InBossSpawnPointId = -1);

	/** Idempotent authority-only entry point used by ship perception. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Encounter")
	bool NotifyPlayerShipSighted(AShip* SensedPlayerShip);

	/** Shared entry point for interaction, perception and future scripted triggers. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Encounter")
	bool TryStartEncounter(AActor* TriggerActor);
	void RefreshChestReservations() { UpdateBossReservation(); }

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
	void HandleStoryChanged();
	UFUNCTION()
	void HandleChestSpawned(AStorageChest* Chest);

	UFUNCTION()
	void OnRep_EncounterState(EBossEncounterState OldState);

	bool SpawnBossFor(AActor* Interactor);
	AActor* ResolveEncounterTarget(AActor* TriggerActor) const;
	bool ResolveSpawnPoint(AEnemyShip& HostShip, int32& OutPointId, FTransform& OutTransform) const;
	AStorageChest* ResolveConfiguredEnemyItemBox() const;
	void BindItemBox();
	void UnbindItemBox();
	void SetEncounterState(EBossEncounterState NewState);
	void UpdateBossReservation();
	AChestSpawnPoint* ResolveTriggerChestPoint() const;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Boss|Encounter")
	TObjectPtr<AStorageChest> EnemyItemBox = nullptr;

	/** Child Actor Component selected in BP_EnemyShip that owns the encounter item box. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Encounter",
		meta = (UseComponentPicker, AllowedClasses = "/Script/Engine.ChildActorComponent"))
	FComponentReference EnemyItemBoxComponent;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Encounter")
	FComponentReference TriggerChestSpawnPointComponent;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Encounter")
	EStoryNode RequiredStoryNode = EStoryNode::ReconQuestAccepted;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Encounter")
	EStoryNode StopAfterStoryNode = EStoryNode::MiddleBoss1Defeated;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Encounter")
	bool bEncounterEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Encounter")
	EBossEncounterTrigger EncounterTrigger = EBossEncounterTrigger::ItemBoxInteraction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Encounter")
	TSubclassOf<AShipBossEnemy> BossClass;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Encounter", meta = (RowType = "/Script/Enemy.EnemyBaseStatsRow"))
	FDataTableRowHandle BossStatsRow;

	/** Exact WaypointId registered on the owning EnemyShip. No alternate point is selected on failure. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Encounter", meta = (ClampMin = "0"))
	int32 BossSpawnPointId = INDEX_NONE;

	UPROPERTY(ReplicatedUsing = OnRep_EncounterState, VisibleInstanceOnly, BlueprintReadOnly, Category = "Boss|Encounter")
	EBossEncounterState EncounterState = EBossEncounterState::Waiting;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Boss|Encounter")
	TObjectPtr<AShipBossEnemy> SpawnedBoss = nullptr;
	FSWRoomBossEncounterState PendingRoomState;
	bool bHasPendingRoomState = false;
	void HandleHostRuntimeStateChanged(const struct FEnemyShipRuntimeState& Previous, const struct FEnemyShipRuntimeState& Current);
	void EvaluateCurrentSight();
	FDelegateHandle HostRuntimeStateHandle;
	FTimerHandle SightEvaluationTimerHandle;
};
