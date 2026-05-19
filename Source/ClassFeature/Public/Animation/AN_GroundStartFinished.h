// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AN_GroundStartFinished.generated.h"

/**
 * Marks the ground locomotion Start segment as finished.
 *
 * Place this notify at the end of the authored start portion, not necessarily
 * at the end of the whole sequence.
 */
UCLASS()
class CLASSFEATURE_API UAN_GroundStartFinished : public UAnimNotify
{
	GENERATED_BODY()

public:
	UAN_GroundStartFinished();

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
};
