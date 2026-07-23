// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "WorkTable.generated.h"

UCLASS()
class CLASSFEATURE_API AWorkTable : public AActor
{
	GENERATED_BODY()

public:
	/** Opens the integrated map/upgrade/crafting workspace instead of the legacy StarForce popup. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Craft")
	bool bOpenIntegratedWorkspace = true;

	UPROPERTY(EditDefaultsOnly, Category = "Craft")
	FGameplayTag ItemTagToTestCraft;
};
