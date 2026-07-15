// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/HealthBarWidget.h"

#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

void UHealthBarWidget::SetHealthValues(float CurrentHealth, float MaxHealth)
{
	const float SafeMaxHealth = FMath::Max(0.0f, MaxHealth);
	const float ClampedCurrentHealth = FMath::Clamp(CurrentHealth, 0.0f, SafeMaxHealth);
	const float HealthPercent = SafeMaxHealth > 0.0f ? ClampedCurrentHealth / SafeMaxHealth : 0.0f;

	if (HealthProgressBar)
	{
		HealthProgressBar->SetPercent(HealthPercent);
	}

	if (HealthText)
	{
		HealthText->SetVisibility(bShowHealthText ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

		if (!bShowHealthText)
		{
			return;
		}

		const int32 CurrentHealthInt = FMath::RoundToInt(ClampedCurrentHealth);
		const int32 MaxHealthInt = FMath::RoundToInt(SafeMaxHealth);
		HealthText->SetText(FText::Format(
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
}
