// Fill out your copyright notice in the Description page of Project Settings.

#include "CapsuleInteractableComponent.h"
#include "CollisionChannels.h"
#include "DrawDebugHelpers.h"

UCapsuleInteractableComponent::UCapsuleInteractableComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;

	// 기본 캡슐 크기 설정 (HalfHeight >= Radius 불변식 준수)
	InitCapsuleSize(100.0f, 100.0f);

	// 오직 Interactable Trace Channel과만 Block 되는 프리셋
	SetCollisionProfileName(TEXT("Interactable"));

	ShapeColor = FColor::Cyan;
}

void UCapsuleInteractableComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
#if ENABLE_DRAW_DEBUG
	if (bDrawDebugInteractionRange && GetWorld())
	{
		DrawDebugCapsule(GetWorld(), GetComponentLocation(), GetScaledCapsuleHalfHeight(), GetScaledCapsuleRadius(), GetComponentQuat(),
			FColor::Cyan, false, 0.12f);
	}
#endif
}

void UCapsuleInteractableComponent::InitializeInteractable(const FText& InObjectName, const FText& InActionText)
{
	InteractUIInfo.ObjectName = InObjectName;
	InteractUIInfo.ActionText = InActionText;
}

FGameplayTag UCapsuleInteractableComponent::GetInteractionTag() const
{
	return InteractionTag;
}

const FInteractionUIInfo& UCapsuleInteractableComponent::GetInteractionUIInfo() const
{
	return InteractUIInfo;
}

void UCapsuleInteractableComponent::Interact(AActor* Interactor)
{
	if (Interactor)
	{
		OnInteracted.Broadcast(Interactor);
	}
}
