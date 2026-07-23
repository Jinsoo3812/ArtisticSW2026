#include "UI/FacilityHubWidget.h"

#include "BasePlayer.h"
#include "BasePlayerController.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/WidgetSwitcher.h"
#include "Crafting/CraftingComponent.h"
#include "UI/Crafting/CraftingPanelWidget.h"

void UFacilityHubWidget::NativeConstruct()
{
	Super::NativeConstruct();

	ResolveCraftingComponent();
	EnsureCraftingPanel();
	BindCraftingEvents();
	BindNavigation();
	ShowShipUpgradeTab();
}

void UFacilityHubWidget::NativeDestruct()
{
	if (CraftingPanelWidget)
	{
		CraftingPanelWidget->DeactivateCraftingPanel();
	}

	UnbindNavigation();
	UnbindCraftingEvents();
	Super::NativeDestruct();
}

void UFacilityHubWidget::InitializeForContext(AActor* InContextActor)
{
	ContextActor = InContextActor;
	ResolveCraftingComponent();
	EnsureCraftingPanel();
	BindCraftingEvents();
}

bool UFacilityHubWidget::RequestOpenCraftingTab()
{
	if (!CraftingComponent || !IsValid(ContextActor))
	{
		return false;
	}

	CraftingComponent->OpenCraftingScreen(ContextActor);
	return true;
}

void UFacilityHubWidget::ShowShipUpgradeTab()
{
	CloseCraftingTabIfActive();
	ShowTab(ShipUpgradeTabIndex);
}

void UFacilityHubWidget::ShowItemCraftingTab()
{
	RequestOpenCraftingTab();
}

void UFacilityHubWidget::ShowSkillUpgradeTab()
{
	CloseCraftingTabIfActive();
	ShowTab(SkillUpgradeTabIndex);
}

void UFacilityHubWidget::RequestCloseFacilityHub()
{
	if (ABasePlayerController* PlayerController = Cast<ABasePlayerController>(GetOwningPlayer()))
	{
		PlayerController->CloseFacilityHub();
	}
}

bool UFacilityHubWidget::IsCraftingTabActive() const
{
	return CraftingComponent
		&& IsValid(ContextActor)
		&& CraftingComponent->GetCurrentCraftingContext() == ContextActor;
}

void UFacilityHubWidget::PrepareToClose()
{
	if (IsCraftingTabActive())
	{
		CraftingComponent->CloseCraftingScreen();
	}
}

void UFacilityHubWidget::ResolveCraftingComponent()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetOwningPlayerPawn());
	CraftingComponent = Player ? Player->GetCraftingComponent() : nullptr;
}

void UFacilityHubWidget::EnsureCraftingPanel()
{
	if (CraftingPanelWidget)
	{
		return;
	}

	TSubclassOf<UCraftingPanelWidget> PanelClass = CraftingPanelWidgetClass;
	if (!PanelClass)
	{
		PanelClass = LoadClass<UCraftingPanelWidget>(
			nullptr,
			TEXT("/Game/Blueprints/02_UI/UI_FacilityHub/Crafting/WBP_CraftingPanel.WBP_CraftingPanel_C"));
	}

	UWidgetSwitcher* Switcher = GetTabSwitcher();
	if (!PanelClass || !Switcher)
	{
		return;
	}

	UCraftingPanelWidget* NewPanel = CreateWidget<UCraftingPanelWidget>(GetOwningPlayer(), PanelClass);
	if (!NewPanel)
	{
		return;
	}

	if (Switcher->GetChildrenCount() == ItemCraftingTabIndex)
	{
		Switcher->AddChild(NewPanel);
		CraftingPanelWidget = NewPanel;
		return;
	}

	if (Switcher->GetChildrenCount() > ItemCraftingTabIndex)
	{
		if (UPanelWidget* ExistingHost = Cast<UPanelWidget>(Switcher->GetChildAt(ItemCraftingTabIndex)))
		{
			ExistingHost->AddChild(NewPanel);
			CraftingPanelWidget = NewPanel;
		}
	}
}

