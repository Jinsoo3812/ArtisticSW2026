#include "UI/SkillUpgradePanel.h"

#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Crafting/CraftingComponent.h"
#include "Engine/Texture2D.h"
#include "Item/ItemSubsystem.h"
#include "Skills/PlayerSkillComponent.h"
#include "UI/Crafting/CraftingCompleteWidget.h"
#include "UI/Crafting/CraftingIngredientEntryWidget.h"

namespace SkillCraftingPanel
{
	constexpr int32 RecipePageIndex = 0;
	constexpr int32 CompletePageIndex = 1;
	constexpr int32 IngredientSlotCount = 3;
}

void USkillUpgradePanel::NativeConstruct()
{
	Super::NativeConstruct();

	if (CraftButton)
	{
		CraftButton->OnClicked.AddUniqueDynamic(this, &USkillUpgradePanel::HandleCraftButtonClicked);
	}
	if (SkillCraftingCompleteWidget)
	{
		SkillCraftingCompleteWidget->OnDismissed.BindUObject(
			this,
			&USkillUpgradePanel::HandleCraftingCompleteDismissed);
	}
	ResolveSkillComponent();
	SetCraftingState(ESkillCraftingUIState::NoSelection);
	RefreshCraftingState();
}

void USkillUpgradePanel::NativeDestruct()
{
	if (CraftButton)
	{
		CraftButton->OnClicked.RemoveDynamic(this, &USkillUpgradePanel::HandleCraftButtonClicked);
	}
	if (SkillCraftingCompleteWidget)
	{
		SkillCraftingCompleteWidget->OnDismissed.Unbind();
	}
	DeactivateSkillCraftingPanel();
	UnbindSkillComponent();
	Super::NativeDestruct();
}

void USkillUpgradePanel::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bConvergenceAnimating)
	{
		UpdateConvergenceAnimation(InDeltaTime);
	}
}

void USkillUpgradePanel::ActivateSkillCraftingPanel(UCraftingComponent* InCraftingComponent)
{
	if (CraftingComponent != InCraftingComponent)
	{
		UnbindCraftingEvents();
		CraftingComponent = InCraftingComponent;
	}
	bPanelActive = CraftingComponent != nullptr;
	BindCraftingEvents();
	ResolveSkillComponent();
	RefreshCraftingState();
}

void USkillUpgradePanel::DeactivateSkillCraftingPanel()
{
	bPanelActive = false;
	bCraftRequestPending = false;
	bConvergenceAnimating = false;
	PendingRequestId.Invalidate();
	UnbindCraftingEvents();
	CraftingComponent = nullptr;
	ResetIngredientRenderTransforms();
	ClearIngredientEntries();
	SetCraftButtonAvailable(false);
}

void USkillUpgradePanel::SetSelectedSkill(ESkillUpgradeSelection InSelectedSkill)
{
	SelectedSkill = InSelectedSkill;
	bHasSelectedSkill = true;
	ResolveSkillComponent();
	BP_OnSkillUpgradeSelectionChanged(SelectedSkill);
	if (WidgetSwitcher_SkillCraftingState)
	{
		WidgetSwitcher_SkillCraftingState->SetActiveWidgetIndex(SkillCraftingPanel::RecipePageIndex);
	}
	RefreshCraftingState();
}

void USkillUpgradePanel::RefreshLockState()
{
	const bool bUnlocked = IsSelectedSkillUnlocked();
	const bool bShowLock = bHasSelectedSkill
		&& !bUnlocked
		&& !IsSelectedSkillUnlockConditionMet();
	const ESlateVisibility LockVisibility = bShowLock
		? ESlateVisibility::HitTestInvisible
		: ESlateVisibility::Collapsed;

	if (Border_LockOverlay)
	{
		Border_LockOverlay->SetVisibility(LockVisibility);
	}
	if (Image_Lock)
	{
		Image_Lock->SetVisibility(LockVisibility);
	}
	if (Text_UnlockCondition)
	{
		Text_UnlockCondition->SetText(GetSelectedUnlockConditionText());
		Text_UnlockCondition->SetVisibility(LockVisibility);
	}
	if (Text_LockedMessage)
	{
		Text_LockedMessage->SetText(NSLOCTEXT("SkillCrafting", "SkillLocked", "아직 해금되지 않은 스킬입니다."));
		Text_LockedMessage->SetVisibility(LockVisibility);
	}
}

