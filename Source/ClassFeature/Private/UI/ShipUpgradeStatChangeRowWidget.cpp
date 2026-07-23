#include "UI/ShipUpgradeStatChangeRowWidget.h"

#include "Components/TextBlock.h"

void UShipUpgradeStatChangeRowWidget::ApplyStatChange(const FShipStatChangeView& InChange)
{
	if (Text_Change)
	{
		Text_Change->SetText(InChange.FormattedText);
		Text_Change->SetColorAndOpacity(InChange.bImprovesStat ? ImprovementColor : WorseningColor);
	}
	BP_OnStatChangeApplied(InChange.bImprovesStat);
}
