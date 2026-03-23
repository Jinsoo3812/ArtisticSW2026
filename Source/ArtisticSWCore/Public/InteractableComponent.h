// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SphereComponent.h"
#include "Interactable.h"
#include "InteractableComponent.generated.h"


// 컴포넌트를 소유한 액터(Item, 작업대 등)에게 상호작용 이벤트가 발생했음을 알리기 위한 델리게이트
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInteractedSignature, AActor*, Interactor);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class ARTISTICSWCORE_API UInteractableComponent : public USphereComponent, public IInteractable
{
	GENERATED_BODY()

public:
	UInteractableComponent();

	// IInteractable 구현
	virtual FGameplayTag GetInteractionTag() const override;
	virtual void Interact(AActor* Interactor) override;

	// 컴포넌트마다 인스턴스별로 태그를 설정할 수 있도록 노출
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	FGameplayTag InteractionTag;

	// Interact 발생 시 Owner Actor(또는 필요로 하는 외부)로 방송할 델리게이트
	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FOnInteractedSignature OnInteracted;
};
