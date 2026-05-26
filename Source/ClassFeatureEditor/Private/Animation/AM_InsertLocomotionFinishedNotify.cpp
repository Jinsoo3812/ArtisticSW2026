#include "Animation/AM_InsertLocomotionFinishedNotify.h"
#include "Animation/AN_LocomotionFinished.h"
#include "Animation/AnimSequence.h"
#include "AnimationBlueprintLibrary.h"

UAM_InsertLocomotionFinishedNotify::UAM_InsertLocomotionFinishedNotify()
{
	NotifyType = ELocomotionNotifyType::StartFinished;
}

void UAM_InsertLocomotionFinishedNotify::OnApply_Implementation(UAnimSequence* AnimationSequence)
{
	if (!AnimationSequence)
	{
		return;
	}

	UClass* TargetNotifyClass = UAN_LocomotionFinished::StaticClass();

	if (bSkipIfNotifyAlreadyExists)
	{
		for (const FAnimNotifyEvent& NotifyEvent : AnimationSequence->Notifies)
		{
			if (NotifyEvent.Notify && NotifyEvent.Notify->IsA<UAN_LocomotionFinished>())
			{
				UAN_LocomotionFinished* ExistingNotify = Cast<UAN_LocomotionFinished>(NotifyEvent.Notify);
				if (ExistingNotify && ExistingNotify->NotifyType == NotifyType)
				{
					return;
				}
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

	UAnimNotify* AddedNotify = UAnimationBlueprintLibrary::AddAnimationNotifyEvent(
		AnimationSequence,
		NotifyTrackName,
		NotifyTime,
		TargetNotifyClass
	);

	if (UAN_LocomotionFinished* LocFinishedNotify = Cast<UAN_LocomotionFinished>(AddedNotify))
	{
		LocFinishedNotify->NotifyType = NotifyType;
		// Set corresponding NotifyColor
		if (NotifyType == ELocomotionNotifyType::StartFinished)
		{
			LocFinishedNotify->NotifyColor = FColor(0, 255, 128, 255);
		}
		else
		{
			LocFinishedNotify->NotifyColor = FColor(255, 128, 0, 255);
		}
	}
}
