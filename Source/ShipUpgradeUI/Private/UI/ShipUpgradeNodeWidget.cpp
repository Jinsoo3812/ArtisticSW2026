#include "UI/ShipUpgradeNodeWidget.h"

#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/Texture2D.h"

void UShipUpgradeNodeWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (Button_Node)
	{
		Button_Node->OnClicked.AddUniqueDynamic(this, &UShipUpgradeNodeWidget::HandleClicked);
		Button_Node->OnHovered.AddUniqueDynamic(this, &UShipUpgradeNodeWidget::HandleHovered);
		Button_Node->OnUnhovered.AddUniqueDynamic(this, &UShipUpgradeNodeWidget::HandleUnhovered);
	}

	RefreshBuiltInVisuals();
}

void UShipUpgradeNodeWidget::NativeDestruct()
{
	if (Button_Node)
	{
		Button_Node->OnClicked.RemoveDynamic(this, &UShipUpgradeNodeWidget::HandleClicked);
		Button_Node->OnHovered.RemoveDynamic(this, &UShipUpgradeNodeWidget::HandleHovered);
		Button_Node->OnUnhovered.RemoveDynamic(this, &UShipUpgradeNodeWidget::HandleUnhovered);
	}

	if (IconLoadHandle.IsValid())
	{
		IconLoadHandle->CancelHandle();
		IconLoadHandle.Reset();
	}
	NodeSelectedDelegate.Clear();

	Super::NativeDestruct();
}

void UShipUpgradeNodeWidget::ApplyNodeView(const FShipUpgradeNodeView& InView)
{
	NodeView = InView;
	if (Text_Name)
	{
		Text_Name->SetText(NodeView.DisplayName);
	}

	RefreshBuiltInVisuals();
	RequestIconLoad();
}

void UShipUpgradeNodeWidget::SetSelected(bool bInSelected)
{
	if (bSelected == bInSelected)
	{
		return;
	}
	bSelected = bInSelected;
	RefreshBuiltInVisuals();
	RefreshScale(false);
}

void UShipUpgradeNodeWidget::HandleClicked()
{
	if (!NodeView.NodeId.IsNone() && NodeView.State != EShipUpgradeNodeState::Locked)
	{
		NodeSelectedDelegate.Broadcast(NodeView.NodeId);
	}
}

void UShipUpgradeNodeWidget::HandleHovered()
{
	RefreshScale(true);
	BP_OnHoverChanged(true);
}

void UShipUpgradeNodeWidget::HandleUnhovered()
{
	RefreshScale(false);
	BP_OnHoverChanged(false);
}

void UShipUpgradeNodeWidget::RefreshBuiltInVisuals()
{
	const bool bLocked = NodeView.State == EShipUpgradeNodeState::Locked;
	const bool bActive = NodeView.State == EShipUpgradeNodeState::Active;

	if (Image_Lock)
	{
		Image_Lock->SetVisibility(bLocked ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (Image_Check)
	{
		// Activation is represented by the node background color. The optional
		// legacy check image stays hidden and can safely be deleted from the WBP.
		Image_Check->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (Image_Icon)
	{
		Image_Icon->SetVisibility(
			bLocked ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		// State color belongs to the node background, never to the icon.
		Image_Icon->SetOpacity(1.0f);
	}
	if (Text_Name)
	{
		Text_Name->SetVisibility(
			bLocked ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	FLinearColor StateColor = LockedGlowColor;
	if (bActive)
	{
		StateColor = ActiveGlowColor;
	}
	else if (!bLocked && !NodeView.bHasEnoughMaterials)
	{
		StateColor = InsufficientMaterialsGlowColor;
	}
	else if (!bLocked)
	{
		StateColor = AvailableGlowColor;
	}
	if (Button_Node)
	{
		// BackgroundColor only multiplies the Button style brushes. Unlike
		// ColorAndOpacity/Foreground it does not tint the icon or label.
		Button_Node->SetBackgroundColor(StateColor);
	}
	if (Border_StateGlow)
	{
		// Legacy inner state layer caused a second, inset colored rectangle.
		// It is optional and may be removed from the Widget Blueprint.
		Border_StateGlow->SetVisibility(ESlateVisibility::Collapsed);
	}

	BP_OnVisualStateApplied(NodeView.State, bSelected);
}

void UShipUpgradeNodeWidget::RefreshScale(bool bHovered)
{
	const float Scale = bHovered ? HoverScale : (bSelected ? SelectedScale : 1.0f);
	SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	SetRenderScale(FVector2D(Scale));
}

void UShipUpgradeNodeWidget::RequestIconLoad()
{
	if (IconLoadHandle.IsValid())
	{
		IconLoadHandle->CancelHandle();
		IconLoadHandle.Reset();
	}

	PendingIconPath = NodeView.Icon.ToSoftObjectPath();
	if (!Image_Icon || !PendingIconPath.IsValid())
	{
		if (Image_Icon)
		{
			Image_Icon->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}

	if (UTexture2D* LoadedTexture = NodeView.Icon.Get())
	{
		Image_Icon->SetBrushFromTexture(LoadedTexture, true);
		Image_Icon->SetVisibility(
			NodeView.State == EShipUpgradeNodeState::Locked
				? ESlateVisibility::Collapsed
				: ESlateVisibility::HitTestInvisible);
		return;
	}

	const FSoftObjectPath RequestedPath = PendingIconPath;
	TWeakObjectPtr<UShipUpgradeNodeWidget> WeakThis(this);
	IconLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		RequestedPath,
		FStreamableDelegate::CreateLambda([WeakThis, RequestedPath]()
		{
			UShipUpgradeNodeWidget* Widget = WeakThis.Get();
			if (!Widget || Widget->PendingIconPath != RequestedPath || !Widget->Image_Icon)
			{
				return;
			}

			if (UTexture2D* LoadedTexture = Cast<UTexture2D>(RequestedPath.ResolveObject()))
			{
				Widget->Image_Icon->SetBrushFromTexture(LoadedTexture, true);
				Widget->Image_Icon->SetVisibility(
					Widget->NodeView.State == EShipUpgradeNodeState::Locked
						? ESlateVisibility::Collapsed
						: ESlateVisibility::HitTestInvisible);
			}
		}));
}
