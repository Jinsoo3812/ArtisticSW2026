// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "BasePlayerState.generated.h"

class UAbilitySystemComponent;
class UBaseAttributeSet;

/*
 * Player媛 二쎌뼱??洹??뺣낫瑜??좎??섏뿬 Player瑜??ㅼ떆 ?뚰솚?섍퀬 ?곹깭瑜?愿由ы븯??PlayerState
 */
UCLASS(Config = Game)
class CLASSFEATURE_API ABasePlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ABasePlayerState();

	// IAbilitySystemInterface ?ㅻ쾭?쇱씠??
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	class UBaseAttributeSet* GetAttributeSet() const;

protected:
	// PlayerState媛 ?뚯쑀??AbilitySystemComponent
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
	UAbilitySystemComponent* AbilitySystemComponent;

	// PlayerState媛 ?뚯쑀??AttributeSet
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
	UBaseAttributeSet* BasicAttributes;

	// INI ?뚯씪(DefaultGame.ini)?먯꽌 媛믪쓣 ?쒖뼱?????덉쓬
	UPROPERTY(Config)
	float BaseNetUpdateFrequency = 100.0f;
};
