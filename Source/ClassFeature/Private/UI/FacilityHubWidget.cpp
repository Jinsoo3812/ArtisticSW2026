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
	/* UE_LOG(LogTemp, Log,
		TEXT("[FacilityHubFlow][CLIENT] Hub constructed. Widget=%s Context=%s Switcher=%s Children=%d ShipButton=%s CraftButton=%s"),
		*GetNameSafe(this),
		*GetNameSafe(ContextActor),
		*GetNameSafe(GetTabSwitcher()),
		GetTabSwitcher() ? GetTabSwitcher()->GetChildrenCount() : 0,
		Button_ShipUpgrade ? TEXT("BOUND") : TEXT("MISSING"),
		Button_ItemCrafting ? TEXT("BOUND") : TEXT("MISSING")); */
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
	/* UE_LOG(LogTemp, Log,
		TEXT("[FacilityHubFlow][CLIENT] Context initialized. Hub=%s Context=%s CraftingComponent=%s CraftingPanel=%s"),
		*GetNameSafe(this),
		*GetNameSafe(ContextActor),
		*GetNameSafe(CraftingComponent),
		*GetNameSafe(CraftingPanelWidget)); */
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
	/* UE_LOG(LogTemp, Log,
		TEXT("[FacilityHubFlow][CLIENT] Ship Upgrade tab requested. TargetIndex=%d"),
		ShipUpgradeTabIndex); */
	CloseCraftingTabIfActive();
	ShowTab(ShipUpgradeTabIndex);
}

void UFacilityHubWidget::ShowItemCraftingTab()
{
	/* UE_LOG(LogTemp, Log,
		TEXT("[FacilityHubFlow][CLIENT] Item Crafting tab requested; waiting for server approval. Context=%s"),
		*GetNameSafe(ContextActor)); */
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
		/* UE_LOG(LogTemp, Log,
			TEXT("[FacilityHubFlow][CLIENT] Existing crafting panel bound. Panel=%s"),
			*GetNameSafe(CraftingPanelWidget)); */
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
		/* UE_LOG(LogTemp, Warning,
			TEXT("[FacilityHubFlow][CLIENT] Crafting panel injection skipped. PanelClass=%s Switcher=%s"),
			*GetNameSafe(PanelClass.Get()),
			*GetNameSafe(Switcher)); */
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
		/* UE_LOG(LogTemp, Log,
			TEXT("[FacilityHubFlow][CLIENT] Crafting panel injected at index %d. Panel=%s"),
			ItemCraftingTabIndex,
			*GetNameSafe(CraftingPanelWidget)); */
		return;
	}

	if (Switcher->GetChildrenCount() > ItemCraftingTabIndex)
	{
		if (UPanelWidget* ExistingHost = Cast<UPanelWidget>(Switcher->GetChildAt(ItemCraftingTabIndex)))
		{
			ExistingHost->AddChild(NewPanel);
			CraftingPanelWidget = NewPanel;
			/* UE_LOG(LogTemp, Log,
				TEXT("[FacilityHubFlow][CLIENT] Crafting panel injected into existing host at index %d. Host=%s"),
				ItemCraftingTabIndex,
				*GetNameSafe(ExistingHost)); */
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
		/* UE_LOG(LogTemp, Error,
			TEXT("[FacilityHubFlow][CLIENT] FAILED: Cannot show tab. Switcher=%s Requested=%d Children=%d"),
			*GetNameSafe(Switcher),
			TabIndex,
			Switcher ? Switcher->GetChildrenCount() : 0); */
		return;
	}

	Switcher->SetActiveWidgetIndex(TabIndex);
	/* UE_LOG(LogTemp, Log,
		TEXT("[FacilityHubFlow][CLIENT] SUCCESS: Active tab changed. Index=%d Widget=%s"),
		TabIndex,
		*GetNameSafe(Switcher->GetActiveWidget())); */
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
