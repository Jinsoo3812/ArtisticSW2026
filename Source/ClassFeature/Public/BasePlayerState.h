// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "BasePlayerState.generated.h"

class UAbilitySystemComponent;
class UBaseAttributeSet;

/*
 * Player가 죽어도 그 정보를 유지하여 Player를 다시 소환하고 상태를 관리하는 PlayerState
 */
UCLASS()
class CLASSFEATURE_API ABasePlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ABasePlayerState();

	// IAbilitySystemInterface 오버라이드
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	class UBaseAttributeSet* GetAttributeSet() const;

protected:
	// PlayerState가 소유할 AbilitySystemComponent
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
	UAbilitySystemComponent* AbilitySystemComponent;

	// PlayerState가 소유할 AttributeSet
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
	UBaseAttributeSet* BasicAttributes;
};