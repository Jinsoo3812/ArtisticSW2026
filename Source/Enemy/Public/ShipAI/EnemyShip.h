// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "DeckAI/DeckPointReservation.h"
#include "Ship.h"
#include "ShipAI/EnemyShipNavigationTypes.h"
#include "EnemyDropData.h"
#include "WaveSystem/Data/WaveSpawnTypes.h"
#include "GameplayAbilitySpecHandle.h"
#include "EnemyShip.generated.h"

class ACannon;
class AStorageChest;
class UChestDefinition;
class UBaseHealthComponent;
class UEnemyHealthBarComponent;
class UEnemyShipArchetypeData;
class UEnemyShipAbilitySet;
class UEnemyShipNavigationComponent;
class UEnemyShipPatternRuntimeComponent;
class UEnemyShipPatternData;
class UEnemyShipSkillModuleData;
class UGameplayAbility;
class UDeckWaypointComponent;
class UBossEncounterComponent;
class ADeckRangedEnemy;
class ABaseEnemy;

/** Editor-time sampling controls for creating editable deck waypoint components from ShipDeckMesh. */
USTRUCT(BlueprintType)
struct ENEMY_API FDeckWaypointGenerationSettings
{
	GENERATED_BODY()

	/** Approximate world-space distance between generated samples. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck AI|Generation", meta = (ClampMin = "25.0", Units = "cm"))
	float GridSpacing = 200.0f;

	/** Requires deck support around each point so a boss capsule is not placed on an edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck AI|Generation", meta = (ClampMin = "0.0", Units = "cm"))
	float EdgeClearance = 65.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck AI|Generation", meta = (ClampMin = "0.0", ClampMax = "89.0", Units = "deg"))
	float MaximumWalkableSlope = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck AI|Generation", meta = (ClampMin = "0.0", Units = "cm"))
	float MaximumStepHeight = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck AI|Generation", meta = (ClampMin = "10.0", Units = "cm"))
	float TraceMargin = 150.0f;

	/** Generated IDs start here, leaving low IDs available for manually authored points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck AI|Generation", meta = (ClampMin = "0"))
	int32 GeneratedWaypointIdBase = 10000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck AI|Generation")
	bool bLinkDiagonalNeighbors = true;

	/** Defaults for newly created points. Regeneration preserves edits made to existing points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck AI|Generation|New Point Defaults")
	bool bNewPointsCanSpawn = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck AI|Generation|New Point Defaults")
	bool bNewPointsCanPatrol = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck AI|Generation|New Point Defaults")
	bool bNewPointsCanUseInCombat = true;
};

UCLASS()
class ENEMY_API AEnemyShip : public AShip
{
	GENERATED_BODY()

#if WITH_DEV_AUTOMATION_TESTS
	friend class FDeckFixedAnchorLifecycleTest;
	friend class FDeckPointReservationLifecycleTest;
#endif

public:
	AEnemyShip();
	virtual bool IsEnemyShipForEffects() const override { return true; }
	virtual bool AllowsPlayerHelmControl() const override { return !bDeathHandled && (bCrewDefeated || !HasLivingCrew()); }
	virtual bool AllowsPlayerCannonControl() const override { return false; }
	virtual bool AllowsPlayerBoarding() const override { return false; }
	virtual bool AllowsPlayerAnchorControl(AActor* Interactor = nullptr) const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintPure, Category = "Ship|AI")
	UEnemyShipNavigationComponent* GetNavigationComponent() const { return NavigationComponent; }

	UFUNCTION(BlueprintPure, Category = "Ship|AI")
	UEnemyShipPatternRuntimeComponent* GetPatternRuntimeComponent() const { return PatternRuntimeComponent; }

	UFUNCTION(BlueprintPure, Category = "Ship|Boss Encounter")
	UBossEncounterComponent* GetBossEncounterComponent() const { return BossEncounterComponent; }

	UFUNCTION(BlueprintPure, Category = "Ship|Death")
	bool IsDeathHandled() const { return bDeathHandled; }

	/** Called on the authority after NavalAIController receives a successful Sight stimulus for a Player ship. */
	void NotifyPlayerShipSighted(AShip* SensedPlayerShip);

