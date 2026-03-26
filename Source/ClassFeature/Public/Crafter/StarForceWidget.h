// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "StarForceWidget.generated.h"

/**
 * 
 */
UCLASS()
class CLASSFEATURE_API UStarForceWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	// 스타포스를 시작하는 함수
	UFUNCTION(BlueprintImplementableEvent, Category = "StarForce")
	void StartStarForce();

	// 스타포스를 멈추는 함수
	UFUNCTION(BlueprintImplementableEvent, Category = "StarForce")
	void StopStarForce();
};