bool USkillUpgradePanel::IsSelectedSkillUnlocked() const
{
	return bHasSelectedSkill
		&& SkillComponent
		&& SkillComponent->IsSkillUnlocked(GetSelectedSkillTag());
}

bool USkillUpgradePanel::IsSelectedSkillUnlockConditionMet() const
{
	return bHasSelectedSkill
		&& SkillComponent
		&& SkillComponent->IsSkillUnlockConditionMet(GetSelectedSkillTag());
}

void USkillUpgradePanel::ResolveSkillComponent()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetOwningPlayerPawn());
	UPlayerSkillComponent* ResolvedComponent = Player ? Player->GetPlayerSkillComponent() : nullptr;
	if (SkillComponent == ResolvedComponent)
	{
		return;
	}

	UnbindSkillComponent();
	SkillComponent = ResolvedComponent;
	if (SkillComponent)
	{
		SkillComponent->OnSkillChanged.AddUniqueDynamic(this, &USkillUpgradePanel::HandleSkillChanged);
	}
}

void USkillUpgradePanel::UnbindSkillComponent()
{
	if (SkillComponent)
	{
		SkillComponent->OnSkillChanged.RemoveDynamic(this, &USkillUpgradePanel::HandleSkillChanged);
		SkillComponent = nullptr;
	}
}

void USkillUpgradePanel::BindCraftingEvents()
{
	if (CraftingComponent)
	{
		CraftingComponent->OnCraftingDataChanged.AddUniqueDynamic(
			this,
			&USkillUpgradePanel::HandleCraftingDataChanged);
		CraftingComponent->OnCraftingResult.AddUniqueDynamic(
			this,
			&USkillUpgradePanel::HandleCraftingResult);
	}
}

void USkillUpgradePanel::UnbindCraftingEvents()
{
	if (CraftingComponent)
	{
		CraftingComponent->OnCraftingDataChanged.RemoveDynamic(
			this,
			&USkillUpgradePanel::HandleCraftingDataChanged);
		CraftingComponent->OnCraftingResult.RemoveDynamic(
			this,
			&USkillUpgradePanel::HandleCraftingResult);
	}
}

