#include "UI/FacilityHubWidget.h"

#include "BasePlayer.h"
#include "BasePlayerController.h"
#include "Crafting/CraftingComponent.h"

void UFacilityHubWidget::NativeConstruct()
{
	Super::NativeConstruct();

	ResolveCraftingComponent();
	BindCraftingEvents();
}

void UFacilityHubWidget::NativeDestruct()
{
	UnbindCraftingEvents();
	Super::NativeDestruct();
}

void UFacilityHubWidget::InitializeForContext(AActor* InContextActor)
{
	ContextActor = InContextActor;
	ResolveCraftingComponent();
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

void UFacilityHubWidget::HandleCraftingScreenOpened(AActor* ApprovedContext)
{
	if (ApprovedContext != ContextActor)
	{
		return;
	}

	BP_OnCraftingTabApproved(ApprovedContext);
}

void UFacilityHubWidget::HandleCraftingScreenClosed()
{
	BP_OnCraftingTabClosed();
}
