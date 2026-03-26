// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InteractUserWidget.generated.h"

// Player가 Interactable Object에 접근했을 때 UI에 표시할 정보 구조체
USTRUCT(BlueprintType)
struct FInteractionUIInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|UI")
	FText ObjectName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|UI")
	FText ActionText;
};

/**
 * 
 */
UCLASS()
class ARTISTICSWCORE_API UInteractUserWidget : public UUserWidget
{
	GENERATED_BODY()
	
public:
	// Interactable Object에 접근했을 때 UI를 띄우는 함수(BP에서 구현)
	UFUNCTION(BlueprintImplementableEvent, Category = "Interaction")
	void OnUpdateInteractUI(const FInteractionUIInfo& UIInfo);
};