void USkillUpgradePanel::RefreshSelectedSkillPresentation()
{
	FText SkillName = FText::GetEmpty();
	UTexture2D* SkillIcon = nullptr;
	if (bHasSelectedSkill && SkillComponent)
	{
		const FGameplayTag SkillItemTag = GetSelectedSkillItemTag();
		if (UWorld* World = GetWorld())
		{
			if (UItemSubsystem* Items = World->GetSubsystem<UItemSubsystem>())
			{
				SkillName = Items->GetItemName(SkillItemTag);
				SkillIcon = Items->GetIcon2D(SkillItemTag).LoadSynchronous();
			}
		}
	}

	if (SkillNameText)
	{
		SkillNameText->SetText(SkillName);
		SkillNameText->SetVisibility(
			SkillName.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (ResultSkillIconImage)
	{
		ResultSkillIconImage->SetBrushFromTexture(SkillIcon, true);
		ResultSkillIconImage->SetVisibility(
			SkillIcon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
}

void USkillUpgradePanel::RefreshCraftingState()
{
	RefreshSelectedSkillPresentation();
	ClearIngredientEntries();
	SelectedRecipeId = NAME_None;
	SelectedRecipeHeader = FCraftingListEntry();
	SetCraftButtonAvailable(false);

	if (!bHasSelectedSkill || !SkillComponent || !CraftingComponent)
	{
		SetCraftingState(ESkillCraftingUIState::NoSelection);
		RefreshLockState();
		return;
	}
	if (IsSelectedSkillUnlocked())
	{
		SetCraftingState(ESkillCraftingUIState::Complete);
		RefreshLockState();
		return;
	}
	if (!IsSelectedSkillUnlockConditionMet())
	{
		SetCraftingState(ESkillCraftingUIState::Locked);
		RefreshLockState();
		return;
	}

	FCraftingListQuery Query;
	Query.bIncludeLocked = true;
	Query.bIncludeDisabled = false;
	Query.ResultItemTag = GetSelectedSkillItemTag();
	const TArray<FCraftingListEntry> MatchingRecipes = CraftingComponent->GetCraftableList(Query);
	if (MatchingRecipes.Num() != 1)
	{
		SetCraftingState(ESkillCraftingUIState::Error);
		RefreshLockState();
		return;
	}

	SelectedRecipeHeader = MatchingRecipes[0];
	SelectedRecipeId = SelectedRecipeHeader.RecipeId;
	FCraftingDetailsView Details;
	if (!CraftingComponent->GetCraftingDetails(SelectedRecipeId, 1, Details))
	{
		SetCraftingState(ESkillCraftingUIState::Error);
		RefreshLockState();
		return;
	}
	ApplyCraftingDetails(Details);
}

void USkillUpgradePanel::ApplyCraftingDetails(const FCraftingDetailsView& Details)
{
	ClearIngredientEntries();

	TArray<FCraftingIngredientView> DisplayIngredients;
	if (Details.bHasRequiredRecipeItem)
	{
		DisplayIngredients.Add(Details.RequiredRecipeItem);
	}
	DisplayIngredients.Append(Details.Ingredients);

	const bool bSupportedIngredientCount =
		DisplayIngredients.Num() <= SkillCraftingPanel::IngredientSlotCount;
	const bool bAvailable = Details.Availability == ECraftingAvailability::Available
		&& bSupportedIngredientCount;
	const bool bAwaitingIngredients =
		(Details.Availability == ECraftingAvailability::MissingIngredients
			|| Details.Availability == ECraftingAvailability::MissingRecipe)
		&& bSupportedIngredientCount;
	SetCraftingState(bAvailable
		? ESkillCraftingUIState::Ready
		: bAwaitingIngredients
			? ESkillCraftingUIState::AwaitingIngredients
			: ESkillCraftingUIState::Error);

	TArray<UPanelWidget*> DirectionSlots = {
		IngredientSlot1.Get(),
		IngredientSlot2.Get(),
		IngredientSlot3.Get()};
	TArray<UCraftingIngredientEntryWidget*> IngredientEntries = {
		IngredientEntry1.Get(),
		IngredientEntry2.Get(),
		IngredientEntry3.Get()};
	if (bAvailable || bAwaitingIngredients)
	{
		const int32 IngredientCount = FMath::Min(DisplayIngredients.Num(), SkillCraftingPanel::IngredientSlotCount);
		for (int32 Index = 0; Index < IngredientCount; ++Index)
		{
			UPanelWidget* TargetSlot = DirectionSlots[Index];
			UCraftingIngredientEntryWidget* Entry = IngredientEntries[Index];
			if (!Entry)
			{
				continue;
			}
			Entry->SetupFromIngredient(DisplayIngredients[Index]);
			Entry->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			if (TargetSlot)
			{
				TargetSlot->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			}
			SpawnedIngredientEntries.Add(Entry);
		}
	}
	SetCraftButtonAvailable(bAvailable && bPanelActive && !bCraftRequestPending);
	RefreshLockState();
}

void USkillUpgradePanel::ClearIngredientEntries()
{
	ResetIngredientRenderTransforms();
	SpawnedIngredientEntries.Reset();
	for (UCraftingIngredientEntryWidget* IngredientEntry : {
		IngredientEntry1.Get(), IngredientEntry2.Get(), IngredientEntry3.Get()})
	{
		if (IngredientEntry)
		{
			IngredientEntry->SetVisibility(ESlateVisibility::Hidden);
		}
	}
	for (UPanelWidget* IngredientHost : {
		IngredientSlot1.Get(), IngredientSlot2.Get(), IngredientSlot3.Get()})
	{
		if (IngredientHost)
		{
			IngredientHost->SetVisibility(ESlateVisibility::Hidden);
		}
	}
}

void USkillUpgradePanel::SetCraftButtonAvailable(bool bAvailable)
{
	if (CraftButton)
	{
		CraftButton->SetIsEnabled(bAvailable);
		CraftButton->SetVisibility(
			CraftingState == ESkillCraftingUIState::AwaitingIngredients
				|| CraftingState == ESkillCraftingUIState::Ready
				|| CraftingState == ESkillCraftingUIState::RequestPending
				? ESlateVisibility::Visible
				: ESlateVisibility::Collapsed);
	}
}

void USkillUpgradePanel::SetCraftingState(ESkillCraftingUIState NewState)
{
	CraftingState = NewState;
	SetCraftButtonAvailable(
		NewState == ESkillCraftingUIState::Ready && bPanelActive && !bCraftRequestPending);
}

void USkillUpgradePanel::HandleCraftButtonClicked()
{
	if (!bPanelActive || !CraftingComponent || SelectedRecipeId.IsNone()
		|| bCraftRequestPending || IsSelectedSkillUnlocked())
	{
		return;
	}

	FCraftingDetailsView Details;
	if (!CraftingComponent->GetCraftingDetails(SelectedRecipeId, 1, Details)
		|| Details.Availability != ECraftingAvailability::Available)
	{
		RefreshCraftingState();
		return;
	}

	FCraftingRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.RecipeId = SelectedRecipeId;
	Request.CraftCount = 1;
	Request.Output.Type = ECraftingOutputType::SkillUnlock;
	PendingRequestId = Request.RequestId;
	bCraftRequestPending = true;
	SetCraftingState(ESkillCraftingUIState::RequestPending);
	CraftingComponent->RequestCraft(Request);
}

void USkillUpgradePanel::HandleCraftingResult(const FCraftingResult& Result)
{
	if (!bCraftRequestPending || Result.RequestId != PendingRequestId)
	{
		return;
	}
	bCraftRequestPending = false;
	PendingRequestId.Invalidate();
	if (Result.Reason == ECraftingFailureReason::Success)
	{
		SetCraftingState(ESkillCraftingUIState::Animating);
		BeginConvergenceAnimation();
		return;
	}

	SetCraftingState(ESkillCraftingUIState::Error);
	RefreshLockState();
}

void USkillUpgradePanel::BeginConvergenceAnimation()
{
	IngredientStartOffsets.Reset();
	ForceLayoutPrepass();
	if (!CraftingCenterAnchor || SpawnedIngredientEntries.IsEmpty())
	{
		FinishConvergenceAnimation();
		return;
	}

	const FGeometry PanelGeometry = GetCachedGeometry();
	const FGeometry CenterGeometry = CraftingCenterAnchor->GetCachedGeometry();
	const FVector2D CenterAbsolute = CenterGeometry.LocalToAbsolute(CenterGeometry.GetLocalSize() * 0.5f);
	const FVector2D CenterLocal = PanelGeometry.AbsoluteToLocal(CenterAbsolute);
	for (UCraftingIngredientEntryWidget* Entry : SpawnedIngredientEntries)
	{
		const FGeometry EntryGeometry = Entry->GetCachedGeometry();
		const FVector2D EntryAbsolute = EntryGeometry.LocalToAbsolute(EntryGeometry.GetLocalSize() * 0.5f);
		IngredientStartOffsets.Add(PanelGeometry.AbsoluteToLocal(EntryAbsolute) - CenterLocal);
	}
	ConvergenceElapsed = 0.0f;
	bConvergenceAnimating = IngredientStartOffsets.Num() == SpawnedIngredientEntries.Num();
	if (!bConvergenceAnimating)
	{
		FinishConvergenceAnimation();
	}
}

void USkillUpgradePanel::UpdateConvergenceAnimation(float DeltaTime)
{
	ConvergenceElapsed += DeltaTime;
	const float Duration = FMath::Max(ConvergenceDuration, UE_SMALL_NUMBER);
	const float LinearAlpha = FMath::Clamp(ConvergenceElapsed / Duration, 0.0f, 1.0f);
	const float Alpha = FMath::InterpEaseInOut(0.0f, 1.0f, LinearAlpha, 2.0f);
	const float AngleRadians = Alpha * ConvergenceRevolutions * 2.0f * UE_PI;
	const float CosAngle = FMath::Cos(AngleRadians);
	const float SinAngle = FMath::Sin(AngleRadians);
	const float RemainingRadius = 1.0f - Alpha;

	for (int32 Index = 0; Index < SpawnedIngredientEntries.Num(); ++Index)
	{
		UCraftingIngredientEntryWidget* Entry = SpawnedIngredientEntries[Index];
		if (!Entry || !IngredientStartOffsets.IsValidIndex(Index))
		{
			continue;
		}
		const FVector2D StartOffset = IngredientStartOffsets[Index];
		const FVector2D RotatedOffset(
			(StartOffset.X * CosAngle - StartOffset.Y * SinAngle) * RemainingRadius,
			(StartOffset.X * SinAngle + StartOffset.Y * CosAngle) * RemainingRadius);
		Entry->SetRenderTranslation(RotatedOffset - StartOffset);
		Entry->SetRenderTransformAngle(Alpha * ConvergenceRevolutions * 360.0f);
	}

	if (LinearAlpha >= 1.0f)
	{
		FinishConvergenceAnimation();
	}
}

void USkillUpgradePanel::FinishConvergenceAnimation()
{
	bConvergenceAnimating = false;
	ResetIngredientRenderTransforms();
	SetCraftingState(ESkillCraftingUIState::Complete);
	if (SkillCraftingCompleteWidget && WidgetSwitcher_SkillCraftingState)
	{
		SkillCraftingCompleteWidget->ShowCraftedItem(SelectedRecipeHeader);
		WidgetSwitcher_SkillCraftingState->SetActiveWidgetIndex(SkillCraftingPanel::CompletePageIndex);
	}
}

void USkillUpgradePanel::ResetIngredientRenderTransforms()
{
	for (UCraftingIngredientEntryWidget* Entry : SpawnedIngredientEntries)
	{
		if (Entry)
		{
			Entry->SetRenderTranslation(FVector2D::ZeroVector);
			Entry->SetRenderTransformAngle(0.0f);
		}
	}
	IngredientStartOffsets.Reset();
}

void USkillUpgradePanel::HandleCraftingCompleteDismissed()
{
	if (WidgetSwitcher_SkillCraftingState)
	{
		WidgetSwitcher_SkillCraftingState->SetActiveWidgetIndex(SkillCraftingPanel::RecipePageIndex);
	}
	RefreshCraftingState();
}

FGameplayTag USkillUpgradePanel::GetSelectedSkillTag() const
{
	switch (SelectedSkill)
	{
	case ESkillUpgradeSelection::GravityVortex:
		return GameplayAbility_Skill_GravityVortex;
	case ESkillUpgradeSelection::WaterBomb:
		return GameplayAbility_Skill_WaterBomb;
	case ESkillUpgradeSelection::Bombardment:
		return GameplayAbility_Skill_Bombardment;
	default:
		return FGameplayTag();
	}
}

FGameplayTag USkillUpgradePanel::GetSelectedSkillItemTag() const
{
	return SkillComponent ? SkillComponent->GetSkillItemTag(GetSelectedSkillTag()) : FGameplayTag();
}

FText USkillUpgradePanel::GetSelectedUnlockConditionText() const
{
	switch (SelectedSkill)
	{
	case ESkillUpgradeSelection::GravityVortex:
		return GravityVortexUnlockConditionText;
	case ESkillUpgradeSelection::WaterBomb:
		return WaterBombUnlockConditionText;
	case ESkillUpgradeSelection::Bombardment:
		return BombardmentUnlockConditionText;
	default:
		return FText::GetEmpty();
	}
}

void USkillUpgradePanel::HandleSkillChanged(FGameplayTag ChangedSkillTag)
{
	if (bHasSelectedSkill
		&& ChangedSkillTag.MatchesTagExact(GetSelectedSkillTag())
		&& !bCraftRequestPending
		&& !bConvergenceAnimating)
	{
		RefreshCraftingState();
	}
}

void USkillUpgradePanel::HandleCraftingDataChanged()
{
	if (bPanelActive && !bCraftRequestPending && !bConvergenceAnimating)
	{
		RefreshCraftingState();
	}
}
