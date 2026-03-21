// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseCharacter.h"

#include "BaseEnemy.generated.h"

class UBehaviorTree;
class ABaseAIController;
class ABasePlayer;
class ABaseItem;

UCLASS()
class ENEMY_API ABaseEnemy : public ABaseCharacter
{
	GENERATED_BODY()

protected:
	// Enemy에게 장착된 AI Controller
	UPROPERTY()
	TObjectPtr<ABaseAIController> AIController;
	// Enemy가 사용할 Behavior Tree
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI | Behavior Tree")
	TObjectPtr<UBehaviorTree> BehaviorTree;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	TObjectPtr<ABaseItem> DefaultWeapon;
	
public:
	ABaseEnemy();

protected:
	virtual void BeginPlay() override;

public:
	//virtual void Tick(float DeltaTime) override;
	
	
public:
	// Getters
	FORCEINLINE TObjectPtr<ABaseAIController> GetAIController() const { check(AIController) return AIController; }
	FORCEINLINE TObjectPtr<UBehaviorTree> GetBehaviorTree() const { check(BehaviorTree) return BehaviorTree; }
	FORCEINLINE ABaseItem* GetDefaultWeapon() const { check(DefaultWeapon) return DefaultWeapon; }
};
