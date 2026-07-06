// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BowCrosshairWidget.generated.h"

/**
 * Bow-only crosshair guide lines. The base center dot is drawn by the player HUD.
 */
UCLASS()
class CLASSFEATURE_API UBowCrosshairWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UBowCrosshairWidget(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category = "Bow Crosshair")
	void SetBowEquipped(bool bNewBowEquipped);

	UFUNCTION(BlueprintCallable, Category = "Bow Crosshair")
	void SetBowAiming(bool bNewAiming);

	UFUNCTION(BlueprintCallable, Category = "Bow Crosshair")
	void SetDrawAlpha(float NewDrawAlpha);

	UFUNCTION(BlueprintPure, Category = "Bow Crosshair")
	bool IsBowEquipped() const { return bBowEquipped; }

	UFUNCTION(BlueprintPure, Category = "Bow Crosshair")
	bool IsBowAiming() const { return bBowAiming; }

	UFUNCTION(BlueprintPure, Category = "Bow Crosshair")
	float GetDrawAlpha() const { return DrawAlpha; }

protected:
	virtual int32 NativePaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

	float GetResponsiveScale(const FVector2D& LocalSize) const;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bow Crosshair|Lines")
	FLinearColor LineColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bow Crosshair|Lines", meta = (ClampMin = "0.0"))
	float LineLength = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bow Crosshair|Lines", meta = (ClampMin = "0.0"))
	float RestGap = 24.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bow Crosshair|Lines", meta = (ClampMin = "0.0"))
	float ChargedGap = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bow Crosshair|Lines", meta = (ClampMin = "0.0"))
	float LineThickness = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bow Crosshair|Responsive", meta = (ClampMin = "1.0"))
	float ReferenceShortSide = 1080.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bow Crosshair|Responsive", meta = (ClampMin = "0.01"))
	float MinScale = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bow Crosshair|Responsive", meta = (ClampMin = "0.01"))
	float MaxScale = 1.5f;

	UPROPERTY(BlueprintReadOnly, Category = "Bow Crosshair|State")
	bool bBowEquipped = false;

	UPROPERTY(BlueprintReadOnly, Category = "Bow Crosshair|State")
	bool bBowAiming = false;

	UPROPERTY(BlueprintReadOnly, Category = "Bow Crosshair|State", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DrawAlpha = 0.0f;
};
