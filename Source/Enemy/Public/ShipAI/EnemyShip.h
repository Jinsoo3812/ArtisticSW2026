// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "DeckAI/DeckPointReservation.h"
#include "Ship.h"
#include "ShipAI/EnemyShipNavigationTypes.h"
#include "WaveSystem/Data/WaveSpawnTypes.h"
#include "GameplayAbilitySpecHandle.h"
#include "IncomingDamageMultiplierInterface.h"
#include "ItemSpawn/LootSpawnPoint.h"
#include "DeckAI/DeckEnemySpawnerComponent.h"
#include "BossAI/BossEncounterComponent.h"
#include "ShipAI/EnemyShipRuntimeState.h"
#include "EnemyShip.generated.h"

USTRUCT()
struct FSWRoomEnemyShipState
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) bool bDeathHandled = false;
	UPROPERTY(SaveGame) bool bHasDropped = false;
	UPROPERTY(SaveGame) bool bCrewDefeated = false;
	UPROPERTY(SaveGame) bool bHasEverHadLivingCrew = false;
	UPROPERTY(SaveGame) bool bStoryGateOpen = false;
	UPROPERTY(SaveGame) TArray<FGuid> CrewIds;
	UPROPERTY(SaveGame) FGuid BossId;
	UPROPERTY(SaveGame) FSWRoomDeckSpawnerState DeckSpawner;
	UPROPERTY(SaveGame) FSWRoomBossEncounterState BossEncounter;
};

class ACannon;
class AStorageChest;
class UChestDefinition;
class UBaseHealthComponent;
class UEnemyHealthBarComponent;
class UEnemyShipArchetypeData;
class UEnemyShipNavigationComponent;
class UEnemyShipPatternRuntimeComponent;
class UEnemyShipSkillModuleData;
class UGameplayAbility;
class USWCabinWaterCullComponent;

UENUM(BlueprintType)
enum class EEnemyShipOrbitDirectionOverride : uint8
{
	UseArchetypeDefault,
	Clockwise,
	Counterclockwise
};
class UDeckWaypointComponent;
class UDeckEnemySpawnerComponent;
class UDeckWalkAreaComponent;
class UBossEncounterComponent;
class ABaseEnemy;
class ADeckEnemy;
class AShipBossEnemy;
class AEnemyShip;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnEnemyShipOwnedEnemiesDefeated,
	AEnemyShip*, EnemyShip);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEnemyShipCrewDefeated, AEnemyShip*, EnemyShip);

UCLASS(HideCategories = ("Ship|Stats"))
class ENEMY_API AEnemyShip : public AShip, public IIncomingDamageMultiplierInterface
{
	GENERATED_BODY()

#if WITH_DEV_AUTOMATION_TESTS
	friend class FDeckFixedAnchorLifecycleTest;
	friend class FDeckPointReservationLifecycleTest;
	friend class FDeckEnemySpawnerCompositionTest;
	friend class FBossEncounterSightSpawnTest;
	friend class FBossEncounterSightSpawnWithoutRidingPlayerTest;
#endif

public:
	AEnemyShip();
	FOnEnemyShipRuntimeStateChanged OnRuntimeStateChanged;
	/** Client-local presentation notification, never an authority spawn command. */
	FOnEnemyShipRuntimeStateChanged OnRuntimePresentationChanged;
	const FEnemyShipRuntimeState& GetRuntimeStateSnapshot() const { return RuntimeState; }
	bool CanDeployDeckEnemies() const;
	void NotifyPlayerShipSightLost(AShip* PlayerShip);
	virtual void CaptureRoomDomains(TArray<FSWRoomDomainPart>& OutParts, TArray<FSWRoomCaptureIssue>& OutIssues) const override;
	virtual bool RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& OutError) override;
	virtual bool CompareRoomDomain(const FSWRoomDomainPart& Expected, const FSWRoomDomainPart& Actual,
		float TimeToleranceSeconds, TArray<FString>& OutFields) const override;
	virtual bool FinalizeRoomRestore(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError) override;
	virtual bool IsEnemyShipForEffects() const override { return true; }
	virtual bool AllowsPlayerHelmControl() const override { return !IsStoryGateDormant() && !IsSinking() && !bDeathHandled && bCrewDefeated; }
	virtual bool IsStoryGateDormantForDeckContent() const override { return IsStoryGateDormant(); }
	virtual bool IsFinalBossSquadForDeckContent() const override { return IsFinalBossSquadShip(); }
	virtual void RefreshStoryGateOwnedActors() override;
	virtual bool AllowsPlayerCannonControl() const override { return false; }
	virtual bool AllowsPlayerBoarding() const override { return false; }
	virtual bool AllowsPlayerAnchorControl(AActor* Interactor = nullptr) const override;
	virtual float GetCannonCooldownMultiplier() const override;
	virtual float GetIncomingDamageMultiplier() const override;
	virtual bool IsProtectedFromOwnHullCannonSplash(const AActor* Candidate) const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Client-local, distance-selected cabin water culling shared with the player ship. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Water")
	TObjectPtr<USWCabinWaterCullComponent> CabinWaterCullComponent;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** Enemy ships receive their authored stats exclusively from EnemyShipArchetype.SpecRow. */
	virtual void InitializeDefaultAttributes() override;

