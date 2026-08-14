// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HealthBarWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UImage;

UCLASS()
class CLASSFEATURE_API UHealthBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Legacy single-health entry point used by enemy world-space health bars. */
	UFUNCTION(BlueprintCallable, Category = "Health")
	void SetHealthValues(float CurrentHealth, float MaxHealth);

	UFUNCTION(BlueprintCallable, Category = "Health")
	void SetPlayerHealthValues(float CurrentHealth, float MaxHealth);

	UFUNCTION(BlueprintCallable, Category = "Health")
	void SetShipHealthValues(float CurrentHealth, float MaxHealth);

	UFUNCTION(BlueprintCallable, Category = "Health")
	void SetShipHealthVisible(bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "Health")
	void SetShowHealthText(bool bShow);

protected:
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> HealthProgressBar;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HealthText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> PlayerImage;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ShipHealthProgressBar;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ShipHealthText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health")
	bool bShowHealthText = true;

private:
	void UpdateHealthDisplay(
		UProgressBar* ProgressBar,
		UTextBlock* TextBlock,
		float CurrentHealth,
		float MaxHealth) const;
};
