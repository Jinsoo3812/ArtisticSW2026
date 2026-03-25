// Fill out your copyright notice in the Description page of Project Settings.


#include "InteractableComponent.h"
#include "CollisionChannels.h"

UInteractableComponent::UInteractableComponent()
{
	// 충돌체이므로 Tick은 필요 없음
	PrimaryComponentTick.bCanEverTick = false;

	// 기본 반경 설정
	InitSphereRadius(100.f);

	// 오직 Interactable Trace Channel과만 Block 되는 프리셋
	SetCollisionProfileName(TEXT("Interactable"));
}

FGameplayTag UInteractableComponent::GetInteractionTag() const
{
	return InteractionTag;
}

void UInteractableComponent::Interact(AActor* Interactor)
{
	if (Interactor)
	{
		OnInteracted.Broadcast(Interactor);
	}
}