	UFUNCTION(BlueprintPure, Category = "Ship|Deck AI")
	UDeckWaypointComponent* GetDeckWaypoint(int32 WaypointId) const;

	UFUNCTION(BlueprintPure, Category = "Ship|Deck AI")
	FVector GetDeckWaypointWorldLocation(int32 WaypointId) const;
	/** Deterministic fixed-emplacement transform; does not run a floor query. */
	bool ResolveFixedDeckAnchorTransform(int32 WaypointId, float CapsuleHalfHeight, FTransform& OutTransform) const;
	bool ResolveDeckCharacterTransform(int32 WaypointId, float CapsuleHalfHeight, FTransform& OutTransform) const;

	/** Creates persistent, individually editable waypoint components in this Blueprint asset or placed actor. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Ship|Deck AI|Generation")
	void GenerateDeckWaypointsFromDeckMesh();

	/** Removes only mesh-generated points. Hand-authored waypoint components are left untouched. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Ship|Deck AI|Generation")
	void ClearGeneratedDeckWaypoints();

	/** Checks IDs, links, deck attachment and whether generated points still resolve to the deck. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Ship|Deck AI|Generation")
	void ValidateDeckWaypoints();

	/** Returns deterministic, ID-sorted deck points usable by ability and movement selectors. */
	void GetDeckWaypointIds(TArray<int32>& OutWaypointIds, bool bRequireCombatPoint = false) const;

