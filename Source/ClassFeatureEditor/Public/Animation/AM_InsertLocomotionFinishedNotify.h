// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AnimationModifier.h"
#include "Animation/AN_LocomotionFinished.h"
#include "AM_InsertLocomotionFinishedNotify.generated.h"

/**
 * Inserts Locomotion Finished Notifies automatically based on duration/ratio properties.
 */
UCLASS()
class CLASSFEATUREEDITOR_API UAM_InsertLocomotionFinishedNotify : public UAnimationModifier
{
	GENERATED_BODY()

public:
	UAM_InsertLocomotionFinishedNotify();

	virtual void OnApply_Implementation(UAnimSequence* AnimationSequence) override;

protected:
	UPROPERTY(EditAnywhere, Category = "Locomotion Finished")
	ELocomotionNotifyType NotifyType;

	UPROPERTY(EditAnywhere, Category = "Locomotion Finished")
	float NotifyTimeRatio = 0.3f;

	UPROPERTY(EditAnywhere, Category = "Locomotion Finished")
	float MinNotifyTime = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Locomotion Finished")
	float MaxNotifyTime = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Locomotion Finished")
	FName NotifyTrackName = TEXT("LocomotionFinished");

	UPROPERTY(EditAnywhere, Category = "Locomotion Finished")
	bool bSkipIfNotifyAlreadyExists = true;
};
