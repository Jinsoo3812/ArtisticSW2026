// Fill out your copyright notice in the Description page of Project Settings.


#include "Crafter/StarForceWidget.h"

void UStarForceWidget::ReportStarPosition(float CurrentXPosition, float BarLength)
{
	// UI의 구체적인 X좌표를 절대값(오차범위)으로 추상화
	float ErrorMargin = FMath::Abs(CurrentXPosition);

	// CrafterComponent 등 이 위젯을 띄운 주체에게 결과를 알림
	if (OnStarforceAttempted.IsBound())
	{
		OnStarforceAttempted.Broadcast(ErrorMargin, BarLength);
	}
}