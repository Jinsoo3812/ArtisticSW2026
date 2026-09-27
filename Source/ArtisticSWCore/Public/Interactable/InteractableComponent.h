// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SphereComponent.h"
#include "Interactable.h"
#include "Blueprint/UserWidget.h"
#include "InteractUserWidget.h"
#include "InteractableComponent.generated.h"

class UUserWidget;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class ARTISTICSWCORE_API UInteractableComponent : public USphereComponent, public IInteractable
{
	GENERATED_BODY()

public:
	UInteractableComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// IInteractable 구현
	virtual FGameplayTag GetInteractionTag() const override;
	virtual const FInteractionUIInfo& GetInteractionUIInfo() const override;
	virtual void Interact(AActor* Interactor) override;

public:
	// Interactable Component 초기화. 단순 Text 등을 ItemFeatureData로 부터 주입받는 용도
	void InitializeInteractable(const FText& InObjectName, const FText& InActionText);

	// 컴포넌트마다 인스턴스별로 태그를 설정할 수 있도록 노출
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	FGameplayTag InteractionTag;

	/** Draw this component's scaled collision sphere while playing. Disabled by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Debug")
	bool bDrawDebugInteractionRange = false;

	// Interact 발생 시 Owner Actor(또는 필요로 하는 외부)로 방송할 델리게이트
	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FOnInteractedSignature OnInteracted;

	/* UI */
	// Interact UI에 표시할 정보 구조체
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	FInteractionUIInfo InteractUIInfo;

	// Interact UI에 표시할 정보 구조체
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	TSubclassOf<UUserWidget> InteractPopupUIClass;
};
