// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "BasePlayerState.generated.h"

class UAbilitySystemComponent;
class UBaseAttributeSet;
class UShipUpgradeComponent;

/*
 * Player가 죽어도 그 정보를 유지하여 Player를 다시 소환하고 상태를 관리하는 PlayerState
 */
UCLASS(Config = Game)
class CLASSFEATURE_API ABasePlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ABasePlayerState();

	// IAbilitySystemInterface 오버라이드
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	class UBaseAttributeSet* GetAttributeSet() const;

	UFUNCTION(BlueprintPure, Category = "Ship Upgrade")
	UShipUpgradeComponent* GetShipUpgradeComponent() const { return ShipUpgradeComponent; }

protected:
	// PlayerState가 소유할 AbilitySystemComponent
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
	UAbilitySystemComponent* AbilitySystemComponent;

	// PlayerState가 소유할 AttributeSet
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
	UBaseAttributeSet* BasicAttributes;

	/** Server-authoritative player ship progression and UI facade. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship Upgrade")
	TObjectPtr<UShipUpgradeComponent> ShipUpgradeComponent;

	// INI 파일(DefaultGame.ini)에서 값을 제어할 수 있음
	UPROPERTY(Config)
	float BaseNetUpdateFrequency = 100.0f;
};
