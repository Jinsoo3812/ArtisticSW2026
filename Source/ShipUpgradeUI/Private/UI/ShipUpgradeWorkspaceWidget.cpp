#include "UI/ShipUpgradeWorkspaceWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"

namespace ShipUpgradeWorkspaceStyle
{
	TMap<TWeakObjectPtr<UButton>, FButtonStyle> OriginalTopMenuStyles;

	void CacheOriginalStyle(UButton* Button)
	{
		const TWeakObjectPtr<UButton> ButtonKey(Button);
		if (Button && !OriginalTopMenuStyles.Contains(ButtonKey))
		{
			OriginalTopMenuStyles.Add(ButtonKey, Button->GetStyle());
		}
	}

	void RemoveCachedStyle(UButton* Button)
	{
		if (Button)
		{
			OriginalTopMenuStyles.Remove(TWeakObjectPtr<UButton>(Button));
		}
	}

	void ApplySelectionStyle(
		UButton* Button,
		bool bSelected,
		const FLinearColor& SelectedColor)
	{
		if (!Button)
		{
			return;
		}

		CacheOriginalStyle(Button);
		const FButtonStyle* OriginalStyle = OriginalTopMenuStyles.Find(
			TWeakObjectPtr<UButton>(Button));
		if (!OriginalStyle)
		{
			return;
		}

		FButtonStyle UpdatedStyle = *OriginalStyle;
		if (bSelected)
		{
			const FSlateColor SelectedSlateColor(SelectedColor);
			UpdatedStyle.Normal.TintColor = SelectedSlateColor;
			UpdatedStyle.Hovered.TintColor = SelectedSlateColor;
			UpdatedStyle.Pressed.TintColor = SelectedSlateColor;
			UpdatedStyle.Disabled.TintColor = SelectedSlateColor;
		}

		Button->SetBackgroundColor(FLinearColor::White);
		Button->SetStyle(UpdatedStyle);
	}
}

void UShipUpgradeWorkspaceWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (Button_ShipUpgrade)
	{
		ShipUpgradeWorkspaceStyle::CacheOriginalStyle(Button_ShipUpgrade);
		Button_ShipUpgrade->OnClicked.AddUniqueDynamic(
			this, &UShipUpgradeWorkspaceWidget::HandleShipUpgradeMenuClicked);
	}
	if (Button_ItemCrafting)
	{
		ShipUpgradeWorkspaceStyle::CacheOriginalStyle(Button_ItemCrafting);
		Button_ItemCrafting->OnClicked.AddUniqueDynamic(
			this, &UShipUpgradeWorkspaceWidget::HandleItemCraftingMenuClicked);
	}
	if (Button_SkillUpgrade)
	{
		ShipUpgradeWorkspaceStyle::CacheOriginalStyle(Button_SkillUpgrade);
		Button_SkillUpgrade->OnClicked.AddUniqueDynamic(
			this, &UShipUpgradeWorkspaceWidget::HandleSkillUpgradeMenuClicked);
	}
}

void UShipUpgradeWorkspaceWidget::NativeDestruct()
{
	if (Button_ShipUpgrade)
	{
		Button_ShipUpgrade->OnClicked.RemoveDynamic(
			this, &UShipUpgradeWorkspaceWidget::HandleShipUpgradeMenuClicked);
		ShipUpgradeWorkspaceStyle::RemoveCachedStyle(Button_ShipUpgrade);
	}
	if (Button_ItemCrafting)
	{
		Button_ItemCrafting->OnClicked.RemoveDynamic(
			this, &UShipUpgradeWorkspaceWidget::HandleItemCraftingMenuClicked);
		ShipUpgradeWorkspaceStyle::RemoveCachedStyle(Button_ItemCrafting);
	}
	if (Button_SkillUpgrade)
	{
		Button_SkillUpgrade->OnClicked.RemoveDynamic(
			this, &UShipUpgradeWorkspaceWidget::HandleSkillUpgradeMenuClicked);
		ShipUpgradeWorkspaceStyle::RemoveCachedStyle(Button_SkillUpgrade);
	}

	Super::NativeDestruct();
}

void UShipUpgradeWorkspaceWidget::NativeOnFacilityTabChanged(int32 NewTabIndex)
{
	Super::NativeOnFacilityTabChanged(NewTabIndex);
	if (NewTabIndex == ShipUpgradeTabIndex)
	{
		SetSelectedTopMenu(EShipUpgradeWorkspaceMenuSelection::ShipUpgrade);
	}
	else if (NewTabIndex == ItemCraftingTabIndex)
	{
		SetSelectedTopMenu(EShipUpgradeWorkspaceMenuSelection::ItemCrafting);
	}
	else if (NewTabIndex == SkillUpgradeTabIndex)
	{
		SetSelectedTopMenu(EShipUpgradeWorkspaceMenuSelection::SkillUpgrade);
	}

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

void UShipUpgradeWorkspaceWidget::HandleShipUpgradeMenuClicked()
{
	SetSelectedTopMenu(EShipUpgradeWorkspaceMenuSelection::ShipUpgrade);
}

void UShipUpgradeWorkspaceWidget::HandleItemCraftingMenuClicked()
{
	SetSelectedTopMenu(EShipUpgradeWorkspaceMenuSelection::ItemCrafting);
}

void UShipUpgradeWorkspaceWidget::HandleSkillUpgradeMenuClicked()
{
	SetSelectedTopMenu(EShipUpgradeWorkspaceMenuSelection::SkillUpgrade);
}

void UShipUpgradeWorkspaceWidget::SetSelectedTopMenu(
	EShipUpgradeWorkspaceMenuSelection NewSelection)
{
	SelectedTopMenu = NewSelection;
	ApplySelectedTopMenuColor();
}

void UShipUpgradeWorkspaceWidget::ApplySelectedTopMenuColor()
{
	ShipUpgradeWorkspaceStyle::ApplySelectionStyle(
		Button_ShipUpgrade,
		SelectedTopMenu == EShipUpgradeWorkspaceMenuSelection::ShipUpgrade,
		SelectedTopMenuBackgroundColor);
	ShipUpgradeWorkspaceStyle::ApplySelectionStyle(
		Button_ItemCrafting,
		SelectedTopMenu == EShipUpgradeWorkspaceMenuSelection::ItemCrafting,
		SelectedTopMenuBackgroundColor);
	ShipUpgradeWorkspaceStyle::ApplySelectionStyle(
		Button_SkillUpgrade,
		SelectedTopMenu == EShipUpgradeWorkspaceMenuSelection::SkillUpgrade,
		SelectedTopMenuBackgroundColor);
}
