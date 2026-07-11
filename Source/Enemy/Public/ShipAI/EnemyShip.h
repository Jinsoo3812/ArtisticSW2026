// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Ship.h"
#include "ShipAI/BTTask_NavalDrive.h" // For ENavalCombatState
#include "EnemyDropData.h"
#include "UI/EnemyHealthBarTypes.h"
#include "EnemyShip.generated.h"

class ACannon;
class AStorageChest;
class UBaseHealthComponent;
class UHealthBarWidget;
class UWidgetComponent;

UCLASS()
class ENEMY_API AEnemyShip : public AShip
{
	GENERATED_BODY()

public:
	AEnemyShip();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void Tick(float DeltaTime) override;

	// AI Control APIs
	UFUNCTION(BlueprintCallable, Category = "Ship|AI")
	void SetAITarget(AActor* Target) { AITargetShip = Target; }

	UFUNCTION(BlueprintCallable, Category = "Ship|AI")
	void SetNavalCombatState(ENavalCombatState State) { CurrentCombatState = State; }

	UFUNCTION(BlueprintCallable, Category = "Ship|AI")
	void SetMaxActiveCannons(int32 Count) { MaxActiveCannons = Count; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|AI")
	FName SquadID = TEXT("Squad_Alpha");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|AI")
	float IdealDistance = 2000.f;

protected:
	void FindAttachedCannons();
	void UpdateActiveCannons();

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
	TArray<TObjectPtr<ACannon>> AttachedCannons;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Ship|AI Cannon")
	TArray<TObjectPtr<ACannon>> ActiveAICannons;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|AI Cannon")
	int32 MaxActiveCannons = 2;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Ship|AI Cannon")
	TObjectPtr<AActor> AITargetShip = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Ship|AI Combat")
	ENavalCombatState CurrentCombatState = ENavalCombatState::Idle;

	FTimerHandle ActiveCannonsTimerHandle;
};
