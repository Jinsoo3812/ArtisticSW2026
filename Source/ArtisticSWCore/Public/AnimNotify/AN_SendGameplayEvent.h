// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "GameplayTagContainer.h"
#include "AN_SendGameplayEvent.generated.h"

/**
 * 어떤 애니메이션에서든 Gameplay Tag를 통해 GAS 이벤트를 발생시키는 범용 노티파이입니다.
 */
UCLASS()
class ARTISTICSWCORE_API UAN_SendGameplayEvent : public UAnimNotify
{
	GENERATED_BODY()

public:
	UAN_SendGameplayEvent();

	// 노티파이가 실행될 때 호출되는 함수
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

	// 에디터 타임라인에서 태그 이름이 바로 보이도록 하는 편의성 함수
	virtual FString GetNotifyName_Implementation() const override;

protected:
	// 블루프린트(에디터)에서 할당할 이벤트 태그
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GAS")
	FGameplayTag EventTag;

};
