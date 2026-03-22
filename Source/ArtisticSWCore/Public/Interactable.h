// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Interactable.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 * 게임 내 상호작용(F) 가능한 모든 객체는 이 인터페이스를 구현해야 합니다.
 */
class ARTISTICSWCORE_API IInteractable
{
	GENERATED_BODY()

public:
	/**
	 * 객체 고유의 상호작용 태그를 반환합니다. (예: Interaction.Pickup, Interaction.Crafting)
	 */
	virtual FGameplayTag GetInteractionTag() const = 0;

	/**
	 * 상호작용을 실행합니다.
	 * @param Instigator 상호작용을 시도한 주체 (주로 Player)
	 */
	virtual void Interact(AActor* Interactor) = 0;
};
