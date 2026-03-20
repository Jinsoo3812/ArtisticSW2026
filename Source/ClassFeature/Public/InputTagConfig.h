// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "InputTagConfig.generated.h"

class UInputAction;

// IA와 Slot Tag의 Mapping 정보
USTRUCT(BlueprintType)
struct FKeyInputAction
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<const UInputAction> InputAction = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (Categories = "InputTag"))
	FGameplayTag KeyTag;
};

/**
 * ItemSlot Tag와 IA를 Mapping하는 DataAsset
 */
UCLASS()
class CLASSFEATURE_API UInputTagConfig : public UDataAsset
{
	GENERATED_BODY()
public:
	// IA와 Tag 목록
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TArray<FKeyInputAction> KeyInputActions;
};
