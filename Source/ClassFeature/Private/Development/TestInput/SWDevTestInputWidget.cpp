#include "Development/TestInput/SWDevTestInputWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
TSharedRef<SWidget> USWDevTestInputWidget::RebuildWidget()
{
 return SNew(SBorder).Padding(8).BorderBackgroundColor(FLinearColor(0,0,0,0.65f))
 [SAssignNew(Label, STextBlock).Text(FText::FromString(Guide)).ColorAndOpacity(FLinearColor::White)];
}
void USWDevTestInputWidget::SetGuide(const FString& Text) { Guide = Text; if (Label) Label->SetText(FText::FromString(Guide)); }
void USWDevTestInputWidget::SetResult(const FString& Text) { Result = Text; ResultUntil = FPlatformTime::Seconds()+4; }
void USWDevTestInputWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
 Super::NativeTick(Geometry, DeltaTime);
 if (Label) Label->SetText(FText::FromString(Guide + (FPlatformTime::Seconds()<ResultUntil ? TEXT("\n")+Result : FString())));
}
