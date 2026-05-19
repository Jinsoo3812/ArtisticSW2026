// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/AM_InsertGroundStartFinishedNotify.h"

#include "Animation/AN_GroundStartFinished.h"
#include "Animation/AnimSequence.h"
#include "AnimationBlueprintLibrary.h"

void UAM_InsertGroundStartFinishedNotify::OnApply_Implementation(UAnimSequence* AnimationSequence)
{
	if (!AnimationSequence)
	{
		return;
	}

	if (bSkipIfNotifyAlreadyExists)
	{
		for (const FAnimNotifyEvent& NotifyEvent : AnimationSequence->Notifies)
		{
			if (NotifyEvent.Notify && NotifyEvent.Notify->IsA<UAN_GroundStartFinished>())
			{
				return;
			}
		}
	}

	const float PlayLength = AnimationSequence->GetPlayLength();
	if (PlayLength <= 0.0f)
	{
		return;
	}

	const float ClampedMaxTime = FMath::Clamp(MaxNotifyTime, 0.0f, PlayLength);
	const float ClampedMinTime = FMath::Clamp(MinNotifyTime, 0.0f, ClampedMaxTime);
	const float NotifyTime = FMath::Clamp(PlayLength * NotifyTimeRatio, ClampedMinTime, ClampedMaxTime);

	if (!UAnimationBlueprintLibrary::IsValidAnimNotifyTrackName(AnimationSequence, NotifyTrackName))
	{
		UAnimationBlueprintLibrary::AddAnimationNotifyTrack(
			AnimationSequence,
			NotifyTrackName,
			FLinearColor(0.25f, 0.75f, 1.0f)
		);
	}

	UAnimationBlueprintLibrary::AddAnimationNotifyEvent(
		AnimationSequence,
		NotifyTrackName,
		NotifyTime,
		UAN_GroundStartFinished::StaticClass()
	);
}
