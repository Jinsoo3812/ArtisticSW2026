// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseCharacter.h"

#include "BaseEnemy.generated.h"

class ABaseAIController;
class ABasePlayer;

UCLASS()
class ENEMY_API ABaseEnemy : public ABaseCharacter
{
	GENERATED_BODY()

protected:
	UPROPERTY()
	TObjectPtr<ABaseAIController> AIController;

	UPROPERTY()
	TObjectPtr<ABaseCharacter> BasePlayerClass;
public:
	ABaseEnemy();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	
public: // Getters
	FORCEINLINE TObjectPtr<ABaseAIController> GetAIController() const { check(AIController) return AIController; }
	FORCEINLINE TObjectPtr<ABaseCharacter> GetBasePlayerClass() const { check(BasePlayerClass) return BasePlayerClass; }
};
