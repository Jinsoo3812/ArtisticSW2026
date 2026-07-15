// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EnemyHealthBarTypes.generated.h"

UENUM(BlueprintType)
enum class EEnemyHealthBarVisibilityPolicy : uint8
{
	AlwaysVisible UMETA(DisplayName = "Always Visible"),
	ShowOnDamage UMETA(DisplayName = "Show On Damage")
};
