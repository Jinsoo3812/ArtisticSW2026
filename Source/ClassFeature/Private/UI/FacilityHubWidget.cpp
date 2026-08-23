#include "UI/FacilityHubWidget.h"

#include "BasePlayer.h"
#include "BasePlayerController.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Crafting/CraftingComponent.h"
#include "UI/Crafting/CraftingMenuEntryWidget.h"
#include "UI/Crafting/CraftingPanelWidget.h"
#include "UI/SkillUpgradePanel.h"

namespace FacilityCraftingMenu
{
	struct FNode
	{
		FString Label;
		TMap<FString, TSharedPtr<FNode>> Children;
		TArray<FCraftingListEntry> Recipes;
	};
}

void UFacilityHubWidget::NativeConstruct()
{
	Super::NativeConstruct();

	ResolveCraftingComponent();
	EnsureCraftingPanel();
	ResolveSkillUpgradePanel();
	BindCraftingEvents();
	BindNavigation();
	RefreshSkillSubmenuExpandedHeight();
	RefreshCraftingSubmenuExpandedHeight();
	if (SizeBox_SkillUpgradeMenu)
	{
		SizeBox_SkillUpgradeMenu->SetClipping(EWidgetClipping::ClipToBounds);
		SizeBox_SkillUpgradeMenu->SetHeightOverride(0.0f);
		SizeBox_SkillUpgradeMenu->SetIsEnabled(false);
	}
	if (SizeBox_CraftingMenu)
	{
		SizeBox_CraftingMenu->SetClipping(EWidgetClipping::ClipToBounds);
		SizeBox_CraftingMenu->SetHeightOverride(0.0f);
		SizeBox_CraftingMenu->SetIsEnabled(false);
	}
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

void UFacilityHubWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (bSkillSubmenuAnimating && SizeBox_SkillUpgradeMenu)
	{
		SkillSubmenuAnimationElapsed += InDeltaTime;
		const float Duration = FMath::Max(SkillSubmenuAnimationDuration, UE_SMALL_NUMBER);
		const float LinearAlpha = FMath::Clamp(SkillSubmenuAnimationElapsed / Duration, 0.0f, 1.0f);
		const float EasedAlpha = FMath::InterpEaseIn(0.0f, 1.0f, LinearAlpha, 2.0f);
		SizeBox_SkillUpgradeMenu->SetHeightOverride(FMath::Lerp(
			SkillSubmenuAnimationStartHeight, SkillSubmenuAnimationTargetHeight, EasedAlpha));
		if (LinearAlpha >= 1.0f)
		{
			bSkillSubmenuAnimating = false;
			SizeBox_SkillUpgradeMenu->SetHeightOverride(SkillSubmenuAnimationTargetHeight);
			SizeBox_SkillUpgradeMenu->SetIsEnabled(bSkillSubmenuExpanded);
		}
	}

	if (bCraftingSubmenuAnimating && SizeBox_CraftingMenu)
	{
		CraftingSubmenuAnimationElapsed += InDeltaTime;
		const float Duration = FMath::Max(SkillSubmenuAnimationDuration, UE_SMALL_NUMBER);
		const float LinearAlpha = FMath::Clamp(CraftingSubmenuAnimationElapsed / Duration, 0.0f, 1.0f);
		const float EasedAlpha = FMath::InterpEaseIn(0.0f, 1.0f, LinearAlpha, 2.0f);
		SizeBox_CraftingMenu->SetHeightOverride(FMath::Lerp(
			CraftingSubmenuAnimationStartHeight, CraftingSubmenuAnimationTargetHeight, EasedAlpha));
		if (LinearAlpha >= 1.0f)
		{
			bCraftingSubmenuAnimating = false;
			SizeBox_CraftingMenu->SetHeightOverride(CraftingSubmenuAnimationTargetHeight);
			SizeBox_CraftingMenu->SetIsEnabled(bCraftingSubmenuExpanded);
		}
	}
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
	SetSkillSubmenuExpanded(false);
	SetCraftingSubmenuExpanded(false);
	CloseCraftingTabIfActive();
	ShowTab(ShipUpgradeTabIndex);
}

void UFacilityHubWidget::ShowItemCraftingTab()
{
	/* UE_LOG(LogTemp, Log,
		TEXT("[FacilityHubFlow][CLIENT] Item Crafting tab requested; waiting for server approval. Context=%s"),
		*GetNameSafe(ContextActor)); */
	SetSkillSubmenuExpanded(false);
	SetCraftingSubmenuExpanded(!bCraftingSubmenuExpanded);
}

