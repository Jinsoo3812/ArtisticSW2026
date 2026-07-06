// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HealthBarWidget.generated.h"

class UProgressBar;
class UTextBlock;

UCLASS()
class CLASSFEATURE_API UHealthBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Health")
	void SetHealthValues(float CurrentHealth, float MaxHealth);

	UFUNCTION(BlueprintCallable, Category = "Health")
	void SetShowHealthText(bool bShow);

protected:
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> HealthProgressBar;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HealthText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health")
	bool bShowHealthText = true;
};
