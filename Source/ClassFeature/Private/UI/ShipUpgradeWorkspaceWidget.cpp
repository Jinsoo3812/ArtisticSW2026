#include "UI/ShipUpgradeWorkspaceWidget.h"

#include "BasePlayerController.h"
#include "Components/Button.h"
#include "Components/WidgetSwitcher.h"

void UShipUpgradeWorkspaceWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (Button_ShipUpgrade)
	{
		Button_ShipUpgrade->OnClicked.AddUniqueDynamic(this, &UShipUpgradeWorkspaceWidget::ShowShipUpgradeTab);
	}
	if (Button_ItemCrafting)
	{
		Button_ItemCrafting->OnClicked.AddUniqueDynamic(this, &UShipUpgradeWorkspaceWidget::ShowItemCraftingTab);
	}
	if (Button_SkillUpgrade)
	{
		Button_SkillUpgrade->OnClicked.AddUniqueDynamic(this, &UShipUpgradeWorkspaceWidget::ShowSkillUpgradeTab);
	}
	if (Button_Close)
	{
		Button_Close->OnClicked.AddUniqueDynamic(this, &UShipUpgradeWorkspaceWidget::HandleCloseClicked);
	}

	ShowShipUpgradeTab();
}

void UShipUpgradeWorkspaceWidget::NativeDestruct()
{
	if (Button_ShipUpgrade)
	{
		Button_ShipUpgrade->OnClicked.RemoveDynamic(this, &UShipUpgradeWorkspaceWidget::ShowShipUpgradeTab);
	}
	if (Button_ItemCrafting)
	{
		Button_ItemCrafting->OnClicked.RemoveDynamic(this, &UShipUpgradeWorkspaceWidget::ShowItemCraftingTab);
	}
	if (Button_SkillUpgrade)
	{
		Button_SkillUpgrade->OnClicked.RemoveDynamic(this, &UShipUpgradeWorkspaceWidget::ShowSkillUpgradeTab);
	}
	if (Button_Close)
	{
		Button_Close->OnClicked.RemoveDynamic(this, &UShipUpgradeWorkspaceWidget::HandleCloseClicked);
	}

	Super::NativeDestruct();
}

void UShipUpgradeWorkspaceWidget::ShowShipUpgradeTab()
{
	ShowTab(ShipUpgradeTabIndex);
}

void UShipUpgradeWorkspaceWidget::ShowItemCraftingTab()
{
	ShowTab(ItemCraftingTabIndex);
}

void UShipUpgradeWorkspaceWidget::ShowSkillUpgradeTab()
{
	ShowTab(SkillUpgradeTabIndex);
}

void UShipUpgradeWorkspaceWidget::HandleCloseClicked()
{
	if (ABasePlayerController* PlayerController = Cast<ABasePlayerController>(GetOwningPlayer()))
	{
		PlayerController->CloseShipUpgradeWorkspace();
	}
}

void UShipUpgradeWorkspaceWidget::ShowTab(int32 TabIndex)
{
	if (!WidgetSwitcher_Content || !WidgetSwitcher_Content->GetChildrenCount())
	{
		return;
	}

	const int32 ClampedIndex = FMath::Clamp(TabIndex, 0, WidgetSwitcher_Content->GetChildrenCount() - 1);
	WidgetSwitcher_Content->SetActiveWidgetIndex(ClampedIndex);
	BP_OnWorkspaceTabChanged(ClampedIndex);
}
