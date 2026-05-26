// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AN_LocomotionFinished.generated.h"

UENUM(BlueprintType)
enum class ELocomotionNotifyType : uint8
{
	StartFinished UMETA(DisplayName = "Start Finished"),
	StopFinished  UMETA(DisplayName = "Stop Finished")
};

/**
 * Custom Anim Notify that marks start or stop phase as finished, selectable in the Editor.
 */
UCLASS()
class CLASSFEATURE_API UAN_LocomotionFinished : public UAnimNotify
{
	GENERATED_BODY()

public:
	UAN_LocomotionFinished();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion")
	ELocomotionNotifyType NotifyType;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
