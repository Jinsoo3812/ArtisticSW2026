#include "UI/ShipRepairProgressWidget.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SProgressBar.h"

TSharedRef<SWidget> UShipRepairProgressWidget::RebuildWidget()
{
	return SNew(SBox)
		.WidthOverride(360.0f)
		.HeightOverride(22.0f)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SAssignNew(ProgressBar, SProgressBar)
			.Percent(0.0f)
		];
}

void UShipRepairProgressWidget::SetRepairProgress(const float NormalizedProgress)
{
	if (ProgressBar.IsValid())
	{
		ProgressBar->SetPercent(FMath::Clamp(NormalizedProgress, 0.0f, 1.0f));
	}
}
