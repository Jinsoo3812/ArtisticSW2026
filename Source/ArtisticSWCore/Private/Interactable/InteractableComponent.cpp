// Fill out your copyright notice in the Description page of Project Settings.

#include "InteractableComponent.h"
#include "CollisionChannels.h"
#include "DrawDebugHelpers.h"
#include "Interactable/InteractionSubsystem.h"

UInteractableComponent::UInteractableComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;

	// 기본 반경 설정
	InitSphereRadius(100.f);

	// 오직 Interactable Trace Channel과만 Block 되는 프리셋
	SetCollisionProfileName(TEXT("Interactable"));

	ShapeColor = FColor::Cyan;
}

void UInteractableComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshInteractionUIFromData();
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

bool UInteractableComponent::RefreshInteractionUIFromData()
{
	if (!InteractableIdTag.IsValid() || !GetWorld())
	{
		return false;
	}

	const UInteractionSubsystem* InteractionSubsystem = GetWorld()->GetSubsystem<UInteractionSubsystem>();
	const FInteractionFeatureData* Feature = InteractionSubsystem
		? InteractionSubsystem->GetInteractionFeature(InteractableIdTag)
		: nullptr;
	if (!Feature)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[%s] No interaction feature row found for %s."),
			*GetNameSafe(this),
			*InteractableIdTag.ToString());
		return false;
	}

	InitializeInteractable(Feature->ObjectName, Feature->ActionText);
	return true;
}

FGameplayTag UInteractableComponent::GetInteractionTag() const
{
	return InteractionTag;
}

const FInteractionUIInfo& UInteractableComponent::GetInteractionUIInfo() const
{
	return InteractUIInfo;
}

FVector UInteractableComponent::GetInteractionPromptWorldLocation() const
{
	return GetComponentLocation() + GetComponentTransform().TransformVectorNoScale(InteractionPromptOffset);
}

void UInteractableComponent::Interact(AActor* Interactor)
{
	if (Interactor)
	{
		OnInteracted.Broadcast(Interactor);
	}
}
