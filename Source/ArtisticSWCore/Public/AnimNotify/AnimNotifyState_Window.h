// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_Window.generated.h"

class FGameplayTags;

UCLASS()
class ARTISTICSWCORE_API UAnimNotifyState_Window : public UAnimNotifyState
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notify")
	FString Name;

	// Notify Begin 시 Owner에게 보낼 Gameplay Tag
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notify")
	FGameplayTag BeginEventTag;

	// Notify End 시 Owner에게 보낼 Gameplay Tag
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notify")
	FGameplayTag EndEventTag;
	
	virtual FString GetNotifyName_Implementation() const override;

	// Notify Begin시 BeginEventTag를 SendGameplayEventToActor 
	virtual void NotifyBegin(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		float TotalDuration,
		const FAnimNotifyEventReference& EventReference
	) override;

	// Notify End시 EndEventTag를 SendGameplayEventToActor 
	virtual void NotifyEnd(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference
	) override;

};

