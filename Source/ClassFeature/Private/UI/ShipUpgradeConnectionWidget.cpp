#include "UI/ShipUpgradeConnectionWidget.h"

#include "Components/Image.h"
#include "Engine/Texture2D.h"

void UShipUpgradeConnectionWidget::ConfigureConnection(bool bInDashed)
{
	if (Image_Line)
	{
		UTexture2D* Texture = bInDashed ? DashedLineTexture : SolidLineTexture;
		if (Texture)
		{
			Image_Line->SetBrushFromTexture(Texture, false);
		}
		Image_Line->SetColorAndOpacity(bInDashed ? DashedLineColor : SolidLineColor);
	}
	BP_OnConnectionStyleApplied(bInDashed);
}
