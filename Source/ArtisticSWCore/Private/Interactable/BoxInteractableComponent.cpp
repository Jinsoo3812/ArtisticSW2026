// Fill out your copyright notice in the Description page of Project Settings.

#include "BoxInteractableComponent.h"
#include "CollisionChannels.h"
#include "DrawDebugHelpers.h"

UBoxInteractableComponent::UBoxInteractableComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;

	// 기본 박스 범위 설정
	InitBoxExtent(FVector(100.0f));

	// 오직 Interactable Trace Channel과만 Block 되는 프리셋
	SetCollisionProfileName(TEXT("Interactable"));

	ShapeColor = FColor::Cyan;
}

void UBoxInteractableComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
#if ENABLE_DRAW_DEBUG
	if (bDrawDebugInteractionRange && GetWorld())
	{
		DrawDebugBox(GetWorld(), GetComponentLocation(), GetScaledBoxExtent(), GetComponentQuat(),
			FColor::Cyan, false, 0.12f);
	}
#endif
}

void UBoxInteractableComponent::InitializeInteractable(const FText& InObjectName, const FText& InActionText)
{
	InteractUIInfo.ObjectName = InObjectName;
	InteractUIInfo.ActionText = InActionText;
}

FGameplayTag UBoxInteractableComponent::GetInteractionTag() const
{
	return InteractionTag;
}

const FInteractionUIInfo& UBoxInteractableComponent::GetInteractionUIInfo() const
{
	return InteractUIInfo;
}

void UBoxInteractableComponent::Interact(AActor* Interactor)
{
	if (Interactor)
	{
		OnInteracted.Broadcast(Interactor);
	}
}
