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

void UInteractableComponent::InitializeInteractable(const FText& InObjectName, const FText& InActionText)
{
	UE_LOG(LogTemp, Log, TEXT("Initializing Interactable Component with ObjectName: %s, ActionText: %s"), *InObjectName.ToString(), *InActionText.ToString());
	UE_LOG(LogTemp, Log, TEXT("Is Server: %s, Is Client: %s"), GetOwner()->HasAuthority() ? TEXT("Yes") : TEXT("No"), IsNetMode(NM_Client) ? TEXT("Yes") : TEXT("No"));
	InteractUIInfo.ObjectName = InObjectName;
	InteractUIInfo.ActionText = InActionText;
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

