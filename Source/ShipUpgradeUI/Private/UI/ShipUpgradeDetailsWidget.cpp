#include "UI/ShipUpgradeDetailsWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Throbber.h"
#include "Components/VerticalBox.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/Texture2D.h"
#include "UI/ShipUpgradeMaterialRowWidget.h"
#include "UI/ShipUpgradeStatChangeRowWidget.h"
#include "Upgrade/ShipUpgradeComponent.h"

void UShipUpgradeDetailsWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (Button_Activate)
	{
		Button_Activate->OnClicked.AddUniqueDynamic(this, &UShipUpgradeDetailsWidget::HandleActivateClicked);
	}
	ClearNode();
}

void UShipUpgradeDetailsWidget::NativeDestruct()
{
	if (Button_Activate)
	{
		Button_Activate->OnClicked.RemoveDynamic(this, &UShipUpgradeDetailsWidget::HandleActivateClicked);
	}
	if (IconLoadHandle.IsValid())
	{
		IconLoadHandle->CancelHandle();
		IconLoadHandle.Reset();
	}
	ActivationRequestedDelegate.Clear();
	PreviewRequestedDelegate.Clear();
	Super::NativeDestruct();
}

void UShipUpgradeDetailsWidget::SetUpgradeComponent(UShipUpgradeComponent* InComponent)
{
	UpgradeComponent = InComponent;
	RefreshActivationState();
}

