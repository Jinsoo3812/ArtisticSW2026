// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AnimationModifier.h"
#include "AM_InsertGroundStartFinishedNotify.generated.h"

/**
 * Adds AN_GroundStartFinished to start animations.
 *
 * Apply this only to Run_Start / Sprint_Start style sequences. Locomotion,
 * Stop, Land, Transition, Pivot, Box, Diamond, and SharpTurn clips should stay
 * out of this modifier pass.
 */
UCLASS()
class CLASSFEATUREEDITOR_API UAM_InsertGroundStartFinishedNotify : public UAnimationModifier
{
	GENERATED_BODY()

public:
	virtual void OnApply_Implementation(UAnimSequence* AnimationSequence) override;

protected:
	UPROPERTY(EditAnywhere, Category = "Ground Start")
	float NotifyTimeRatio = 0.3f;

	UPROPERTY(EditAnywhere, Category = "Ground Start")
	float MinNotifyTime = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Ground Start")
	float MaxNotifyTime = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Ground Start")
	FName NotifyTrackName = TEXT("Start");

	UPROPERTY(EditAnywhere, Category = "Ground Start")
	bool bSkipIfNotifyAlreadyExists = true;
};
