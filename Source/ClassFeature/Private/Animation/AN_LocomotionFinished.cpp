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

	if (!MeshComp) return;

	UAnimInstance* AnimInstance = MeshComp->GetAnimInstance();
	if (!AnimInstance) return;

	APawn* Pawn = AnimInstance->TryGetPawnOwner();
	if (!Pawn) return;

	ULocomotionAnimStateComponent* StateComp = Pawn->FindComponentByClass<ULocomotionAnimStateComponent>();
	if (!StateComp) return;

	if (NotifyType == ELocomotionNotifyType::StartFinished)
	{
		if (StateComp->CurrentState == ELocomotionState::Start)
		{
			StateComp->NotifyStartFinished();
		}
		else if (StateComp->CurrentState == ELocomotionState::InAir)
		{
			StateComp->FinishJumpStart();
		}
	}
	else if (NotifyType == ELocomotionNotifyType::StopFinished)
	{
		if (StateComp->CurrentState == ELocomotionState::Stop)
		{
			StateComp->NotifyStopFinished();
		}
		else if (StateComp->CurrentState == ELocomotionState::Landing)
		{
			StateComp->NotifyLandingFinished();
		}
		else if (StateComp->CurrentState == ELocomotionState::InAir)
		{
			if (StateComp->bIsFallOffStart)
			{
				StateComp->FinishFallOffStart();
			}
		}
	}
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
