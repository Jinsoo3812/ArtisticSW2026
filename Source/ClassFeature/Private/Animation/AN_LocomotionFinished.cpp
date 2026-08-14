// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/AN_LocomotionFinished.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Animation/LocomotionAnimStateComponent.h"

UAN_LocomotionFinished::UAN_LocomotionFinished()
{
	NotifyType = ELocomotionNotifyType::StartFinished;
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(0, 255, 128, 255);
#endif
}

void UAN_LocomotionFinished::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	// State transitions are deliberately independent of animation Notifies.
	// These clips are shared with Pose Search and their old "Stop Finished"
	// marker could prematurely convert Landing -> Locomotion (around 0.19 s).
	// Completion is now driven by the State Controller's selected asset time.
}

FString UAN_LocomotionFinished::GetNotifyName_Implementation() const
{
	if (NotifyType == ELocomotionNotifyType::StartFinished)
	{
		return FString(TEXT("Start Finished"));
	}
	else if (NotifyType == ELocomotionNotifyType::StopFinished)
	{
		return FString(TEXT("Stop Finished"));
	}
	return FString(TEXT("Locomotion Finished"));
}

#if WITH_EDITOR
void UAN_LocomotionFinished::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (NotifyType == ELocomotionNotifyType::StartFinished)
	{
		NotifyColor = FColor(0, 255, 128, 255);
	}
	else if (NotifyType == ELocomotionNotifyType::StopFinished)
	{
		NotifyColor = FColor(255, 128, 0, 255);
	}
}
#endif
