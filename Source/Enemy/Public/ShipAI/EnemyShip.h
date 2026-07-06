// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Ship.h"
#include "ShipAI/BTTask_NavalDrive.h" // For ENavalCombatState
#include "EnemyShip.generated.h"

class ACannon;
class UBaseHealthComponent;

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

protected:
	void FindAttachedCannons();
	void UpdateActiveCannons();

	// Aiming and firing logic
	void TickAIAimingAndFiring(float DeltaTime);

	// ---- Death Handling ----
	UFUNCTION()
	void OnDeathStarted(UBaseHealthComponent* InHealthComponent);

	void HandleShipDeath();

	// ---- Death Properties ----
	/** 사망 후 Destroy까지의 대기 시간 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Death")
	float DestroyAfterDeathDelay = 5.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Ship|Death")
	bool bDeathHandled = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBaseHealthComponent> HealthComponent;

	FTimerHandle DeathDestroyTimerHandle;

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