void UShipUpgradeDetailsWidget::ShowNode(const FShipUpgradeNodeView& InView, bool bRequestPending)
{
	SelectedView = InView;
	bHasSelection = true;
	bIsRequestPending = bRequestPending;
	/* UE_LOG(LogTemp, Log,
		TEXT("[ShipUpgradeUI] Details rendering started. NodeId=%s State=%s Stats=%d Materials=%d Pending=%s"),
		*SelectedView.NodeId.ToString(),
		*UEnum::GetValueAsString(SelectedView.State),
		SelectedView.StatChanges.Num(),
		SelectedView.MaterialCosts.Num(),
		bIsRequestPending ? TEXT("YES") : TEXT("NO")); */

	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (Text_NodeName)
	{
		Text_NodeName->SetText(SelectedView.DisplayName);
	}
	if (Text_Description)
	{
		Text_Description->SetText(SelectedView.Description);
	}
	if (Text_UnavailableReason)
	{
		Text_UnavailableReason->SetText(SelectedView.UnavailableReason);
		Text_UnavailableReason->SetVisibility(
			SelectedView.UnavailableReason.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	const bool bUse3D = SelectedView.PreviewType != EShipUpgradePreviewType::Icon2D
		&& !SelectedView.PreviewActorClass.IsNull();
	if (Image_2DPreview)
	{
		Image_2DPreview->SetVisibility(bUse3D ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (Panel_3DPreview)
	{
		Panel_3DPreview->SetVisibility(bUse3D ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	RebuildStatChanges();
	RebuildMaterialCosts();
	RequestPreviewIconLoad();
	RefreshActivationState();
	PreviewRequestedDelegate.Broadcast(SelectedView);
}

void UShipUpgradeDetailsWidget::ClearNode()
{
	bHasSelection = false;
	bIsRequestPending = false;
	if (VerticalBox_StatChanges)
	{
		VerticalBox_StatChanges->ClearChildren();
	}
	if (VerticalBox_MaterialCosts)
	{
		VerticalBox_MaterialCosts->ClearChildren();
	}
	if (Button_Activate)
	{
		Button_Activate->SetIsEnabled(false);
	}
	if (Throbber_Requesting)
	{
		Throbber_Requesting->SetVisibility(ESlateVisibility::Collapsed);
	}
	SetVisibility(ESlateVisibility::Collapsed);
}

void UShipUpgradeDetailsWidget::SetRequestPending(bool bPending)
{
	bIsRequestPending = bPending;
	RefreshActivationState();
}

void UShipUpgradeDetailsWidget::HandleActivateClicked()
{
	if (!bHasSelection || bIsRequestPending || SelectedView.NodeId.IsNone())
	{
		return;
	}

	FText Reason;
	if (!UpgradeComponent || !UpgradeComponent->CanActivateNode(SelectedView.NodeId, Reason))
	{
		if (Text_UnavailableReason)
		{
			Text_UnavailableReason->SetText(Reason);
			Text_UnavailableReason->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		return;
	}

	SetRequestPending(true);
	ActivationRequestedDelegate.Broadcast(SelectedView.NodeId);
}

void UShipUpgradeDetailsWidget::RebuildStatChanges()
{
	if (!VerticalBox_StatChanges)
	{
		/* UE_LOG(LogTemp, Warning,
			TEXT("[ShipUpgradeUI] Stat rows skipped: VerticalBox_StatChanges is not bound.")); */
		return;
	}
	VerticalBox_StatChanges->ClearChildren();
	if (!StatChangeRowClass)
	{
		/* UE_LOG(LogTemp, Warning,
			TEXT("[ShipUpgradeUI] Stat rows skipped: StatChangeRowClass is None.")); */
		return;
	}

	for (const FShipStatChangeView& Change : SelectedView.StatChanges)
	{
		if (UShipUpgradeStatChangeRowWidget* Row = CreateWidget<UShipUpgradeStatChangeRowWidget>(
			GetOwningPlayer(), StatChangeRowClass))
		{
			Row->ApplyStatChange(Change);
			VerticalBox_StatChanges->AddChildToVerticalBox(Row);
		}
	}
	/* UE_LOG(LogTemp, Log,
		TEXT("[ShipUpgradeUI] Stat rows rendered. Requested=%d Created=%d"),
		SelectedView.StatChanges.Num(),
		VerticalBox_StatChanges->GetChildrenCount()); */
}

void UShipUpgradeDetailsWidget::RebuildMaterialCosts()
{
	if (!VerticalBox_MaterialCosts)
	{
		/* UE_LOG(LogTemp, Warning,
			TEXT("[ShipUpgradeUI] Material rows skipped: VerticalBox_MaterialCosts is not bound.")); */
		return;
	}
	VerticalBox_MaterialCosts->ClearChildren();
	if (!MaterialRowClass)
	{
		/* UE_LOG(LogTemp, Warning,
			TEXT("[ShipUpgradeUI] Material rows skipped: MaterialRowClass is None.")); */
		return;
	}

	for (const FShipUpgradeMaterialView& Material : SelectedView.MaterialCosts)
	{
		if (UShipUpgradeMaterialRowWidget* Row = CreateWidget<UShipUpgradeMaterialRowWidget>(
			GetOwningPlayer(), MaterialRowClass))
		{
			Row->ApplyMaterialView(Material);
			VerticalBox_MaterialCosts->AddChildToVerticalBox(Row);
		}
	}
	/* UE_LOG(LogTemp, Log,
		TEXT("[ShipUpgradeUI] Material rows rendered. Requested=%d Created=%d"),
		SelectedView.MaterialCosts.Num(),
		VerticalBox_MaterialCosts->GetChildrenCount()); */
}

void UShipUpgradeDetailsWidget::RequestPreviewIconLoad()
{
	if (IconLoadHandle.IsValid())
	{
		IconLoadHandle->CancelHandle();
		IconLoadHandle.Reset();
	}

	PendingIconPath = SelectedView.Icon.ToSoftObjectPath();
	if (!Image_2DPreview || Image_2DPreview->GetVisibility() == ESlateVisibility::Collapsed || !PendingIconPath.IsValid())
	{
		if (Image_2DPreview && !PendingIconPath.IsValid())
		{
			Image_2DPreview->SetVisibility(ESlateVisibility::Hidden);
		}
		return;
	}

	if (UTexture2D* LoadedTexture = SelectedView.Icon.Get())
	{
		Image_2DPreview->SetBrushFromTexture(LoadedTexture, true);
		return;
	}

	const FSoftObjectPath RequestedPath = PendingIconPath;
	TWeakObjectPtr<UShipUpgradeDetailsWidget> WeakThis(this);
	IconLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		RequestedPath,
		FStreamableDelegate::CreateLambda([WeakThis, RequestedPath]()
		{
			UShipUpgradeDetailsWidget* Widget = WeakThis.Get();
			if (!Widget || Widget->PendingIconPath != RequestedPath || !Widget->Image_2DPreview)
			{
				return;
			}
			if (UTexture2D* LoadedTexture = Cast<UTexture2D>(RequestedPath.ResolveObject()))
			{
				Widget->Image_2DPreview->SetBrushFromTexture(LoadedTexture, true);
			}
		}));
}

void UShipUpgradeDetailsWidget::RefreshActivationState()
{
	if (!bHasSelection)
	{
		return;
	}

	FText Reason;
	const bool bIsActive = SelectedView.State == EShipUpgradeNodeState::Active;
	const bool bCanActivate = !bIsRequestPending
		&& UpgradeComponent
		&& UpgradeComponent->CanActivateNode(SelectedView.NodeId, Reason);

	if (Button_Activate)
	{
		Button_Activate->SetIsEnabled(bCanActivate);
	}
	if (Throbber_Requesting)
	{
		Throbber_Requesting->SetVisibility(
			bIsRequestPending ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (Text_Activate)
	{
		Text_Activate->SetText(
			SelectedView.State == EShipUpgradeNodeState::Active
				? NSLOCTEXT("ShipUpgrade", "AlreadyActivatedButton", "활성화 완료")
				: NSLOCTEXT("ShipUpgrade", "ActivateButton", "활성화"));
	}
	if (!bIsActive && !bCanActivate && !Reason.IsEmpty() && Text_UnavailableReason)
	{
		Text_UnavailableReason->SetText(Reason);
		Text_UnavailableReason->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	BP_OnDetailsStateApplied(SelectedView.State, bCanActivate, bIsRequestPending);
}
