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
	UPROPERTY(EditDefaultsOnly, Category = "Craft")
	FGameplayTag ItemTagToTestCraft;
};
