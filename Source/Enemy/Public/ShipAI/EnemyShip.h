// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Ship.h"
#include "ShipAI/EnemyShipNavigationTypes.h"
#include "EnemyDropData.h"
#include "GameplayAbilitySpecHandle.h"
#include "UI/EnemyHealthBarTypes.h"
#include "EnemyShip.generated.h"

class ACannon;
class AStorageChest;
class UBaseHealthComponent;
class UHealthBarWidget;
class UWidgetComponent;
class UEnemyShipArchetypeData;
class UEnemyShipAbilitySet;
class UEnemyShipNavigationComponent;
class UEnemyShipPatternRuntimeComponent;
class UEnemyShipPatternData;
class UEnemyShipSkillModuleData;
class UGameplayAbility;

UCLASS()
class ENEMY_API AEnemyShip : public AShip
{
	GENERATED_BODY()

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

	UFUNCTION(BlueprintPure, Category = "Ship|Death")
	bool IsDeathHandled() const { return bDeathHandled; }
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

	// Aiming and firing logic
	void TickAIAimingAndFiring(float DeltaTime);

	// ---- Death Handling ----
	UFUNCTION()
	void OnDeathStarted(UBaseHealthComponent* InHealthComponent);

	void HandleShipDeath();
	void InitializeEnemyDropData();
	void DropAtDeathLocation(const FVector& DeathLocation, const FRotator& DeathRotation);

	UFUNCTION()
	void OnHealthChanged(UBaseHealthComponent* InHealthComponent, float OldValue, float NewValue, AActor* InstigatorActor);

	UFUNCTION()
	void OnMaxHealthChanged(UBaseHealthComponent* InHealthComponent, float OldValue, float NewValue, AActor* InstigatorActor);

	void InitializeHealthBarWidget();
	void RefreshHealthBarWidget();
	void UpdateHealthBarVisibilityAfterHealthChanged(float OldValue, float NewValue);
	void HideHealthBarForDamagePolicy();

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

	// 죽었을 때, 드랍할 Storage 클래스
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
	TObjectPtr<UWidgetComponent> HealthBarWidgetComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|HealthBar")
	TSubclassOf<UHealthBarWidget> HealthBarWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|HealthBar")
	FVector HealthBarOffset = FVector(0.0f, 0.0f, 300.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|HealthBar")
	FVector2D HealthBarDrawSize = FVector2D(220.0f, 28.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|HealthBar")
	EEnemyHealthBarVisibilityPolicy HealthBarVisibilityPolicy = EEnemyHealthBarVisibilityPolicy::AlwaysVisible;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|HealthBar", meta = (EditCondition = "HealthBarVisibilityPolicy == EEnemyHealthBarVisibilityPolicy::ShowOnDamage", ClampMin = "0.0"))
	float HealthBarVisibleDurationAfterDamage = 2.0f;

	FTimerHandle DeathDestroyTimerHandle;
	FTimerHandle HealthBarHideTimerHandle;
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
	TArray<FGameplayAbilitySpecHandle> GrantedEnemyShipAbilityHandles;
};
