// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CrosshairWidget.generated.h"

/**
 * Bow guide lines and water-cannon indicator. The normal center dot is drawn by the HUD.
 */
UCLASS()
class CLASSFEATURE_API UCrosshairWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UCrosshairWidget(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category = "Crosshair")
	void SetWaterBombMode(bool bNewWaterBombMode);

	UFUNCTION(BlueprintPure, Category = "Crosshair")
	bool IsWaterBombMode() const { return bWaterBombMode; }

	UFUNCTION(BlueprintCallable, Category = "Crosshair")
	void SetBowEquipped(bool bNewBowEquipped);

	UFUNCTION(BlueprintCallable, Category = "Crosshair")
	void SetBowAiming(bool bNewAiming);

	UFUNCTION(BlueprintCallable, Category = "Crosshair")
	void SetDrawAlpha(float NewDrawAlpha);

	UFUNCTION(BlueprintPure, Category = "Crosshair")
	bool IsBowEquipped() const { return bBowEquipped; }

	UFUNCTION(BlueprintPure, Category = "Crosshair")
	bool IsBowAiming() const { return bBowAiming; }

	UFUNCTION(BlueprintPure, Category = "Crosshair")
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Lines")
	FLinearColor LineColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Lines", meta = (ClampMin = "0.0"))
	float LineLength = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Lines", meta = (ClampMin = "0.0"))
	float RestGap = 24.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Lines", meta = (ClampMin = "0.0"))
	float ChargedGap = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Lines", meta = (ClampMin = "0.0"))
	float LineThickness = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Responsive", meta = (ClampMin = "1.0"))
	float ReferenceShortSide = 1080.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Responsive", meta = (ClampMin = "0.01"))
	float MinScale = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Responsive", meta = (ClampMin = "0.01"))
	float MaxScale = 1.5f;

	UPROPERTY(BlueprintReadOnly, Category = "Crosshair|State")
	bool bBowEquipped = false;

	UPROPERTY(BlueprintReadOnly, Category = "Crosshair|State")
	bool bBowAiming = false;

	UPROPERTY(BlueprintReadOnly, Category = "Crosshair|State", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DrawAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Water Cannon")
	FLinearColor WaterCannonColor = FLinearColor::Blue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Water Cannon", meta = (ClampMin = "0.0"))
	float WaterCannonDotSize = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Water Cannon", meta = (ClampMin = "0.0"))
	float WaterCannonRingDiameter = 32.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair|Water Cannon", meta = (ClampMin = "0.0"))
	float WaterCannonRingThickness = 2.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Crosshair|State")
	bool bWaterBombMode = false;
};
