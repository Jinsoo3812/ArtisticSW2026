// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "StarForceWidget.generated.h"

// 스타포스 성공을 알리는 이벤트 디스패처 선언
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnStarForceSuccessSignature);

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

	// 스타포스 성공을 알리는 이벤트 (Crafter에게 Item이 들어가도록)
	UPROPERTY(BlueprintCallable, Category = "StarForce")
	FOnStarForceSuccessSignature OnStarForceSuccess;
};
