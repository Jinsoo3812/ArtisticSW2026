// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "StarForceWidget.generated.h"

// 스타포스 성공을 알리는 이벤트 디스패처 선언
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnStarForceSuccessSignature);

// 스타포스 시도 시 별의 위치 오차를 보내는 이벤트
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStarforceAttempted, float, ErrorMargin);

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

	// 스타포스가 멈췄을 때 호출되어 현재 별의 위치를 보고
	UFUNCTION(BlueprintCallable, Category = "Crafting")
	void ReportStarPosition(float CurrentXPosition);

	// CrafterComponent가 바인딩할 이벤트(스타포스 성공 여부 판정)
	UPROPERTY(BlueprintCallable, Category = "Crafting")
	FOnStarforceAttempted OnStarforceAttempted;
};
