// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/HealthBarWidget.h"

#include "Components/ProgressBar.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

void UHealthBarWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	ApplyHealthBarDisplayMode();

	if (IsDesignTime())
	{
		if (SecondaryHealthContainer)
		{
			SecondaryHealthContainer->SetVisibility(ESlateVisibility::HitTestInvisible);
		}

		SetShipWidgetsVisibility(ESlateVisibility::HitTestInvisible);
	}
}

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
	SetHealthBarDisplayMode(
		bVisible
			? EHealthBarDisplayMode::PlayerPrimaryWithShipSecondary
			: EHealthBarDisplayMode::PlayerOnly);
}

void UHealthBarWidget::SetHealthBarDisplayMode(EHealthBarDisplayMode NewDisplayMode)
{
	if (HealthBarDisplayMode == NewDisplayMode)
	{
		return;
	}

	HealthBarDisplayMode = NewDisplayMode;
	ApplyHealthBarDisplayMode();
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

void UHealthBarWidget::ApplyHealthBarDisplayMode()
{
	const bool bShowShip = HealthBarDisplayMode != EHealthBarDisplayMode::PlayerOnly;
	const bool bShipPrimary = HealthBarDisplayMode == EHealthBarDisplayMode::ShipPrimaryWithPlayerSecondary;

	if (PrimaryHealthContainer && SecondaryHealthContainer && PlayerHealthOverlay && ShipHealthOverlay)
	{
		USizeBox* PlayerTarget = bShipPrimary
			? SecondaryHealthContainer.Get()
			: PrimaryHealthContainer.Get();
		USizeBox* ShipTarget = bShipPrimary
			? PrimaryHealthContainer.Get()
			: SecondaryHealthContainer.Get();
		if (PlayerHealthOverlay->GetParent() != PlayerTarget || ShipHealthOverlay->GetParent() != ShipTarget)
		{
			PlayerHealthOverlay->RemoveFromParent();
			ShipHealthOverlay->RemoveFromParent();
			PlayerTarget->AddChild(PlayerHealthOverlay);
			ShipTarget->AddChild(ShipHealthOverlay);
		}

		PrimaryHealthContainer->SetVisibility(ESlateVisibility::HitTestInvisible);
		SecondaryHealthContainer->SetVisibility(
			bShowShip ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	SetShipWidgetsVisibility(
		bShowShip ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UHealthBarWidget::SetShipWidgetsVisibility(ESlateVisibility InVisibility)
{
	if (ShipHealthProgressBar)
	{
		ShipHealthProgressBar->SetVisibility(InVisibility);
	}
	if (ShipHealthText)
	{
		ShipHealthText->SetVisibility(
			InVisibility != ESlateVisibility::Collapsed && bShowHealthText
				? ESlateVisibility::HitTestInvisible
				: ESlateVisibility::Collapsed);
	}
}
