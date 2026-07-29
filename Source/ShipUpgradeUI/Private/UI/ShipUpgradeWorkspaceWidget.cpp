#include "UI/ShipUpgradeWorkspaceWidget.h"

#include "Components/TextBlock.h"

void UShipUpgradeWorkspaceWidget::NativeOnFacilityTabChanged(int32 NewTabIndex)
{
	Super::NativeOnFacilityTabChanged(NewTabIndex);

	if (!Text_RightTitle)
	{
		return;
	}

	if (NewTabIndex == ShipUpgradeTabIndex)
	{
		Text_RightTitle->SetText(ShipUpgradeTabTitle);
	}
	else if (NewTabIndex == ItemCraftingTabIndex)
	{
		Text_RightTitle->SetText(ItemCraftingTabTitle);
	}
	else if (NewTabIndex == SkillUpgradeTabIndex)
	{
		Text_RightTitle->SetText(SkillUpgradeTabTitle);
	}
}
