// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/HealthBarWidget.h"

#include "Components/ProgressBar.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"

void UHealthBarWidget::SetHealthValues(float CurrentHealth, float MaxHealth)
{
	SetShipHealthVisible(false);
	SetPlayerHealthValues(CurrentHealth, MaxHealth);
}

void UHealthBarWidget::SetPlayerHealthValues(float CurrentHealth, float MaxHealth)
{
	UpdateHealthDisplay(HealthProgressBar, HealthText, CurrentHealth, MaxHealth);
}

void UHealthBarWidget::SetShipHealthValues(float CurrentHealth, float MaxHealth)
{
	UpdateHealthDisplay(ShipHealthProgressBar, ShipHealthText, CurrentHealth, MaxHealth);
}

void UHealthBarWidget::SetShipHealthVisible(bool bVisible)
{
	const ESlateVisibility ShipVisibility = bVisible
		? ESlateVisibility::HitTestInvisible
		: ESlateVisibility::Collapsed;

	if (ShipHealthProgressBar)
	{
		ShipHealthProgressBar->SetVisibility(ShipVisibility);
	}
	if (ShipHealthText)
	{
		ShipHealthText->SetVisibility(
			bVisible && bShowHealthText
				? ESlateVisibility::HitTestInvisible
				: ESlateVisibility::Collapsed);
	}
}

void UHealthBarWidget::UpdateHealthDisplay(
	UProgressBar* ProgressBar,
	UTextBlock* TextBlock,
	float CurrentHealth,
	float MaxHealth) const
{
	const float SafeMaxHealth = FMath::Max(0.0f, MaxHealth);
	const float ClampedCurrentHealth = FMath::Clamp(CurrentHealth, 0.0f, SafeMaxHealth);
	const float HealthPercent = SafeMaxHealth > 0.0f ? ClampedCurrentHealth / SafeMaxHealth : 0.0f;

	if (ProgressBar)
	{
		ProgressBar->SetPercent(HealthPercent);
	}

	if (TextBlock)
	{
		TextBlock->SetVisibility(bShowHealthText ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

		if (!bShowHealthText)
		{
			return;
		}

		const int32 CurrentHealthInt = FMath::RoundToInt(ClampedCurrentHealth);
		const int32 MaxHealthInt = FMath::RoundToInt(SafeMaxHealth);
		TextBlock->SetText(FText::Format(
			NSLOCTEXT("HealthBarWidget", "HealthValueFormat", "{0} / {1}"),
			FText::AsNumber(CurrentHealthInt),
			FText::AsNumber(MaxHealthInt)
		));
	}
}

void UHealthBarWidget::SetShowHealthText(bool bShow)
{
	bShowHealthText = bShow;

	if (HealthText)
	{
		HealthText->SetVisibility(bShowHealthText ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (ShipHealthText && ShipHealthProgressBar
		&& ShipHealthProgressBar->GetVisibility() != ESlateVisibility::Collapsed)
	{
		ShipHealthText->SetVisibility(bShowHealthText ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}