void UFacilityHubWidget::ShowSkillUpgradeTab()
{
	SetCraftingSubmenuExpanded(false);
	SetSkillSubmenuExpanded(true);
}

void UFacilityHubWidget::SelectSkillUpgrade(ESkillUpgradeSelection Skill)
{
	SetSkillSubmenuExpanded(true);
	SetCraftingSubmenuExpanded(false);
	CloseCraftingTabIfActive();
	ResolveSkillUpgradePanel();
	if (SkillUpgradePanelWidget)
	{
		SkillUpgradePanelWidget->SetSelectedSkill(Skill);
	}
	ShowTab(SkillUpgradeTabIndex);
	OnSkillUpgradeSelected.Broadcast(Skill);
	BP_OnSkillUpgradeSelected(Skill);
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

	if (Switcher->GetChildrenCount() > ItemCraftingTabIndex)
	{
		if (UCraftingPanelWidget* ExistingPanel =
			Cast<UCraftingPanelWidget>(Switcher->GetChildAt(ItemCraftingTabIndex)))
		{
			CraftingPanelWidget = ExistingPanel;
			return;
		}
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
		UWidget* ExistingCraftingTab = Switcher->GetChildAt(ItemCraftingTabIndex);
		if (UPanelWidget* ExistingHost = Cast<UPanelWidget>(ExistingCraftingTab))
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
	CraftingComponent->OnCraftingDataChanged.AddUniqueDynamic(this, &UFacilityHubWidget::HandleCraftingDataChanged);
}

void UFacilityHubWidget::UnbindCraftingEvents()
{
	if (!CraftingComponent)
	{
		return;
	}
	CraftingComponent->OnCraftingScreenOpened.RemoveDynamic(this, &UFacilityHubWidget::HandleCraftingScreenOpened);
	CraftingComponent->OnCraftingScreenClosed.RemoveDynamic(this, &UFacilityHubWidget::HandleCraftingScreenClosed);
	CraftingComponent->OnCraftingDataChanged.RemoveDynamic(this, &UFacilityHubWidget::HandleCraftingDataChanged);
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
	if (Button_GravityVortex)
	{
		Button_GravityVortex->OnClicked.AddUniqueDynamic(this, &UFacilityHubWidget::HandleGravityVortexClicked);
	}
	if (Button_WaterBomb)
	{
		Button_WaterBomb->OnClicked.AddUniqueDynamic(this, &UFacilityHubWidget::HandleWaterBombClicked);
	}
	if (Button_Bombardment)
	{
		Button_Bombardment->OnClicked.AddUniqueDynamic(this, &UFacilityHubWidget::HandleBombardmentClicked);
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
	if (Button_GravityVortex)
	{
		Button_GravityVortex->OnClicked.RemoveDynamic(this, &UFacilityHubWidget::HandleGravityVortexClicked);
	}
	if (Button_WaterBomb)
	{
		Button_WaterBomb->OnClicked.RemoveDynamic(this, &UFacilityHubWidget::HandleWaterBombClicked);
	}
	if (Button_Bombardment)
	{
		Button_Bombardment->OnClicked.RemoveDynamic(this, &UFacilityHubWidget::HandleBombardmentClicked);
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

void UFacilityHubWidget::ResolveSkillUpgradePanel()
{
	if (SkillUpgradePanelWidget)
	{
		return;
	}

	UWidgetSwitcher* Switcher = GetTabSwitcher();
	if (Switcher && Switcher->GetChildrenCount() > SkillUpgradeTabIndex)
	{
		SkillUpgradePanelWidget = Cast<USkillUpgradePanel>(Switcher->GetChildAt(SkillUpgradeTabIndex));
	}
}

void UFacilityHubWidget::SetSkillSubmenuExpanded(bool bExpanded)
{
	if (!SizeBox_SkillUpgradeMenu || bSkillSubmenuExpanded == bExpanded)
	{
		return;
	}

	if (bExpanded)
	{
		RefreshSkillSubmenuExpandedHeight();
		SizeBox_SkillUpgradeMenu->SetIsEnabled(true);
	}

	bSkillSubmenuExpanded = bExpanded;
	bSkillSubmenuAnimating = true;
	SkillSubmenuAnimationElapsed = 0.0f;
	SkillSubmenuAnimationStartHeight = SizeBox_SkillUpgradeMenu->GetHeightOverride();
	SkillSubmenuAnimationTargetHeight = bExpanded ? SkillSubmenuExpandedHeight : 0.0f;
}

void UFacilityHubWidget::RefreshSkillSubmenuExpandedHeight()
{
	if (!SizeBox_SkillUpgradeMenu)
	{
		return;
	}

	const float PreviousOverride = SizeBox_SkillUpgradeMenu->GetHeightOverride();
	SizeBox_SkillUpgradeMenu->ClearHeightOverride();
	SizeBox_SkillUpgradeMenu->ForceLayoutPrepass();
	if (UWidget* Content = SizeBox_SkillUpgradeMenu->GetContent())
	{
		Content->ForceLayoutPrepass();
		SkillSubmenuExpandedHeight = Content->GetDesiredSize().Y;
	}
	else
	{
		SkillSubmenuExpandedHeight = SizeBox_SkillUpgradeMenu->GetDesiredSize().Y;
	}
	SizeBox_SkillUpgradeMenu->SetHeightOverride(PreviousOverride);
}

void UFacilityHubWidget::SetCraftingSubmenuExpanded(bool bExpanded)
{
	if (!SizeBox_CraftingMenu || bCraftingSubmenuExpanded == bExpanded)
	{
		return;
	}
	if (bExpanded)
	{
		RefreshCraftingMenu();
		RefreshCraftingSubmenuExpandedHeight();
		SizeBox_CraftingMenu->SetIsEnabled(true);
	}
	bCraftingSubmenuExpanded = bExpanded;
	bCraftingSubmenuAnimating = true;
	CraftingSubmenuAnimationElapsed = 0.0f;
	CraftingSubmenuAnimationStartHeight = SizeBox_CraftingMenu->GetHeightOverride();
	CraftingSubmenuAnimationTargetHeight = bExpanded ? CraftingSubmenuExpandedHeight : 0.0f;
}

void UFacilityHubWidget::RefreshCraftingSubmenuExpandedHeight()
{
	if (!SizeBox_CraftingMenu)
	{
		return;
	}
	const float PreviousOverride = SizeBox_CraftingMenu->GetHeightOverride();
	SizeBox_CraftingMenu->ClearHeightOverride();
	SizeBox_CraftingMenu->ForceLayoutPrepass();
	if (UWidget* Content = SizeBox_CraftingMenu->GetContent())
	{
		Content->ForceLayoutPrepass();
		CraftingSubmenuExpandedHeight = FMath::Min(Content->GetDesiredSize().Y, 360.0f);
	}
	else
	{
		CraftingSubmenuExpandedHeight = FMath::Min(SizeBox_CraftingMenu->GetDesiredSize().Y, 360.0f);
	}
	SizeBox_CraftingMenu->SetHeightOverride(PreviousOverride);
}

void UFacilityHubWidget::RefreshCraftingMenu()
{
	SpawnedCraftingMenuEntries.Reset();
	if (!VerticalBox_CraftingMenu)
	{
		return;
	}
	VerticalBox_CraftingMenu->ClearChildren();
	if (!CraftingComponent)
	{
		return;
	}

	FCraftingListQuery Query;
	Query.bIncludeLocked = true;
	Query.bIncludeDisabled = false;
	const TArray<FCraftingListEntry> Entries = CraftingComponent->GetCraftableList(Query);
	FacilityCraftingMenu::FNode Root;
	for (const FCraftingListEntry& Entry : Entries)
	{
		TArray<FString> Segments;
		Entry.ResultItemTag.ToString().ParseIntoArray(Segments, TEXT("."), true);
		FacilityCraftingMenu::FNode* Node = &Root;
		const int32 FirstHierarchySegment =
			Segments.Num() >= 2 && Segments[0] == TEXT("Item") && Segments[1] == TEXT("Id") ? 2 : 0;
		for (int32 Index = FirstHierarchySegment; Index < Segments.Num() - 1; ++Index)
		{
			TSharedPtr<FacilityCraftingMenu::FNode>& Child = Node->Children.FindOrAdd(Segments[Index]);
			if (!Child.IsValid())
			{
				Child = MakeShared<FacilityCraftingMenu::FNode>();
				Child->Label = Segments[Index];
			}
			Node = Child.Get();
		}
		Node->Recipes.Add(Entry);
	}

	TFunction<void(const FacilityCraftingMenu::FNode&, int32)> RenderNode;
	RenderNode = [this, &RenderNode](const FacilityCraftingMenu::FNode& Node, int32 Depth)
	{
		TArray<FString> ChildKeys;
		Node.Children.GetKeys(ChildKeys);
		ChildKeys.Sort();
		for (const FString& Key : ChildKeys)
		{
			const TSharedPtr<FacilityCraftingMenu::FNode> Child = Node.Children.FindRef(Key);
			if (!Child.IsValid())
			{
				continue;
			}
			AddCraftingMenuCategory(FText::FromString(Child->Label), Depth);
			RenderNode(*Child, Depth + 1);
		}
		for (const FCraftingListEntry& Recipe : Node.Recipes)
		{
			AddCraftingMenuRecipe(Recipe, Depth);
		}
	};
	RenderNode(Root, 0);
}

void UFacilityHubWidget::AddCraftingMenuCategory(const FText& Label, int32 Depth)
{
	if (!VerticalBox_CraftingMenu)
	{
		return;
	}
	TSubclassOf<UCraftingMenuEntryWidget> RowClass = CraftingMenuEntryClass;
	if (!RowClass)
	{
		RowClass = UCraftingMenuEntryWidget::StaticClass();
	}
	UCraftingMenuEntryWidget* Row = CreateWidget<UCraftingMenuEntryWidget>(this, RowClass);
	if (Row)
	{
		Row->SetupCategory(Label, Depth);
		VerticalBox_CraftingMenu->AddChild(Row);
		SpawnedCraftingMenuEntries.Add(Row);
	}
}

void UFacilityHubWidget::AddCraftingMenuRecipe(const FCraftingListEntry& Entry, int32 Depth)
{
	if (!VerticalBox_CraftingMenu)
	{
		return;
	}
	TSubclassOf<UCraftingMenuEntryWidget> RowClass = CraftingMenuEntryClass;
	if (!RowClass)
	{
		RowClass = UCraftingMenuEntryWidget::StaticClass();
	}
	UCraftingMenuEntryWidget* Row = CreateWidget<UCraftingMenuEntryWidget>(this, RowClass);
	if (Row)
	{
		Row->SetupRecipe(Entry.RecipeId, Entry.DisplayName, Depth, Entry.RecipeId == PendingCraftingRecipeId);
		Row->OnRecipeActivated.BindUObject(this, &UFacilityHubWidget::HandleCraftingRecipeClicked);
		VerticalBox_CraftingMenu->AddChild(Row);
		SpawnedCraftingMenuEntries.Add(Row);
	}
}

void UFacilityHubWidget::HandleCraftingRecipeClicked(FName RecipeId)
{
	if (RecipeId.IsNone())
	{
		return;
	}
	PendingCraftingRecipeId = RecipeId;
	RefreshCraftingMenu();
	if (IsCraftingTabActive())
	{
		if (CraftingPanelWidget)
		{
			CraftingPanelWidget->ActivateCraftingPanel(CraftingComponent);
			CraftingPanelWidget->SelectRecipe(RecipeId);
		}
		ShowTab(ItemCraftingTabIndex);
		return;
	}
	RequestOpenCraftingTab();
}

void UFacilityHubWidget::HandleGravityVortexClicked()
{
	SelectSkillUpgrade(ESkillUpgradeSelection::GravityVortex);
}

void UFacilityHubWidget::HandleWaterBombClicked()
{
	SelectSkillUpgrade(ESkillUpgradeSelection::WaterBomb);
}

void UFacilityHubWidget::HandleBombardmentClicked()
{
	SelectSkillUpgrade(ESkillUpgradeSelection::Bombardment);
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
	NativeOnFacilityTabChanged(TabIndex);
}

void UFacilityHubWidget::NativeOnFacilityTabChanged(int32 NewTabIndex)
{
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
		if (!PendingCraftingRecipeId.IsNone())
		{
			CraftingPanelWidget->SelectRecipe(PendingCraftingRecipeId);
		}
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

void UFacilityHubWidget::HandleCraftingDataChanged()
{
	if (bCraftingSubmenuExpanded)
	{
		RefreshCraftingMenu();
		RefreshCraftingSubmenuExpandedHeight();
	}
}
