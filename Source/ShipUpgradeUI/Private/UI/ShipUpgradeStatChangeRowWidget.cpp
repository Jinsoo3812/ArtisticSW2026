#include "UI/ShipUpgradeStatChangeRowWidget.h"

#include "Components/TextBlock.h"

void UShipUpgradeStatChangeRowWidget::ApplyStatChange(
	const FShipStatChangeView& InChange,
	bool bAfterValueOnly)
{
	if (Text_Change)
	{
		const FText DisplayText = bAfterValueOnly
			? FText::Format(
				NSLOCTEXT("ShipUpgrade", "ActiveStatValueFormat", "{0}: {1}{2}"),
				InChange.DisplayName,
				FText::AsNumber(InChange.AfterValue),
				InChange.Unit)
			: FText::Format(
				NSLOCTEXT("ShipUpgrade", "AvailableStatValueFormat", "{0}: {1}{2} → {3}{2}"),
				InChange.DisplayName,
				FText::AsNumber(InChange.BeforeValue),
				InChange.Unit,
				FText::AsNumber(InChange.AfterValue));
		Text_Change->SetText(DisplayText);
		Text_Change->SetColorAndOpacity(InChange.bImprovesStat ? ImprovementColor : WorseningColor);
	}
	BP_OnStatChangeApplied(InChange.bImprovesStat);
}
