// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "NavalAIController.generated.h"

class UAIPerceptionComponent;
class UAISenseConfig_Sight;
class AShip;

UCLASS()
class ENEMY_API ANavalAIController : public AAIController
{
	GENERATED_BODY()

public:
	ANavalAIController();
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "AI|Target")
	void SetTargetShip(AShip* InTargetShip);

	UFUNCTION(BlueprintPure, Category = "AI|Target")
	AShip* GetTargetShip() const { return TargetShip.Get(); }

	/** Immediately re-evaluate and route the closest valid Player ship. */
	UFUNCTION(BlueprintCallable, Category = "AI|Target")
	void RefreshTargetShip();

protected:
	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI | Perception")
	TObjectPtr<UAIPerceptionComponent> PerceptionComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI | Perception")
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

protected:
	// 비헤이비어 트리를 구동할 데이터 설정
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "AI | Behavior Tree")
	TObjectPtr<class UBehaviorTree> DefaultBehaviorTree;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Blackboard")
	FName TargetShipKeyName = TEXT("TargetShip");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Target", meta = (ClampMin = "0.05", Units = "s"))
	float TargetRefreshInterval = 0.2f;

private:
	UFUNCTION()
	void HandleTargetPerceptionUpdated(AActor* SensedActor, FAIStimulus Stimulus);

	AShip* FindClosestPlayerShip() const;

	TWeakObjectPtr<AShip> TargetShip;
	float TargetRefreshRemaining = 0.0f;
};