void UFacilityHubWidget::BindCraftingEvents()
{
	if (!CraftingComponent)
	{
		return;
	}
	CraftingComponent->OnCraftingScreenOpened.AddUniqueDynamic(this, &UFacilityHubWidget::HandleCraftingScreenOpened);
	CraftingComponent->OnCraftingScreenClosed.AddUniqueDynamic(this, &UFacilityHubWidget::HandleCraftingScreenClosed);
}

void UFacilityHubWidget::UnbindCraftingEvents()
{
	if (!CraftingComponent)
	{
		return;
	}
	CraftingComponent->OnCraftingScreenOpened.RemoveDynamic(this, &UFacilityHubWidget::HandleCraftingScreenOpened);
	CraftingComponent->OnCraftingScreenClosed.RemoveDynamic(this, &UFacilityHubWidget::HandleCraftingScreenClosed);
}

void UFacilityHubWidget::BindNavigation()
{
	if (Button_ShipUpgrade)
	{
		Button_ShipUpgrade->OnClicked.AddUniqueDynamic(this, &UFacilityHubWidget::ShowShipUpgradeTab);
	}
	if (Button_ItemCrafting)
	{
		Button_ItemCrafting->OnClicked.AddUniqueDynamic(this, &UFacilityHubWidget::ShowItemCraftingTab);
	}
	if (Button_SkillUpgrade)
	{
		Button_SkillUpgrade->OnClicked.AddUniqueDynamic(this, &UFacilityHubWidget::ShowSkillUpgradeTab);
	}
	if (Button_Close)
	{
		Button_Close->OnClicked.AddUniqueDynamic(this, &UFacilityHubWidget::RequestCloseFacilityHub);
	}
	// WBP_FacilityHub already connects CraftingTabButton and CloseButton in its
	// existing Event Graph. Do not bind those again and issue duplicate RPCs.
}

void UFacilityHubWidget::UnbindNavigation()
{
	if (Button_ShipUpgrade)
	{
		Button_ShipUpgrade->OnClicked.RemoveDynamic(this, &UFacilityHubWidget::ShowShipUpgradeTab);
	}
	if (Button_ItemCrafting)
	{
		Button_ItemCrafting->OnClicked.RemoveDynamic(this, &UFacilityHubWidget::ShowItemCraftingTab);
	}
	if (Button_SkillUpgrade)
	{
		Button_SkillUpgrade->OnClicked.RemoveDynamic(this, &UFacilityHubWidget::ShowSkillUpgradeTab);
	}
	if (Button_Close)
	{
		Button_Close->OnClicked.RemoveDynamic(this, &UFacilityHubWidget::RequestCloseFacilityHub);
	}
}

void UFacilityHubWidget::CloseCraftingTabIfActive()
{
	if (IsCraftingTabActive())
	{
		CraftingComponent->CloseCraftingScreen();
	}
}

void UFacilityHubWidget::ShowTab(int32 TabIndex)
{
	UWidgetSwitcher* Switcher = GetTabSwitcher();
	if (!Switcher || TabIndex < 0 || TabIndex >= Switcher->GetChildrenCount())
	{
		return;
	}

	Switcher->SetActiveWidgetIndex(TabIndex);
	BP_OnFacilityTabChanged(TabIndex);
}

UWidgetSwitcher* UFacilityHubWidget::GetTabSwitcher() const
{
	return WidgetSwitcher_Content ? WidgetSwitcher_Content.Get() : MainWidgetSwitcher.Get();
}

void UFacilityHubWidget::HandleCraftingScreenOpened(AActor* ApprovedContext)
{
	if (ApprovedContext != ContextActor)
	{
		return;
	}

	if (CraftingPanelWidget)
	{
		CraftingPanelWidget->ActivateCraftingPanel(CraftingComponent);
	}

	ShowTab(ItemCraftingTabIndex);
	BP_OnCraftingTabApproved(ApprovedContext);
}

void UFacilityHubWidget::HandleCraftingScreenClosed()
{
	if (CraftingPanelWidget)
	{
		CraftingPanelWidget->DeactivateCraftingPanel();
	}

	BP_OnCraftingTabClosed();
}
