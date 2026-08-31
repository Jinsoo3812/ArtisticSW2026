// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "DeckAI/DeckPointReservation.h"
#include "Ship.h"
#include "ShipAI/EnemyShipNavigationTypes.h"
#include "EnemyDropData.h"
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
class UDeckEnemySpawnerComponent;
class UDeckNavigationComponent;
class UBossEncounterComponent;
class ADeckEnemy;
class AEnemyShip;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnEnemyShipOwnedEnemiesDefeated,
	AEnemyShip*, EnemyShip);

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

	/** IDs below GeneratedWaypointIdBase are reserved for designer-placed points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck AI|Generation", meta = (ClampMin = "1"))
	int32 ManualWaypointIdBase = 1;

	/** Maximum local deck distance used when rebuilding links from actual point positions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck AI|Generation", meta = (ClampMin = "1.0", Units = "cm"))
	float AutomaticLinkDistance = 300.0f;

	/** Smaller samples catch gaps between points while still allowing deliberately narrow deck routes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deck AI|Generation", meta = (ClampMin = "10.0", Units = "cm"))
	float AutomaticLinkSampleSpacing = 75.0f;

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
	friend class FDeckEnemySpawnerCompositionTest;
	friend class FBossEncounterSightSpawnTest;
#endif

public:
	AEnemyShip();
	virtual bool IsEnemyShipForEffects() const override { return true; }
	virtual bool AllowsPlayerHelmControl() const override { return false; }
	virtual bool AllowsPlayerCannonControl() const override { return false; }
	virtual bool AllowsPlayerBoarding() const override { return false; }

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

	UFUNCTION(BlueprintPure, Category = "Ship|Deck AI")
	UDeckEnemySpawnerComponent* GetDeckEnemySpawnerComponent() const { return DeckEnemySpawnerComponent; }

	UFUNCTION(BlueprintPure, Category = "Ship|Deck AI")
	UDeckNavigationComponent* GetDeckNavigationComponent() const { return DeckNavigationComponent; }

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

	/** Internal owner notification from a deck enemy at authoritative death start. */
	void NotifyOwnedDeckEnemyDefeated(ADeckEnemy* Enemy);

	/** Internal completion callback from DeckEnemySpawnerComponent. */
	void NotifyAllOwnedDeckEnemiesDefeated();

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
	bool IsDeckCombatPointClaimAvailable(int32 WaypointId, const AActor* Requester = nullptr) const;
	bool TryClaimDeckCombatPoint(int32 WaypointId, AActor* Requester);
	void ReleaseDeckCombatPointClaim(int32 WaypointId, AActor* Requester);
	void ReleaseAllDeckPointsFor(AActor* Actor);

	/** Activates one inactive pooled enemy at a validated live deck point. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Ship|Deck AI")
	bool ActivateDeckEnemyAtPoint(
		int32 SpawnPointId,
		AActor* InitialTarget,
		ADeckEnemy*& OutEnemy);

	/** Activates a pooled enemy only while the caller still owns this reservation. */
	bool ActivateDeckEnemyAtReservation(
		FDeckPointReservation& Reservation,
		AActor* InitialTarget,
		ADeckEnemy*& OutEnemy);

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

	// ================= Legacy Deck Enemy authoring bridge =================
	/** Compatibility fallback. New authoring belongs on DeckEnemySpawnerComponent. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Deck AI")
	bool bEnableDeckEnemyMVP = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Deck AI", meta = (EditCondition = "bEnableDeckEnemyMVP"))
	TSubclassOf<ADeckEnemy> DeckEnemyClass;

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
	void UpdateActiveCannons();
	void MigrateLegacyNavigationAuthoring();
	void DrawEnemyShipAIDebug() const;
	void InitializeDeckWaypoints();
	void InitializeDeckEnemyPool();
	void DestroyDeckEnemyPool();

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

	/** Owns the server-only pool, deployment queue, waypoint registry, and all point claims. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDeckEnemySpawnerComponent> DeckEnemySpawnerComponent;

	/** Owns the immutable ship-local waypoint graph; claims remain in the spawner transaction service. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDeckNavigationComponent> DeckNavigationComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Boss Encounter")
	TObjectPtr<UBossEncounterComponent> BossEncounterComponent;

	TArray<FGameplayAbilitySpecHandle> GrantedEnemyShipAbilityHandles;
};
