#include "Recovery/StuckRecoveryWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Recovery/StuckRecoveryComponent.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"

void UStuckRecoveryWidget::SetRecoverySource(UStuckRecoveryComponent* InRecovery, const FText& InKeyLabel)
{
	Recovery = InRecovery;
	KeyLabel = InKeyLabel;
}

void UStuckRecoveryWidget::ShowFailure()
{
	FailureUntil = FPlatformTime::Seconds() + 3.;
}

FText UStuckRecoveryWidget::GetGuideText() const
{
	if (FPlatformTime::Seconds() < FailureUntil)
		return NSLOCTEXT("StuckRecovery", "Failed", "현재 안전하게 탈출할 수 없습니다. 키를 놓고 방향을 바꾼 뒤 다시 시도하세요.");
	return FText::Format(NSLOCTEXT("StuckRecovery", "Guide", "끼임 감지 · 탈출하려면 [{0}]을(를) {1}초간 누르세요."),
		KeyLabel, FText::AsNumber(Recovery ? Recovery->GetHoldDuration() : 3.f));
}

float UStuckRecoveryWidget::GetProgress() const
{
	return Recovery ? Recovery->GetHoldProgress() : 0.f;
}

TSharedRef<SWidget> UStuckRecoveryWidget::RebuildWidget()
{
	if (WidgetTree && WidgetTree->RootWidget) return Super::RebuildWidget();
	return SNew(SBorder).Padding(16.f).BorderBackgroundColor(FLinearColor(0.02f, 0.03f, 0.05f, 0.9f))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
		[
			SNew(STextBlock).Text_UObject(this, &ThisClass::GetGuideText)
			.ColorAndOpacity(FLinearColor::White).WrapTextAt(560.f)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SProgressBar).Percent_Lambda([this]() { return TOptional<float>(GetProgress()); })
		]
	];
}
