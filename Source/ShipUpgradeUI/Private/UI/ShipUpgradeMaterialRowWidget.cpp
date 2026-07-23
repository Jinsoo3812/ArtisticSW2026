#include "UI/ShipUpgradeMaterialRowWidget.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/Texture2D.h"

void UShipUpgradeMaterialRowWidget::NativeDestruct()
{
	if (IconLoadHandle.IsValid())
	{
		IconLoadHandle->CancelHandle();
		IconLoadHandle.Reset();
	}
	Super::NativeDestruct();
}

void UShipUpgradeMaterialRowWidget::ApplyMaterialView(const FShipUpgradeMaterialView& InView)
{
	MaterialView = InView;

	if (Text_ItemName)
	{
		Text_ItemName->SetText(MaterialView.DisplayName);
	}
	if (Text_Count)
	{
		Text_Count->SetText(FText::Format(
			NSLOCTEXT("ShipUpgrade", "MaterialCountFormat", "{0} / {1}"),
			FText::AsNumber(MaterialView.OwnedQuantity),
			FText::AsNumber(MaterialView.RequiredQuantity)));
		Text_Count->SetColorAndOpacity(MaterialView.bEnough ? EnoughColor : InsufficientColor);
	}

	BP_OnMaterialStateApplied(MaterialView.bEnough);
	RequestIconLoad();
}

void UShipUpgradeMaterialRowWidget::RequestIconLoad()
{
	if (IconLoadHandle.IsValid())
	{
		IconLoadHandle->CancelHandle();
		IconLoadHandle.Reset();
	}

	PendingIconPath = MaterialView.Icon.ToSoftObjectPath();
	if (!Image_ItemIcon || !PendingIconPath.IsValid())
	{
		if (Image_ItemIcon)
		{
			Image_ItemIcon->SetVisibility(ESlateVisibility::Hidden);
		}
		return;
	}

	if (UTexture2D* LoadedTexture = MaterialView.Icon.Get())
	{
		Image_ItemIcon->SetBrushFromTexture(LoadedTexture, true);
		Image_ItemIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
		return;
	}

	const FSoftObjectPath RequestedPath = PendingIconPath;
	TWeakObjectPtr<UShipUpgradeMaterialRowWidget> WeakThis(this);
	IconLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		RequestedPath,
		FStreamableDelegate::CreateLambda([WeakThis, RequestedPath]()
		{
			UShipUpgradeMaterialRowWidget* Widget = WeakThis.Get();
			if (!Widget || Widget->PendingIconPath != RequestedPath || !Widget->Image_ItemIcon)
			{
				return;
			}
			if (UTexture2D* LoadedTexture = Cast<UTexture2D>(RequestedPath.ResolveObject()))
			{
				Widget->Image_ItemIcon->SetBrushFromTexture(LoadedTexture, true);
				Widget->Image_ItemIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
		}));
}
