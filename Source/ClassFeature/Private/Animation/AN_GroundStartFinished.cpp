// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/AN_GroundStartFinished.h"

#include "Animation/BasePlayerAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"

UAN_GroundStartFinished::UAN_GroundStartFinished()
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(64, 192, 255, 255);
#endif
}

void UAN_GroundStartFinished::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp)
	{
		return;
	}

	if (UBasePlayerAnimInstance* AnimInstance = Cast<UBasePlayerAnimInstance>(MeshComp->GetAnimInstance()))
	{
		AnimInstance->MarkGroundStartFinished();
	}
}

FString UAN_GroundStartFinished::GetNotifyName_Implementation() const
{
	return FString(TEXT("Ground Start Finished"));
}