	void GetConnectedDeckWaypointIds(int32 WaypointId, TArray<int32>& OutWaypointIds) const;
	int32 FindNearestDeckWaypoint(const FVector& WorldLocation, bool bRequirePatrolPoint = true) const;

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
		ADeckRangedEnemy*& OutEnemy);

	UFUNCTION(BlueprintPure, Category = "Ship|Crew")
	bool HasLivingCrew() const;

	UFUNCTION(BlueprintPure, Category = "Ship|Crew")
	int32 GetLivingCrewCount() const;

	UFUNCTION(BlueprintCallable, Category = "Ship|Crew")
	void RegisterCrewEnemy(ABaseEnemy* CrewEnemy);

	UFUNCTION(BlueprintCallable, Category = "Ship|Crew")
	void UnregisterCrewEnemy(ABaseEnemy* CrewEnemy);

	UFUNCTION(BlueprintPure, Category = "Ship|Crew")
	bool IsCrewDefeated() const { return bCrewDefeated; }
	/** Activates a pooled enemy only while the caller still owns this reservation. */
	bool ActivateDeckEnemyAtReservation(
		FDeckPointReservation& Reservation,
		AActor* InitialTarget,
		ADeckRangedEnemy*& OutEnemy);

	UStaticMeshComponent* GetShipDeckMesh() const { return ShipDeckMesh; }
	bool IsUsingLegacyAICompatibility() const
	{
		return !EnemyShipArchetype && bLegacyAutomaticCannonFireWithoutArchetype;
	}

	bool GrantEnemyShipAbilities(const UEnemyShipAbilitySet* AbilitySet);
	bool GrantEnemyShipAbilityClasses(const TArray<TSubclassOf<UGameplayAbility>>& AbilityClasses);
	bool ConfigureEnemyShipPattern(UEnemyShipPatternData* Pattern);
	void SetCoreSkillModules(const TArray<UEnemyShipSkillModuleData*>& InCoreModules);

	// AI Control APIs
	UFUNCTION(BlueprintCallable, Category = "Ship|AI")
	void SetAITarget(AActor* Target) { AITargetShip = Target; }

	UFUNCTION(BlueprintCallable, Category = "Ship|AI")
	void SetNavalCombatState(ENavalCombatState State) { CurrentCombatState = State; }

	UFUNCTION(BlueprintCallable, Category = "Ship|AI")
	void SetMaxActiveCannons(int32 Count) { MaxActiveCannons = Count; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|AI")
	FName SquadID = TEXT("Squad_Alpha");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LEGACY|Ship AI", meta = (
		DisplayName = "[LEGACY] Ideal Distance",
		DeprecatedProperty,
		DeprecationMessage = "Use EnemyShipArchetype.Pattern.NavigationProfile.IdealDistance",
		AdvancedDisplay))
	float IdealDistance = 2000.f;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Ship|AI|Navigation")
	TObjectPtr<AActor> NavigationHomeActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|AI|Navigation", meta = (ClampMin = "0.0", Units = "cm"))
	float NavigationHomeArrivalDistance = 800.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|AI|Data")
	TObjectPtr<UEnemyShipArchetypeData> EnemyShipArchetype;

	/** Always-on modules, normally just CannonVolley. Pattern modules are composed on top. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|AI|Data", meta = (TitleProperty = "ModuleId"))
	TArray<TObjectPtr<UEnemyShipSkillModuleData>> CoreSkillModules;

	// ================= Deck Enemy MVP =================
	/** Explicit opt-in so existing EnemyShip Blueprints keep their previous behavior. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Deck AI")
	bool bEnableDeckEnemyMVP = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Deck AI", meta = (EditCondition = "bEnableDeckEnemyMVP"))
	TSubclassOf<ADeckRangedEnemy> DeckEnemyClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Deck AI", meta = (EditCondition = "bEnableDeckEnemyMVP", ClampMin = "1", ClampMax = "8"))
	int32 DeckEnemyPoolSize = 2;

	/** Small settle delay after the first successful Sight stimulus. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Deck AI", meta = (EditCondition = "bEnableDeckEnemyMVP", ClampMin = "0.0", Units = "s"))
	float DeckEnemySightActivationDelay = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Deck AI", meta = (EditCondition = "bEnableDeckEnemyMVP", ClampMin = "0.05", Units = "s"))
	float DeckEnemyActivationInterval = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Deck AI", meta = (EditCondition = "bEnableDeckEnemyMVP", ClampMin = "0", ClampMax = "5"))
	int32 MaxDeckSpawnRetries = 3;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Deck AI", meta = (EditCondition = "bEnableDeckEnemyMVP", ClampMin = "0.05", Units = "s"))
	float DeckSpawnRetryInterval = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Deck AI", meta = (EditCondition = "bEnableDeckEnemyMVP"))
	int32 DeckEnemyRandomSeed = 1337;

	/** Settings used by the editor buttons above. Generated components can be edited after generation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ship|Deck AI|Generation")
	FDeckWaypointGenerationSettings DeckWaypointGenerationSettings;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Ship|Deck AI|Generation")
	FString LastDeckWaypointValidationSummary;
	// ================= End Deck Enemy MVP =================

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
	void UpdateActiveCannons();
	void EvaluateCrewControlState();
	void DisableEnemyShipAIForCapture();

	UFUNCTION()
	void OnRep_CrewDefeated();

	UFUNCTION()
	void HandleCrewEnemyRemoved(ABaseEnemy* Enemy, EWaveEnemyRemoveReason Reason);
	void MigrateLegacyNavigationAuthoring();
	void DrawEnemyShipAIDebug() const;
	void InitializeDeckWaypoints();
	void InitializeDeckEnemyPool();
	void DestroyDeckEnemyPool();
	void PruneDeckPointRuntimeState();
	bool ActivateReservedDeckEnemy(
		ADeckRangedEnemy& Enemy,
		FDeckPointReservation& Reservation,
		AActor* InitialTarget,
		int32 RandomSeed,
		ADeckRangedEnemy*& OutEnemy);
	bool ResolveDeckEnemySpawnTransform(const UDeckWaypointComponent* SpawnWaypoint, FTransform& OutTransform) const;
	UDeckWaypointComponent* SelectDeckSpawnWaypoint(int32 DeploymentIndex) const;

	UFUNCTION()
	void BeginDeckEnemyDeployment();

	UFUNCTION()
	void DeployNextDeckEnemy();

	// Aiming and firing logic
	void TickAIAimingAndFiring(float DeltaTime);

	// ---- Death Handling ----
	UFUNCTION()
	void OnDeathStarted(UBaseHealthComponent* InHealthComponent);

	void HandleShipDeath();
	void InitializeEnemyDropData();
	void DropAtDeathLocation(const FVector& DeathLocation, const FRotator& DeathRotation);

	// ---- Death Properties ----
	/** 사망 후 Destroy까지의 대기 시간 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Death")
	float DestroyAfterDeathDelay = 5.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Ship|Death")
	bool bDeathHandled = false;
	
	// 사망 시 Drop 아이템 정보 담은 Data Table
	UPROPERTY(EditDefaultsOnly, Category = "Ship|Drop")
	TObjectPtr<UDataTable> EnemyDropDataTable;

	// 적이 가지는 고유 식별 Tag (For Drop)
	UPROPERTY(EditDefaultsOnly, Category = "Ship|Drop")
	FGameplayTag EnemyTypeTag;

	/** 침몰 시 스폰할 상자 정의 DataAsset (설정 시 데이터 기반 드랍 테이블/퀘스트 아이템 사용) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Drop|Storage")
	TObjectPtr<UChestDefinition> SunkChestDefinition;

	// 죽었을 때, 드랍할 Storage 클래스 (SunkChestDefinition 미설정 시 Fallback)
	UPROPERTY(EditDefaultsOnly, Category = "Ship|Drop|Storage")
	TSubclassOf<AStorageChest> EnemyCorpseStorageClass;

	UPROPERTY(EditDefaultsOnly, Category = "Ship|Drop|Storage", meta = (ClampMin = "1", UIMin = "1"))
	int32 EnemyCorpseStorageSlotCount = 5;

	UPROPERTY(EditDefaultsOnly, Category = "Ship|Drop|Storage", meta = (ClampMin = "1", UIMin = "1"))
	int32 EnemyCorpseStorageColumnCount = 4;

	UPROPERTY(EditDefaultsOnly, Category = "Ship|Drop|Storage")
	FVector EnemyCorpseStorageSpawnOffset = FVector(0.0f, 0.0f, 250.0f);

	// 한 Ship이 드랍할 정보를 저장하는 구조체
	UPROPERTY()
	FEnemyDropData EnemyDropData;

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
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Ship|AI Cannon")
	TArray<TObjectPtr<ACannon>> ActiveAICannons;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LEGACY|Ship AI", meta = (
		DisplayName = "[LEGACY] Max Active Cannons",
		DeprecatedProperty,
		DeprecationMessage = "Use EnemyShipArchetype.Pattern.NavigationProfile.MaxActiveCannons",
		AdvancedDisplay))
	int32 MaxActiveCannons = 2;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Ship|AI Cannon")
	TObjectPtr<AActor> AITargetShip = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Ship|AI Combat")
	ENavalCombatState CurrentCombatState = ENavalCombatState::Idle;

	FTimerHandle ActiveCannonsTimerHandle;
	FTimerHandle DeckEnemySightDelayTimerHandle;
	FTimerHandle DeckEnemyDeploymentTimerHandle;

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UDeckWaypointComponent>> DeckWaypointsById;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDeckWaypointComponent>> DeckSpawnWaypoints;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ADeckRangedEnemy>> DeckEnemyPool;

	struct FDeckPointRuntimeState
	{
		TWeakObjectPtr<AActor> Occupant;
		TWeakObjectPtr<AActor> ReservedBy;
		uint32 ReservationSerial = 0;
	};

	/** Server-only transactional state; replicated actors carry the committed point IDs. */
	TMap<int32, FDeckPointRuntimeState> DeckPointRuntimeStates;
	uint32 NextDeckPointReservationSerial = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Boss Encounter")
	TObjectPtr<UBossEncounterComponent> BossEncounterComponent;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Ship|Crew")
	TArray<TObjectPtr<ABaseEnemy>> RegisteredCrewEnemies;

	UPROPERTY(ReplicatedUsing = OnRep_CrewDefeated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Ship|Crew")
	bool bCrewDefeated = false;

	bool bDeckDeploymentTriggered = false;
	int32 NextDeckEnemyPoolIndex = 0;
	int32 CurrentDeckSpawnRetryCount = 0;
	int32 DeckEnemyActivationSerial = 0;
	TArray<FGameplayAbilitySpecHandle> GrantedEnemyShipAbilityHandles;
};