public:
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintPure, Category = "Ship|AI")
	UEnemyShipNavigationComponent* GetNavigationComponent() const { return NavigationComponent; }

	UFUNCTION(BlueprintPure, Category = "Ship|AI")
	UEnemyShipPatternRuntimeComponent* GetPatternRuntimeComponent() const { return PatternRuntimeComponent; }

	UFUNCTION(BlueprintPure, Category = "Ship|Boss Encounter")
	UBossEncounterComponent* GetBossEncounterComponent() const { return BossEncounterComponent; }

	UFUNCTION(BlueprintPure, Category = "Ship|Deck AI")
	UDeckEnemySpawnerComponent* GetDeckEnemySpawnerComponent() const { return DeckEnemySpawnerComponent; }

	UDeckWalkAreaComponent* GetDeckWalkAreaComponent() const { return DeckWalkAreaComponent; }

	UFUNCTION(BlueprintPure, Category = "Ship|Death")
	bool IsDeathHandled() const { return bDeathHandled; }

	/** Called on the authority after NavalAIController receives a successful Sight stimulus for a Player ship. */
	void NotifyPlayerShipSighted(AShip* SensedPlayerShip);

	/** True after this ship's deck deployment completed and every deployed enemy died. */
	UFUNCTION(BlueprintPure, Category = "Ship|Deck AI")
	bool AreAllOwnedDeckEnemiesDefeated() const;

	UFUNCTION(BlueprintPure, Category = "Ship|Deck AI")
	int32 GetAliveOwnedDeckEnemyCount() const;

	/** Future ship-movement unlock logic can subscribe here instead of polling. Authority only. */
	UPROPERTY(BlueprintAssignable, Category = "Ship|Deck AI")
	FOnEnemyShipOwnedEnemiesDefeated OnOwnedDeckEnemiesDefeated;
	UPROPERTY(BlueprintAssignable, Category = "Ship|Crew")
	FOnEnemyShipCrewDefeated OnCrewDefeated;

	/** Internal owner notification from a deck enemy at authoritative death start. */
	void NotifyOwnedDeckEnemyDefeated(ADeckEnemy* Enemy);

	/** Internal completion callback from DeckEnemySpawnerComponent. */
	void NotifyAllOwnedDeckEnemiesDefeated();

	UFUNCTION(BlueprintPure, Category = "Ship|Deck AI")
	UDeckWaypointComponent* GetDeckWaypoint(int32 WaypointId) const;

	UFUNCTION(BlueprintPure, Category = "Ship|Deck AI")
	FVector GetDeckWaypointWorldLocation(int32 WaypointId) const;
	bool ResolveDeckCharacterTransform(int32 WaypointId, float CapsuleHalfHeight, FTransform& OutTransform) const;

	/** Validates manual spawn IDs, attachment, surface settings and authored spawn references. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Ship|Deck AI|Validation")
	void ValidateDeckWaypoints();



	/** Authority-only logical occupancy. Selection and reservation are atomic on the server. */
	bool IsDeckPointAvailable(int32 WaypointId, const AActor* Requester = nullptr) const;
	bool TryReserveDeckPoint(int32 WaypointId, AActor* Requester, FDeckPointReservation& OutReservation);
	bool TryReserveDeckEnemySpawnPoint(
		const FDeckEnemySpawnRequest& Request,
		FDeckPointReservation& OutReservation);
	bool CommitDeckPointReservation(const FDeckPointReservation& Reservation, AActor* Occupant);
	void ReleaseDeckPointReservation(FDeckPointReservation& Reservation);
	bool TryOccupyDeckPoint(int32 WaypointId, AActor* Occupant);
	void ReleaseDeckPointOccupancy(int32 WaypointId, AActor* Occupant);
	void ReleaseAllDeckPointsFor(AActor* Actor);

	/** Activates one inactive pooled enemy at a validated live deck point. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Ship|Deck AI")
	bool ActivateDeckEnemyAtPoint(
		int32 SpawnPointId,
		AActor* InitialTarget,
		ADeckEnemy*& OutEnemy);

	UFUNCTION(BlueprintPure, Category = "Ship|Crew")
	bool HasLivingCrew() const;

	UFUNCTION(BlueprintPure, Category = "Ship|Crew")
	int32 GetLivingCrewCount() const;

	UFUNCTION(BlueprintCallable, Category = "Ship|Crew")
	void RegisterCrewEnemy(ABaseEnemy* CrewEnemy);
	void NotifyCrewEnemyReactivated(ABaseEnemy* CrewEnemy);
	bool RegisterBossEnemy(AShipBossEnemy* BossEnemy);
	AShipBossEnemy* GetRegisteredBossEnemy() const { return RegisteredBoss; }
	bool IsOwnedCannonSplashProtectedActor(const AActor* Candidate) const;
	/** Pooled deck enemies guard the deck chest without being double-counted as manual crew. */
	void RegisterDeckEnemyChestGuard(ABaseEnemy* CrewEnemy);

	UFUNCTION(BlueprintCallable, Category = "Ship|Crew")
	void UnregisterCrewEnemy(ABaseEnemy* CrewEnemy);

	UFUNCTION(BlueprintPure, Category = "Ship|Crew")
	bool IsCrewDefeated() const { return bCrewDefeated; }
	/** Activates a pooled enemy only while the caller still owns this reservation. */
	bool ActivateDeckEnemyAtReservation(
		FDeckPointReservation& Reservation,
		AActor* InitialTarget,
		ADeckEnemy*& OutEnemy);

	UStaticMeshComponent* GetShipDeckMesh() const { return GetDeckMeshComplex(); }
	bool GrantEnemyShipAbilityClasses(const TArray<TSubclassOf<UGameplayAbility>>& AbilityClasses);
	bool ConfigureEnemyShipArchetype(UEnemyShipArchetypeData* Archetype);
	void SetSquadAssignedIdealDistance(float IdealDistance);
	void ResetAfterReturnToSpawn();

	/** Server-authored far-distance lifecycle. The visual hull remains visible while dormant. */
	void SetDistanceOptimizationDormant(bool bDormant);
	bool CanEnterDistanceOptimizationDormancy() const;
	bool IsDistanceOptimizationDormant() const { return bDistanceOptimizationDormant; }
	bool IsDistanceOptimizationEnabled() const { return bEnableDistanceOptimization; }
	bool IsFinalBossSquadShip() const;
	bool IsStoryGateDormant() const;
	float GetDistanceOptimizationRange() const { return DistanceOptimizationRange; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|AI")
	FName SquadID = TEXT("Squad_Alpha");

	/** Enables cheap at-home dormancy when every player ship is farther than DistanceOptimizationRange. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Optimization")
	bool bEnableDistanceOptimization = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Optimization", meta = (ClampMin = "0.0", Units = "cm"))
	float DistanceOptimizationRange = 100000.0f;

	/** May be overridden per placed instance so one BP_EnemyShip class can represent many archetypes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|AI|Data")
	TObjectPtr<UEnemyShipArchetypeData> EnemyShipArchetype;

	/** Used at encounter initialization when this ship's boss campaign gate is closed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|AI|Data")
	TObjectPtr<UEnemyShipArchetypeData> NormalFallbackArchetype;

	UPROPERTY(Transient)
	TObjectPtr<UEnemyShipArchetypeData> AuthoredEncounterArchetype;

	void NotifyCrewEnemyDeactivated(ABaseEnemy* CrewEnemy);

	/** Override the archetype's normal cannon lead for this ship, including during PIE on the server. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|AI|Cannon Lead")
	bool bOverrideCannonLeadSpeed = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|AI|Cannon Lead", meta = (EditCondition = "bOverrideCannonLeadSpeed", ClampMin = "0.0", Units = "cm/s"))
	float CannonLeadSpeedOverride = 1000.0f;

	/** Multiplies damage received after all registered ordinary crew are defeated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Crew", meta = (ClampMin = "1.0"))
	float CrewDefeatedDamageMultiplier = 3.0f;

	/** Per-instance Chest settings forwarded to every ChestSpawnPoint Child Actor owned by this ship. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest", meta = (ShowOnlyInnerProperties))
	FChestSpawnPointChestSettings ChestSpawnPointChestSettings;

	/** Per-instance Loot settings forwarded to every ChestSpawnPoint Child Actor owned by this ship. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot", meta = (ShowOnlyInnerProperties))
	FChestSpawnPointLootSettings ChestSpawnPointLootSettings;

	/** Per-level-instance override applied after the Archetype navigation profile is copied. */
	/** Legacy serialized field. Runtime navigation always uses counterclockwise orbiting. */
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Enemy ships always orbit counterclockwise."))
	EEnemyShipOrbitDirectionOverride OrbitDirectionOverride = EEnemyShipOrbitDirectionOverride::UseArchetypeDefault;


	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Ship|Deck AI|Validation")
	FString LastDeckWaypointValidationSummary;
	// ================= End legacy bridge =================
	/** LEGACY bootstrap only: delete after every Enemy Ship Archetype has an AbilitySet. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "LEGACY|Ship AI", meta = (
		DisplayName = "[LEGACY] Native Ability Bootstrap Without Archetype",
		DeprecatedProperty,
		DeprecationMessage = "Assign abilities through EnemyShip Pattern Skill Modules",
		AdvancedDisplay))
	TArray<TSubclassOf<UGameplayAbility>> LegacyAbilityBootstrapClasses;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "LEGACY|Ship AI", meta = (
		DisplayName = "[LEGACY] Automatic Cannon Fire Without Archetype",
		DeprecatedProperty,
		DeprecationMessage = "Assign an Archetype and use the CannonVolley Core Skill Module",
		AdvancedDisplay))
	bool bLegacyAutomaticCannonFireWithoutArchetype = true;

protected:
	void ApplyChestSpawnPointSettings();
	void SetStoryGateOpen(bool bOpen);
	void ApplyEffectiveDormancyState();
	void PublishRuntimeState();
	void HandleRoomRestoreCompleted();
	UFUNCTION() void OnRep_RuntimeState();
	void ApplyStoryGatePresentation();
	void ApplyStoryGateToSpawnedChests();
	UFUNCTION() void OnRep_StoryGateOpen();
	UFUNCTION() void HandleStoryGateChanged();
	UFUNCTION() void HandleStoryGatedChestSpawned(AStorageChest* Chest);
	void EvaluateCrewControlState();
	void DisableEnemyShipAIForCapture();
	void ApplyNavigationCollisionPolicy(ENavalCombatState State);

	UFUNCTION()
	void OnRep_DistanceOptimizationDormant();

	UFUNCTION()
	void OnRep_CrewDefeated();

	UFUNCTION()
	void HandleCrewEnemyRemoved(ABaseEnemy* Enemy, EWaveEnemyRemoveReason Reason);

	UFUNCTION()
	void HandleNavigationStateChanged(ENavalCombatState PreviousState, ENavalCombatState NewState);
	void DrawEnemyShipAIDebug() const;
	void InitializeDeckWaypoints();
	void InitializeDeckEnemyPool();
	void DestroyDeckEnemyPool();

	// Aiming and firing logic

	// ---- Death Handling ----
	UFUNCTION()
	void OnDeathStarted(UBaseHealthComponent* InHealthComponent);

	void HandleShipDeath();
	void DropAtDeathLocation(const FVector& DeathLocation, const FRotator& DeathRotation);

	// ---- Death Properties ----
	/** 사망 후 Destroy까지의 대기 시간 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Death")
	float DestroyAfterDeathDelay = 5.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Ship|Death")
	bool bDeathHandled = false;
	
	/** Retired serialized reference. Sunk loot now uses the deck zone's cached progression rolls. */
	UPROPERTY()
	TObjectPtr<UChestDefinition> SunkChestDefinition;

	UPROPERTY(EditDefaultsOnly, Category = "Ship|Chest Reward")
	FVector EnemyCorpseStorageSpawnOffset = FVector(0.0f, 0.0f, 250.0f);

	UPROPERTY()
	bool bHasDropped = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBaseHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UEnemyShipNavigationComponent> NavigationComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UEnemyShipPatternRuntimeComponent> PatternRuntimeComponent;

	// ================= Health Bar =================
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UEnemyHealthBarComponent> EnemyHealthBarComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|HealthBar")
	FVector HealthBarOffset = FVector(0.0f, 0.0f, 300.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|HealthBar")
	FVector2D HealthBarDrawSize = FVector2D(220.0f, 28.0f);

	FTimerHandle DeathDestroyTimerHandle;
	// ================= End of Health Bar =================
	
	// ---- Cannon & AI State ----
	/** Owns the server-only pool, deployment queue, waypoint registry, and all point claims. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDeckEnemySpawnerComponent> DeckEnemySpawnerComponent;


	/** Ship-local walkable area sampled from the authored deck and fence collision. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDeckWalkAreaComponent> DeckWalkAreaComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Boss Encounter")
	TObjectPtr<UBossEncounterComponent> BossEncounterComponent;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Ship|Crew")
	TArray<TObjectPtr<ABaseEnemy>> RegisteredCrewEnemies;
	UPROPERTY(Transient)
	TObjectPtr<AShipBossEnemy> RegisteredBoss;

	UPROPERTY(ReplicatedUsing = OnRep_CrewDefeated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Ship|Crew")
	bool bCrewDefeated = false;

	UPROPERTY(SaveGame, ReplicatedUsing = OnRep_DistanceOptimizationDormant, VisibleInstanceOnly, BlueprintReadOnly, Category = "Ship|Optimization")
	bool bDistanceOptimizationDormant = false;
	UPROPERTY(ReplicatedUsing = OnRep_StoryGateOpen, VisibleInstanceOnly, BlueprintReadOnly, Category = "Ship|Story")
	bool bStoryGateOpen = false;
 bool bDevelopmentStoryGateOpened = false;

	TArray<TWeakObjectPtr<UActorComponent>> DistanceDormancySuspendedTickComponents;
	TArray<TWeakObjectPtr<ACannon>> DistanceDormancySuspendedCannons;
	struct FCannonDormancyState
	{
		TWeakObjectPtr<ACannon> Cannon;
		bool bCollisionEnabled = false;
		bool bTickEnabled = false;
	};
	TArray<FCannonDormancyState> DormancyCannonStates;
	bool bEffectiveDormancyApplied = false;
	bool bApplyingRuntimeState = false;
	FDelegateHandle RoomRestoreCompletedHandle;
	UPROPERTY(ReplicatedUsing = OnRep_RuntimeState, VisibleInstanceOnly, BlueprintReadOnly, Category = "Ship|Runtime")
	FEnemyShipRuntimeState RuntimeState;
	FEnemyShipRuntimeState LastClientRuntimeState;
	bool bDormancyShipCollisionEnabled = false;
	bool bDormancyShipTickEnabled = false;
	bool bDormancyShipPhysicsEnabled = false;

	/** Prevents an unconfigured or not-yet-deployed empty crew roster from being treated as defeated. */
	bool bHasEverHadLivingCrew = false;
	bool bEndingPlay = false;
	bool bCaptureCannonTagAdded = false;
	bool bStoryGateCannonTagAdded = false;
	bool bStorySubsystemMissingLogged = false;
	TArray<FGameplayAbilitySpecHandle> GrantedEnemyShipAbilityHandles;
	FSWRoomEnemyShipState PendingRoomState;
	bool bHasPendingRoomState = false;
};
