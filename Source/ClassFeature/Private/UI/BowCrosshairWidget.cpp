// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/BowCrosshairWidget.h"

#include "Rendering/DrawElements.h"

UBowCrosshairWidget::UBowCrosshairWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

// 활 장착 확인하는 함수, 활이 장착되었으면, 십자선 그림
void UBowCrosshairWidget::SetBowEquipped(bool bNewBowEquipped)
{
	if (bBowEquipped == bNewBowEquipped)
	{
		return;
	}

	bBowEquipped = bNewBowEquipped;
	Invalidate(EInvalidateWidgetReason::Paint);
}

// 조준 상태를 확인하는 함수
void UBowCrosshairWidget::SetBowAiming(bool bNewAiming)
{
	if (bBowAiming == bNewAiming)
	{
		return;
	}

	bBowAiming = bNewAiming;
	Invalidate(EInvalidateWidgetReason::Paint);
}

// 차징 값을 저장
void UBowCrosshairWidget::SetDrawAlpha(float NewDrawAlpha)
{
	const float ClampedDrawAlpha = FMath::Clamp(NewDrawAlpha, 0.0f, 1.0f);
	if (FMath::IsNearlyEqual(DrawAlpha, ClampedDrawAlpha))
	{
		return;
	}

	DrawAlpha = ClampedDrawAlpha;
	Invalidate(EInvalidateWidgetReason::Paint);
}

// UI를 그리는 함수
int32 UBowCrosshairWidget::NativePaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	// 부모 함수부터 그린 후, 부모에서 마지막으로 사용한  layerID 반환하여 저장
	const int32 PaintedLayerId = Super::NativePaint(
		Args,
		AllottedGeometry,
		MyCullingRect,
		OutDrawElements,
		LayerId,
		InWidgetStyle,
		bParentEnabled);

	//위젯이 사용할 영역 (여기서는 전체 화면)
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const FVector2D Center = LocalSize * 0.5f;
	// 화면 크기에 따라 위젯의 Scale 조정
	const float Scale = GetResponsiveScale(LocalSize);
	
	// 활이 장착되어 있을 때만 그림
	if (bBowEquipped || bBowAiming)
	{
		const float ScaledLineLength = LineLength * Scale;
		// RestGap, ChargedGap 사이를 DrawAlpha값에 따라서 중앙 공간으로 모이는 정도를 보간
		const float ScaledGap = FMath::Lerp(RestGap, ChargedGap, DrawAlpha) * Scale;
		const float ScaledThickness = LineThickness * Scale;
		// 선이 뻗을 대각선 방향
		const FVector2D Directions[4] =
		{
			FVector2D(-1.0f, -1.0f).GetSafeNormal(),
			FVector2D(1.0f, -1.0f).GetSafeNormal(),
			FVector2D(-1.0f, 1.0f).GetSafeNormal(),
			FVector2D(1.0f, 1.0f).GetSafeNormal()
		};

		// 방향마다 선 하나씩 그리기
		for (const FVector2D& Direction : Directions)
		{
			TArray<FVector2D> Points;
			Points.Add(Center + Direction * ScaledGap); // 각 선의 시작점 
			Points.Add(Center + Direction * (ScaledGap + ScaledLineLength)); // 각 선의 끝점

			// 시작, 끝 점을 이어서 선을 만듦
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				PaintedLayerId + 2,
				AllottedGeometry.ToPaintGeometry(),
				Points,
				ESlateDrawEffect::None,
				LineColor,
				true,
				ScaledThickness);
		}
	}

	return PaintedLayerId + 1;
}

float UBowCrosshairWidget::GetResponsiveScale(const FVector2D& LocalSize) const
{
	// 해상도에서 짧은 부분 저장
	const float ShortSide = FMath::Min(LocalSize.X, LocalSize.Y);
	if (ReferenceShortSide <= 0.0f || ShortSide <= 0.0f)
	{
		return 1.0f;
	}

	// 짧은 길이 / 기준 길이 비율을 clamp
	return FMath::Clamp(ShortSide / ReferenceShortSide, MinScale, MaxScale);
}
