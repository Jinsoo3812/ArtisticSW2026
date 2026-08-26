// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HealthBarWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UImage;
class UOverlay;
class USizeBox;
class UVerticalBox;

UENUM(BlueprintType)
enum class EHealthBarDisplayMode : uint8
{
	PlayerOnly,
	PlayerPrimaryWithShipSecondary,
	ShipPrimaryWithPlayerSecondary
};

UCLASS()
class CLASSFEATURE_API UHealthBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativePreConstruct() override;

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
	void SetHealthBarDisplayMode(EHealthBarDisplayMode NewDisplayMode);

	UFUNCTION(BlueprintCallable, Category = "Health")
	void SetShowHealthText(bool bShow);

protected:
	/** Designer hierarchy root: HealthBarsVerticalBox. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> HealthBarsVerticalBox;

	/** Designer-authored large/top position: PrimaryHealthContainer. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> PrimaryHealthContainer;

	/** Designer-authored small/bottom position: SecondaryHealthContainer. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> SecondaryHealthContainer;

	/** Target-specific content moved between designer-authored position containers. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> PlayerHealthOverlay;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> ShipHealthOverlay;

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Health|Layout")
	EHealthBarDisplayMode HealthBarDisplayMode = EHealthBarDisplayMode::PlayerOnly;

private:
	void ApplyHealthBarDisplayMode();
	void SetShipWidgetsVisibility(ESlateVisibility InVisibility);

	void UpdateHealthDisplay(
		UProgressBar* ProgressBar,
		UTextBlock* TextBlock,
		float CurrentHealth,
		float MaxHealth) const;
};
