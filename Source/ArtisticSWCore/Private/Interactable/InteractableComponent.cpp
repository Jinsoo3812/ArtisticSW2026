// Fill out your copyright notice in the Description page of Project Settings.


#include "InteractableComponent.h"
#include "CollisionChannels.h"
#include "DrawDebugHelpers.h"

UInteractableComponent::UInteractableComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;

	// 기본 반경 설정
	InitSphereRadius(100.f);

	// 오직 Interactable Trace Channel과만 Block 되는 프리셋
	SetCollisionProfileName(TEXT("Interactable"));
}

void UInteractableComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
#if ENABLE_DRAW_DEBUG
	if (bDrawDebugInteractionRange && GetWorld())
	{
		DrawDebugSphere(GetWorld(), GetComponentLocation(), GetScaledSphereRadius(), 24,
			FColor::Cyan, false, 0.12f);
	}
#endif
}

void UInteractableComponent::InitializeInteractable(const FText& InObjectName, const FText& InActionText)
{
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

